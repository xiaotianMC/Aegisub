// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "speech_transcribe.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "format.h"
#include "magia_timeline.h"
#include "version.h"

#include <libaegisub/background_runner.h>
#include <libaegisub/cajun/reader.h>
#include <libaegisub/cajun/visitor.h>
#include <libaegisub/exception.h>
#include <libaegisub/io.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <sstream>
#include <thread>

#include <wx/intl.h>
#include <wx/process.h>
#include <wx/filefn.h>
#include <wx/utils.h>

#ifdef WITH_CURL
#include <curl/curl.h>
#endif

namespace transcribe {
namespace {
std::string Trim(std::string_view s) {
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
	return std::string(s);
}

std::vector<std::string> Split(std::string const& setting, std::string_view at) {
	std::vector<std::string> out;
	size_t start = 0;
	while (start <= setting.size()) {
		size_t end = setting.find_first_of(at, start);
		if (end == std::string::npos) end = setting.size();
		auto piece = Trim(std::string_view(setting).substr(start, end - start));
		if (!piece.empty()) out.push_back(std::move(piece));
		start = end + 1;
	}
	return out;
}

/// A recogniser's name for a speaker, SPEAKER_00 or speaker_1, not a name
bool SpeakerLabel(std::string const& name) {
	std::string lower;
	for (char c : name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (!lower.starts_with("speaker_") && !lower.starts_with("speaker ")) return false;
	auto rest = lower.substr(8);
	return !rest.empty() && std::all_of(rest.begin(), rest.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

/// The "message" in an error body's "detail", or the detail as a string
struct DetailOf final : json::ConstVisitor {
	std::string message;
	void Visit(const json::Array&) override {}
	void Visit(const json::Object& o) override {
		auto it = o.find("message");
		if (it != o.end()) it->second.Accept(*this);
	}
	void Visit(int64_t) override {}
	void Visit(double) override {}
	void Visit(const json::String& s) override { message = s; }
	void Visit(bool) override {}
	void Visit(const json::Null&) override {}
};

std::string Detail(std::string const& body) {
	try {
		json::UnknownElement root;
		std::istringstream in(body);
		json::Reader::Read(root, in);
		auto const& obj = static_cast<json::Object const&>(root);
		auto it = obj.find("detail");
		if (it == obj.end()) return {};
		DetailOf d;
		it->second.Accept(d);
		return d.message;
	}
	catch (json::Exception const&) { return {}; }
}
}

FoundKey FindKey(KeySources const& sources) {
	if (sources.environment && !Trim(sources.environment).empty()) return {Trim(sources.environment), KeyPlace::Environment};
	if (sources.keychain && !Trim(*sources.keychain).empty()) return {Trim(*sources.keychain), KeyPlace::Keychain};
	if (sources.session && !Trim(*sources.session).empty()) return {Trim(*sources.session), KeyPlace::Session};
	if (!Trim(sources.setting).empty()) return {Trim(sources.setting), KeyPlace::Setting};
	return {};
}

std::vector<std::string> SplitCodes(std::string const& setting) { return Split(setting, ", \t\r\n;"); }
std::vector<std::string> SplitList(std::string const& setting) { return Split(setting, ",\r\n;"); }

std::vector<std::string> Keyterms(AssFile const& file, std::vector<std::string> extra, size_t limit) {
	std::vector<std::string> terms;
	auto add = [&](std::string const& term) {
		auto t = Trim(term);
		if (t.empty() || terms.size() >= limit) return;
		if (std::find(terms.begin(), terms.end(), t) == terms.end()) terms.push_back(std::move(t));
	};
	for (auto const& term : extra) add(term);
	for (auto const& line : file.Events) {
		auto const& actor = line.Actor.get();
		if (!SpeakerLabel(actor)) add(actor);
	}
	return terms;
}

std::vector<std::pair<std::string, std::string>> Form(Request const& request, ElevenLabs const& settings) {
	std::vector<std::pair<std::string, std::string>> fields;
	fields.emplace_back("model_id", settings.model.empty() ? "scribe_v3" : settings.model);
	fields.emplace_back("timestamps_granularity", "word");
	fields.emplace_back("diarize", request.diarize ? "true" : "false");
	fields.emplace_back("tag_audio_events", "true");
	if (!request.language.empty()) fields.emplace_back("language_code", request.language);
	for (auto const& code : request.secondary_languages) fields.emplace_back("secondary_language_codes", code);
	size_t count = 0;
	for (auto const& term : request.keyterms) {
		if (count++ >= 100) break;
		fields.emplace_back("keyterms", term);
	}
	return fields;
}

bool Accepted(agi::fs::path const& media) {
	auto ext = media.extension().string();
	for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	for (auto ok : {".mp3", ".wav", ".m4a", ".flac", ".ogg", ".oga", ".webm", ".aac", ".aiff", ".aif", ".opus",
	                ".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv", ".mpeg", ".mpg", ".3gp", ".3gpp"})
		if (ext == ok) return true;
	return false;
}

std::string Explain(long status, std::string const& body) {
	if (status == 200) return {};
	std::string why;
	switch (status) {
		case 401: why = from_wx(_("ElevenLabs did not accept the API key.")); break;
		case 402: why = from_wx(_("The ElevenLabs account's plan does not allow this.")); break;
		case 413: why = from_wx(_("ElevenLabs found the audio too large.")); break;
		case 422: why = from_wx(_("ElevenLabs refused how the request was made.")); break;
		case 429: why = from_wx(_("ElevenLabs is taking too many requests from this account: wait a while.")); break;
		default: why = agi::format(from_wx(_("ElevenLabs answered %d.")), status); break;
	}
	auto detail = Detail(body);
	if (detail.empty() && body.size() <= 200 && body.find('<') == std::string::npos) detail = Trim(body);
	if (!detail.empty()) why += " " + detail;
	return why;
}

std::string Duration(int ms) {
	int const s = std::max(0, ms) / 1000;
	if (s >= 3600) return agi::format("%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
	return agi::format("%d:%02d", s / 60, s % 60);
}

#ifdef WITH_CURL
bool CanUpload() { return true; }

namespace {
size_t WriteToString(char *contents, size_t size, size_t nmemb, std::string *s) {
	s->append(contents, size * nmemb);
	return size * nmemb;
}

/// The file read for the form part, by a stream opened from its path as
/// written, which the C functions curl would use cannot on Windows
struct FileSource {
	std::unique_ptr<std::istream> in;
	static size_t Read(char *buffer, size_t size, size_t nitems, void *arg) {
		auto self = static_cast<FileSource*>(arg);
		self->in->read(buffer, static_cast<std::streamsize>(size * nitems));
		return static_cast<size_t>(self->in->gcount());
	}
	static int Seek(void *arg, curl_off_t offset, int origin) {
		auto self = static_cast<FileSource*>(arg);
		self->in->clear();
		self->in->seekg(offset, origin == SEEK_SET ? std::ios::beg : origin == SEEK_CUR ? std::ios::cur : std::ios::end);
		return self->in->fail() ? CURL_SEEKFUNC_CANTSEEK : CURL_SEEKFUNC_OK;
	}
};

struct Progress {
	agi::ProgressSink *ps;
	bool uploaded = false;
	static int Tell(void *arg, curl_off_t, curl_off_t, curl_off_t ultotal, curl_off_t ulnow) {
		auto self = static_cast<Progress*>(arg);
		if (self->ps->IsCancelled()) return 1;
		if (ultotal <= 0) return 0;
		if (ulnow < ultotal)
			self->ps->SetProgress(ulnow, ultotal);
		else if (!self->uploaded) {
			self->uploaded = true;
			self->ps->SetMessage(from_wx(_("Waiting for ElevenLabs to hear it through...")));
			self->ps->SetIndeterminate();
		}
		return 0;
	}
};
}

std::string Upload(Request const& request, ElevenLabs const& settings, agi::ProgressSink *ps) {
	struct Handle {
		CURL *c = curl_easy_init();
		curl_mime *mime = nullptr;
		curl_slist *headers = nullptr;
		~Handle() {
			if (mime) curl_mime_free(mime);
			if (headers) curl_slist_free_all(headers);
			if (c) curl_easy_cleanup(c);
		}
	} h;
	if (!h.c) throw agi::EnvironmentError(from_wx(_("Curl could not be initialized.")));

	h.mime = curl_mime_init(h.c);
	for (auto const& [name, value] : Form(request, settings)) {
		auto part = curl_mime_addpart(h.mime);
		curl_mime_name(part, name.c_str());
		curl_mime_data(part, value.c_str(), CURL_ZERO_TERMINATED);
	}
	FileSource source{agi::io::Open(request.media, true)};
	auto const size = agi::fs::Size(request.media);
	auto file = curl_mime_addpart(h.mime);
	curl_mime_name(file, "file");
	curl_mime_filename(file, request.media.filename().string().c_str());
	curl_mime_data_cb(file, static_cast<curl_off_t>(size), FileSource::Read, FileSource::Seek, nullptr, &source);

	h.headers = curl_slist_append(h.headers, ("xi-api-key: " + settings.key).c_str());
	h.headers = curl_slist_append(h.headers, "Accept: application/json");
	// curl waits for the server's go-ahead before a large body, which
	// ElevenLabs does not give
	h.headers = curl_slist_append(h.headers, "Expect:");

	std::string body;
	Progress progress{ps};
	curl_easy_setopt(h.c, CURLOPT_URL, "https://api.elevenlabs.io/v1/speech-to-text");
	curl_easy_setopt(h.c, CURLOPT_MIMEPOST, h.mime);
	curl_easy_setopt(h.c, CURLOPT_HTTPHEADER, h.headers);
	curl_easy_setopt(h.c, CURLOPT_USERAGENT, agi::format("Aegisub %s", GetAegisubLongVersionString()).c_str());
	curl_easy_setopt(h.c, CURLOPT_WRITEFUNCTION, WriteToString);
	curl_easy_setopt(h.c, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(h.c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(h.c, CURLOPT_XFERINFOFUNCTION, Progress::Tell);
	curl_easy_setopt(h.c, CURLOPT_XFERINFODATA, &progress);
	curl_easy_setopt(h.c, CURLOPT_CONNECTTIMEOUT, 30L);
	curl_easy_setopt(h.c, CURLOPT_FOLLOWLOCATION, 0L);

	auto const res = curl_easy_perform(h.c);
	if (res == CURLE_ABORTED_BY_CALLBACK) throw agi::UserCancelException("cancelled");
	if (res != CURLE_OK)
		throw agi::EnvironmentError(agi::format(from_wx(_("Could not reach ElevenLabs: %s.")), curl_easy_strerror(res)));
	long status = 0;
	curl_easy_getinfo(h.c, CURLINFO_RESPONSE_CODE, &status);
	if (status == 401) throw KeyRefused(Explain(status, body));
	if (auto why = Explain(status, body); !why.empty()) throw agi::EnvironmentError(why);
	return body;
}
#else
bool CanUpload() { return false; }
std::string Upload(Request const&, ElevenLabs const&, agi::ProgressSink *) {
	throw agi::EnvironmentError(from_wx(_("This build of Aegisub has no libcurl, so nothing can be sent to ElevenLabs.")));
}
#endif

std::vector<std::string> CommandLine(std::string const& templ, agi::fs::path const& media,
                                     agi::fs::path const& out, std::string const& language) {
	std::pair<std::string, std::string> const values[] = {
		{"{media}", media.string()}, {"{out}", out.string()}, {"{language}", language},
	};
	auto has_empty = [&](std::string_view part) {
		for (auto const& [name, value] : values)
			if (value.empty() && part.find(name) != std::string_view::npos) return true;
		return false;
	};
	// The bracketed parts kept or dropped
	std::string text;
	for (size_t i = 0; i < templ.size();) {
		if (templ[i] != '[') { text += templ[i++]; continue; }
		size_t close = templ.find(']', i);
		if (close == std::string::npos) { text += templ.substr(i); break; }
		std::string_view part(templ.data() + i + 1, close - i - 1);
		if (!has_empty(part)) text += part;
		i = close + 1;
	}

	// Its words, quotes grouping
	std::vector<std::string> words;
	std::string word;
	bool in_word = false;
	char quote = 0;
	for (char c : text) {
		if (quote) {
			if (c == quote) quote = 0;
			else word += c;
		}
		else if (c == '"' || c == '\'') { quote = c; in_word = true; }
		else if (std::isspace(static_cast<unsigned char>(c))) {
			if (in_word) words.push_back(std::move(word));
			word.clear();
			in_word = false;
		}
		else { word += c; in_word = true; }
	}
	if (in_word) words.push_back(std::move(word));

	for (auto& w : words) {
		for (auto const& [name, value] : values) {
			for (size_t at = w.find(name); at != std::string::npos; at = w.find(name, at + value.size()))
				w.replace(at, name.size(), value);
		}
	}
	return words;
}

std::optional<agi::fs::path> OutputJson(agi::fs::path const& folder, agi::fs::path const& media) {
	auto const wanted = media.stem().string() + ".json";
	std::vector<std::string> found;
	for (auto const& name : agi::fs::DirectoryIterator(folder, "*.json")) {
		if (name == wanted) return folder / name;
		found.push_back(name);
	}
	if (found.size() == 1) return folder / found[0];
	return std::nullopt;
}

namespace {
class Process final : public wxProcess {
public:
	std::atomic<bool> ended{false};
	std::atomic<int> status{-1};
	bool abandoned = false;
	Process() : wxProcess(wxPROCESS_REDIRECT) { }
	void OnTerminate(int, int code) override {
		status = code;
		ended = true;
		if (abandoned) delete this;
	}
};

void Drain(wxInputStream *in, std::string &into) {
	if (!in) return;
	while (in->CanRead()) {
		int const ch = in->GetC();
		if (ch == wxEOF) break;
		into.push_back(static_cast<char>(ch));
	}
}
}

struct Program::Impl {
	Process *process = nullptr;
	long pid = 0;
	std::string output;
};

namespace {
/// Whether @p program can be run: a path to a file, or a name found on the
/// PATH. wxExecute forks before it finds out, and would report a program
/// that is not there as having ended at once.
bool Runnable(std::string const& program) {
	if (program.find('/') != std::string::npos || program.find('\\') != std::string::npos)
		return agi::fs::FileExists(program);
	wxString path;
	if (!wxGetEnv("PATH", &path)) return true;
	std::vector<std::string> names{program};
#ifdef _WIN32
	for (auto ext : {".exe", ".bat", ".cmd", ".com"}) names.push_back(program + ext);
#endif
	for (auto const& dir : Split(from_wx(path), std::string(1, wxPATH_SEP[0]))) {
		for (auto const& name : names)
			if (agi::fs::FileExists(agi::fs::path(dir) / name)) return true;
	}
	return false;
}
}

Program::Program(std::vector<std::string> const& argv, agi::fs::path const& folder)
: impl(std::make_unique<Impl>())
{
	if (argv.empty()) throw agi::EnvironmentError(from_wx(_("There is no command to run.")));
	if (!Runnable(argv[0]))
		throw agi::EnvironmentError(agi::format(from_wx(_("There is no program %s to run: is it installed and on the path?")), argv[0]));
	std::vector<std::wstring> words;
	for (auto const& a : argv) words.push_back(wxString::FromUTF8(a).ToStdWstring());
	std::vector<wchar_t const*> ptrs;
	for (auto const& w : words) ptrs.push_back(w.c_str());
	ptrs.push_back(nullptr);

	wxExecuteEnv env;
	env.cwd = folder.wstring();
	wxGetEnvMap(&env.env);
	env.env["PYTHONIOENCODING"] = "utf-8";
	env.env["PYTHONUTF8"] = "1";
	env.env["PYTHONUNBUFFERED"] = "1";

	impl->process = new Process;
	impl->pid = wxExecute(ptrs.data(), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE | wxEXEC_MAKE_GROUP_LEADER, impl->process, &env);
	if (!impl->pid) {
		delete impl->process;
		impl->process = nullptr;
		throw agi::EnvironmentError(agi::format(from_wx(_("Could not start %s.")), argv[0]));
	}
	impl->process->CloseOutput();
}

Program::~Program() {
	if (!impl->process) return;
	if (impl->process->ended)
		delete impl->process;
	else
		impl->process->abandoned = true;
}

void Program::Wait(agi::ProgressSink *ps) {
	std::string shown;
	while (true) {
		bool const ended = impl->process->ended;
		Drain(impl->process->GetInputStream(), impl->output);
		Drain(impl->process->GetErrorStream(), impl->output);
		auto last = magia_timeline::Readable(magia_timeline::LastLines(magia_timeline::WithoutProgress(impl->output), 1));
		while (!last.empty() && (last.back() == '\n' || last.back() == '\r')) last.pop_back();
		if (!last.empty() && last != shown) {
			ps->SetMessage(last);
			shown = last;
		}
		if (ps->IsCancelled()) {
			wxProcess::Kill(static_cast<int>(impl->pid), wxSIGKILL, wxKILL_CHILDREN);
			throw agi::UserCancelException("cancelled");
		}
		if (ended) return;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

std::string Program::Output() const { return magia_timeline::Readable(impl->output); }
int Program::ExitCode() const { return impl->process ? impl->process->status.load() : -1; }

}

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "magia_timeline.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "dialog_progress.h"

#include <libaegisub/background_runner.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#include <wx/intl.h>
#include <wx/process.h>
#include <wx/stream.h>
#include <wx/utils.h>

namespace magia_timeline {

namespace {
bool IsFile(agi::fs::path const& p) {
	try { return agi::fs::FileExists(p); }
	catch (...) { return false; }
}

bool IsFolder(agi::fs::path const& p) {
	try { return agi::fs::DirectoryExists(p); }
	catch (...) { return false; }
}

std::string Lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

/// The Python its install script set up in @p folder, if it did
std::optional<agi::fs::path> VenvPython(agi::fs::path const& folder) {
#ifdef _WIN32
	for (auto name : {"venv/Scripts/python.exe"})
#else
	for (auto name : {"venv/bin/python3", "venv/bin/python"})
#endif
		if (IsFile(folder / name)) return folder / name;
	return std::nullopt;
}

agi::fs::path SystemPython() {
#ifdef _WIN32
	return "python";
#else
	return "python3";
#endif
}

Launch Script(agi::fs::path const& script) {
	auto folder = script.parent_path();
	return {VenvPython(folder).value_or(SystemPython()), {"-u", script.string()}, folder};
}

/// A top-level key's line: "key:" at the start of the line
bool IsKey(std::string const& line, std::string const& key) {
	return line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == ':';
}

/// A line that belongs to the key above it: indented, a list item, a
/// comment or blank
bool Continues(std::string const& line) {
	return line.empty() || line[0] == ' ' || line[0] == '\t' || line[0] == '-' || line[0] == '#' || line[0] == '\r';
}

/// A string as YAML reads it back unchanged, in ASCII alone: in double
/// quotes, with everything else written as an escape. MagiaTimeline 1.0
/// reads its config in the system's code page (GBK in Chinese Windows) and
/// its packaged program ignores PYTHONUTF8, so a path in UTF-8 would not be
/// read; escapes read the same whatever the code page.
std::string Quote(std::string const& s) {
	std::string out = "\"";
	char buf[16];
	auto const* p = reinterpret_cast<unsigned char const*>(s.data());
	auto const* end = p + s.size();
	while (p < end) {
		uint32_t c = *p;
		int extra = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
		if (extra < 0 || end - p <= extra) { ++p; continue; } // not UTF-8: dropped
		if (extra) c &= 0x3F >> extra;
		for (int i = 1; i <= extra; ++i) c = (c << 6) | (p[i] & 0x3F);
		p += extra + 1;
		if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
		else if (c >= 0x20 && c < 0x7F) out += static_cast<char>(c);
		else {
			snprintf(buf, sizeof buf, c > 0xFFFF ? "\\U%08X" : c > 0xFF ? "\\u%04X" : "\\x%02X", c);
			out += buf;
		}
	}
	return out + "\"";
}
}

std::optional<Launch> FindLaunch(agi::fs::path const& setting) {
	if (setting.empty()) return std::nullopt;
	if (IsFile(setting)) {
		auto ext = Lower(setting.extension().string());
		if (ext == ".py") return Script(setting);
		if (ext == ".exe" || ext.empty()) return Launch{setting, {}, setting.parent_path()};
		return std::nullopt;
	}
	if (!IsFolder(setting)) return std::nullopt;
	auto const script = setting / "MagiaTimeline.py";
	bool const has_script = IsFile(script);
	if (has_script && VenvPython(setting)) return Script(script);
	for (auto name : {"MagiaTimeline.exe", "MagiaTimeline"})
		if (IsFile(setting / name)) return Launch{setting / name, {}, setting};
	if (has_script) return Script(script);
	return std::nullopt;
}

std::vector<std::string> Arguments(Launch const& launch, agi::fs::path const& config) {
	auto args = launch.args;
	args.push_back("--config");
	args.push_back(config.string());
	return args;
}

std::optional<std::string> WriteConfig(std::string const& config, std::string const& video,
                                       std::string const& destination, std::string const& strategy) {
	std::string out;
	bool source = false, dest = false;
	size_t pos = 0;
	bool skipping = false;
	while (pos < config.size()) {
		size_t end = config.find('\n', pos);
		if (end == std::string::npos) end = config.size();
		std::string line = config.substr(pos, end - pos);
		pos = end + 1;
		if (skipping) {
			if (Continues(line)) continue;
			skipping = false;
		}
		if (IsKey(line, "source")) {
			out += "source:\n- " + Quote(video) + "\n";
			source = skipping = true;
		}
		else if (IsKey(line, "destination")) {
			out += "destination:\n- " + Quote(destination) + "\n";
			dest = skipping = true;
		}
		else if (IsKey(line, "strategy") && !strategy.empty())
			out += "strategy: " + strategy + "\n";
		else
			out += line + "\n";
	}
	if (!source || !dest) return std::nullopt;
	return out;
}

std::vector<Interval> ReadTimeline(AssFile const& output) {
	std::vector<Interval> found;
	for (auto const& line : output.Events) {
		if (line.Comment) continue;
		found.push_back({int(line.Start), int(line.End)});
	}
	std::stable_sort(found.begin(), found.end(), [](Interval const& a, Interval const& b) { return a.start < b.start; });
	return found;
}

bool ReportsError(std::string const& output) {
	return output.find("Exception caught") != std::string::npos ||
		output.find("Traceback (most recent call last)") != std::string::npos;
}

std::string Readable(std::string const& output) {
	if (output.empty() || !wxString::FromUTF8(output.data(), output.size()).empty())
		return output;
	// Not UTF-8: from a program that ignored PYTHONIOENCODING (the packaged
	// one), in the system's code page
	return wxString(output.data(), wxConvLibc, output.size()).utf8_string();
}

std::string LastLines(std::string const& output, size_t count) {
	std::vector<std::string> lines;
	size_t pos = 0;
	while (pos < output.size()) {
		size_t end = output.find_first_of("\r\n", pos);
		if (end == std::string::npos) end = output.size();
		if (end > pos) lines.push_back(output.substr(pos, end - pos));
		pos = end + 1;
	}
	if (lines.size() > count) lines.erase(lines.begin(), lines.end() - count);
	std::string out;
	for (auto const& l : lines) out += l + "\n";
	return out;
}

namespace {
/// Told on the main thread when the program ends; deletes itself then if
/// it was given up on before
class Process final : public wxProcess {
public:
	std::atomic<bool> ended{false};
	bool abandoned = false;
	Process() : wxProcess(wxPROCESS_REDIRECT) { }
	void OnTerminate(int, int) override {
		ended = true;
		if (abandoned) delete this;
	}
};

/// What is there to be read now, without waiting for more
void Drain(wxInputStream *in, std::string &into) {
	if (!in) return;
	while (in->CanRead()) {
		int const ch = in->GetC();
		if (ch == wxEOF) break;
		into.push_back(static_cast<char>(ch));
	}
}
}

Outcome Run(Launch const& launch, agi::fs::path const& config, wxWindow *parent) {
	Outcome outcome;
	std::vector<std::wstring> words{launch.program.wstring()};
	for (auto const& a : Arguments(launch, config))
		words.push_back(wxString::FromUTF8(a).ToStdWstring());
	std::vector<wchar_t const*> argv;
	for (auto const& w : words) argv.push_back(w.c_str());
	argv.push_back(nullptr);

	wxExecuteEnv env;
	env.cwd = launch.folder.wstring();
	wxGetEnvMap(&env.env);
	// Its output in UTF-8 whatever the system's code page, and as it is printed
	env.env["PYTHONIOENCODING"] = "utf-8";
	env.env["PYTHONUTF8"] = "1";
	env.env["PYTHONUNBUFFERED"] = "1";

	auto process = new Process;
	long const pid = wxExecute(argv.data(), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE | wxEXEC_MAKE_GROUP_LEADER, process, &env);
	if (!pid) {
		delete process;
		outcome.output = "Could not start " + launch.program.string() + "\n";
		outcome.reported_error = true;
		return outcome;
	}
	// It waits for Enter at the end of a run (1.0 always, later ones unless
	// told not to with an option the earlier ones refuse): it is pressed now
	if (auto in = process->GetOutputStream()) in->Write("\n", 1);
	process->CloseOutput();

	DialogProgress progress(parent, "MagiaTimeline", _("Starting MagiaTimeline..."));
	try {
		progress.Run([&](agi::ProgressSink *ps) {
			ps->SetIndeterminate();
			std::string shown;
			while (true) {
				bool const ended = process->ended;
				Drain(process->GetInputStream(), outcome.output);
				Drain(process->GetErrorStream(), outcome.output);
				auto last = Readable(LastLines(outcome.output, 1));
				if (!last.empty()) last.pop_back();
				if (!last.empty() && last != shown) {
					ps->SetMessage(last);
					shown = last;
				}
				if (ps->IsCancelled()) {
					wxProcess::Kill(static_cast<int>(pid), wxSIGKILL, wxKILL_CHILDREN);
					return;
				}
				if (ended) return;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		});
	}
	catch (agi::UserCancelException const&) {
		outcome.cancelled = true;
	}
	if (process->ended)
		delete process;
	else
		process->abandoned = true;
	outcome.output = Readable(outcome.output);
	outcome.reported_error = ReportsError(outcome.output);
	return outcome;
}

}

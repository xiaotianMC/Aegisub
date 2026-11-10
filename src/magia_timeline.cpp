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

namespace {
/// A line of a config.yml taken apart: its indent, and the key and the
/// value after it if it is "key: value"
struct Line {
	std::string text;
	size_t indent = 0;
	std::string key, value;
	bool blank = false, comment = false, item = false;
};

std::string Trim(std::string s) {
	size_t b = s.find_first_not_of(" \t\r");
	if (b == std::string::npos) return "";
	size_t e = s.find_last_not_of(" \t\r");
	return s.substr(b, e - b + 1);
}

/// @p value without a comment after it, which starts at a # after a space
/// outside quotes
std::string WithoutComment(std::string const& value) {
	char quote = 0;
	for (size_t i = 0; i < value.size(); ++i) {
		char c = value[i];
		if (quote) {
			if (c == quote) quote = 0;
		}
		else if (c == '"' || c == '\'') quote = c;
		else if (c == '#' && (i == 0 || value[i - 1] == ' ' || value[i - 1] == '\t'))
			return Trim(value.substr(0, i));
	}
	return Trim(value);
}

Line Split(std::string text) {
	if (!text.empty() && text.back() == '\r') text.pop_back();
	Line line;
	line.text = text;
	line.indent = text.find_first_not_of(' ');
	if (line.indent == std::string::npos) {
		line.blank = true;
		line.indent = 0;
		return line;
	}
	char const first = text[line.indent];
	if (first == '#') { line.comment = true; return line; }
	if (first == '-') { line.item = true; line.value = WithoutComment(text.substr(line.indent + 1)); return line; }
	size_t colon = text.find(':', line.indent);
	if (colon == std::string::npos) return line;
	auto key = text.substr(line.indent, colon - line.indent);
	if (key.empty() || key.find_first_of(" \t\"'") != std::string::npos) return line;
	line.key = key;
	line.value = WithoutComment(text.substr(colon + 1));
	return line;
}

std::vector<Line> Lines(std::string const& config) {
	std::vector<Line> lines;
	size_t pos = 0;
	while (pos < config.size()) {
		size_t end = config.find('\n', pos);
		if (end == std::string::npos) end = config.size();
		lines.push_back(Split(config.substr(pos, end - pos)));
		pos = end + 1;
	}
	return lines;
}

/// Where keys are: at the top, under a top-level key (two spaces in) and
/// under one of those (four spaces in), as its config.yml is laid out
struct Paths {
	std::string top, middle;
	/// The path of @p line's key, empty for one at another depth
	std::string Of(Line const& line) {
		if (line.indent == 0) { top = line.key; middle.clear(); return top; }
		if (line.indent == 2 && !top.empty()) { middle = line.key; return top + "/" + middle; }
		if (line.indent == 4 && !middle.empty()) return top + "/" + middle + "/" + line.key;
		return "";
	}
};

std::string Number(double v) {
	char buf[32];
	snprintf(buf, sizeof buf, "%.4f", v);
	std::string s = buf;
	while (s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
	return s;
}
}

std::string Installed::Get(std::string const& path) const {
	auto it = values.find(path);
	return it == values.end() ? std::string() : it->second;
}

Installed Inspect(std::string const& config) {
	Installed found;
	auto const lines = Lines(config);
	Paths paths;
	for (size_t i = 0; i < lines.size(); ++i) {
		auto const& line = lines[i];
		if (line.key.empty()) continue;
		auto path = paths.Of(line);
		if (path.empty()) continue;
		if (line.indent == 4) {
			// A key two levels down: its top-level key is a strategy, the
			// one between a preset of it
			auto& presets = found.presets[paths.top];
			if (presets.empty()) found.strategies.push_back(paths.top);
			if (std::find(presets.begin(), presets.end(), paths.middle) == presets.end())
				presets.push_back(paths.middle);
		}
		if (!line.value.empty()) {
			found.values[path] = line.value;
			continue;
		}
		// A list written an item a line
		std::vector<std::string> items;
		size_t j = i + 1;
		for (; j < lines.size(); ++j) {
			auto const& next = lines[j];
			if (next.blank || next.comment) continue;
			if (!next.item || next.indent < line.indent) break;
			items.push_back(next.value);
		}
		if (!items.empty()) {
			found.values[path] = FormatList(items);
			continue;
		}
		// Nothing after it at all (YAML's null), rather than keys under it
		bool holds = false;
		for (j = i + 1; j < lines.size(); ++j) {
			if (lines[j].blank || lines[j].comment) continue;
			holds = lines[j].indent > line.indent;
			break;
		}
		if (!holds) found.values[path] = "";
	}
	return found;
}

std::optional<std::string> WriteConfig(std::string const& config, std::string const& video,
                                       std::string const& destination, Values const& overrides) {
	std::string out;
	bool source = false, dest = false;
	Paths paths;
	enum { none, list, value } skipping = none;
	size_t skip_indent = 0;
	for (auto const& line : Lines(config)) {
		if (skipping == list) {
			// source and destination: the old list, its commented-out items
			// and the blank lines after it
			if (line.blank || line.comment || line.item || line.indent > 0) continue;
			skipping = none;
		}
		else if (skipping == value) {
			// What belongs to a replaced value: deeper lines and its items
			if (!line.blank && (line.indent > skip_indent || (line.item && line.indent == skip_indent))) continue;
			skipping = none;
		}
		if (line.key.empty()) {
			out += line.text + "\n";
			continue;
		}
		auto const path = paths.Of(line);
		if (path == "source" || path == "destination") {
			out += path + ":\n- " + Quote(path == "source" ? video : destination) + "\n";
			(path == "source" ? source : dest) = true;
			skipping = list;
			continue;
		}
		auto it = path.empty() ? overrides.end() : overrides.find(path);
		if (it == overrides.end()) {
			out += line.text + "\n";
			continue;
		}
		out += std::string(line.indent, ' ') + line.key + ": " + it->second + "\n";
		skipping = value;
		skip_indent = line.indent;
	}
	if (!source || !dest) return std::nullopt;
	return out;
}

std::optional<std::string> WriteConfig(std::string const& config, std::string const& video,
                                       std::string const& destination, std::string const& strategy) {
	Values overrides;
	if (!strategy.empty()) overrides["strategy"] = strategy;
	return WriteConfig(config, video, destination, overrides);
}

std::optional<Rect> ParseRect(std::string const& value) {
	auto items = ParseList(value);
	if (items.size() != 4) return std::nullopt;
	double v[4];
	for (int i = 0; i < 4; ++i) {
		try {
			size_t used = 0;
			v[i] = std::stod(items[i], &used);
			if (used != items[i].size()) return std::nullopt;
		}
		catch (...) { return std::nullopt; }
	}
	return Rect{v[0], v[1], v[2], v[3]};
}

std::string FormatRect(Rect const& r) {
	return "[" + Number(r.left) + ", " + Number(r.right) + ", " + Number(r.top) + ", " + Number(r.bottom) + "]";
}

std::vector<std::string> ParseList(std::string const& value) {
	std::vector<std::string> items;
	auto v = Trim(value);
	if (v.size() < 2 || v.front() != '[' || v.back() != ']') return items;
	v = v.substr(1, v.size() - 2);
	size_t pos = 0;
	while (pos <= v.size()) {
		size_t end = v.find(',', pos);
		if (end == std::string::npos) end = v.size();
		auto item = Trim(v.substr(pos, end - pos));
		if (!item.empty()) items.push_back(item);
		pos = end + 1;
	}
	return items;
}

std::string FormatList(std::vector<std::string> const& items) {
	std::string out = "[";
	for (size_t i = 0; i < items.size(); ++i) {
		if (i) out += ", ";
		out += items[i];
	}
	return out + "]";
}

std::string Unquote(std::string const& value) {
	auto v = Trim(value);
	if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
		return v.substr(1, v.size() - 2);
	return v;
}

std::string QuoteValue(std::string const& text) {
	return Quote(text);
}

bool CanSkipFrames(std::string const& strategy) {
	return strategy == "bcs" || strategy == "dtd" || strategy == "otl" || strategy == "prk";
}

bool CanReadText(std::string const& strategy) {
	return strategy == "bcs" || strategy == "dtd" || strategy == "otl";
}

Values ParseSaved(std::string const& saved) {
	Values values;
	size_t pos = 0;
	while (pos < saved.size()) {
		size_t end = saved.find('\n', pos);
		if (end == std::string::npos) end = saved.size();
		auto line = saved.substr(pos, end - pos);
		pos = end + 1;
		size_t eq = line.find('=');
		if (eq == std::string::npos || eq == 0) continue;
		values[line.substr(0, eq)] = line.substr(eq + 1);
	}
	return values;
}

std::string FormatSaved(Values const& saved) {
	std::string out;
	for (auto const& [path, value] : saved) {
		if (value.find('\n') != std::string::npos) continue;
		out += path + "=" + value + "\n";
	}
	return out;
}

Values Effective(Installed const& installed, Values const& saved) {
	Values values = installed.values;
	for (auto const& [path, value] : saved)
		if (installed.Has(path)) values[path] = value;
	auto const& strategies = installed.strategies;
	if (std::find(strategies.begin(), strategies.end(), values["strategy"]) == strategies.end())
		values["strategy"] = installed.Get("strategy");
	auto it = installed.presets.find(values["strategy"]);
	if (it != installed.presets.end() && !it->second.empty() &&
		std::find(it->second.begin(), it->second.end(), values["preset"]) == it->second.end()) {
		auto const own = installed.Get("preset");
		values["preset"] = std::find(it->second.begin(), it->second.end(), own) != it->second.end() ? own : it->second.front();
	}
	return values;
}

Values Changes(Installed const& installed, Values const& chosen) {
	Values changes;
	for (auto const& [path, value] : chosen) {
		if (!installed.Has(path)) continue;
		if (installed.Get(path) != value) changes[path] = value;
	}
	return changes;
}

std::vector<Interval> ReadTimeline(AssFile const& output) {
	std::vector<Interval> found;
	for (auto const& line : output.Events) {
		if (line.Comment) continue;
		Interval interval{int(line.Start), int(line.End), line.Text.get(), ""};
		// Without OCR its text is its name for the stretch
		auto const& t = interval.text;
		if (t.rfind("Subtitle_", 0) == 0 && t.find_first_of(" \\{") == std::string::npos) {
			auto last = t.rfind('_');
			if (last > 8 && last + 1 < t.size() && t.find_first_not_of("0123456789", last + 1) == std::string::npos) {
				interval.name = t;
				interval.text.clear();
			}
		}
		found.push_back(std::move(interval));
	}
	std::stable_sort(found.begin(), found.end(), [](Interval const& a, Interval const& b) { return a.start < b.start; });
	return found;
}

void AddReadText(std::vector<Interval> &intervals, std::string const& ocr_file) {
	std::map<std::string, std::string> read;
	size_t pos = 0;
	while (pos < ocr_file.size()) {
		size_t end = ocr_file.find('\n', pos);
		if (end == std::string::npos) end = ocr_file.size();
		auto line = ocr_file.substr(pos, end - pos);
		pos = end + 1;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		size_t comma = line.find(',');
		if (comma == std::string::npos) continue;
		read[line.substr(0, comma)] = line.substr(comma + 1);
	}
	for (auto& interval : intervals) {
		if (!interval.text.empty() || interval.name.empty()) continue;
		auto it = read.find(interval.name);
		if (it != read.end()) interval.text = it->second;
	}
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

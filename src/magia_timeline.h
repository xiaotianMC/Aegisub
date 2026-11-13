// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <libaegisub/fs.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

class AssFile;
class wxWindow;

/// Times lines from the subtitles burnt into a video by running MagiaTimeline
/// (https://github.com/HurryPeng/MagiaTimeline), a program of its own under
/// the GPL, installed apart from this one (docs/magiatimeline.md). Nothing of
/// it is built in: it is started as another process, given a config file
/// and its output read back.
namespace magia_timeline {

/// How to start it: the program, the arguments before ours, and the folder
/// it is run in, which its config file's relative paths are taken from
struct Launch {
	agi::fs::path program;
	std::vector<std::string> args;
	agi::fs::path folder;
};

/// The way to start the MagiaTimeline at @p setting: its folder, its
/// MagiaTimeline.py or a packaged MagiaTimeline.exe. From a folder, the
/// Python of the virtual environment its install script made (venv) runs
/// the script; failing that, a packaged program; failing that, the script
/// with whatever Python is on the path. Nothing when none is there.
std::optional<Launch> FindLaunch(agi::fs::path const& setting);

/// The arguments that run @p launch on @p config
std::vector<std::string> Arguments(Launch const& launch, agi::fs::path const& config);

/// The values in a config.yml by their path: "strategy",
/// "framewise/sampleInterval", "bcs/default/dialogRect". A value is its
/// text as written, without a comment after it; a list written one item
/// a line is given as "[a, b]".
using Values = std::map<std::string, std::string>;

/// What a config.yml holds: its values, and the strategies (the top-level
/// keys holding presets of settings) with their presets, in its order.
/// Which keys are there tells what the installed version can do: 1.0 has
/// no maxResWidth, no dtd and writes its OCR to a file of its own.
struct Installed {
	Values values;
	std::vector<std::string> strategies;
	std::map<std::string, std::vector<std::string>> presets;

	bool Has(std::string const& path) const { return values.count(path) > 0; }
	/// The value at @p path, or empty
	std::string Get(std::string const& path) const;
};

/// Read @p config, a config.yml
Installed Inspect(std::string const& config);

/// @p config, MagiaTimeline's own config.yml, made to read @p video and
/// write @p destination (a path without its extension, .ass being added),
/// with the values at the paths of @p overrides replaced. Everything else
/// is left as it is, and a path the file does not have is not added: an
/// earlier version would refuse what it does not know. Nothing when the
/// file has no top-level source or destination to replace.
std::optional<std::string> WriteConfig(std::string const& config, std::string const& video,
                                       std::string const& destination, Values const& overrides);
/// As above, with only @p strategy chosen, if not empty
std::optional<std::string> WriteConfig(std::string const& config, std::string const& video,
                                       std::string const& destination, std::string const& strategy);

/// A rectangle as its config gives one: [left, right, top, bottom], each a
/// share of the frame's width or height
struct Rect {
	double left = 0, right = 1, top = 0, bottom = 1;
};
std::optional<Rect> ParseRect(std::string const& value);
std::string FormatRect(Rect const& rect);

/// The items of a list value, "[a, b]"
std::vector<std::string> ParseList(std::string const& value);
std::string FormatList(std::vector<std::string> const& items);
/// A string value without its quotes; and one put in them
std::string Unquote(std::string const& value);
std::string QuoteValue(std::string const& text);

/// The strategies that can skip the frames where nothing changes (the
/// speculative engine) and run extra jobs such as OCR: the others look at
/// every frame
bool CanSkipFrames(std::string const& strategy);
bool CanReadText(std::string const& strategy);

/// The choices kept between runs, one "path=value" a line
Values ParseSaved(std::string const& saved);
std::string FormatSaved(Values const& saved);
/// What a run starts from: what is installed with @p saved over it. A
/// saved value at a path the config no longer has is dropped, and a
/// strategy or preset it no longer has falls back to its own.
Values Effective(Installed const& installed, Values const& saved);
/// Of @p chosen, the values that are not as installed: what is kept, and
/// what the config is written with
Values Changes(Installed const& installed, Values const& chosen);

/// A stretch of time MagiaTimeline found a subtitle shown in
struct Interval {
	int start = 0, end = 0;
	/// What it read there with OCR; empty without it
	std::string text;
	/// Its name for the stretch, Subtitle_<kind>_<n>
	std::string name;
};

/// The stretches in what it wrote, in order of their start. Without OCR a
/// line's text is only its name, which is not kept as text.
std::vector<Interval> ReadTimeline(AssFile const& output);

/// Put the text 1.0 read with OCR, which it wrote to a file of its own
/// ("name,text" a line), on the stretches of those names
void AddReadText(std::vector<Interval> &intervals, std::string const& ocr_file);

/// What became of a run
struct Outcome {
	bool cancelled = false;
	/// What it printed, stdout and stderr together
	std::string output;
	/// It printed that it failed
	bool reported_error = false;
};

/// Whether @p output says the run failed, as MagiaTimeline says so: it
/// catches its errors, prints them and ends as if nothing were wrong
bool ReportsError(std::string const& output);

/// @p output as UTF-8: kept if it is, otherwise taken to be in the
/// system's code page
std::string Readable(std::string const& output);

/// @p output without the progress bars it draws (" 18%|██   | 1714/9740 [...]"),
/// which would push what went wrong out of sight
std::string WithoutProgress(std::string const& output);

/// The last @p count lines of @p output, for telling what went wrong
std::string LastLines(std::string const& output, size_t count);

/// How far through the stage it is in a run is, from what it has printed so
/// far: while building the timeline, the time of the frame it last
/// printed ("frame 00:03:21.45") out of @p duration_ms, the video's length;
/// while reading text, the stretches read ("Subtitle_Dialog_40", every
/// tenth) out of those it said it has. Nothing in a stage that tells no
/// progress, or when the video's length is unknown (0) while building.
std::optional<double> ProgressOf(std::string const& output, int duration_ms);

/// The stages of a run, as told apart by what it prints
enum class Stage {
	/// Loading, before it says which video it is on
	Starting,
	/// Going through the video for when the subtitles show
	Analysing,
	/// Reading the subtitles' text
	Reading,
	/// Writing the result
	Writing,
};

/// The stage a run is in, from what it has printed so far
Stage StageOf(std::string const& output);

/// Run @p launch on @p config in a progress dialog over @p parent, the
/// stage it is in and how long it has run shown as it goes (what it prints
/// is kept for telling what went wrong, not shown: it is mostly what the
/// programs it runs say, and the packaged one's own words come in bursts),
/// and how far it is through a video @p duration_ms long (0 if unknown)
/// where it tells, until it ends or is cancelled, when it and what it
/// started are killed. From the main thread.
Outcome Run(Launch const& launch, agi::fs::path const& config, wxWindow *parent, int duration_ms = 0);

}

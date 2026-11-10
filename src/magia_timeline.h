// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <libaegisub/fs.h>

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

/// @p config, MagiaTimeline's own config.yml, made to read @p video and
/// write @p destination (a path without its extension, .ass being added),
/// with @p strategy if not empty. Everything else is left as it is: the
/// strategies' settings are the user's. Nothing when the file has no
/// top-level source or destination to replace.
std::optional<std::string> WriteConfig(std::string const& config, std::string const& video,
                                       std::string const& destination, std::string const& strategy);

/// A stretch of time MagiaTimeline found a subtitle shown in
struct Interval {
	int start = 0, end = 0;
};

/// The stretches in what it wrote, in order of their start. Its lines carry
/// no text, only a name for each of them, which is not kept.
std::vector<Interval> ReadTimeline(AssFile const& output);

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

/// The last @p count lines of @p output, for telling what went wrong
std::string LastLines(std::string const& output, size_t count);

/// Run @p launch on @p config in a progress dialog over @p parent, its
/// latest line of output shown as it goes, until it ends or is cancelled,
/// when it and what it started are killed. From the main thread.
Outcome Run(Launch const& launch, agi::fs::path const& config, wxWindow *parent);

}

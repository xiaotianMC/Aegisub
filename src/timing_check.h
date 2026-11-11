// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The timing page's check (docs/style-panel.md#时间页): lines too short,
// too fast to read, overlapping another of their style, or a frame or two
// from a scene change.
#pragma once

#include <set>
#include <vector>

namespace agi::vfr { class Framerate; }
class AssDialogue;

struct TimingCheckLimits {
	int min_duration = 500; ///< Shorter, in ms, is too short
	int cps_warning = 15, cps_error = 30;
	bool ignore_whitespace = true, ignore_punctuation = true;
	/// A start or end this many frames from a scene change, or fewer but
	/// not none, missed it
	int cut_frames = 3;
};

struct TimingIssue {
	enum class Kind { Short, Fast, Overlap, NearCut };
	Kind kind = Kind::Short;
	int id = 0;    ///< The line's Id
	int row = 0;   ///< Its row, from 0
	int start = 0; ///< Its start time
	/// Short: its length in ms; Fast: characters per second; Overlap: by
	/// how many ms; NearCut: frames to the cut, positive when it is later
	int value = 0;
	bool severe = false;  ///< Fast: at the error threshold
	bool end = false;     ///< NearCut: its end rather than its start
	int other_row = -1;   ///< Overlap: the row it overlaps
	int time = 0;         ///< Where it is: the start, or for an end the end
};

/// The issues of @p lines (the file's, in order; comments are left out),
/// only those of the lines whose Id is in @p only if it is given, though
/// overlaps are found against every line. Lines without keyframes or
/// timecodes are not checked against scene changes. In row order, a row's
/// issues in the order of their kinds.
std::vector<TimingIssue> FindTimingIssues(std::vector<AssDialogue const*> const& lines, std::set<int> const* only,
	TimingCheckLimits const& limits, std::vector<int> const& keyframes, int frame_count, agi::vfr::Framerate const& fps);

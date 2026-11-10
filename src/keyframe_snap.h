// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <vector>

namespace agi::vfr { class Framerate; }
struct AssDialogueBase;

/// How far, in milliseconds, a line's start or end may move back or forward
/// to land on a keyframe
struct KeyframeSnapThresholds {
	int before_start = 0, after_start = 0, before_end = 0, after_end = 0;
	/// Those the timing post-processor keeps
	static KeyframeSnapThresholds FromOptions();
};

/// Move each line's start to a keyframe and its end to the frame before one,
/// when the nearest is within @p limits. A line's frames between its start
/// and end are left as they are: it is not split at a keyframe inside it.
/// @p frame_count, if positive, ends the last scene.
/// @return How many starts and how many ends were moved
struct KeyframeSnapResult { int starts = 0, ends = 0; };
KeyframeSnapResult SnapToKeyframes(std::vector<AssDialogueBase*> const& rows, std::vector<int> keyframes, int frame_count,
	agi::vfr::Framerate const& fps, KeyframeSnapThresholds const& limits);

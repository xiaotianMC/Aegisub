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
	/// Those Snap to Nearby Scene Changes and the style panel keep: a start
	/// moved earlier or an end later may go further than one moved in, which
	/// would cover a word or cut one off
	static KeyframeSnapThresholds FromSceneSnap();
	/// How far a start (@p end false) or end may move @p later, or earlier
	int Limit(bool end, bool later) const;
};

/// One distance either way, Timing/Scene Snap Distance, became four: a
/// distance set there goes on outward and, no more than 100 ms, inward
void MigrateSceneSnapDistance();

/// The keyframe in @p keyframes (sorted, not empty) nearest @p frame: the
/// earlier one when two are as near
int ClosestKeyframe(std::vector<int> const& keyframes, int frame);

/// Move each line's start to a keyframe and its end to the frame before one,
/// when the nearest is within @p limits. A line's frames between its start
/// and end are left as they are: it is not split at a keyframe inside it.
/// @p frame_count, if positive, ends the last scene.
/// @return How many starts and how many ends were moved
struct KeyframeSnapResult { int starts = 0, ends = 0; };
KeyframeSnapResult SnapToKeyframes(std::vector<AssDialogueBase*> const& rows, std::vector<int> keyframes, int frame_count,
	agi::vfr::Framerate const& fps, KeyframeSnapThresholds const& limits);

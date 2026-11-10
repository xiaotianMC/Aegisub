// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Timing shared by the style panel's timing page and the commands and
// dialogs that came before it (docs/style-panel.md#时间页): lead-in and
// lead-out, characters per second, and the scene change near a line's end.
#pragma once

#include <string_view>
#include <vector>

namespace agi::vfr { class Framerate; }
class AssDialogue;

/// Move each of @p targets' starts @p lead_in earlier and ends @p lead_out
/// later, the lead-ins first, but not over a line of @p bounds that it did
/// not already overlap: one before it in @p bounds for a start, one after
/// it for an end. Both are sorted by start; @p bounds may hold @p targets
/// and lines that are not to change. The timing post-processor passes the
/// same lines as both.
void AddLead(std::vector<AssDialogue*> const& targets, std::vector<AssDialogue*> const& bounds, int lead_in, int lead_out);

/// Characters per second of @p text over @p duration_ms, counting as the
/// subtitle grid's column does: -1 where it shows nothing (a line of 100 ms
/// or less, or one with more bytes than milliseconds)
int CharactersPerSecond(std::string_view text, int duration_ms, bool ignore_whitespace, bool ignore_punctuation);

/// The scene change nearest a line's start or end
struct SceneCut {
	/// Where the line's start or end would go: a cut's first frame for a
	/// start, the frame before it for an end, and that frame's time
	int frame = 0, time = 0;
	/// How many frames away it is: positive when later than the line's
	int frames = 0;
};
/// The cut nearest the start (@p end false) or end at @p ms, as
/// SnapToKeyframes finds it; @p frame_count, if positive, ends the last
/// scene. Nothing when there are no keyframes.
bool NearestSceneCut(std::vector<int> keyframes, int frame_count, agi::vfr::Framerate const& fps, int ms, bool end, SceneCut &cut);

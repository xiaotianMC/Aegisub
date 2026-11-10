// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <optional>
#include <string>
#include <string_view>

/// One of the moving effects a line's Effect field can hold, as the renderers
/// read it (libass ass_apply_transition_effects, VSFilter):
///   Banner;delay[;left to right[;fade width]]
///   Scroll up;y1;y2;delay[;fade height]
///   Scroll down;y1;y2;delay[;fade height]
/// The delay is milliseconds per pixel of the script's resolution. The fades
/// are VSFilter's alone; libass ignores them. Anything else in the field
/// (karaoke templates, notes) is not one of these, and is left alone.
struct ScrollEffect {
	enum class Kind { Banner, ScrollUp, ScrollDown };
	Kind kind = Kind::ScrollUp;
	int delay = 1;
	bool left_to_right = false; ///< Banner only
	int y1 = 0, y2 = 0;         ///< Scroll only, in either order
	std::optional<int> fade;

	bool IsScroll() const { return kind != Kind::Banner; }
	int Top() const { return y1 < y2 ? y1 : y2; }
	int Bottom() const { return y1 < y2 ? y2 : y1; }
	bool operator==(ScrollEffect const&) const = default;
};

/// The effect @p effect holds, or none if it holds anything else. As libass
/// reads it: the name exactly (case matters), then whole numbers after each
/// ';', a Banner needing one and a scroll three.
std::optional<ScrollEffect> ParseScrollEffect(std::string_view effect);
/// The Effect field for @p effect; ParseScrollEffect reads it back the same
std::string FormatScrollEffect(ScrollEffect const& effect);

/// The delay the renderers use: whole milliseconds, at least one
int EffectiveDelay(int delay);
/// How far the text has moved @p ms after the line starts, in script pixels
double ScrollShift(ScrollEffect const& effect, int ms);
/// The delay that moves the text @p shift script pixels in @p duration_ms
int DelayForShift(int duration_ms, double shift);
/// A new effect of @p kind for a line @p duration_ms long in a script
/// @p script_w × @p script_h: a scroll over the lower half, or a banner
/// right to left, crossing it by the line's end
ScrollEffect DefaultScrollEffect(ScrollEffect::Kind kind, int duration_ms, int script_w, int script_h);
/// The delay that crosses @p effect's area (a scroll) or the screen (a
/// banner) in @p duration_ms
int CrossingDelay(ScrollEffect const& effect, int duration_ms, int script_w);

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "scroll_effect.h"

#include <libaegisub/format.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace {
constexpr int max_delay = 1000000;

/// The numbers after each ';', up to four, read as atoi reads them
std::vector<int> Numbers(std::string_view effect) {
	std::vector<int> values;
	std::string copy(effect);
	for (size_t pos = copy.find(';'); pos != std::string::npos && values.size() < 4; pos = copy.find(';', pos + 1))
		values.push_back(std::atoi(copy.c_str() + pos + 1));
	return values;
}
}

std::optional<ScrollEffect> ParseScrollEffect(std::string_view effect) {
	ScrollEffect result;
	if (effect.starts_with("Banner;")) {
		auto v = Numbers(effect);
		if (v.empty()) return std::nullopt;
		result.kind = ScrollEffect::Kind::Banner;
		result.delay = v[0];
		result.left_to_right = v.size() >= 2 && v[1];
		if (v.size() >= 3) result.fade = v[2];
		return result;
	}
	if (effect.starts_with("Scroll up;")) result.kind = ScrollEffect::Kind::ScrollUp;
	else if (effect.starts_with("Scroll down;")) result.kind = ScrollEffect::Kind::ScrollDown;
	else return std::nullopt;
	auto v = Numbers(effect);
	if (v.size() < 3) return std::nullopt;
	result.y1 = v[0];
	result.y2 = v[1];
	result.delay = v[2];
	if (v.size() >= 4) result.fade = v[3];
	return result;
}

std::string FormatScrollEffect(ScrollEffect const& effect) {
	if (effect.kind == ScrollEffect::Kind::Banner) {
		std::string out = agi::format("Banner;%d", effect.delay);
		if (effect.left_to_right || effect.fade) out += agi::format(";%d", effect.left_to_right ? 1 : 0);
		if (effect.fade) out += agi::format(";%d", *effect.fade);
		return out;
	}
	std::string out = agi::format("%s;%d;%d;%d", effect.kind == ScrollEffect::Kind::ScrollUp ? "Scroll up" : "Scroll down",
		effect.y1, effect.y2, effect.delay);
	if (effect.fade) out += agi::format(";%d", *effect.fade);
	return out;
}

int EffectiveDelay(int delay) {
	return std::max(delay, 1);
}

double ScrollShift(ScrollEffect const& effect, int ms) {
	return std::max(ms, 0) / static_cast<double>(EffectiveDelay(effect.delay));
}

int DelayForShift(int duration_ms, double shift) {
	if (shift <= 0 || duration_ms <= 0) return max_delay;
	return std::clamp(static_cast<int>(std::lround(duration_ms / shift)), 1, max_delay);
}

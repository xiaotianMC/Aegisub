// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "timing_adjust.h"

#include "ass_dialogue.h"
#include "keyframe_snap.h"

#include <libaegisub/character_count.h>
#include <libaegisub/vfr.h>

#include <algorithm>

void AddLead(std::vector<AssDialogue*> const& targets, std::vector<AssDialogue*> const& bounds, int lead_in, int lead_out) {
	auto place = [&](AssDialogue *line) {
		return std::find(bounds.begin(), bounds.end(), line) - bounds.begin();
	};
	// A line it does not collide with stops it at its edge
	if (lead_in) {
		for (auto line : targets) {
			int start = line->Start - lead_in;
			auto const at = place(line);
			for (long i = 0; i < at; ++i)
				if (!line->CollidesWith(bounds[i])) start = std::max<int>(start, bounds[i]->End);
			line->Start = start;
		}
	}
	if (lead_out) {
		for (auto line : targets) {
			int end = line->End + lead_out;
			auto const at = place(line);
			for (auto i = at + 1; i < static_cast<long>(bounds.size()); ++i)
				if (!line->CollidesWith(bounds[i])) end = std::min<int>(end, bounds[i]->Start);
			line->End = end;
		}
	}
}

int CharactersPerSecond(std::string_view text, int duration_ms, bool ignore_whitespace, bool ignore_punctuation) {
	if (duration_ms <= 100 || text.size() > static_cast<size_t>(duration_ms))
		return -1;
	int ignore = agi::IGNORE_BLOCKS;
	if (ignore_whitespace) ignore |= agi::IGNORE_WHITESPACE;
	if (ignore_punctuation) ignore |= agi::IGNORE_PUNCTUATION;
	return static_cast<int>(agi::CharacterCount(text, ignore) * 1000 / duration_ms);
}

bool NearestSceneCut(std::vector<int> kf, int frame_count, agi::vfr::Framerate const& fps, int ms, bool end, SceneCut &cut) {
	if (kf.empty() || !fps.IsLoaded()) return false;
	if (frame_count > 0) kf.push_back(frame_count - 1);
	std::sort(kf.begin(), kf.end());
	if (end) {
		int const frame = fps.FrameAtTime(ms, agi::vfr::END);
		cut.frame = ClosestKeyframe(kf, frame) - 1;
		cut.time = fps.TimeAtFrame(cut.frame, agi::vfr::END);
		cut.frames = cut.frame - frame;
	}
	else {
		int const frame = fps.FrameAtTime(ms, agi::vfr::START);
		cut.frame = ClosestKeyframe(kf, frame);
		cut.time = fps.TimeAtFrame(cut.frame, agi::vfr::START);
		cut.frames = cut.frame - frame;
	}
	return true;
}

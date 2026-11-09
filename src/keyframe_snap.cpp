// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "keyframe_snap.h"

#include "ass_dialogue.h"
#include "options.h"

#include <libaegisub/vfr.h>

#include <algorithm>

KeyframeSnapThresholds KeyframeSnapThresholds::FromOptions() {
	KeyframeSnapThresholds t;
	t.before_start = OPT_GET("Tool/Timing Post Processor/Threshold/Key Start Before")->GetInt();
	t.after_start = OPT_GET("Tool/Timing Post Processor/Threshold/Key Start After")->GetInt();
	t.before_end = OPT_GET("Tool/Timing Post Processor/Threshold/Key End Before")->GetInt();
	t.after_end = OPT_GET("Tool/Timing Post Processor/Threshold/Key End After")->GetInt();
	return t;
}

namespace {
int ClosestKeyframe(std::vector<int> const& kf, int frame) {
	auto const pos = std::upper_bound(kf.begin(), kf.end(), frame);
	// The last keyframe if this is after it
	if (pos == kf.end()) return kf.back();
	// *pos is after frame, and *(pos - 1) at or before it
	return (pos == kf.begin() || *pos - frame < frame - *(pos - 1)) ? *pos : *(pos - 1);
}
}

void SnapToKeyframes(std::vector<AssDialogueBase*> const& rows, std::vector<int> kf, int frame_count,
	agi::vfr::Framerate const& fps, KeyframeSnapThresholds const& limits) {
	if (frame_count > 0) kf.push_back(frame_count - 1);
	if (kf.empty()) return;
	std::sort(kf.begin(), kf.end());

	for (auto cur : rows) {
		int const startF = fps.FrameAtTime(cur->Start, agi::vfr::START);
		int const endF = fps.FrameAtTime(cur->End, agi::vfr::END);

		int closest = ClosestKeyframe(kf, startF);
		int time = fps.TimeAtFrame(closest, agi::vfr::START);
		if ((closest > startF && time - cur->Start <= limits.before_start) || (closest < startF && cur->Start - time <= limits.after_start))
			cur->Start = time;

		closest = ClosestKeyframe(kf, endF) - 1;
		time = fps.TimeAtFrame(closest, agi::vfr::END);
		if ((closest > endF && time - cur->End <= limits.before_end) || (closest < endF && cur->End - time <= limits.after_end))
			cur->End = time;
	}
}

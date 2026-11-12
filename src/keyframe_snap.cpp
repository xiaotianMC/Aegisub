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

KeyframeSnapThresholds KeyframeSnapThresholds::FromSceneSnap() {
	KeyframeSnapThresholds t;
	t.after_start = OPT_GET("Timing/Scene Snap/Start Earlier")->GetInt();
	t.before_start = OPT_GET("Timing/Scene Snap/Start Later")->GetInt();
	t.after_end = OPT_GET("Timing/Scene Snap/End Earlier")->GetInt();
	t.before_end = OPT_GET("Timing/Scene Snap/End Later")->GetInt();
	return t;
}

int KeyframeSnapThresholds::Limit(bool end, bool later) const {
	// before_*: the line's start or end is before the keyframe, so moves later
	if (end) return later ? before_end : after_end;
	return later ? before_start : after_start;
}

void MigrateSceneSnapDistance() {
	int const old = static_cast<int>(OPT_GET("Timing/Scene Snap Distance")->GetInt());
	if (old < 0) return;
	OPT_SET("Timing/Scene Snap/Start Earlier")->SetInt(old);
	OPT_SET("Timing/Scene Snap/End Later")->SetInt(old);
	OPT_SET("Timing/Scene Snap/Start Later")->SetInt(std::min(old, 100));
	OPT_SET("Timing/Scene Snap/End Earlier")->SetInt(std::min(old, 100));
	OPT_SET("Timing/Scene Snap Distance")->SetInt(-1);
}

int ClosestKeyframe(std::vector<int> const& kf, int frame) {
	auto const pos = std::upper_bound(kf.begin(), kf.end(), frame);
	// The last keyframe if this is after it
	if (pos == kf.end()) return kf.back();
	// *pos is after frame, and *(pos - 1) at or before it
	return (pos == kf.begin() || *pos - frame < frame - *(pos - 1)) ? *pos : *(pos - 1);
}

KeyframeSnapResult SnapToKeyframes(std::vector<AssDialogueBase*> const& rows, std::vector<int> kf, int frame_count,
	agi::vfr::Framerate const& fps, KeyframeSnapThresholds const& limits) {
	KeyframeSnapResult result;
	if (frame_count > 0) kf.push_back(frame_count - 1);
	if (kf.empty()) return result;
	std::sort(kf.begin(), kf.end());

	for (auto cur : rows) {
		int const old_start = cur->Start, old_end = cur->End;
		int const startF = fps.FrameAtTime(old_start, agi::vfr::START);
		int const endF = fps.FrameAtTime(old_end, agi::vfr::END);

		int new_start = old_start, new_end = old_end;
		int closest = ClosestKeyframe(kf, startF);
		int time = fps.TimeAtFrame(closest, agi::vfr::START);
		if ((closest > startF && time - old_start <= limits.before_start) || (closest < startF && old_start - time <= limits.after_start))
			new_start = time;

		closest = ClosestKeyframe(kf, endF) - 1;
		time = fps.TimeAtFrame(closest, agi::vfr::END);
		if ((closest > endF && time - old_end <= limits.before_end) || (closest < endF && old_end - time <= limits.after_end))
			new_end = time;

		// A short line about one cut would have its start moved on to the
		// cut and its end back to it, leaving nothing: only the end that
		// keeps more of it moves, or neither
		if (new_end <= new_start) {
			int const keep_start = new_end > old_start ? new_end - old_start : 0;
			int const keep_end = old_end > new_start ? old_end - new_start : 0;
			if (keep_start == 0 && keep_end == 0) new_start = old_start, new_end = old_end;
			else if (keep_start >= keep_end) new_start = old_start;
			else new_end = old_end;
		}

		if (new_start != old_start) ++result.starts;
		if (new_end != old_end) ++result.ends;
		cur->Start = new_start;
		cur->End = new_end;
	}
	return result;
}

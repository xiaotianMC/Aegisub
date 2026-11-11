// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "timing_check.h"

#include "ass_dialogue.h"
#include "timing_adjust.h"

#include <libaegisub/vfr.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>

std::vector<TimingIssue> FindTimingIssues(std::vector<AssDialogue const*> const& lines, std::set<int> const* only,
	TimingCheckLimits const& limits, std::vector<int> const& keyframes, int frame_count, agi::vfr::Framerate const& fps)
{
	std::vector<TimingIssue> issues;
	auto wanted = [&](AssDialogue const* line) { return !only || only->count(line->Id); };
	auto issue = [](AssDialogue const* line, TimingIssue::Kind kind, int value) {
		TimingIssue i;
		i.kind = kind;
		i.id = line->Id;
		i.row = line->Row;
		i.start = line->Start;
		i.time = line->Start;
		i.value = value;
		return i;
	};
	bool const cuts = !keyframes.empty() && fps.IsLoaded() && limits.cut_frames > 0;
	auto const cut_frames = cuts ? SceneCutFrames(keyframes, frame_count) : std::vector<int>();

	for (auto line : lines) {
		if (line->Comment || !wanted(line)) continue;
		int const start = line->Start, end = line->End;
		int const length = end - start;
		if (length >= 0 && length < limits.min_duration)
			issues.push_back(issue(line, TimingIssue::Kind::Short, length));
		int const cps = CharactersPerSecond(line->Text.get(), length, limits.ignore_whitespace, limits.ignore_punctuation);
		if (cps > limits.cps_warning) {
			auto fast = issue(line, TimingIssue::Kind::Fast, cps);
			fast.severe = cps >= std::max(limits.cps_error, limits.cps_warning);
			issues.push_back(fast);
		}
		if (cuts) {
			for (bool at_end : {false, true}) {
				SceneCut cut;
				if (!NearestSceneCutIn(cut_frames, fps, at_end ? end : start, at_end, cut)) continue;
				if (cut.frames == 0 || std::abs(cut.frames) > limits.cut_frames) continue;
				auto near = issue(line, TimingIssue::Kind::NearCut, cut.frames);
				near.end = at_end;
				near.time = at_end ? end : start;
				issues.push_back(near);
			}
		}
	}

	// Overlaps, among the lines of each style in the order they show: the
	// later of two carries it
	std::map<std::string, std::vector<AssDialogue const*>> by_style;
	for (auto line : lines)
		if (!line->Comment) by_style[line->Style.get()].push_back(line);
	for (auto &[style, group] : by_style) {
		std::stable_sort(group.begin(), group.end(), [](AssDialogue const* a, AssDialogue const* b) { return a->Start < b->Start; });
		// The line that ends last so far: a long line overlaps every short
		// one inside it, not only the next
		AssDialogue const* longest = nullptr;
		for (auto line : group) {
			if (longest && line->Start < longest->End && wanted(line)) {
				auto overlap = issue(line, TimingIssue::Kind::Overlap, std::min<int>(longest->End, line->End) - line->Start);
				overlap.other_row = longest->Row;
				issues.push_back(overlap);
			}
			if (!longest || line->End > longest->End) longest = line;
		}
	}

	std::stable_sort(issues.begin(), issues.end(), [](TimingIssue const& a, TimingIssue const& b) {
		if (a.row != b.row) return a.row < b.row;
		if (a.kind != b.kind) return a.kind < b.kind;
		return !a.end && b.end;
	});
	return issues;
}

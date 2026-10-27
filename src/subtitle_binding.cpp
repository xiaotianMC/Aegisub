// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "subtitle_binding.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"

#include <array>
#include <random>
#include <sstream>
#include <iomanip>
#include <set>
#include <optional>
#include <wx/intl.h>

namespace {
constexpr auto key = "aegisub.enhanced.subtitle-binding";

void SetBinding(AssFile& file, AssDialogue& line, std::optional<uint32_t> binding) {
    auto ids = line.ExtradataIds.get();
    for (auto const& entry : file.GetExtradata(ids)) {
        if (entry.key == key) std::erase(ids, entry.id);
    }
    if (binding) ids.push_back(*binding);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    line.ExtradataIds = std::move(ids);
}
}

std::string GetSubtitleBinding(const AssFile& file, const AssDialogue& line) {
    for (auto const& entry : file.GetExtradata(line.ExtradataIds.get()))
        if (entry.key == key) return entry.value;
    return {};
}

int SubtitleBindingColorIndex(std::string const& group) {
    return group.size() > 1 && group[0] >= '0' && group[0] <= '7' && group[1] == ':' ? group[0] - '0' : 0;
}

void BindSelectedSubtitles(agi::Context *c) {
    auto const& selection = c->selectionController->GetSelectedSet();
    if (selection.size() < 2) return;
    auto& file = *c->ass;
    std::set<std::string> merging;
    std::array<int, 8> colors{};
    std::set<std::string> all_groups;
    for (auto& line : file.Events) {
        auto group = GetSubtitleBinding(file, line);
        if (group.empty()) continue;
        if (all_groups.insert(group).second) ++colors[SubtitleBindingColorIndex(group)];
        if (selection.count(&line)) merging.insert(group);
    }
    if (merging.size() == 1 && std::all_of(selection.begin(), selection.end(), [&](auto line) {
        return GetSubtitleBinding(file, *line) == *merging.begin();
    })) return;
    int color = merging.empty() ? int(std::min_element(colors.begin(), colors.end()) - colors.begin())
        : SubtitleBindingColorIndex(*merging.begin());
    std::random_device random;
    std::ostringstream token;
    token << color << ':' << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) token << std::setw(8) << random();
    auto group = token.str();
    auto id = file.AddExtradata(key, group);
    for (auto& line : file.Events) {
        if (selection.count(&line) || merging.count(GetSubtitleBinding(file, line)))
            SetBinding(file, line, id);
    }
    file.Commit(_("bind subtitles"), AssFile::COMMIT_EXTRADATA | AssFile::COMMIT_DIAG_META);
}

void UnbindSelectedSubtitles(agi::Context *c) {
    auto& file = *c->ass;
    bool changed = false;
    for (auto line : c->selectionController->GetSelectedSet()) {
        if (!GetSubtitleBinding(file, *line).empty()) {
            SetBinding(file, *line, std::nullopt);
            changed = true;
        }
    }
    if (!changed) return;
    // A remaining singleton has no binding partner.
    std::map<std::string, std::vector<AssDialogue*>> groups;
    for (auto& line : file.Events) {
        auto group = GetSubtitleBinding(file, line);
        if (!group.empty()) groups[group].push_back(&line);
    }
    for (auto const& [group, members] : groups)
        if (members.size() == 1) SetBinding(file, *members.front(), std::nullopt);
    file.Commit(_("unbind subtitles"), AssFile::COMMIT_EXTRADATA | AssFile::COMMIT_DIAG_META);
}

bool AssFile::SynchronizeBoundDurations(int type, AssDialogue *preferred) {
    if (type != COMMIT_NEW && !(type & (COMMIT_DIAG_TIME | COMMIT_DIAG_ADDREM | COMMIT_EXTRADATA)))
        return false;
    std::vector<BoundSubtitleTiming> times;
    std::vector<AssDialogue*> lines;
    for (auto& line : Events) {
        auto group = GetSubtitleBinding(*this, line);
        if (group.empty()) continue;
        int duration = int(line.End) - int(line.Start);
        auto previous = bound_durations.find(line.Id);
        times.push_back({std::move(group), previous == bound_durations.end() ? duration : previous->second,
            int(line.Start), int(line.End), &line == preferred});
        lines.push_back(&line);
    }
    bool changed = type != COMMIT_NEW && (type & COMMIT_DIAG_TIME)
        && SynchronizeBoundSubtitleDurations(times);
    bound_durations.clear();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (changed) lines[i]->End = times[i].end;
        bound_durations[lines[i]->Id] = int(lines[i]->End) - int(lines[i]->Start);
    }
    return changed;
}

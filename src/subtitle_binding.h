// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

class AssFile;
class AssDialogue;
namespace agi { struct Context; }

std::string GetSubtitleBinding(const AssFile& file, const AssDialogue& line);
int SubtitleBindingColorIndex(std::string const& group);
void BindSelectedSubtitles(agi::Context *c);
void UnbindSelectedSubtitles(agi::Context *c);

struct BoundSubtitleTiming {
    std::string group;
    int previous_duration;
    int start;
    int end;
    bool preferred = false;
};

// Keep each group's duration offsets. Explicit multi-line edits are applied
// once per group, with the single edited line (or first changed row) as driver.
inline bool SynchronizeBoundSubtitleDurations(std::vector<BoundSubtitleTiming>& lines) {
    struct Adjustment {
        int64_t delta = 0;
        int64_t minimum = std::numeric_limits<int64_t>::min();
        int64_t maximum = std::numeric_limits<int64_t>::max();
        bool changed = false;
        bool preferred = false;
    };
    std::map<std::string, Adjustment> groups;
    for (auto const& line : lines) {
        if (line.group.empty()) continue;
        auto& group = groups[line.group];
        int64_t delta = int64_t(line.end) - line.start - line.previous_duration;
        if (delta && !group.preferred && (!group.changed || line.preferred)) {
            group.delta = delta;
            group.changed = true;
            group.preferred = line.preferred;
        }
        group.minimum = std::max(group.minimum, -int64_t(line.previous_duration));
        group.maximum = std::min(group.maximum, int64_t(35999990) - line.start - line.previous_duration);
    }
    bool changed = false;
    for (auto& line : lines) {
        if (line.group.empty()) continue;
        auto const& group = groups.at(line.group);
        if (!group.changed || group.minimum > group.maximum) continue;
        int end = int(int64_t(line.start) + line.previous_duration
            + std::clamp(group.delta, group.minimum, group.maximum));
        changed |= line.end != end;
        line.end = end;
    }
    return changed;
}

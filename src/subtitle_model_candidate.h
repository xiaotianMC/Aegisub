// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include "ass_attachment.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_info.h"
#include "ass_style.h"
#include "subtitle_binding.h"
#include <algorithm>
#include <limits>
#include <memory>
#include <iterator>

class SubtitleModelInstallation;
// Immutable prepared input. All mutable source nodes remain independent of live.
class SubtitleModelCandidate {
    friend class SubtitleModelInstallation;
    std::unique_ptr<AssFile> file;
    std::set<int> selected;
    int active = 0;
    bool cancelled = false, consumed = false;
public:
    explicit SubtitleModelCandidate(AssFile const& source) : file(std::make_unique<AssFile>(source)) {
        file->Properties=source.Properties;
        auto original=source.Events.begin();
        for (auto& row : file->Events) row.Id=(original++)->Id;
        if (file->Styles.empty()) file->Styles.push_back(*new AssStyle);
        if (file->Events.empty()) file->Events.push_back(*new AssDialogue);
        std::set<int> ids;
        std::set<uint32_t> extras;
        for (auto const& entry : file->Extradata) {
            if (entry.id == std::numeric_limits<uint32_t>::max() || !extras.insert(entry.id).second)
                throw std::invalid_argument("Invalid candidate extradata identity");
            file->next_extradata_id=std::max(file->next_extradata_id,entry.id+1);
        }
        int row_number=0;
        for (auto& row : file->Events) {
            if (row_number==std::numeric_limits<int>::max()) throw std::overflow_error("Candidate row count exhausted");
            if (row.Id <= 0 || !ids.insert(row.Id).second) throw std::invalid_argument("Invalid candidate row identity");
            row.Row=row_number++;
            for (auto id : row.ExtradataIds.get())
                if (!extras.contains(id)) throw std::invalid_argument("Missing candidate extradata reference");
        }
        auto index=std::clamp(file->Properties.active_row,0,row_number-1);
        auto row=&*std::next(file->Events.begin(),index);
        active=row->Id;
        for (auto selected_row : ExpandBoundSelection(*file,{row})) selected.insert(selected_row->Id);
        file->Properties.active_row=index;
    }
    SubtitleModelCandidate(SubtitleModelCandidate const&) = delete;
    SubtitleModelCandidate& operator=(SubtitleModelCandidate const&) = delete;
    SubtitleModelCandidate(SubtitleModelCandidate&&) noexcept = default;
    AssFile const& Read() const { if (!file) throw std::logic_error("Moved candidate"); return *file; }
    std::set<int> const& SelectedIDs() const { return selected; }
    int ActiveID() const { return active; }
    void Cancel() noexcept { cancelled=true; }
    bool Available() const noexcept { return file && !cancelled && !consumed; }
};

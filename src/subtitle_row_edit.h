// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include "subtitle_project_access.h"
#include <set>

// Mutable pointers here refer exclusively to owned values, never live nodes.
// Callers retain their existing WriteScope and Undo/Commit boundaries.
class SubtitleRowEdit {
    std::vector<std::unique_ptr<AssDialogue>> owned;
public:
    std::vector<AssDialogue*> rows;
    std::set<int> selected;
    int active = 0;
    explicit SubtitleRowEdit(agi::Context const* c, bool all = false) {
        auto current = c->selectionController->ReadActiveLine();
        for (auto row : c->selectionController->ReadSelectedLines()) selected.insert(row->Id);
        for (auto const& row : c->subtitleAccess->Read().Events) {
            if (&row == current) active = row.Id;
            if (all || selected.contains(row.Id)) {
                owned.push_back(std::make_unique<AssDialogue>(static_cast<AssDialogueBase const&>(row)));
                rows.push_back(owned.back().get());
            }
        }
    }
    std::vector<AssDialogueBase> Values() const {
        std::vector<AssDialogueBase> values; values.reserve(rows.size());
        for (auto row : rows) values.push_back(*row);
        return values;
    }
};

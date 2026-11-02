// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include "subtitle_row_edit.h"

// Detached list for ordinary structural algorithms. It contains no live nodes.
// Mutable row pointers below belong exclusively to this independent file.
class SubtitleRowListEdit {
    agi::Context* c;
    std::optional<SubtitleProjectAccess::WriteScope> scope;
    agi::SubtitleEditGate::RevisionToken revision;
    std::vector<AssDialogueBase> originals;
    std::vector<std::unique_ptr<AssDialogue>> retired;
    bool applied = false;
    void Apply() {
        if (applied) return;
        std::vector<AssDialogueBase> values;
        for (auto const& row : file.Events) values.push_back(row);
        retired = c->subtitleAccess->Edit(scope->Lease()).ReplaceRows(revision, originals, values);
        applied = true;
    }
public:
    AssFile file;
    std::set<AssDialogue*> selection;
    AssDialogue* active = nullptr;
    explicit SubtitleRowListEdit(agi::Context* c)
    : c(c), scope(c->subtitleAccess->TryWrite()), revision(c->subtitleEditGate.Capture()) {
        if (!scope) throw std::logic_error("Row-list edit requires authorization");
        std::set<int> selected;
        for (auto row : c->selectionController->ReadSelectedLines()) selected.insert(row->Id);
        auto current = c->selectionController->ReadActiveLine();
        for (auto const& row : c->subtitleAccess->Read().Events) {
            originals.push_back(row);
            auto copy = std::make_unique<AssDialogue>(static_cast<AssDialogueBase const&>(row));
            if (selected.contains(row.Id)) selection.insert(copy.get());
            if (&row == current) active = copy.get();
            file.Events.push_back(*copy.release());
        }
    }
    std::vector<AssDialogue*> SortedSelection() const {
        std::vector<AssDialogue*> rows(selection.begin(), selection.end());
        std::sort(rows.begin(), rows.end(), [](auto a, auto b) { return a->Row < b->Row; });
        return rows;
    }
    int Commit(wxString const& message, int type) {
        Apply();
        return c->subtitleAccess->Edit(scope->Lease()).Commit(message, type);
    }
    template<class Row> void Select(std::set<Row*> const& chosen, AssDialogue const* current, bool expand = true) {
        Select(Selection(chosen.begin(), chosen.end()), current, expand);
    }
    void Select(Selection const& chosen, AssDialogue const* current, bool expand = true) {
        std::set<int> ids;
        for (auto row : chosen) ids.insert(row->Id);
        int active_id = current ? current->Id : 0;
        Apply();
        c->selectionController->SetSelectionAndActiveIDs(std::move(ids), active_id, expand);
    }
};

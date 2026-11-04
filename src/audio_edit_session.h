// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include "include/aegisub/context.h"
#include "subtitle_project_access.h"
#include "selection_controller.h"
#include <tuple>

// Independent UI snapshots and the shared pending audio task. No live row pointer
// is retained. The Karaoke view and its timing controller share this task only.
class AudioEditSession {
    agi::Context *context;
    std::optional<agi::SubtitleEditGate::RevisionToken> observed;
    std::vector<AssDialogueBase> baseline;
    std::optional<SubtitleProjectAccess::Interaction> interaction;
    bool stale = false;

    static auto Fields(AssDialogueBase const& r) {
        return std::make_tuple(r.Id, r.Row, r.Comment, r.Layer, r.Margin,
            r.Start.GetMilliseconds(), r.End.GetMilliseconds(), r.Style,
            r.Actor, r.Effect, r.ExtradataIds, r.Text);
    }
public:
    bool split_pending = false;
    std::optional<std::pair<int, int>> pending_times;

    explicit AudioEditSession(agi::Context *c) : context(c) { Refresh(); }

    void Refresh() {
        interaction.reset();
        stale = true;
        baseline.clear();
        split_pending = false;
        pending_times.reset();
        for (auto const& row : context->subtitleAccess->Read().Events)
            baseline.push_back(row);
        observed = context->subtitleEditGate.Capture();
        stale = false;
    }

    bool Begin() {
        if (stale) return false;
        if (interaction) {
            if (interaction->Current()) return true;
            interaction.reset();
            stale = true;
            return false;
        }
        if (!observed) return false;
        auto now = context->subtitleEditGate.Revision();
        if (!observed->Current()) {
            // A C1-A scope advances the generation even for a no-op command.
            // Only an idle cache with identical complete row data can rebind;
            // an existing, stale interaction above always fails closed.
            if (now.document != observed->Revision().document) { stale = true; return false; }
            auto expected = baseline.begin();
            for (auto const& row : context->subtitleAccess->Read().Events) {
                if (expected == baseline.end() || Fields(row) != Fields(*expected++)) { stale = true; return false; }
            }
            if (expected != baseline.end()) { stale = true; return false; }
        }
        std::vector<int> ids;
        ids.reserve(baseline.size());
        for (auto const& row : baseline) ids.push_back(row.Id);
        if (ids.empty()) return false;
        interaction = context->subtitleAccess->TryBeginInteraction(std::move(ids));
        if (interaction) observed = context->subtitleEditGate.Capture();
        return interaction.has_value();
    }

    std::optional<AssDialogueBase> Read(int id) const {
        if (!observed || context->subtitleEditGate.Revision().document != observed->Revision().document) return {};
        for (auto const& row : context->subtitleAccess->Read().Events)
            if (row.Id == id) return AssDialogueBase(row);
        return {};
    }

    std::optional<AssDialogueBase> Active(int previous_id = 0) const {
        // Undo announces the new document before installing its selection.
        // Compare addresses only; never dereference an old selection borrow.
        auto active = context->selectionController->ReadActiveLine();
        for (auto const& row : context->subtitleAccess->Read().Events)
            if (&row == active) return AssDialogueBase(row);
        return Read(previous_id);
    }

    void Update(std::vector<AssDialogueBase> const& rows) { interaction->UpdateRows(rows); }
    bool UpdateLinkingAdjacent(std::vector<AssDialogueBase> const& rows, SubtitleProjectAccess::AdjacentLink const& link) {
        return interaction->UpdateRowsLinkingAdjacent(rows, link);
    }
    int Commit(wxString const& description, int type, int amend = -1, int single = 0) {
        try {
            auto result = interaction->Commit(description, type, amend, single);
            baseline.clear();
            for (auto const& row : context->subtitleAccess->Read().Events) baseline.push_back(row);
            observed = context->subtitleEditGate.Capture();
            split_pending = false;
            pending_times.reset();
            return result;
        }
        catch (...) { End(); stale = true; throw; }
    }
    void End() noexcept { interaction.reset(); }
};

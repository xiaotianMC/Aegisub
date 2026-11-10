// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
// C2 Apply Engine: a comparison of the current document with a baseline,
// issued by SubsController. While it is valid it holds the edit gate's
// comparison session, so no writer (ordinary edit, Undo/Redo, Lua, load,
// save, metadata change) can run; its items therefore keep describing the live
// document. ApplyComparison publishes chosen items as one undoable step.
#include "subtitle_apply.h"
#include <libaegisub/subtitle_edit_gate.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

class AssFile;
class SubsController;

class SubtitleComparison {
    friend class SubsController;
    SubsController const* owner = nullptr;
    std::optional<agi::SubtitleEditGate::ComparisonSession> session;
    std::optional<agi::SubtitleEditGate::RevisionToken> expected;
    std::unique_ptr<AssFile> current, baseline;
    agi::ass::compare::Fields fields = 0;
    std::vector<agi::ass::compare::Item> items;
    std::vector<int> current_ids; // row IDs of current, in order
    SubtitleComparison();
public:
    SubtitleComparison(SubtitleComparison&&) noexcept;
    SubtitleComparison& operator=(SubtitleComparison&&) noexcept;
    ~SubtitleComparison();
    /// False once applied, ended or invalidated; holds no session then.
    bool Valid() const;
    /// Release the comparison session without applying anything.
    void End() noexcept;
    /// Independent copy of the document at the start of the comparison
    AssFile const& Current() const { return *current; }
    AssFile const& Baseline() const { return *baseline; }
    agi::ass::compare::Fields Fields() const { return fields; }
    std::vector<agi::ass::compare::Item> const& Items() const { return items; }
};

/// Thrown when a comparison cannot start or be applied; nothing was changed.
struct SubtitleApplyRejected : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
// C1-C1c-9: Lua macros run against a private work model and publish their
// result to the same document in one controlled step (see docs/lua-isolation-design.md).
#include <libaegisub/subtitle_edit_gate.h>

#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>
#include <wx/string.h>

class AssEntry;
class AssFile;
class SubsController;

/// Everything a Lua macro produced on its work model. Holds no live pointer.
struct LuaSubtitleResult {
    struct Step {
        wxString description;
        int type = 0;
        /// false only for the legacy "final changes without an undo description" case
        bool commit = true;
        /// The script's line list at this step. nullptr entries stand for the
        /// original Script Info entries, in their original order.
        std::vector<AssEntry const*> lines;
    };
    /// Undo points in order, then the changes after the last undo point
    std::vector<Step> steps;
    /// Owns every node created during the run (new lines, Script Info copies).
    /// Original lines remain owned by the work model.
    std::vector<std::unique_ptr<AssEntry>> pool;
    /// The script's final line list, used to resolve returned row indexes
    std::vector<AssEntry const*> final_lines;
};

/// One Lua macro run, issued by SubsController on the host thread. Owns an
/// independent snapshot of the document with stable row IDs; nothing in it
/// aliases the live model, so the script may freely modify it on any thread.
class LuaSubtitleTransaction {
    friend class SubsController;
    SubsController const* owner = nullptr;
    std::optional<agi::SubtitleEditGate::RevisionToken> expected;
    std::unique_ptr<AssFile> work;
    std::set<int> selected;
    int active = 0;
    LuaSubtitleTransaction();
public:
    LuaSubtitleTransaction(LuaSubtitleTransaction&&) noexcept;
    LuaSubtitleTransaction& operator=(LuaSubtitleTransaction&&) noexcept;
    ~LuaSubtitleTransaction();
    /// The run's private work model (never the live document)
    AssFile& Work() const { return *work; }
    std::set<int> const& SelectedIDs() const { return selected; }
    int ActiveID() const { return active; }
};

/// A validated, fully prepared Lua result. Publishing it is the only step that
/// touches the live document. Holds the exclusive borrow of the edit gate until
/// published or destroyed; destroying it unpublished leaves the document unchanged.
class LuaSubtitlePublication {
    friend class SubsController;
    struct Data;
    std::unique_ptr<Data> data;
    LuaSubtitlePublication();
public:
    LuaSubtitlePublication(LuaSubtitlePublication&&) noexcept;
    LuaSubtitlePublication& operator=(LuaSubtitlePublication&&) noexcept;
    ~LuaSubtitlePublication();
    /// Row IDs of the dialogue lines in the document once published (after the
    /// usual empty-file normalisation), in order
    std::vector<int> const& FinalEventIds() const;
    /// Whether the run changed the document at all
    bool Modifies() const;
};

/// Thrown when a Lua result cannot be published; the live document is unchanged.
struct LuaSubtitleRejected : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Copyright (c) 2010, Niels Martin Hansen
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#pragma once
#include <memory>

#include <libaegisub/signal.h>
#include <libaegisub/subtitle_edit_gate.h>
#include "subtitle_model_observers.h"

#include <set>
#include <vector>
#include <optional>
#include <thread>

class AssDialogue;
class AssFile;
using Selection = std::set<AssDialogue const*>;

namespace agi { struct Context; }

// Selection changes are UI state and require no subtitle WriteLease. Returned
// row views are read-only and valid only until the next model mutation.
class SelectionController {
protected:
    // Declared first: expires only after all owned bindings/cache members are destroyed.
    std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);
private:
    friend class SubtitleModelInstallation;
    SubtitleModelObservers<> AnnounceActiveLineChanged{"active"};
    SubtitleModelObservers<> AnnounceSelectedSetChanged{"selection"};
    AssFile *file;
    agi::SubtitleEditGate *gate;
    agi::SubtitleEditGate::RevisionToken lifetime;
    std::thread::id owner_thread = std::this_thread::get_id();
    std::set<int> selection;
    int active_id = 0;
    bool model_replaced = false;
    agi::signal::Connection commit_connection;
    void Check() const;
    Selection Validate(std::set<int> const&) const;
    void Notify(bool selected, bool active);
public:
    SelectionController(agi::Context*);
    SelectionController(AssFile&, agi::SubtitleEditGate&);
    int ActiveID() const;
    std::set<int> SelectedIDs() const;
    AssDialogue const* ReadActiveLine() const;
    AssDialogue const* ReadActiveLine(agi::SubtitleEditGate::RevisionToken const&) const;
    Selection ReadSelectedSet() const;
    std::vector<AssDialogue const*> ReadSelectedLines() const;
    std::vector<AssDialogue const*> ReadSelectedLines(agi::SubtitleEditGate::RevisionToken const&) const;
    std::vector<AssDialogue const*> ReadSortedSelection() const;

    // Active-only changes leave the selected set unchanged. Zero clears active.
    void SetActiveID(int);
    // Always announces Selection, including an identical set. Binding expansion
    // can be disabled for a temporary single-line operation.
    void SetSelectedIDs(std::set<int>, bool expand_bound_groups = true);
    // Installs both values, announces Selection, then Active if it changed.
    void SetSelectionAndActiveIDs(std::set<int>, int, bool expand_bound_groups = true);

    class PreparedSelection {
        std::set<int> selection;
        int active = 0;
        std::vector<int> model_ids;
        AssFile *owner;
        agi::SubtitleEditGate::RevisionToken revision;
        PreparedSelection(AssFile*, agi::SubtitleEditGate::RevisionToken);
        friend class SelectionController;
    public:
        PreparedSelection(PreparedSelection const&) = delete;
        PreparedSelection& operator=(PreparedSelection const&) = delete;
        PreparedSelection(PreparedSelection&&) noexcept;
        PreparedSelection& operator=(PreparedSelection&&) = delete;
    };
    PreparedSelection PrepareSilent(agi::SubtitleEditGate::ComparisonSession const&,
        std::vector<int> const& selected_ids, std::optional<int> active_id) const;
    // Validation precedes the no-allocation swap. No notifications or expansion.
    void InstallSilent(agi::SubtitleEditGate::ComparisonSession const&, PreparedSelection&&);
    void NextLine();
    void PrevLine();
    DEFINE_SIGNAL_ADDERS(AnnounceSelectedSetChanged, AddSelectionListener)
    // No row payload: each observer reads current state, including after nested navigation.
    DEFINE_SIGNAL_ADDERS(AnnounceActiveLineChanged, AddActiveLineListener)
};

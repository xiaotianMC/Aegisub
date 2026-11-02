// Copyright (c) 2014, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#include "selection_controller.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "include/aegisub/context.h"
#include "subtitle_binding.h"
#include "subs_controller.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>

SelectionController::SelectionController(agi::Context *c) : SelectionController(*c->ass, c->subtitleEditGate) { }
SelectionController::SelectionController(AssFile& model, agi::SubtitleEditGate& edit_gate)
: file(&model), gate(&edit_gate), lifetime(edit_gate.Capture()),
  commit_connection(model.AddCommitListener([this](int type, AssDialogue const*) {
      if (type == AssFile::COMMIT_NEW) model_replaced = true;
  })) { }

void SelectionController::Check() const {
    if (owner_thread != std::this_thread::get_id() || !lifetime.Alive())
        throw std::logic_error("Subtitle selection requires a live owner on its thread");
}
int SelectionController::ActiveID() const { Check(); return active_id; }
std::set<int> SelectionController::SelectedIDs() const { Check(); return selection; }
Selection SelectionController::Validate(std::set<int> const& ids) const {
    Check();
    std::set<int> seen;
    Selection rows;
    for (auto const& row : file->Events) {
        if (!seen.insert(row.Id).second) throw std::invalid_argument("Duplicate subtitle line identity");
        if (ids.contains(row.Id)) rows.insert(&row);
    }
    if (rows.size() != ids.size()) throw std::out_of_range("Selected subtitle line missing");
    return rows;
}
AssDialogue const* SelectionController::ReadActiveLine() const {
    Check();
    for (auto const& row : file->Events) if (row.Id == active_id) return &row;
    return nullptr;
}
Selection SelectionController::ReadSelectedSet() const {
    Check();
    Selection rows;
    for (auto const& row : file->Events) if (selection.contains(row.Id)) rows.insert(&row);
    return rows;
}
std::vector<AssDialogue const*> SelectionController::ReadSelectedLines() const {
    auto rows = ReadSelectedSet();
    return {rows.begin(), rows.end()};
}
AssDialogue const* SelectionController::ReadActiveLine(agi::SubtitleEditGate::RevisionToken const& token) const {
    Check();
    if (!gate->Authorizes(token)) throw std::logic_error("Stale subtitle selection observer");
    return ReadActiveLine();
}
std::vector<AssDialogue const*> SelectionController::ReadSelectedLines(agi::SubtitleEditGate::RevisionToken const& token) const {
    Check();
    if (!gate->Authorizes(token)) throw std::logic_error("Stale subtitle selection observer");
    return ReadSelectedLines();
}
std::vector<AssDialogue const*> SelectionController::ReadSortedSelection() const {
    auto rows = ReadSelectedLines();
    std::sort(rows.begin(), rows.end(), [](auto a, auto b) { return a->Row < b->Row; });
    return rows;
}
void SelectionController::Notify(bool selected, bool active) {
    // The existing model-notification boundary also rejects controlled writes
    // from selection observers. Nested UI selection remains synchronous.
    bool previous = file->notifying;
    file->notifying = true;
    try {
        if (selected) AnnounceSelectedSetChanged();
        if (active) AnnounceActiveLineChanged();
    }
    catch (...) { file->notifying = previous; throw; }
    file->notifying = previous;
}
void SelectionController::SetActiveID(int id) {
    Validate(id ? std::set<int>{id} : std::set<int>{});
    bool changed = active_id != id || (model_replaced && active_id != 0);
    active_id = id;
    model_replaced = false;
    if (auto row = ReadActiveLine()) file->Properties.active_row = row->Row;
    if (changed) Notify(false, true);
}
void SelectionController::SetSelectedIDs(std::set<int> ids, bool expand) {
    auto rows = Validate(ids);
    if (expand) for (auto row : ExpandBoundSelection(*file, rows)) ids.insert(row->Id);
    selection.swap(ids);
    Notify(true, false);
}
void SelectionController::SetSelectionAndActiveIDs(std::set<int> ids, int id, bool expand) {
    auto rows = Validate(ids);
    if (id) Validate({id});
    if (expand) for (auto row : ExpandBoundSelection(*file, rows)) ids.insert(row->Id);
    bool changed = active_id != id || (model_replaced && active_id != 0);
    selection.swap(ids);
    active_id = id;
    model_replaced = false;
    if (auto row = ReadActiveLine()) file->Properties.active_row = row->Row;
    Notify(true, changed);
}
SelectionController::PreparedSelection::PreparedSelection(AssFile* owner, agi::SubtitleEditGate::RevisionToken revision)
: owner(owner), revision(std::move(revision)) { }
SelectionController::PreparedSelection::PreparedSelection(PreparedSelection&& other) noexcept
: selection(std::move(other.selection)), active(other.active), model_ids(std::move(other.model_ids)),
  owner(std::exchange(other.owner, nullptr)), revision(std::move(other.revision)) { }
SelectionController::PreparedSelection SelectionController::PrepareSilent(
    agi::SubtitleEditGate::ComparisonSession const& session, std::vector<int> const& ids, std::optional<int> active) const {
    Check();
    if (!gate->Authorizes(session)) throw std::logic_error("Invalid subtitle selection preparation session");
    PreparedSelection prepared(file, session.Capture());
    prepared.selection.insert(ids.begin(), ids.end());
    Validate(prepared.selection);
    if (active) { Validate({*active}); prepared.active = *active; }
    for (auto const& row : file->Events) prepared.model_ids.push_back(row.Id);
    return prepared;
}
void SelectionController::InstallSilent(agi::SubtitleEditGate::ComparisonSession const& session, PreparedSelection&& prepared) {
    Check();
    if (prepared.owner != file || !gate->Authorizes(session) || !gate->Authorizes(prepared.revision))
        throw std::logic_error("Stale subtitle selection installation");
    auto id = prepared.model_ids.begin();
    for (auto const& row : file->Events) {
        if (id == prepared.model_ids.end() || *id++ != row.Id)
            throw std::logic_error("Subtitle model changed after selection preparation");
    }
    if (id != prepared.model_ids.end()) throw std::logic_error("Subtitle model changed after selection preparation");
    static_assert(noexcept(selection.swap(prepared.selection)));
    selection.swap(prepared.selection);
    active_id = prepared.active;
    model_replaced = false;
    if (auto row = ReadActiveLine()) file->Properties.active_row = row->Row;
    prepared.owner = nullptr;
}
void SelectionController::PrevLine() {
    Check();
    int previous = 0;
    for (auto const& row : file->Events) {
        if (row.Id == active_id) {
            if (previous) SetSelectionAndActiveIDs({previous}, previous);
            return;
        }
        previous = row.Id;
    }
}
void SelectionController::NextLine() {
    Check();
    bool found = false;
    for (auto const& row : file->Events) {
        if (found) { SetSelectionAndActiveIDs({row.Id}, row.Id); return; }
        found = row.Id == active_id;
    }
}

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
SelectionController::SelectionController(AssFile& file, agi::SubtitleEditGate& gate) : file(&file), gate(&gate) { }

std::vector<AssDialogue const*> SelectionController::ReadSelectedLines() const {
    if (owner_thread != std::this_thread::get_id()) throw std::logic_error("Subtitle selection requires the owner thread");
    return {selection.begin(), selection.end()};
}
AssDialogue const* SelectionController::ReadActiveLine() const {
    if (owner_thread != std::this_thread::get_id()) throw std::logic_error("Subtitle selection requires the owner thread");
    return active_line;
}
AssDialogue const* SelectionController::ReadActiveLine(agi::SubtitleEditGate::RevisionToken const& token) const {
    if (!gate->Authorizes(token)) throw std::logic_error("Stale subtitle selection observer");
    return ReadActiveLine();
}
std::vector<AssDialogue const*> SelectionController::ReadSelectedLines(agi::SubtitleEditGate::RevisionToken const& token) const {
    if (!gate->Authorizes(token)) throw std::logic_error("Stale subtitle selection observer");
    return ReadSelectedLines();
}
SelectionController::PreparedSelection::PreparedSelection(AssFile* owner, agi::SubtitleEditGate::RevisionToken revision)
: owner(owner), revision(std::move(revision)) { }
SelectionController::PreparedSelection::PreparedSelection(PreparedSelection&& other) noexcept
: selection(std::move(other.selection)), active(other.active), owner(std::exchange(other.owner, nullptr)), revision(std::move(other.revision)) { }
SelectionController::PreparedSelection SelectionController::PrepareSilent(
    agi::SubtitleEditGate::ComparisonSession const& session, std::vector<int> const& ids, std::optional<int> active_id) const {
    if (owner_thread != std::this_thread::get_id() || !gate->Authorizes(session))
        throw std::logic_error("Invalid subtitle selection preparation session or thread");
    PreparedSelection prepared(file, session.Capture());
    std::unordered_map<int, AssDialogue*> rows;
    for (auto& line : file->Events)
        if (!rows.emplace(line.Id, &line).second) throw std::invalid_argument("Duplicate subtitle line identity");
    for (auto id : ids) {
        auto found = rows.find(id);
        if (found == rows.end()) throw std::out_of_range("Selected subtitle line missing");
        prepared.selection.insert(found->second);
    }
    if (active_id) {
        auto found = rows.find(*active_id);
        if (found == rows.end()) throw std::out_of_range("Active subtitle line missing");
        prepared.active = found->second;
    }
    return prepared;
}
void SelectionController::InstallSilent(agi::SubtitleEditGate::ComparisonSession const& session, PreparedSelection&& prepared) {
    if (owner_thread != std::this_thread::get_id() || prepared.owner != file || !gate->Authorizes(session) || !gate->Authorizes(prepared.revision))
        throw std::logic_error("Stale subtitle selection installation");
    static_assert(noexcept(selection.swap(prepared.selection)));
    selection.swap(prepared.selection);
    active_line = prepared.active;
    if (active_line) file->Properties.active_row = active_line->Row;
    prepared.owner = nullptr;
}

void SelectionController::SetSelectedSet(Selection new_selection, bool expand_bound_groups) {
	if (expand_bound_groups)
		new_selection = ExpandBoundSelection(*file, new_selection);
	selection = std::move(new_selection);
	AnnounceSelectedSetChanged();
}

void SelectionController::SetActiveLine(AssDialogue *new_line) {
	if (new_line != active_line) {
		active_line = new_line;
		if (active_line)
			file->Properties.active_row = active_line->Row;
		AnnounceActiveLineChanged(new_line);
	}
}

void SelectionController::SetSelectionAndActive(Selection new_selection, AssDialogue *new_line, bool expand_bound_groups) {
	bool active_line_changed = new_line != active_line;
	if (expand_bound_groups)
		new_selection = ExpandBoundSelection(*file, new_selection);
	selection = std::move(new_selection);
	active_line = new_line;
	if (active_line)
		file->Properties.active_row = active_line->Row;

	AnnounceSelectedSetChanged();
	if (active_line_changed)
		AnnounceActiveLineChanged(new_line);
}

std::vector<AssDialogue *> SelectionController::GetSortedSelection() const {
	std::vector<AssDialogue *> ret(selection.begin(), selection.end());
	sort(begin(ret), end(ret), [](AssDialogue *a, AssDialogue *b) { return a->Row < b->Row; });
	return ret;
}

void SelectionController::PrevLine() {
	if (!active_line) return;
	auto it = file->iterator_to(*active_line);
	if (it != file->Events.begin()) {
		--it;
		SetSelectionAndActive({&*it}, &*it);
	}
}

void SelectionController::NextLine() {
	if (!active_line) return;
	auto it = file->iterator_to(*active_line);
	if (++it != file->Events.end())
		SetSelectionAndActive({&*it}, &*it);
}

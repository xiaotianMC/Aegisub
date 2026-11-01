// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "subtitle_project_access.h"
#include <thread>
#include <algorithm>
#include <unordered_map>

struct SubtitleProjectAccess::ActiveWrite {
    agi::SubtitleEditGate::WriteLease lease;
    explicit ActiveWrite(agi::SubtitleEditGate::WriteLease&& lease) : lease(std::move(lease)) { }
};
struct SubtitleProjectAccess::State {
    AssFile *file;
    std::thread::id thread = std::this_thread::get_id();
    bool alive = true;
    std::weak_ptr<ActiveWrite> active;
    agi::SubtitleEditGate *gate = nullptr;
    void Check() const {
        if (thread != std::this_thread::get_id()) throw std::logic_error("Live subtitles require the owner thread");
        if (!alive) throw std::logic_error("Subtitle project access closed");
    }
};
SubtitleProjectAccess::WriteScope::WriteScope(std::shared_ptr<State> const& owner, std::shared_ptr<ActiveWrite> active)
: owner(owner), active(std::move(active)) { }
agi::SubtitleEditGate::WriteLease const& SubtitleProjectAccess::WriteScope::Lease() const {
    auto state = owner.lock();
    if (!state) throw std::logic_error("Expired subtitle edit scope");
    state->Check();
    if (!active || !active->lease.Valid()) throw std::logic_error("Invalid subtitle edit scope");
    return active->lease;
}
std::optional<SubtitleProjectAccess::WriteScope> SubtitleProjectAccess::TryWrite() {
    state->Check();
    // Undo snapshot maintenance (PushState) precedes observer notification.
    // Ordinary observers may refresh UI, but may not reenter model editing.
    if (state->file->IsNotifying()) return {};
    if (auto active = state->active.lock()) {
        if (!active->lease.Valid()) return {};
        return WriteScope(state, std::move(active));
    }
    auto lease = gate.TryBeginWrite();
    if (!lease) return {};
    auto active = std::make_shared<ActiveWrite>(std::move(*lease));
    state->active = active;
    return WriteScope(state, std::move(active));
}
SubtitleProjectAccess::SubtitleProjectAccess(AssFile& file, agi::SubtitleEditGate& gate)
: state(std::make_shared<State>(&file)), gate(gate) {
    if (file.edit_gate) throw std::logic_error("Subtitle file already belongs to a project");
    file.edit_gate = &gate;
    state->gate = &gate;
}
SubtitleProjectAccess::Interaction::Interaction(std::shared_ptr<State> const& owner, WriteScope scope,
    agi::SubtitleEditGate::RevisionToken revision, std::vector<int> targets)
: owner(owner), scope(std::move(scope)), revision(std::move(revision)), targets(std::move(targets)) { }
bool SubtitleProjectAccess::Interaction::Current() const {
    auto state = owner.lock();
    if (!state || !scope || !revision.Current()) return false;
    if (state->thread != std::this_thread::get_id()) throw std::logic_error("Interaction requires owner thread");
    return state->alive && !state->file->IsNotifying();
}
std::optional<SubtitleProjectAccess::Interaction> SubtitleProjectAccess::TryBeginInteraction(std::vector<int> targets) {
    state->Check();
    std::sort(targets.begin(), targets.end());
    if (targets.empty() || std::adjacent_find(targets.begin(), targets.end()) != targets.end())
        throw std::invalid_argument("Interaction requires distinct target line IDs");
    std::unordered_map<int, bool> present;
    for (auto const& row : state->file->Events)
        if (!present.emplace(row.Id, true).second) throw std::logic_error("Duplicate live subtitle ID");
    for (auto id : targets) if (!present.contains(id)) throw std::out_of_range("Interaction target missing");
    auto scope = TryWrite();
    if (!scope) return {};
    return Interaction(state, std::move(*scope), gate.Capture(), std::move(targets));
}
void SubtitleProjectAccess::Interaction::UpdateRows(std::vector<AssDialogueBase> const& updates) {
    try {
        if (!Current()) throw std::logic_error("Stale subtitle interaction");
        auto state = owner.lock();
        scope->Lease();
        std::unordered_map<int, AssDialogue*> live;
        for (auto& row : state->file->Events)
            if (!live.emplace(row.Id, &row).second) throw std::logic_error("Duplicate live subtitle ID");
        // Validate every original target, not only the subset written by this callback.
        for (auto id : targets) if (!live.contains(id)) throw std::out_of_range("Interaction target removed");
        std::unordered_map<int, bool> written;
        for (auto const& row : updates) {
            if (!std::binary_search(targets.begin(), targets.end(), row.Id) || !written.emplace(row.Id,true).second)
                throw std::invalid_argument("Invalid interaction update target");
            if (row.Row != live.at(row.Id)->Row) throw std::invalid_argument("Interaction cannot reorder rows");
        }
        for (auto const& row : updates) static_cast<AssDialogueBase&>(*live.at(row.Id)) = row;
    }
    catch (...) { End(); throw; }
}
int SubtitleProjectAccess::Interaction::InsertBefore(int target_id, AssDialogueBase const& data, AssStyle const* missing_style) {
    try {
        if (!Current() || !std::binary_search(targets.begin(), targets.end(), target_id))
            throw std::logic_error("Invalid visual insertion target");
        auto state = owner.lock();
        scope->Lease();
        std::unordered_map<int, AssDialogue*> live;
        for (auto& row : state->file->Events)
            if (!live.emplace(row.Id, &row).second) throw std::logic_error("Duplicate live subtitle ID");
        for (auto id : targets) if (!live.contains(id)) throw std::out_of_range("Visual target removed");
        if (data.Id <= 0 || live.contains(data.Id)) throw std::invalid_argument("Invalid inserted row ID");
        if (!data.ExtradataIds.get().empty()) throw std::invalid_argument("Drawing insertion does not import extradata");
        if (missing_style && data.Style.get() != missing_style->name)
            throw std::invalid_argument("Drawing style does not match inserted style");
        if (!missing_style && !state->file->GetStyle(data.Style))
            throw std::invalid_argument("Missing drawing style");
        if (missing_style && state->file->GetStyle(missing_style->name))
            throw std::logic_error("Insertion style already exists");
        auto row = std::make_unique<AssDialogue>(data);
        auto style = missing_style ? std::make_unique<AssStyle>(*missing_style) : nullptr;
        // Intrusive insertions below allocate nothing. A node belongs to one file only.
        if (style) state->file->Styles.push_back(*style.release());
        state->file->Events.insert(state->file->iterator_to(*live.at(target_id)), *row.release());
        return data.Id;
    }
    catch (...) { End(); throw; }
}
bool SubtitleProjectAccess::Interaction::LinkAdjacent(int id,bool start,int old,int threshold) {
    try {
        if(!Current() || !std::binary_search(targets.begin(),targets.end(),id))throw std::logic_error("Invalid audio neighbor target");
        auto state=owner.lock();scope->Lease();
        // The existing helper may also change a neighbor. Require authorization
        // for the complete live set before delegating its neighbor search.
        std::vector<int> live_ids;
        live_ids.reserve(state->file->Events.size());
        for(auto const& row:state->file->Events) live_ids.push_back(row.Id);
        std::sort(live_ids.begin(),live_ids.end());
        if(live_ids!=targets)throw std::logic_error("Unauthorized or stale audio neighbor set");
        for(auto& row:state->file->Events)if(row.Id==id)return state->file->LinkAdjacentLine(&row,start,old,threshold,true);
        throw std::out_of_range("Audio target missing");
    } catch (...) { End();throw; }
}
int SubtitleProjectAccess::Interaction::Commit(wxString const& description, int type, int amend_id, int single_line_id) {
    try {
        if (!Current()) throw std::logic_error("Stale subtitle interaction");
        auto state = owner.lock();
        scope->Lease();
        AssDialogue *single=nullptr;
        if(single_line_id) {
            if(!std::binary_search(targets.begin(),targets.end(),single_line_id))throw std::logic_error("Unauthorized single line commit");
            for(auto& row:state->file->Events)if(row.Id==single_line_id)single=&row;
            if(!single)throw std::out_of_range("Commit target missing");
        }
        auto id = state->file->Commit(description,type,amend_id,single);
        auto next = state->gate->Capture();
        if (next.Revision().generation != revision.Revision().generation + 1)
            throw std::logic_error("Unexpected nested subtitle revision during interaction commit");
        revision = std::move(next);
        return id;
    }
    catch (...) { End(); throw; }
}
SubtitleProjectAccess::~SubtitleProjectAccess() { Close(); }
void SubtitleProjectAccess::Close() noexcept {
    state->alive = false;
    if (state->file->edit_gate == &gate) state->file->edit_gate = nullptr;
}
AssFile const& SubtitleProjectAccess::Read() const { state->Check(); return *state->file; }
AssFile const& SubtitleProjectAccess::Read(agi::SubtitleEditGate::RevisionToken const& token) const {
    state->Check();
    if (!gate.Authorizes(token)) throw std::logic_error("Stale subtitle observer");
    return *state->file;
}
SubtitleProjectAccess::WriteAccess SubtitleProjectAccess::Edit(agi::SubtitleEditGate::WriteLease const& lease) {
    state->Check();
    if (!gate.Authorizes(lease)) throw std::logic_error("Subtitle write lease belongs to another project or expired");
    return WriteAccess(state, lease.Capture());
}
SubtitleProjectAccess::WriteAccess::WriteAccess(std::shared_ptr<State> const& state, agi::SubtitleEditGate::WriteLease::Token token)
: state(state), token(std::move(token)) { }
SubtitleProjectAccess::WriteAccess::Borrow SubtitleProjectAccess::WriteAccess::Check() const {
    auto owner = state.lock();
    if (!owner) throw std::logic_error("Subtitle project access expired");
    owner->Check();
    auto authority = token.Hold();
    auto file = owner->file;
    return {std::move(authority), std::move(owner), file};
}
void SubtitleProjectAccess::WriteAccess::SetExtradataValue(uint32_t id, std::string value) const {
    auto access = Check();
    for (auto& extra : access.file->Extradata)
        if (extra.id == id) { extra.value = std::move(value); return; }
    throw std::out_of_range("Subtitle extradata no longer exists");
}

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "subtitle_project_access.h"
#include <thread>

struct SubtitleProjectAccess::ActiveWrite {
    agi::SubtitleEditGate::WriteLease lease;
    explicit ActiveWrite(agi::SubtitleEditGate::WriteLease&& lease) : lease(std::move(lease)) { }
};
struct SubtitleProjectAccess::State {
    AssFile *file;
    std::thread::id thread = std::this_thread::get_id();
    bool alive = true;
    std::weak_ptr<ActiveWrite> active;
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

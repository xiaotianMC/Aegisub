// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "subtitle_project_access.h"
#include <thread>

struct SubtitleProjectAccess::State {
    AssFile *file;
    std::thread::id thread = std::this_thread::get_id();
    bool alive = true;
    void Check() const {
        if (thread != std::this_thread::get_id()) throw std::logic_error("Live subtitles require the owner thread");
        if (!alive) throw std::logic_error("Subtitle project access closed");
    }
};
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

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <libaegisub/subtitle_rebind.h>
#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace agi {
struct SubtitleRebindBatch::State {
    mutable std::mutex mutex;
    SubtitleEditGate::RevisionToken token;
    std::vector<std::string> observers;
    std::vector<bool> acknowledged;
    std::vector<std::exception_ptr> failures;
    std::vector<bool> dispatching;
    size_t in_flight = 0;
    std::shared_ptr<void const> retired;
    bool active = true;
    State(SubtitleEditGate::RevisionToken token, std::vector<std::string> names)
    : token(std::move(token)), observers(std::move(names)), acknowledged(observers.size()), failures(observers.size()), dispatching(observers.size()) { }
};
SubtitleRebindBatch::Ticket::Ticket(std::shared_ptr<State> const& value, size_t index) : state(value), observer(index) { }
bool SubtitleRebindBatch::Ticket::Current() const {
    auto s = state.lock(); if (!s) return false;
    std::lock_guard lock(s->mutex); return s->active && s->token.Current();
}
SubtitleRevision SubtitleRebindBatch::Ticket::Revision() const {
    auto s = state.lock(); if (!s) throw std::logic_error("Expired subtitle observer");
    std::lock_guard lock(s->mutex);
    if (!s->active || !s->token.Current()) throw std::logic_error("Stale subtitle observer");
    return s->token.Revision();
}
void SubtitleRebindBatch::Ticket::Acknowledge(SubtitleRevision expected) {
    auto s = state.lock(); if (!s) throw std::logic_error("Expired subtitle observer");
    std::lock_guard lock(s->mutex);
    if (!s->active || !s->token.Current() || s->token.Revision() != expected)
        throw std::logic_error("Stale subtitle observer acknowledgement");
    s->acknowledged.at(observer) = true;
    s->failures.at(observer) = nullptr;
}
SubtitleRebindBatch::SubtitleRebindBatch(SubtitleEditGate::ComparisonSession&& value,
    std::shared_ptr<void const> retired, std::vector<std::string> observers)
: state(std::make_shared<State>(value.Capture(), std::move(observers))), session(std::move(value)) {
    auto sorted = state->observers;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) throw std::invalid_argument("Duplicate subtitle observer name");
    state->retired = retired; // Also pins nodes if the document closes during a callback.
    session->RetainUntilRebound(std::move(retired));
}
SubtitleRebindBatch::~SubtitleRebindBatch() {
    std::lock_guard lock(state->mutex); state->active = false;
    // If incomplete, the gate keeps retired nodes and stays blocked until
    // document shutdown. The owner must retain a failed batch to retry it.
}
SubtitleRebindBatch::Ticket SubtitleRebindBatch::Observer(size_t index) const {
    if (index >= state->observers.size()) throw std::out_of_range("Subtitle observer index");
    return Ticket(state, index);
}
void SubtitleRebindBatch::Dispatch(size_t index, std::function<void(Ticket)> const& callback) {
    auto ticket = Observer(index);
    {
        std::lock_guard lock(state->mutex);
        if (!state->active || !state->token.Current()) throw std::logic_error("Stale subtitle notification");
        if (state->dispatching[index]) throw std::logic_error("Reentrant subtitle observer delivery");
        state->dispatching[index] = true; ++state->in_flight;
        state->acknowledged[index] = false; state->failures[index] = nullptr;
    }
    try { callback(ticket); }
    catch (...) {
        std::lock_guard lock(state->mutex);
        state->acknowledged[index] = false; state->failures[index] = std::current_exception();
    }
    {
        std::lock_guard lock(state->mutex);
        state->dispatching[index] = false; --state->in_flight;
    }
}
std::vector<std::string> SubtitleRebindBatch::Pending() const {
    std::lock_guard lock(state->mutex); std::vector<std::string> result;
    for (size_t i=0;i<state->observers.size();++i) if (!state->acknowledged[i]) result.push_back(state->observers[i]);
    return result;
}
std::exception_ptr SubtitleRebindBatch::Failure(size_t index) const {
    std::lock_guard lock(state->mutex); return state->failures.at(index);
}
bool SubtitleRebindBatch::Complete() {
    {
        std::lock_guard lock(state->mutex);
        if (!state->active || !state->token.Current()) throw std::logic_error("Stale subtitle rebinding batch");
        if (state->in_flight) return false;
        if (std::find(state->acknowledged.begin(), state->acknowledged.end(), false) != state->acknowledged.end()) return false;
        state->active = false;
    }
    session->FinishRebinding(state->token.Revision());
    session.reset();
    std::shared_ptr<void const> retired;
    { std::lock_guard lock(state->mutex); retired = std::move(state->retired); }
    return true;
}
}

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <libaegisub/subtitle_edit_gate.h>
#include <atomic>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace agi {
namespace {
uint64_t next_document() {
    static std::atomic<uint64_t> last{0};
    auto old = last.load(std::memory_order_relaxed);
    do {
        if (old == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Subtitle document identity exhausted");
    } while (!last.compare_exchange_weak(old, old + 1, std::memory_order_relaxed));
    return old + 1;
}
void increment(uint64_t& value) {
    if (value == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Subtitle revision exhausted");
    ++value;
}
}
struct SubtitleEditGate::State {
    mutable std::mutex mutex;
    SubtitleRevision revision{next_document(), 0};
    uint64_t writers = 0, comparison = 0, last_comparison = 0;
    bool alive = true, rebinding = false;
    std::shared_ptr<void const> retired;
};
SubtitleEditGate::RevisionToken::RevisionToken(std::shared_ptr<State> const& value, SubtitleRevision stamp)
: state(value), revision(stamp) { }
bool SubtitleEditGate::RevisionToken::Current() const {
    auto s = state.lock();
    if (!s) return false;
    std::lock_guard lock(s->mutex); return s->alive && s->revision == revision;
}
struct SubtitleEditGate::WriteLease::Registration {
    std::shared_ptr<State> state;
    bool enrolled = false;
    bool owner_active = false;
    explicit Registration(std::shared_ptr<State> value) : state(std::move(value)) { }
    ~Registration() {
        if (!enrolled) return;
        std::lock_guard lock(state->mutex); --state->writers;
    }
};
struct SubtitleEditGate::ComparisonSession::Registration {
    std::shared_ptr<State> state;
    uint64_t id = 0;
    explicit Registration(std::shared_ptr<State> value) : state(std::move(value)) { }
    ~Registration() {
        std::lock_guard lock(state->mutex);
        if (id && state->comparison == id && !state->rebinding) state->comparison = 0;
    }
};
SubtitleEditGate::WriteLease::WriteLease(std::shared_ptr<Registration> value) : registration(std::move(value)) { }
SubtitleEditGate::WriteLease::WriteLease(WriteLease&&) noexcept = default;
SubtitleEditGate::WriteLease& SubtitleEditGate::WriteLease::operator=(WriteLease&& other) noexcept {
    if (this != &other) { Release(); registration = std::move(other.registration); }
    return *this;
}
void SubtitleEditGate::WriteLease::Release() noexcept {
    auto old = std::move(registration);
    if (old) {
        std::lock_guard lock(old->state->mutex); old->owner_active = false;
    }
}
SubtitleEditGate::WriteLease::~WriteLease() { Release(); }
bool SubtitleEditGate::WriteLease::Valid() const {
    if (!registration) return false;
    std::lock_guard lock(registration->state->mutex);
    return registration->owner_active && registration->state->alive && !registration->state->comparison;
}
SubtitleEditGate::WriteLease::Token::Token(std::shared_ptr<Registration> const& value) : registration(value) { }
bool SubtitleEditGate::WriteLease::Token::Valid() const {
    auto r = registration.lock();
    if (!r) return false;
    std::lock_guard lock(r->state->mutex);
    return r->owner_active && r->state->alive && !r->state->comparison;
}
std::shared_ptr<void const> SubtitleEditGate::WriteLease::Token::Hold() const {
    auto r = registration.lock();
    if (!r) throw std::logic_error("Expired subtitle write access");
    {
        std::lock_guard lock(r->state->mutex);
        if (!r->owner_active || !r->state->alive || r->state->comparison)
            throw std::logic_error("Invalid subtitle write access");
    }
    return r;
}
SubtitleEditGate::WriteLease::Token SubtitleEditGate::WriteLease::Capture() const {
    if (!Valid()) throw std::logic_error("Expired subtitle write lease");
    return Token(registration);
}
SubtitleEditGate::ComparisonSession::ComparisonSession(std::unique_ptr<Registration> value) : registration(std::move(value)) { }
SubtitleEditGate::ComparisonSession::ComparisonSession(ComparisonSession&&) noexcept = default;
SubtitleEditGate::ComparisonSession& SubtitleEditGate::ComparisonSession::operator=(ComparisonSession&&) noexcept = default;
SubtitleEditGate::ComparisonSession::~ComparisonSession() = default;
bool SubtitleEditGate::ComparisonSession::Valid() const {
    if (!registration) return false;
    std::lock_guard lock(registration->state->mutex);
    return registration->state->alive && registration->id && registration->state->comparison == registration->id;
}
SubtitleRevision SubtitleEditGate::ComparisonSession::Revision() const {
    if (!registration) throw std::logic_error("Expired subtitle comparison session");
    std::lock_guard lock(registration->state->mutex);
    if (!registration->state->alive || registration->state->comparison != registration->id)
        throw std::logic_error("Expired subtitle comparison session");
    return registration->state->revision;
}
SubtitleRevision SubtitleEditGate::ComparisonSession::AdvanceRevision() {
    if (!registration) throw std::logic_error("Expired subtitle comparison session");
    std::lock_guard lock(registration->state->mutex);
    auto& s = *registration->state;
    if (!s.alive || s.comparison != registration->id || s.rebinding)
        throw std::logic_error("Subtitle session cannot advance during recovery");
    increment(s.revision.generation); return s.revision;
}
SubtitleEditGate::RevisionToken SubtitleEditGate::ComparisonSession::Capture() const {
    auto revision = Revision();
    return RevisionToken(registration->state, revision);
}
void SubtitleEditGate::ComparisonSession::RetainUntilRebound(std::shared_ptr<void const> retired) {
    if (!registration || !retired) throw std::invalid_argument("Missing subtitle session or retired state");
    std::lock_guard lock(registration->state->mutex);
    auto& s = *registration->state;
    if (!s.alive || s.comparison != registration->id || s.rebinding)
        throw std::logic_error("Subtitle rebinding already active or session expired");
    s.retired = std::move(retired); s.rebinding = true;
}
void SubtitleEditGate::ComparisonSession::FinishRebinding(SubtitleRevision expected) {
    if (!registration) throw std::logic_error("Expired subtitle comparison session");
    std::shared_ptr<void const> retired;
    {
        std::lock_guard lock(registration->state->mutex);
        auto& s = *registration->state;
        if (!s.alive || s.comparison != registration->id || s.revision != expected || !s.rebinding)
            throw std::logic_error("Stale subtitle rebinding completion");
        retired = std::move(s.retired); s.rebinding = false;
    }
    // Retired nodes are destroyed outside the mutex.
}
SubtitleEditGate::SubtitleEditGate() : state(std::make_shared<State>()) { }
SubtitleEditGate::~SubtitleEditGate() { Close(); }
std::optional<SubtitleEditGate::WriteLease> SubtitleEditGate::TryBeginWrite() {
    auto registration = std::make_shared<WriteLease::Registration>(state);
    std::lock_guard lock(state->mutex);
    if (!state->alive || state->comparison || state->rebinding) return {};
    if (state->writers == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Subtitle writer count exhausted");
    increment(state->revision.generation);
    ++state->writers; registration->enrolled = registration->owner_active = true;
    return WriteLease(std::move(registration));
}
std::optional<SubtitleEditGate::ComparisonSession> SubtitleEditGate::TryBeginComparison() {
    auto registration = std::make_unique<ComparisonSession::Registration>(state);
    std::lock_guard lock(state->mutex);
    if (!state->alive || state->writers || state->comparison || state->rebinding) return {};
    increment(state->last_comparison); state->comparison = registration->id = state->last_comparison;
    return ComparisonSession(std::move(registration));
}
SubtitleRevision SubtitleEditGate::Revision() const { std::lock_guard lock(state->mutex); return state->revision; }
SubtitleEditGate::RevisionToken SubtitleEditGate::Capture() const {
    std::lock_guard lock(state->mutex);
    if (!state->alive) throw std::logic_error("Subtitle document closed");
    return RevisionToken(state, state->revision);
}
bool SubtitleEditGate::IsCurrent(SubtitleRevision expected) const {
    std::lock_guard lock(state->mutex); return state->alive && state->revision == expected;
}
bool SubtitleEditGate::Authorizes(WriteLease const& lease) const {
    return lease.registration && lease.registration->state == state && lease.Valid();
}
bool SubtitleEditGate::Authorizes(ComparisonSession const& session) const {
    return session.registration && session.registration->state == state && session.Valid();
}
bool SubtitleEditGate::Authorizes(RevisionToken const& token) const {
    return token.state.lock() == state && token.Current();
}
bool SubtitleEditGate::NeedsRebinding() const { std::lock_guard lock(state->mutex); return state->rebinding; }
void SubtitleEditGate::NoteLegacyCommit() {
    std::lock_guard lock(state->mutex);
    if (state->alive) increment(state->revision.generation);
}
void SubtitleEditGate::Close() noexcept {
    std::shared_ptr<void const> retired;
    {
        std::lock_guard lock(state->mutex);
        state->alive = false; retired = std::move(state->retired);
    }
}
}

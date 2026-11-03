// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include <libaegisub/fs.h>
#include <libaegisub/subtitle_edit_gate.h>
#include <memory>
#include <stdexcept>
#include <thread>
#include <string>
#include <vector>

class SubsController;
class SubtitleModelCandidate;
// Thrown when a candidate was published but observer confirmation is incomplete.
// This is neither success nor "nothing changed": the host stays sealed until an
// explicit retry confirms every observer or the host is closed.
struct SubtitleModelRecoveryBlocked : std::runtime_error {
    SubtitleModelRecoveryBlocked() : std::runtime_error("Model installation is RecoveryBlocked; inspect pending observers and retry or close host") {}
};
// The object itself is the host capability: only SubsController can construct it
// or submit a candidate. Ordinary leases and comparison sessions cannot publish.
class SubtitleModelInstallation {
public:
    enum class State { Idle, Preparing, Prepared, Authorizing, Publishing,
        Rebinding, Committed, Aborting, Recovering, RecoveryBlocked };
private:
    friend class SubsController;
    struct Pending;
    SubsController& owner;
    std::thread::id thread = std::this_thread::get_id();
    State state = State::Idle;
    std::unique_ptr<Pending> pending;
    std::optional<agi::SubtitleEditGate::RevisionToken> expected;
    uint64_t metadata_revision = 0;
    bool cancelled = false;
    explicit SubtitleModelInstallation(SubsController&);
    ~SubtitleModelInstallation();
    void CheckThread() const;
    void Begin();
    void Install(SubtitleModelCandidate&&, agi::fs::path const&);
    void Abort() noexcept;
    void Confirm();
    bool Retry();
    bool Busy() const noexcept;
    std::vector<std::string> PendingObservers() const;
    friend struct std::default_delete<SubtitleModelInstallation>;
};

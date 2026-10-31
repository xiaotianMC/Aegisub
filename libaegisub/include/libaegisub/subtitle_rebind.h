// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include <libaegisub/subtitle_edit_gate.h>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace agi {
// Explicit acknowledgements, NOT an assumption that a successful callback
// released its old pointers. Legacy GUI observers are not enrolled implicitly.
class SubtitleRebindBatch {
    struct State;
    std::shared_ptr<State> state;
    std::optional<SubtitleEditGate::ComparisonSession> session;
public:
    class Ticket {
        std::weak_ptr<State> state;
        size_t observer;
        Ticket(std::shared_ptr<State> const&, size_t);
        friend class SubtitleRebindBatch;
    public:
        bool Current() const;
        SubtitleRevision Revision() const;
        void Acknowledge(SubtitleRevision);
    };
    SubtitleRebindBatch(SubtitleEditGate::ComparisonSession&&,
        std::shared_ptr<void const> retired, std::vector<std::string> observers);
    SubtitleRebindBatch(SubtitleRebindBatch const&) = delete;
    SubtitleRebindBatch& operator=(SubtitleRebindBatch const&) = delete;
    ~SubtitleRebindBatch();
    Ticket Observer(size_t index) const;
    // Only this batch-local delivery isolates errors. No generic Signal changes.
    void Dispatch(size_t index, std::function<void(Ticket)> const& callback);
    std::vector<std::string> Pending() const;
    std::exception_ptr Failure(size_t index) const;
    bool Complete();
};
}

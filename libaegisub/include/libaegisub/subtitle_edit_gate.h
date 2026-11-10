// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include <cstdint>
#include <memory>
#include <optional>

class SubtitleModelInstallation;

namespace agi {
struct SubtitleRevision {
    uint64_t document = 0, generation = 0;
    uint64_t document_epoch = 0; // New document within the same project State.
    bool operator==(SubtitleRevision const&) const = default;
};
class SubtitleRebindBatch;
// Excludes comparison from registered writers. Does not serialize writers or
// make AssFile thread-safe; the mutex protects short transitions only.
class SubtitleEditGate {
    struct State;
    std::shared_ptr<State> state;
public:
    class ComparisonSession;
    class RevisionToken {
        std::weak_ptr<State> state;
        SubtitleRevision revision;
        RevisionToken(std::shared_ptr<State> const&, SubtitleRevision);
        friend class SubtitleEditGate;
        friend class ComparisonSession;
    public:
        bool Current() const;
        bool Alive() const;
        SubtitleRevision Revision() const noexcept { return revision; }
    };
    class WriteLease {
        struct Registration;
        std::shared_ptr<Registration> registration;
        explicit WriteLease(std::shared_ptr<Registration>);
        void Release() noexcept;
        friend class SubtitleEditGate;
    public:
        class Token {
            std::weak_ptr<Registration> registration;
            explicit Token(std::shared_ptr<Registration> const&);
            friend class WriteLease;
        public:
            bool Valid() const;
            // Keeps registration alive for one operation even if the task's
            // owning lease is released concurrently. Exposes no model pointer.
            std::shared_ptr<void const> Hold() const;
        };
        WriteLease(WriteLease&&) noexcept;
        WriteLease& operator=(WriteLease&&) noexcept;
        ~WriteLease();
        bool Valid() const;
        Token Capture() const;
    };
    // C1-C1c-12: registration for project state outside the compared rows
    // (metadata, automation settings, save target and baseline, media paths).
    // Like a WriteLease it excludes comparison for its whole lifetime, but it
    // does not advance the revision and grants no row write access: it cannot
    // be passed where a WriteLease is required.
    class ProjectWriteLease {
        struct Registration;
        std::unique_ptr<Registration> registration;
        explicit ProjectWriteLease(std::unique_ptr<Registration>);
        friend class SubtitleEditGate;
    public:
        ProjectWriteLease(ProjectWriteLease&&) noexcept;
        ProjectWriteLease& operator=(ProjectWriteLease&&) noexcept;
        ~ProjectWriteLease();
    };
    class ComparisonSession {
        struct Registration;
        std::unique_ptr<Registration> registration;
        explicit ComparisonSession(std::unique_ptr<Registration>);
        friend class SubtitleEditGate;
        friend class SubtitleRebindBatch;
        void RetainUntilRebound(std::shared_ptr<void const>);
        void FinishRebinding(SubtitleRevision);
        SubtitleRevision PrepareDocumentRevision() const;
        RevisionToken PublishDocumentRevision(SubtitleRevision);
    public:
        ComparisonSession(ComparisonSession&&) noexcept;
        ComparisonSession& operator=(ComparisonSession&&) noexcept;
        ~ComparisonSession();
        bool Valid() const;
        SubtitleRevision Revision() const;
        RevisionToken Capture() const;
        SubtitleRevision AdvanceRevision();
    };
    SubtitleEditGate();
    SubtitleEditGate(SubtitleEditGate const&) = delete;
    SubtitleEditGate& operator=(SubtitleEditGate const&) = delete;
    ~SubtitleEditGate();
    std::optional<WriteLease> TryBeginWrite();
    std::optional<ComparisonSession> TryBeginComparison();
    // Fails while a comparison session (including an installation or Lua
    // publication borrow) or recovery rebinding is active.
    std::optional<ProjectWriteLease> TryBeginProjectWrite();
    SubtitleRevision Revision() const;
    RevisionToken Capture() const;
    bool IsCurrent(SubtitleRevision) const;
    bool Authorizes(WriteLease const&) const;
    bool Authorizes(ComparisonSession const&) const;
    bool Authorizes(RevisionToken const&) const;
    bool NeedsRebinding() const;
    // Transitional invalidation for legacy commits, NOT authorization of their writes.
    void NoteLegacyCommit();
    void Close() noexcept;
};
}

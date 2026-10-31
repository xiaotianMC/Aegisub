// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include <libaegisub/subtitle_edit_gate.h>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Incremental live-model boundary. Detached AssFiles keep their existing API.
// Legacy Context::ass remains a migration gap; this facade exposes no mutable pointer.
class SubtitleProjectAccess {
    struct State;
    struct ActiveWrite;
    std::shared_ptr<State> state;
    agi::SubtitleEditGate& gate;
public:
    // A synchronous ordinary edit, including nested commands and notifications.
    // Shares a C0 lease, never the gate's internal mutex.
    class WriteScope {
        std::weak_ptr<State> owner;
        std::shared_ptr<ActiveWrite> active;
        WriteScope(std::shared_ptr<State> const&, std::shared_ptr<ActiveWrite>);
        friend class SubtitleProjectAccess;
    public:
        agi::SubtitleEditGate::WriteLease const& Lease() const;
    };
    std::optional<WriteScope> TryWrite();
    class WriteAccess {
        std::weak_ptr<State> state;
        agi::SubtitleEditGate::WriteLease::Token token;
        struct Borrow {
            std::shared_ptr<void const> authority;
            std::shared_ptr<State> owner;
            AssFile *file;
        };
        Borrow Check() const;
        WriteAccess(std::shared_ptr<State> const&, agi::SubtitleEditGate::WriteLease::Token);
        friend class SubtitleProjectAccess;
    public:
        template<class T, class Value> void SetDialogueField(int id, T AssDialogueBase::*field, Value value) const {
            auto access = Check();
            if (!field) throw std::invalid_argument("Missing subtitle field");
            if constexpr (std::is_same_v<T, int>)
                if (field == &AssDialogueBase::Id || field == &AssDialogueBase::Row)
                    throw std::invalid_argument("Line identity and order require a structural operation");
            for (auto& line : access.file->Events)
                if (line.Id == id) { line.*field = T(std::move(value)); return; }
            throw std::out_of_range("Subtitle line no longer exists");
        }
        template<class T, class Value> void SetStyleField(std::string const& name, T AssStyle::*field, Value value) const {
            auto access = Check();
            if (!field) throw std::invalid_argument("Missing style field");
            if constexpr (std::is_same_v<T, std::string>)
                if (field == &AssStyle::name) throw std::invalid_argument("Style rename requires dependency handling");
            auto style = access.file->GetStyle(name);
            if (!style) throw std::out_of_range("Subtitle style no longer exists");
            style->*field = T(std::move(value));
            style->UpdateData();
        }
        void SetExtradataValue(uint32_t id, std::string value) const;
    };
    SubtitleProjectAccess(AssFile&, agi::SubtitleEditGate&);
    SubtitleProjectAccess(SubtitleProjectAccess const&) = delete;
    SubtitleProjectAccess& operator=(SubtitleProjectAccess const&) = delete;
    ~SubtitleProjectAccess();
    AssFile const& Read() const;
    AssFile const& Read(agi::SubtitleEditGate::RevisionToken const&) const;
    WriteAccess Edit(agi::SubtitleEditGate::WriteLease const&);
    void Close() noexcept;
};

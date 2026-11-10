// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include "ass_file.h"
#include "include/aegisub/context.h"
#include "subtitle_project_access.h"

// Test-only attorney for agi::Context's private live document.
//
// Tests read the live document through the production read path (LiveRead).
// Mutable access exists only to arrange fixtures and to impersonate an
// unauthorized/legacy writer, so the runtime gates (WriteLease, revision
// tokens, Interaction, Commit rejection) can still be exercised. Production
// sources must never name this type; tests/owner_access_check.py enforces it.
struct LiveSubtitleModelTestAccess {
    static AssFile& Model(agi::Context& c) { return *c.ass; }
};

/// The production read-only view (host thread, open project).
inline AssFile const& LiveRead(agi::Context& c) { return c.subtitleAccess->Read(); }
/// Fixture setup / simulated bypass writer. Not a production capability.
inline AssFile& LiveWriteForTest(agi::Context& c) { return LiveSubtitleModelTestAccess::Model(c); }

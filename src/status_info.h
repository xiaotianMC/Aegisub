// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// What the main window's status bar shows about the subtitles: the current
// line and the file's state (docs/main-window.md).
#pragma once

#include <wx/string.h>

namespace agi { struct Context; }

namespace status_info {
/// The active line, how many lines are selected and its times, e.g.
/// "Line 12 of 340 · 0:01:02.34 → 0:01:05.00". Empty without an active line.
wxString Line(agi::Context *c);
/// Whether there are unsaved changes, or the file a comparison is open with.
wxString File(agi::Context *c);
}

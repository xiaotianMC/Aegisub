// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include <string>
namespace agi { struct Context; }
namespace cmd {
// Data half of ordinary clipboard paste. Clipboard acquisition stays in the UI;
// callers and integration tests can supply text without touching the system clipboard.
void PasteSubtitleLines(agi::Context*, std::string const&);
}

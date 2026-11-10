// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <optional>
#include <string>

class wxString;
class wxWindow;
struct ScrollEffect;

/// "N px/s, M px by the line's end": @p effect's speed, and how far it goes
/// in a line @p duration_ms long
wxString ScrollSpeedText(ScrollEffect const& effect, int duration_ms);

/// Ask for a line's Banner or Scroll effect (docs/scroll-effect.md), its
/// Effect field being @p current, the line @p duration_ms long, in a script
/// @p script_w × @p script_h. The new Effect field, empty for none; nothing
/// if cancelled. A field holding anything else is replaced only if the user
/// chooses an effect.
std::optional<std::string> ShowScrollEffectDialog(wxWindow *parent, std::string const& current, int duration_ms, int script_w, int script_h);

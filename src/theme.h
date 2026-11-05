// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Light and dark appearance (docs/theme.md). "App/Theme" picks the
// appearance: follow the system, or always light or dark. Interface colour
// options keep their meaning: in the dark appearance an option the user never
// changed reads as its dark default; a changed one is used as it is.
#pragma once

#include <libaegisub/signal.h>

#include <functional>
#include <string>

#include <wx/colour.h>

namespace theme {
/// Values of "App/Theme".
enum class Mode { System = 0, Light = 1, Dark = 2 };

/// Apply "App/Theme" to the toolkit. Call once at startup, before the first
/// window is created (Windows reads it only then); later changes of the
/// option are applied as they happen where the toolkit allows it.
void Init();

/// Whether the dark appearance is in use.
bool IsDark();

/// Whether the toolkit itself draws its controls dark: Windows with
/// wxWidgets 3.3, decided once at startup. Elsewhere the toolkit's theme
/// draws borders and the like, and this is false.
bool NativeDark();

/// @p border (a wxBORDER_* style), or a flat border when NativeDark():
/// Windows draws sunken and raised borders light even in its dark mode.
long Border(long border);

/// The colour option @p option as the interface should show it: its dark
/// default in the dark appearance when the user has not changed it.
wxColour Colour(std::string const& option);

/// The dark default of @p option, if it has one (for tests and the preferences).
bool HasDarkDefault(std::string const& option);

/// Report that the system's colours changed (wxEVT_SYS_COLOUR_CHANGED).
void SystemColoursChanged();

/// Called after the appearance changed: windows reread their colours.
agi::signal::UnscopedConnection AddChangeListener(std::function<void()> listener);

/// Colours shared by custom-drawn interface parts, for the current appearance.
struct Ui {
    wxColour accent;            ///< Selected tab, titles, focus marks
    wxColour notice_back, notice_text; ///< Notice bars (e.g. a locked editor)
    wxColour list_back, list_text;     ///< Custom-drawn lists (the command palette)
    wxColour list_dim;                 ///< Secondary text in them: help, shortcuts
    wxColour list_selected;            ///< The selected row's background
};
Ui UiColours();
}

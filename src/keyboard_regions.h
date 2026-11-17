// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The main window's regions for the keyboard (docs/keyboard-regions.md): the
// subtitle grid, the edit box, the audio, the video and the side panel. F6
// and Shift+F6 go from one to the next; Tab and Shift+Tab go round the
// controls of the one the keyboard is in and don't leave it; while the
// keyboard is moved by keys, the region it is in has a line in the accent
// colour round it, drawn on the backdrop between the blocks.
#pragma once

#include <libaegisub/signal.h>

#include <functional>
#include <vector>

class wxDC;
class wxKeyEvent;
class wxWindow;

namespace regions {

struct Region {
	/// What the region covers: the keyboard in it, or in a window in it, is
	/// in the region. The line goes round it
	wxWindow *block = nullptr;
	/// Whether it can be gone to now; by default, when the block is on
	/// the screen
	std::function<bool()> available;
	/// Give it the keyboard, opening it if it has to be. @return Whether
	/// it took it. By default the first control Tab stops at
	std::function<bool()> enter;
};

/// The main window's regions, in the order F6 goes through them, and the
/// backdrop they are blocks on
void Setup(wxWindow *backdrop, std::vector<Region> regions);

/// The region the keyboard is in, -1 for none
int Current();
/// The region @p window is in, -1 for none
int RegionOf(wxWindow const *window);
/// Give the keyboard to the next region that is available after the one
/// it is in (the first when it is in none), or the one before; round from
/// the last to the first. @return Whether one took it
bool Next(bool forward);

/// The controls Tab stops at in a region, in order: those shown, enabled
/// and taking the keyboard (toolbars don't: their buttons have no keys)
std::vector<wxWindow*> Stops(int region);
/// Where Tab (or Shift+Tab) goes from @p from in its region, round from the
/// last stop to the first; null when it is in no region or the region has
/// no stops. A region whose block has its own order (the side panel) isn't
/// asked: it moves the keyboard itself
wxWindow *TabTarget(wxWindow *from, bool forward);
/// As Tab does from @p from: on to the next stop of its region, or its own
/// order's. @return Whether it was in a region
bool MoveOn(wxWindow *from, bool forward);

/// The block with the line round it now: the keyboard's region while keys
/// move it, null otherwise (a click hides the line)
wxWindow *Outlined();
/// Called when the line moves, appears or goes, for windows that draw part
/// of it (the side panel's strip covers the gap left of it)
agi::signal::Connection AddOutlineListener(std::function<void()> listener);
/// Draw the part of the line that falls on @p window, a window in the
/// gaps between the blocks
void DrawOutline(wxWindow *window, wxDC &dc);

/// What Tab and Esc do in a window that takes every key (the grid, the
/// audio, the video and its slider): Tab goes on in its region, Esc goes
/// to the subtitle text. Called with the keys the window has no use for.
/// @return Whether it was one of them
bool HandleKey(wxWindow *window, wxKeyEvent &event);

/// After a click on a button of @p bar, the keyboard goes to @p target
/// (the visual tools' bars: to the video). Other toolbars' clicks leave it
/// where it was, or send it to the subtitle text when it was left nowhere
void FocusAfterClicks(wxWindow *bar, wxWindow *target);

/// Forget the regions, as when the main window goes
void Clear();

}

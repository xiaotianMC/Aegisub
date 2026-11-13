// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "keyboard_regions.h"

#include "flat_controls.h"
#include "style_panel.h"
#include "theme.h"

#include <algorithm>

#include <wx/dcclient.h>
#include <wx/event.h>
#include <wx/panel.h>
#include <wx/sashwin.h>
#include <wx/scrolbar.h>
#include <wx/toolbar.h>
#include <wx/toplevel.h>
#include <wx/weakref.h>
#include <wx/window.h>

namespace regions {
namespace {

wxWindow *backdrop = nullptr;
std::vector<Region> list;
/// Keys moved the keyboard last, not a click: the line shows
bool by_keys = false;
/// The block the line was last drawn round
wxWindow *drawn = nullptr;
agi::signal::Signal<> outline_changed;
/// Toolbars whose clicks give the keyboard to a window
std::vector<std::pair<wxWeakRef<wxWindow>, wxWeakRef<wxWindow>>> click_targets;

bool Within(wxWindow const *window, wxWindow const *outer) {
	for (; window; window = window->GetParent()) {
		if (window == outer) return true;
		if (window->IsTopLevel()) break;
	}
	return false;
}

bool Available(Region const& r) {
	if (!r.block) return false;
	return r.available ? r.available() : r.block->IsShownOnScreen();
}

/// The line's rectangle on the backdrop: round the block, in the gap
wxRect OutlineRect(wxWindow *block) {
	wxRect r(backdrop->ScreenToClient(block->GetScreenPosition()), block->GetSize());
	// A block resized by a strip below it (the audio): the strip is the
	// gap, so the line goes round what is above it, and the block draws
	// the line in its strip, where the backdrop can't
	if (auto sash = dynamic_cast<wxSashWindow*>(block); sash && sash->GetSashVisible(wxSASH_BOTTOM))
		r.height -= sash->GetDefaultBorderSize();
	int gap = theme::BlockGap(backdrop);
	return r.Inflate(std::max(1, gap / 2));
}

void Redraw() {
	wxWindow *now = Outlined();
	if (now == drawn) return;
	if (backdrop) {
		int pad = theme::BlockGap(backdrop) + 2;
		for (auto block : {drawn, now})
			if (block) backdrop->RefreshRect(OutlineRect(block).Inflate(pad));
	}
	drawn = now;
	outline_changed();
}

void Paint(wxPaintEvent &) {
	// The backdrop's colour first: a refresh doesn't always erase it
	// (Windows doesn't), and the line drawn before would stay
	wxPaintDC dc(backdrop);
	dc.SetBackground(wxBrush(backdrop->GetBackgroundColour()));
	dc.Clear();
	if (Outlined()) DrawOutline(backdrop, dc);
}

/// Moves the keyboard on Tab in a region, hides the line on a click and
/// moves it as the keyboard moves
class Filter final : public wxEventFilter {
	int FilterEvent(wxEvent &event) override {
		auto type = event.GetEventType();
		if (type == wxEVT_LEFT_DOWN || type == wxEVT_RIGHT_DOWN || type == wxEVT_MIDDLE_DOWN) {
			if (by_keys) {
				by_keys = false;
				Redraw();
			}
		}
		else if (type == wxEVT_SET_FOCUS) {
			if (by_keys) CallAfterRedraw();
		}
		else if (type == wxEVT_TOOL) {
			if (auto bar = dynamic_cast<wxToolBar*>(event.GetEventObject()))
				AfterClick(bar);
		}
		else if (type == wxEVT_NAVIGATION_KEY) {
			auto &nav = static_cast<wxNavigationKeyEvent&>(event);
			if (nav.IsWindowChange()) return Event_Skip;
			wxWindow *focus = wxWindow::FindFocus();
			if (!focus || RegionOf(focus) < 0) return Event_Skip;
			if (MoveOn(focus, nav.GetDirection())) return Event_Processed;
		}
		return Event_Skip;
	}

	/// Once the tool's command has run (and any window it opened has gone)
	static void AfterClick(wxToolBar *clicked) {
		wxWeakRef<wxWindow> bar(clicked), target;
		for (auto const& [b, t] : click_targets)
			if (b.get() == clicked) target = t;
		clicked->CallAfter([bar, target] {
			if (!bar) return;
			if (target) {
				if (target->IsShownOnScreen()) target->SetFocus();
				return;
			}
			// A toolbar's buttons don't take the keyboard: left with none,
			// or with the bar, it goes home
			wxWindow *focus = wxWindow::FindFocus();
			auto top = dynamic_cast<wxTopLevelWindow*>(wxGetTopLevelParent(bar.get()));
			if ((!focus || focus == bar.get()) && top && top->IsActive())
				flat::GoHome(bar.get());
		});
	}

	void CallAfterRedraw() {
		// Once the keyboard is where it is going
		if (backdrop) backdrop->CallAfter([] { Redraw(); });
	}
};

void Install() {
	static bool installed = false;
	if (installed) return;
	installed = true;
	// Never destroyed: in place for the whole run
	wxEvtHandler::AddFilter(new Filter);
}

/// Mouse-only parts, never stopped at: a toolbar's buttons have no keys,
/// and scroll bars are worked with the window's own keys
bool Skipped(wxWindow *window) {
	return dynamic_cast<wxToolBar*>(window) || dynamic_cast<wxScrollBar*>(window);
}

/// A window holding others, gone into rather than stopped at
bool Holder(wxWindow *window) {
	return dynamic_cast<wxPanel*>(window) || dynamic_cast<wxSashWindow*>(window);
}

void CollectStops(wxWindow *window, std::vector<wxWindow*> &stops) {
	for (auto child : window->GetChildren()) {
		if (!child->IsShownOnScreen() || child->IsTopLevel() || Skipped(child)) continue;
		if (!Holder(child) && child->IsEnabled() && child->AcceptsFocusFromKeyboard()) {
			stops.push_back(child);
			continue;
		}
		CollectStops(child, stops);
	}
}

}

void Setup(wxWindow *new_backdrop, std::vector<Region> regions) {
	Install();
	if (backdrop) backdrop->Unbind(wxEVT_PAINT, &Paint);
	backdrop = new_backdrop;
	list = std::move(regions);
	drawn = nullptr;
	by_keys = false;
	if (!backdrop) return;
	backdrop->Bind(wxEVT_PAINT, &Paint);
	backdrop->Bind(wxEVT_DESTROY, [](wxWindowDestroyEvent &e) {
		if (e.GetEventObject() == backdrop) Clear();
		e.Skip();
	});
}

void FocusAfterClicks(wxWindow *bar, wxWindow *target) {
	Install();
	// Gone windows' entries dropped on the way
	click_targets.erase(std::remove_if(click_targets.begin(), click_targets.end(),
		[](auto const& p) { return !p.first; }), click_targets.end());
	click_targets.emplace_back(bar, target);
}

void Clear() {
	backdrop = nullptr;
	list.clear();
	drawn = nullptr;
	by_keys = false;
}

int RegionOf(wxWindow const *window) {
	for (size_t i = 0; i < list.size(); ++i)
		if (list[i].block && Within(window, list[i].block)) return static_cast<int>(i);
	return -1;
}

int Current() {
	wxWindow *focus = wxWindow::FindFocus();
	return focus ? RegionOf(focus) : -1;
}

std::vector<wxWindow*> Stops(int region) {
	std::vector<wxWindow*> stops;
	if (region < 0 || region >= static_cast<int>(list.size()) || !list[region].block) return stops;
	wxWindow *block = list[region].block;
	// A block that is one control (the grid) is the first stop itself
	if (!Holder(block) && block->IsEnabled() && block->AcceptsFocusFromKeyboard() && block->IsShownOnScreen())
		stops.push_back(block);
	CollectStops(block, stops);
	return stops;
}

wxWindow *TabTarget(wxWindow *from, bool forward) {
	int region = RegionOf(from);
	if (region < 0) return nullptr;
	auto stops = Stops(region);
	if (stops.empty()) return nullptr;
	// The stop the keyboard is in: itself, or the box inside a field
	auto it = std::find_if(stops.begin(), stops.end(), [&](wxWindow *stop) { return Within(from, stop); });
	size_t n = stops.size(), i;
	if (it == stops.end()) i = forward ? 0 : n - 1;
	else i = (static_cast<size_t>(it - stops.begin()) + (forward ? 1 : n - 1)) % n;
	return stops[i];
}

bool MoveOn(wxWindow *from, bool forward) {
	int region = RegionOf(from);
	if (region < 0) return false;
	by_keys = true;
	// The side panel goes round its parts in its own order
	if (auto own = dynamic_cast<PanelFocus*>(list[region].block)) {
		own->MoveFocus(from, forward);
		Redraw();
		return true;
	}
	if (auto target = TabTarget(from, forward)) target->SetFocus();
	Redraw();
	return true;
}

bool Next(bool forward) {
	if (list.empty()) return false;
	int n = static_cast<int>(list.size());
	int start = Current();
	if (start < 0) start = forward ? -1 : n;
	for (int step = 1; step <= n; ++step) {
		int i = ((start + (forward ? step : -step)) % n + n) % n;
		auto const& r = list[i];
		// The side panel opens on the way: its "available" says it can
		if (!Available(r)) continue;
		bool took = false;
		if (r.enter) took = r.enter();
		else {
			auto stops = Stops(i);
			if (!stops.empty()) {
				stops.front()->SetFocus();
				took = true;
			}
		}
		if (took) {
			by_keys = true;
			if (backdrop) backdrop->CallAfter([] { Redraw(); });
			Redraw();
			return true;
		}
	}
	return false;
}

wxWindow *Outlined() {
	if (!by_keys || !backdrop || !backdrop->IsShownOnScreen()) return nullptr;
	int region = Current();
	if (region < 0) return nullptr;
	return list[region].block;
}

agi::signal::Connection AddOutlineListener(std::function<void()> listener) {
	return outline_changed.Connect(std::move(listener));
}

void DrawOutline(wxWindow *window, wxDC &dc) {
	wxWindow *block = Outlined();
	if (!backdrop || !block) return;
	wxRect r = OutlineRect(block);
	// From the backdrop's corner to the window's
	r.Offset(backdrop->ClientToScreen(wxPoint(0, 0)) - window->ClientToScreen(wxPoint(0, 0)));
	flat::DrawOutline(dc, r, backdrop->FromDIP(5), std::max(2, backdrop->FromDIP(2)), theme::UiColours().accent);
}

bool HandleKey(wxWindow *window, wxKeyEvent &event) {
	int key = event.GetKeyCode();
	if (key == WXK_TAB && !event.ControlDown() && !event.AltDown() && !event.MetaDown())
		return MoveOn(window, !event.ShiftDown());
	if (key == WXK_ESCAPE && !event.HasAnyModifiers())
		return flat::GoHome(window);
	return false;
}

}

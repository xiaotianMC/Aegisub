// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "toolbar_keys.h"

#include "flat_controls.h"
#include "keyboard_regions.h"
#include "theme.h"

#include <algorithm>

#include <wx/dcclient.h>
#include <wx/graphics.h>
#include <wx/menu.h>
#include <wx/weakref.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <commctrl.h>
#endif

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace {
int RingThickness(wxWindow const *window) {
	return std::max(2, window->FromDIP(2));
}

int RingRadius(wxWindow const *window) {
	return window->FromDIP(4);
}

#ifdef __WXGTK__
// After the bar and its buttons are drawn: the ring over them
gboolean DrawRing(GtkWidget *, cairo_t *cr, KeyToolBar *bar) {
	wxRect ring = bar->RingRect();
	if (ring.IsEmpty()) return FALSE;
	std::unique_ptr<wxGraphicsContext> gc(wxGraphicsRenderer::GetCairoRenderer()->CreateContextFromNativeContext(cr));
	if (gc) flat::DrawOutline(*gc, ring, RingRadius(bar), RingThickness(bar), theme::UiColours().accent);
	return FALSE;
}
#endif
}

KeyToolBar::KeyToolBar(wxWindow *parent, long style, bool keyboard)
: wxToolBar(parent, -1, wxDefaultPosition, wxDefaultSize, style)
, keyboard(keyboard)
{
	if (!keyboard) return;
	// GTK lets a widget have the keyboard only when told it can
	SetCanFocus(true);
	Bind(wxEVT_CHAR_HOOK, &KeyToolBar::OnKey, this);
	Bind(wxEVT_SET_FOCUS, &KeyToolBar::OnFocus, this);
	Bind(wxEVT_KILL_FOCUS, &KeyToolBar::OnFocus, this);
#ifdef __WXGTK__
	g_signal_connect_after(GetHandle(), "draw", G_CALLBACK(DrawRing), this);
#endif
}

bool KeyToolBar::AcceptsFocusFromKeyboard() const {
	return keyboard && IsEnabled() && !KeyTools().empty();
}

std::vector<int> KeyToolBar::KeyTools() const {
	std::vector<int> tools;
	for (size_t i = 0; i < GetToolsCount(); ++i) {
		auto tool = GetToolByPos(static_cast<int>(i));
		if (tool && tool->IsButton() && tool->IsEnabled())
			tools.push_back(tool->GetId());
	}
	return tools;
}

int KeyToolBar::KeyTool() const {
	auto tools = KeyTools();
	if (tools.empty()) return wxID_NONE;
	if (std::find(tools.begin(), tools.end(), key_tool) != tools.end()) return key_tool;
	for (int id : tools) {
		auto tool = FindById(id);
		if (tool->GetKind() == wxITEM_RADIO && tool->IsToggled()) return id;
	}
	return tools.front();
}

void KeyToolBar::SetKeyTool(int id) {
	if (id == key_tool) return;
	key_tool = id;
	// With the background: a flat bar's buttons are drawn over it, and the
	// ring would stay where it was
	Refresh();
}

bool KeyToolBar::PressTool(int id) {
	auto tool = FindById(id);
	if (!tool || !tool->IsButton() || !tool->IsEnabled()) return false;
	// As the toolkit does on a click: the button's state first, then the event
	bool down = tool->IsToggled();
	if (tool->GetKind() == wxITEM_CHECK) {
		down = !down;
		ToggleTool(id, down);
	}
	else if (tool->GetKind() == wxITEM_RADIO) {
		down = true;
		ToggleTool(id, true);
	}
	OnLeftClick(id, down);
	return true;
}

void KeyToolBar::SetMenuOpener(int id, std::function<void()> open) {
	menu_openers[id] = std::move(open);
}

bool KeyToolBar::OpenMenu(int id) {
	if (auto it = menu_openers.find(id); it != menu_openers.end()) {
		it->second();
		return true;
	}
	auto tool = FindById(id);
	if (!tool || !tool->IsEnabled() || !tool->GetDropdownMenu()) return false;
	// As the arrow of a drop-down button does: told first, which can show
	// a menu of its own instead
	wxCommandEvent event(wxEVT_TOOL_DROPDOWN, id);
	event.SetEventObject(this);
	if (HandleWindowEvent(event)) return true;
	wxRect rect = ToolRect(id);
	wxPoint at = rect.IsEmpty() ? wxPoint(0, GetClientSize().y)
		: HasFlag(wxTB_VERTICAL) ? wxPoint(rect.GetRight() + 1, rect.y) : wxPoint(rect.x, rect.GetBottom() + 1);
	PopupMenu(tool->GetDropdownMenu(), at);
	return true;
}

bool KeyToolBar::Key(int key, int modifiers) {
	auto tools = KeyTools();
	if (tools.empty()) return false;
	int current = KeyTool();
	auto at = std::find(tools.begin(), tools.end(), current);
	bool vertical = HasFlag(wxTB_VERTICAL);
	if (modifiers == wxMOD_ALT && (key == WXK_DOWN || key == WXK_NUMPAD_DOWN))
		return OpenMenu(current);
	if (modifiers != wxMOD_NONE) return false;
	// Along the bar; the other arrows are the region's (the lines, in the
	// video and the audio). Not round from one end to the other
	bool back = vertical ? key == WXK_UP || key == WXK_NUMPAD_UP : key == WXK_LEFT || key == WXK_NUMPAD_LEFT;
	bool on = vertical ? key == WXK_DOWN || key == WXK_NUMPAD_DOWN : key == WXK_RIGHT || key == WXK_NUMPAD_RIGHT;
	if (back) {
		SetKeyTool(*(at == tools.begin() ? at : at - 1));
		return true;
	}
	if (on) {
		SetKeyTool(*(at + 1 == tools.end() ? at : at + 1));
		return true;
	}
	switch (key) {
		case WXK_HOME: case WXK_NUMPAD_HOME:
			SetKeyTool(tools.front());
			return true;
		case WXK_END: case WXK_NUMPAD_END:
			SetKeyTool(tools.back());
			return true;
		case WXK_SPACE: case WXK_RETURN: case WXK_NUMPAD_ENTER: {
			// Taken even on a greyed button: Space must not play instead
			wxWeakRef<KeyToolBar> self(this);
			key_tool = current;
			pressing_by_keys = true;
			PressTool(current);
			if (self) pressing_by_keys = false;
			return true;
		}
		case WXK_F4:
			return OpenMenu(current);
	}
	return false;
}

void KeyToolBar::OnKey(wxKeyEvent &event) {
	if (!HasFocus()) {
		event.Skip();
		return;
	}
	if (Key(event.GetKeyCode(), event.GetModifiers())) return;
	if (regions::OtherKeys(this, event)) return;
	event.Skip();
}

void KeyToolBar::OnFocus(wxFocusEvent &event) {
	event.Skip();
	Refresh();
}

wxRect KeyToolBar::RingRect() const {
	if (!HasFocus()) return {};
	int tool = KeyTool();
	if (tool == wxID_NONE) return {};
	wxRect rect = ToolRect(tool);
	if (rect.IsEmpty()) return {};
	return rect.Deflate(1);
}

wxRect KeyToolBar::ToolRect(int id) const {
	int pos = GetToolPos(id);
	if (pos == wxNOT_FOUND) return {};
#if defined(__WXMSW__)
	RECT r;
	if (!::SendMessage(static_cast<HWND>(GetHWND()), TB_GETITEMRECT, pos, reinterpret_cast<LPARAM>(&r))) return {};
	return wxRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
#elif defined(__WXGTK__)
	GtkToolItem *item = gtk_toolbar_get_nth_item(GTKGetToolbar(), pos);
	if (!item) return {};
	GtkWidget *widget = GTK_WIDGET(item);
	int x = 0, y = 0;
	if (!gtk_widget_translate_coordinates(widget, GetHandle(), 0, 0, &x, &y)) return {};
	return wxRect(x, y, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget));
#else
	return {};
#endif
}

#ifdef __WXMSW__
WXLRESULT KeyToolBar::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) {
	if (message != WM_PAINT) return wxToolBar::MSWWindowProc(message, wParam, lParam);
	wxRect ring = RingRect();
	if (ring.IsEmpty()) return wxToolBar::MSWWindowProc(message, wParam, lParam);
	// Before painting validates what is to be painted: when any of the
	// ring is, all of it is, as a smooth ring drawn again over itself grows
	// at its soft edges; when none of it is, it is left as it is
	HWND hwnd = static_cast<HWND>(GetHWND());
	RECT area{ring.x - 1, ring.y - 1, ring.x + ring.width + 1, ring.y + ring.height + 1};
	HRGN update = ::CreateRectRgn(0, 0, 0, 0);
	bool draw = ::GetUpdateRgn(hwnd, update, FALSE) != ERROR && ::RectInRegion(update, &area);
	::DeleteObject(update);
	if (draw) ::InvalidateRect(hwnd, &area, TRUE);
	WXLRESULT result = wxToolBar::MSWWindowProc(message, wParam, lParam);
	if (draw) {
		wxClientDC dc(this);
		flat::DrawOutline(dc, ring, RingRadius(this), RingThickness(this), theme::UiColours().accent);
	}
	return result;
}
#endif

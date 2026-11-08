// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "flat_controls.h"

#include "theme.h"

#include <algorithm>
#include <set>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/textctrl.h>
#include <wx/textentry.h>
#include <wx/time.h>
#include <wx/utils.h>

namespace flat {
Palette Colours(wxWindow const *window) {
	Palette p;
	p.surface = theme::Surface();
	p.text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
	if (!window->IsEnabled()) p.text = theme::Mix(p.surface, p.text, 0.4);
	p.dim = theme::Mix(p.surface, p.text, 0.55);
	p.field = theme::Mix(p.surface, p.text, theme::IsDark() ? 0.08 : 0.05);
	p.line = theme::Mix(p.surface, p.text, 0.2);
	p.accent = theme::UiColours().accent;
	return p;
}

void DrawCheck(wxDC &dc, wxWindow const *window, wxRect const& check, int state, Palette const& p) {
	int thick = std::max(1, window->FromDIP(1));
	int cy = check.y + check.height / 2;
	if (state == 0) {
		dc.SetBrush(wxBrush(p.field));
		dc.SetPen(wxPen(theme::Mix(p.surface, p.text, 0.45), thick));
		dc.DrawRoundedRectangle(check, window->FromDIP(3));
		return;
	}
	dc.SetBrush(wxBrush(p.accent));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRoundedRectangle(check, window->FromDIP(3));
	wxColour mark = theme::IsDark() ? wxColour(20, 20, 20) : *wxWHITE;
	dc.SetPen(wxPen(mark, std::max(1, window->FromDIP(2))));
	auto d = [window](int v) { return window->FromDIP(v); };
	if (state == 2)
		dc.DrawLine(check.x + d(4), cy, check.GetRight() - d(3), cy);
	else {
		dc.DrawLine(check.x + d(3), cy, check.x + d(6), cy + d(3));
		dc.DrawLine(check.x + d(6), cy + d(3), check.GetRight() - d(3), cy - d(3));
	}
}

int WheelSteps(wxMouseEvent const& event, int &remainder) {
	int delta = std::max(1, event.GetWheelDelta());
	remainder += event.GetWheelRotation();
	int steps = remainder / delta;
	remainder -= steps * delta;
	return steps;
}

namespace {
wxLongLong last_scroll = 0;
}

void NoteScroll() {
	last_scroll = wxGetLocalTimeMillis();
}

bool Scrolling() {
	if (wxGetLocalTimeMillis() - last_scroll > 400) return false;
	NoteScroll();
	return true;
}

namespace {
wxWindow *home = nullptr;
std::set<wxWindow*> fields, wheel_targets;

bool Within(wxWindow *window, wxWindow *ancestor) {
	for (; window; window = window->GetParent())
		if (window == ancestor) return true;
	return false;
}

/// Leaves fields on clicks elsewhere, and sends wheel turns to the control
/// under the pointer
class Filter final : public wxEventFilter {
	bool routing = false;

	int FilterEvent(wxEvent &event) override {
		auto type = event.GetEventType();
		if (type == wxEVT_LEFT_DOWN || type == wxEVT_RIGHT_DOWN || type == wxEVT_MIDDLE_DOWN)
			OnClick(event);
		else if (type == wxEVT_MOUSEWHEEL && !routing)
			return OnWheel(static_cast<wxMouseEvent&>(event));
		return Event_Skip;
	}

	void OnClick(wxEvent &event) {
		auto focus = wxWindow::FindFocus();
		if (!focus || !fields.count(focus)) return;
		auto clicked = wxDynamicCast(event.GetEventObject(), wxWindow);
		if (!clicked) return;
		// In the field or the frame around it: still typing
		if (Within(clicked, focus) || clicked == focus->GetParent()) return;
		// Something that takes the keyboard itself gets it
		if (clicked->AcceptsFocus() && clicked->IsEnabled() && clicked != home && !Within(clicked, home)) {
			if (dynamic_cast<wxTextEntry*>(clicked)) return;
		}
		LeaveField();
	}

	int OnWheel(wxMouseEvent &event) {
		auto source = wxDynamicCast(event.GetEventObject(), wxWindow);
		wxPoint mouse = wxGetMousePosition();
		auto under = wxFindWindowAtPoint(mouse);
		wxWindow *target = nullptr;
		for (auto w = under; w && !target; w = w->GetParent()) {
			if (wheel_targets.count(w)) target = w;
			if (w->IsTopLevel()) break;
		}
		if (!target || !source || Within(source, target)) return Event_Skip;
		wxMouseEvent copy(event);
		copy.SetEventObject(target);
		copy.SetPosition(target->ScreenToClient(mouse));
		routing = true;
		target->GetEventHandler()->ProcessEvent(copy);
		routing = false;
		return Event_Processed;
	}
};

void Install() {
	static bool installed = false;
	if (installed) return;
	installed = true;
	// Never destroyed: it is in place for the whole run
	wxEvtHandler::AddFilter(new Filter);
}

void Track(std::set<wxWindow*> &set, wxWindow *window) {
	Install();
	set.insert(window);
	window->Bind(wxEVT_DESTROY, [&set, window](wxWindowDestroyEvent &e) {
		if (e.GetEventObject() == window) set.erase(window);
		e.Skip();
	});
}
}

void SetHome(wxWindow *window) {
	Install();
	home = window;
	window->Bind(wxEVT_DESTROY, [window](wxWindowDestroyEvent &e) {
		if (e.GetEventObject() == window && home == window) home = nullptr;
		e.Skip();
	});
}

void AddField(wxWindow *field) {
	Track(fields, field);
}

void AddWheelTarget(wxWindow *target) {
	Track(wheel_targets, target);
}

void LeaveField() {
	auto focus = wxWindow::FindFocus();
	if (!focus || !fields.count(focus)) return;
	if (home && home->IsShownOnScreen()) home->SetFocus();
	else {
		// Nowhere to go: the keyboard leaves for the panel around it
		for (auto w = focus->GetParent(); w; w = w->GetParent())
			if (auto panel = dynamic_cast<wxPanel*>(w)) {
				panel->SetFocusIgnoringChildren();
				break;
			}
	}
}
}

namespace {
void DrawChevron(wxDC &dc, wxColour const& colour, int x, int cy, int size, int thick) {
	dc.SetPen(wxPen(colour, thick));
	dc.DrawLine(x, cy - size / 4, x + size / 2, cy + size / 4);
	dc.DrawLine(x + size / 2, cy + size / 4, x + size, cy - size / 4);
}

/// The rounded field every control here sits in
void DrawField(wxDC &dc, wxWindow *window, flat::Palette const& p, bool focused, bool hover) {
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(p.surface));
	dc.DrawRectangle(wxPoint(0, 0), window->GetClientSize());
	wxColour fill = hover && window->IsEnabled() ? theme::Mix(p.field, p.text, 0.05) : p.field;
	dc.SetBrush(wxBrush(fill));
	dc.SetPen(wxPen(focused ? p.accent : p.line, std::max(1, window->FromDIP(1))));
	dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), window->GetClientSize()), window->FromDIP(5));
}

constexpr int field_height = 28;
}

// FlatFrame

FlatFrame::FlatFrame(wxWindow *parent, wxString label)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE | wxTAB_TRAVERSAL)
, label(std::move(label))
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	Bind(wxEVT_PAINT, &FlatFrame::OnPaint, this);
	Bind(wxEVT_SIZE, [this](wxSizeEvent &e) { Arrange(); e.Skip(); });
	Bind(wxEVT_LEFT_DOWN, &FlatFrame::OnLeftDown, this);
	Bind(wxEVT_LEFT_DCLICK, &FlatFrame::OnLeftDown, this);
	Bind(wxEVT_MOTION, &FlatFrame::OnMotion, this);
	Bind(wxEVT_LEFT_UP, &FlatFrame::OnLeftUp, this);
	Bind(wxEVT_MOUSEWHEEL, &FlatFrame::OnWheel, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) {
		bool was = scrubbing;
		pressed = scrubbing = false;
		if (was && on_scrub) on_scrub(scrub_steps, true);
	});
	theme_changed = theme::AddChangeListener([this] { ApplyColours(); Refresh(false); });
}

int FlatFrame::LabelWidth() const {
	if (label.empty() || !label_shown) return 0;
	return GetTextExtent(label).x + FromDIP(6);
}

wxRect FlatFrame::ChevronRect() const {
	if (!on_chevron) return {};
	int width = FromDIP(22);
	return wxRect(GetClientSize().x - width, 0, width, GetClientSize().y);
}

void FlatFrame::SetContent(wxWindow *control, int width) {
	content = control;
	content_width = width;
	content->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) { Refresh(false); e.Skip(); });
	content->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) { Refresh(false); e.Skip(); });
	content->Bind(wxEVT_MOUSEWHEEL, &FlatFrame::OnWheel, this);
	flat::AddField(content);
	ApplyColours();
	Arrange();
}

bool FlatFrame::Enable(bool enable) {
	bool changed = wxWindow::Enable(enable);
	if (changed) { ApplyColours(); Refresh(false); }
	return changed;
}

void FlatFrame::SetDimContent(bool dim) {
	dim_content = dim;
	ApplyColours();
}

void FlatFrame::ShowLabel(bool show) {
	if (show == label_shown) return;
	label_shown = show;
	Arrange();
	Refresh(false);
}

void FlatFrame::SetChevron(std::function<void()> open) {
	on_chevron = std::move(open);
	Arrange();
}

void FlatFrame::SetScrub(std::function<void(int, bool)> change) {
	on_scrub = std::move(change);
}

void FlatFrame::SetWheel(std::function<void(int)> change) {
	on_wheel = std::move(change);
	flat::AddWheelTarget(this);
}

void FlatFrame::ApplyColours() {
	if (!content) return;
	auto p = flat::Colours(this);
	content->SetBackgroundColour(p.field);
	content->SetForegroundColour(dim_content ? p.dim : p.text);
	content->Refresh();
}

void FlatFrame::Arrange() {
	if (!content) return;
	int pad = FromDIP(8);
	int left = pad + LabelWidth();
	int right = on_chevron ? ChevronRect().x : GetClientSize().x - pad / 2;
	int height = std::min(content->GetBestSize().y, GetClientSize().y - FromDIP(4));
	content->SetSize(left, (GetClientSize().y - height) / 2, std::max(1, right - left), std::max(1, height));
	int chevron = on_chevron ? FromDIP(22) : pad / 2;
	SetMinSize(wxSize(left + FromDIP(content_width) + chevron,
		std::max(FromDIP(field_height), content->GetBestSize().y + FromDIP(4))));
}

void FlatFrame::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	auto p = flat::Colours(this);
	bool focused = content && content->HasFocus();
	DrawField(dc, this, p, focused, false);
	dc.SetFont(GetFont());
	if (!label.empty() && label_shown) {
		dc.SetTextForeground(p.dim);
		dc.DrawText(label, FromDIP(8), (GetClientSize().y - dc.GetCharHeight()) / 2);
	}
	if (on_chevron) {
		int size = FromDIP(7);
		wxRect r = ChevronRect();
		DrawChevron(dc, p.dim, r.x + (r.width - size) / 2, r.y + r.height / 2 - FromDIP(1), size, std::max(1, FromDIP(3) / 2));
	}
}

void FlatFrame::OnLeftDown(wxMouseEvent &event) {
	if (!IsEnabled()) return;
	if (on_chevron && ChevronRect().Contains(event.GetPosition())) {
		CallAfter([this] { if (on_chevron) on_chevron(); });
		return;
	}
	if (on_scrub && event.GetX() < FromDIP(8) + LabelWidth()) {
		pressed = true;
		scrubbing = false;
		scrub_steps = 0;
		press_x = event.GetX();
		CaptureMouse();
		return;
	}
	if (content) content->SetFocus();
}

void FlatFrame::OnMotion(wxMouseEvent &event) {
	bool over_label = on_scrub && event.GetX() < FromDIP(8) + LabelWidth();
	SetCursor(over_label || pressed ? wxCursor(wxCURSOR_SIZEWE) : on_chevron && ChevronRect().Contains(event.GetPosition()) ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
	if (!pressed || !HasCapture()) return;
	int dx = event.GetX() - press_x;
	if (!scrubbing && std::abs(dx) < FromDIP(3)) return;
	scrubbing = true;
	int steps = dx / FromDIP(6);
	if (steps == scrub_steps) return;
	scrub_steps = steps;
	if (on_scrub) on_scrub(steps, false);
}

void FlatFrame::OnLeftUp(wxMouseEvent &) {
	if (!pressed) return;
	pressed = false;
	if (HasCapture()) ReleaseMouse();
	if (scrubbing) {
		scrubbing = false;
		if (on_scrub) on_scrub(scrub_steps, true);
	}
	else if (content)
		content->SetFocus();
}

void FlatFrame::OnWheel(wxMouseEvent &event) {
	if (!on_wheel || !IsEnabled() || flat::Scrolling()) {
		event.Skip();
		return;
	}
	if (int steps = flat::WheelSteps(event, wheel_remainder))
		on_wheel(steps);
}

// FlatChoice

FlatChoice::FlatChoice(wxWindow *parent, wxString const& tip)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetToolTip(tip);
	SetCursor(wxCursor(wxCURSOR_HAND));
	Bind(wxEVT_PAINT, &FlatChoice::OnPaint, this);
	Bind(wxEVT_KEY_DOWN, &FlatChoice::OnKey, this);
	Bind(wxEVT_MOUSEWHEEL, &FlatChoice::OnWheel, this);
	Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
		if (!IsEnabled()) return;
		SetFocus();
		CallAfter([this] { ShowList(); });
	});
	Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) { hover = true; Refresh(false); e.Skip(); });
	Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) { hover = false; Refresh(false); e.Skip(); });
	Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) { Refresh(false); e.Skip(); });
	Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) { Refresh(false); e.Skip(); });
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
	SetMinSize(wxSize(FromDIP(60), FromDIP(field_height)));
	flat::AddWheelTarget(this);
}

bool FlatChoice::Enable(bool enable) {
	bool changed = wxWindow::Enable(enable);
	if (changed) Refresh(false);
	return changed;
}

wxSize FlatChoice::DoGetBestClientSize() const {
	int width = FromDIP(60);
	for (auto const& item : items)
		width = std::max(width, GetTextExtent(item).x);
	return wxSize(std::min(width, FromDIP(140)) + FromDIP(8 + 22), FromDIP(field_height));
}

void FlatChoice::Set(std::vector<wxString> new_items) {
	wxString current = GetStringSelection();
	items = std::move(new_items);
	SetStringSelection(current);
	InvalidateBestSize();
}

void FlatChoice::SetStringSelection(wxString const& name) {
	auto it = std::find(items.begin(), items.end(), name);
	int index = it == items.end() ? -1 : static_cast<int>(it - items.begin());
	if (index == selection) return;
	selection = index;
	Refresh(false);
}

wxString FlatChoice::GetStringSelection() const {
	return selection >= 0 && selection < static_cast<int>(items.size()) ? items[selection] : wxString();
}

void FlatChoice::Choose(int index) {
	if (index < 0 || index >= static_cast<int>(items.size()) || index == selection) return;
	selection = index;
	Refresh(false);
	if (on_choose) on_choose(index);
}

void FlatChoice::Step(int delta) {
	if (items.empty()) return;
	int from = selection < 0 ? (delta > 0 ? -1 : static_cast<int>(items.size())) : selection;
	Choose(std::clamp(from + delta, 0, static_cast<int>(items.size()) - 1));
}

void FlatChoice::ShowList() {
	if (items.empty() || !IsEnabled()) return;
	wxMenu menu;
	for (size_t i = 0; i < items.size(); ++i)
		menu.AppendCheckItem(wxID_HIGHEST + 1 + i, items[i])->Check(static_cast<int>(i) == selection);
	int chosen = GetPopupMenuSelectionFromUser(menu, 0, GetSize().y);
	if (chosen > wxID_HIGHEST) Choose(chosen - wxID_HIGHEST - 1);
}

void FlatChoice::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	auto p = flat::Colours(this);
	DrawField(dc, this, p, HasFocus(), hover);
	dc.SetFont(GetFont());
	wxSize size = GetClientSize();
	int pad = FromDIP(8), chevron = FromDIP(7);
	int right = size.x - pad - chevron - FromDIP(6);
	wxString text = GetStringSelection();
	if (dc.GetTextExtent(text).x > right - pad) {
		while (!text.empty() && dc.GetTextExtent(text + wxS("…")).x > right - pad) text.RemoveLast();
		text += wxS("…");
	}
	dc.SetTextForeground(p.text);
	dc.DrawText(text, pad, (size.y - dc.GetCharHeight()) / 2);
	DrawChevron(dc, p.dim, size.x - pad - chevron, size.y / 2 - FromDIP(1), chevron, std::max(1, FromDIP(3) / 2));
}

void FlatChoice::OnKey(wxKeyEvent &event) {
	switch (event.GetKeyCode()) {
		case WXK_UP: case WXK_NUMPAD_UP: Step(-1); break;
		case WXK_DOWN: case WXK_NUMPAD_DOWN:
			if (event.AltDown()) ShowList(); else Step(1);
			break;
		case WXK_HOME: Choose(0); break;
		case WXK_END: Choose(static_cast<int>(items.size()) - 1); break;
		case WXK_SPACE: case WXK_RETURN: case WXK_NUMPAD_ENTER: case WXK_F4: ShowList(); break;
		case WXK_TAB:
			Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
			break;
		default: event.Skip();
	}
}

void FlatChoice::OnWheel(wxMouseEvent &event) {
	if (!IsEnabled() || flat::Scrolling()) {
		event.Skip();
		return;
	}
	// Up goes up the list
	if (int steps = flat::WheelSteps(event, wheel_remainder))
		Step(-steps);
}

// FlatBadge

FlatBadge::FlatBadge(wxWindow *parent)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetCursor(wxCursor(wxCURSOR_HAND));
	Bind(wxEVT_PAINT, &FlatBadge::OnPaint, this);
	Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) { if (on_click) CallAfter([this] { if (on_click) on_click(); }); });
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
}

void FlatBadge::SetText(wxString const& new_text, bool warn) {
	if (text == new_text && warning == warn) return;
	text = new_text;
	warning = warn;
	InvalidateBestSize();
	Refresh(false);
}

wxSize FlatBadge::DoGetBestClientSize() const {
	wxSize extent = GetTextExtent(text.empty() ? wxString("0") : text);
	return wxSize(extent.x + FromDIP(12), extent.y + FromDIP(4));
}

void FlatBadge::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	// On the text box's own background
	wxColour back = GetParent()->GetBackgroundColour();
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(back));
	dc.DrawRectangle(wxPoint(0, 0), GetClientSize());
	auto p = flat::Colours(this);
	wxColour text_colour = theme::Mix(back, p.text, 0.55);
	if (warning) {
		wxColour error = theme::Colour("Colour/Subtitle/Syntax/Background/Error");
		dc.SetBrush(wxBrush(error));
		dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), GetClientSize()), GetClientSize().y / 2.);
		text_colour = theme::Mix(error, p.text, 0.9);
	}
	dc.SetFont(GetFont());
	dc.SetTextForeground(text_colour);
	wxSize extent = dc.GetTextExtent(text);
	dc.DrawText(text, (GetClientSize().x - extent.x) / 2, (GetClientSize().y - extent.y) / 2);
}

// FlatCheck

FlatCheck::FlatCheck(wxWindow *parent, wxString const& label)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS)
, label(label)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetCursor(wxCursor(wxCURSOR_HAND));
	Bind(wxEVT_PAINT, &FlatCheck::OnPaint, this);
	// Not skipped: the text keeps the keyboard and its selection
	auto click = [this](wxMouseEvent &) { if (IsEnabled()) Toggle(); };
	Bind(wxEVT_LEFT_DOWN, click);
	Bind(wxEVT_LEFT_DCLICK, click);
	Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &event) {
		if (event.GetKeyCode() == WXK_SPACE && !event.HasAnyModifiers()) Toggle();
		else event.Skip();
	});
	Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &event) { Refresh(false); event.Skip(); });
	Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &event) { Refresh(false); event.Skip(); });
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
}

void FlatCheck::SetValue(bool checked) {
	if (value == checked) return;
	value = checked;
	Refresh(false);
}

void FlatCheck::Toggle() {
	SetValue(!value);
	wxCommandEvent event(wxEVT_CHECKBOX, GetId());
	event.SetEventObject(this);
	event.SetInt(value);
	ProcessWindowEvent(event);
}

int FlatCheck::Mnemonic() const {
	auto pos = label.find('&');
	if (pos == wxString::npos || pos + 1 >= label.size()) return 0;
	return wxToupper(label[pos + 1]);
}

bool FlatCheck::Enable(bool enable) {
	bool changed = wxWindow::Enable(enable);
	if (changed) Refresh(false);
	return changed;
}

wxSize FlatCheck::DoGetBestClientSize() const {
	wxSize extent = GetTextExtent(wxStripMenuCodes(label));
	return wxSize(FromDIP(14) + FromDIP(6) + extent.x + FromDIP(2), std::max(FromDIP(18), extent.y + FromDIP(4)));
}

void FlatCheck::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	wxSize size = GetClientSize();
	auto p = flat::Colours(this);
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(GetParent()->GetBackgroundColour()));
	dc.DrawRectangle(wxPoint(0, 0), size);

	int box = FromDIP(14);
	wxRect check(0, (size.y - box) / 2, box, box);
	flat::DrawCheck(dc, this, check, value ? 1 : 0, p);
	if (HasFocus()) {
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.SetPen(wxPen(p.accent, std::max(1, FromDIP(1))));
		dc.DrawRoundedRectangle(check.Inflate(FromDIP(2)), FromDIP(4));
	}

	dc.SetFont(GetFont());
	dc.SetTextForeground(p.text);
	wxString shown = wxStripMenuCodes(label);
	wxSize extent = dc.GetTextExtent(shown);
	dc.DrawText(shown, box + FromDIP(6), (size.y - extent.y) / 2);
}

// FlatButton

FlatButton::FlatButton(wxWindow *parent, wxString const& label)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS)
, label(label)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetCursor(wxCursor(wxCURSOR_HAND));
	Bind(wxEVT_PAINT, &FlatButton::OnPaint, this);
	Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) { hover = true; Refresh(false); });
	Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { hover = pressed = false; Refresh(false); });
	// Not skipped: the text keeps the keyboard and its selection
	auto down = [this](wxMouseEvent &) {
		if (!IsEnabled()) return;
		pressed = true;
		Refresh(false);
	};
	Bind(wxEVT_LEFT_DOWN, down);
	Bind(wxEVT_LEFT_DCLICK, down);
	Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
		if (!pressed) return;
		pressed = false;
		Refresh(false);
		if (IsEnabled()) Press();
	});
	Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &event) {
		int key = event.GetKeyCode();
		if ((key == WXK_SPACE || key == WXK_RETURN || key == WXK_NUMPAD_ENTER) && !event.HasAnyModifiers()) Press();
		else event.Skip();
	});
	Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &event) { Refresh(false); event.Skip(); });
	Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &event) { Refresh(false); event.Skip(); });
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
}

void FlatButton::Press() {
	wxCommandEvent event(wxEVT_BUTTON, GetId());
	event.SetEventObject(this);
	ProcessWindowEvent(event);
}

void FlatButton::SetLabel(wxString const& new_label) {
	if (label == new_label) return;
	label = new_label;
	InvalidateBestSize();
	Refresh(false);
}

bool FlatButton::Enable(bool enable) {
	bool changed = wxWindow::Enable(enable);
	if (changed) Refresh(false);
	return changed;
}

wxSize FlatButton::DoGetBestClientSize() const {
	wxSize extent = GetTextExtent(wxStripMenuCodes(label));
	return wxSize(extent.x + FromDIP(20), std::max(FromDIP(24), extent.y + FromDIP(8)));
}

void FlatButton::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	wxSize size = GetClientSize();
	auto p = flat::Colours(this);
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(GetParent()->GetBackgroundColour()));
	dc.DrawRectangle(wxPoint(0, 0), size);

	bool lit = hover && IsEnabled();
	dc.SetBrush(wxBrush(lit ? theme::Mix(p.field, p.text, pressed ? 0.1 : 0.05) : p.field));
	dc.SetPen(wxPen(HasFocus() ? p.accent : p.line, std::max(1, FromDIP(1))));
	dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), FromDIP(5));

	dc.SetFont(GetFont());
	dc.SetTextForeground(p.text);
	wxString shown = wxStripMenuCodes(label);
	wxSize extent = dc.GetTextExtent(shown);
	dc.DrawText(shown, (size.x - extent.x) / 2, (size.y - extent.y) / 2);
}

// FlatSuggest

namespace {
const int max_suggestions = 8;
}

FlatSuggest::FlatSuggest(wxTextCtrl *field, wxWindow *anchor, std::function<std::vector<wxString>()> values)
: wxPopupWindow(anchor, wxBORDER_NONE)
, field(field)
, anchor(anchor)
, values(std::move(values))
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	Bind(wxEVT_PAINT, &FlatSuggest::OnPaint, this);
	Bind(wxEVT_MOTION, [this](wxMouseEvent &event) {
		int row = (event.GetY() - FromDIP(4)) / RowHeight();
		int now = row >= 0 && row < static_cast<int>(shown.size()) ? row : -1;
		if (now != hover) { hover = now; Refresh(false); }
	});
	Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { hover = -1; Refresh(false); });
	Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
		int row = (event.GetY() - FromDIP(4)) / RowHeight();
		if (row >= 0 && row < static_cast<int>(shown.size())) Take(row);
	});
	field->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
		event.Skip();
		// Only what the user types: not values set by the program
		if (!filling && this->field->HasFocus()) Suggest();
	});
	field->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &event) {
		event.Skip();
		CallAfter([this] { Close(); });
	});
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
}

int FlatSuggest::RowHeight() const {
	return GetCharHeight() + FromDIP(8);
}

void FlatSuggest::Suggest() {
	wxString typed = field->GetValue();
	shown.clear();
	if (!typed.empty()) {
		wxString lower = typed.Lower();
		for (auto const& value : values()) {
			if (value.length() > typed.length() && value.Lower().StartsWith(lower))
				shown.push_back(value);
			if (static_cast<int>(shown.size()) == max_suggestions) break;
		}
	}
	if (shown.empty()) { Close(); return; }
	selected = 0;
	hover = -1;
	SetFont(anchor->GetFont());
	int pad = FromDIP(4);
	wxSize size(anchor->GetSize().x, static_cast<int>(shown.size()) * RowHeight() + 2 * pad);
	SetSize(size);
	Position(anchor->ClientToScreen(wxPoint(0, 0)), wxSize(0, anchor->GetSize().y + FromDIP(2)));
	if (!IsShown()) Show();
	Refresh(false);
}

bool FlatSuggest::HandleKey(wxKeyEvent &event) {
	if (!IsShown() || shown.empty()) return false;
	int count = static_cast<int>(shown.size());
	switch (event.GetKeyCode()) {
		case WXK_DOWN: case WXK_NUMPAD_DOWN:
			selected = (selected + 1) % count; Refresh(false); return true;
		case WXK_UP: case WXK_NUMPAD_UP:
			selected = (selected + count - 1) % count; Refresh(false); return true;
		case WXK_RETURN: case WXK_NUMPAD_ENTER: case WXK_TAB:
			if (selected < 0) return false;
			Take(selected);
			return event.GetKeyCode() != WXK_TAB; // Tab goes on to the next field
		case WXK_ESCAPE:
			Close(); return true;
		default:
			return false;
	}
}

void FlatSuggest::Take(int index) {
	if (index < 0 || index >= static_cast<int>(shown.size())) return;
	wxString value = shown[index];
	Close();
	filling = true;
	field->SetValue(value);
	field->SetInsertionPointEnd();
	filling = false;
}

void FlatSuggest::Close() {
	shown.clear();
	selected = hover = -1;
	if (IsShown()) Hide();
}

void FlatSuggest::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	auto p = flat::Colours(this);
	wxSize size = GetClientSize();
	int pad = FromDIP(4), radius = FromDIP(4);
	// A raised surface: a little lighter than the block in the dark
	// appearance, white in the light one, with a fine edge
	wxColour back = theme::IsDark() ? theme::Mix(p.surface, p.text, 0.06) : *wxWHITE;
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(p.surface));
	dc.DrawRectangle(wxPoint(0, 0), size);
	dc.SetBrush(wxBrush(back));
	dc.SetPen(wxPen(p.line, std::max(1, FromDIP(1))));
	dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), radius);

	dc.SetFont(GetFont());
	wxString typed = field->GetValue();
	for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
		wxRect row(pad, pad + i * RowHeight(), size.x - 2 * pad, RowHeight());
		if (i == selected || i == hover) {
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(theme::Mix(back, p.accent, i == selected ? 0.22 : 0.1)));
			dc.DrawRoundedRectangle(row, FromDIP(3));
		}
		// What was typed in the text colour, the rest it would add dim
		int x = row.x + FromDIP(8), y = row.y + (row.height - dc.GetCharHeight()) / 2;
		wxString head = shown[i].Left(typed.length()), tail = shown[i].Mid(typed.length());
		dc.SetTextForeground(p.text);
		dc.DrawText(head, x, y);
		dc.SetTextForeground(theme::Mix(back, p.text, 0.7));
		dc.DrawText(tail, x + dc.GetTextExtent(head).x, y);
	}
}

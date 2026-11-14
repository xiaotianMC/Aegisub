// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "flat_controls.h"

#include "theme.h"
#include "libresrc/libresrc.h"

#include <libaegisub/format.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <numbers>
#include <set>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/stockitem.h>
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

void DrawOutline(wxDC &dc, wxRect const& rect, double radius, int thick, wxColour const& colour) {
	if (rect.width <= 0 || rect.height <= 0) return;
	// A raster DC steps its curves: a context of its own over it smooths them
	std::unique_ptr<wxGraphicsContext> own;
	auto gc = dc.GetGraphicsContext();
	if (!gc) {
		own.reset(wxGraphicsContext::CreateFromUnknownDC(dc));
		gc = own.get();
	}
	if (gc) {
		auto path = gc->CreatePath();
		path.AddRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
		int inner_w = rect.width - 2 * thick, inner_h = rect.height - 2 * thick;
		if (inner_w > 0 && inner_h > 0)
			path.AddRoundedRectangle(rect.x + thick, rect.y + thick, inner_w, inner_h, std::max(0., radius - thick));
		// No pen: a pen's width would shift the fill by half a pixel
		gc->SetPen(wxNullGraphicsPen);
		gc->SetBrush(wxBrush(colour));
		gc->FillPath(path, wxODDEVEN_RULE);
		if (!own) {
			gc->SetPen(dc.GetPen());
			gc->SetBrush(dc.GetBrush());
		}
		return;
	}
	// No graphics: a pen inside the rectangle
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.SetPen(wxPen(colour, thick));
	dc.DrawRoundedRectangle(rect, radius);
}

void DrawCheck(wxDC &dc, wxWindow const *window, wxRect const& check, int state, Palette const& p) {
	int thick = std::max(1, window->FromDIP(1));
	int cy = check.y + check.height / 2;
	if (state == 0) {
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(p.field));
		dc.DrawRoundedRectangle(check, window->FromDIP(3));
		DrawOutline(dc, check, window->FromDIP(3), thick, theme::Mix(p.surface, p.text, 0.45));
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

namespace {
/// The thin line under a section's title, in the line colour
class Rule final : public wxWindow {
	agi::signal::Connection theme_changed;
public:
	explicit Rule(wxWindow *parent) : wxWindow(parent, -1) {
		SetMinSize(wxSize(-1, std::max(1, FromDIP(1))));
		SetBackgroundColour(Colours(this).line);
		theme_changed = theme::AddChangeListener([this] { SetBackgroundColour(Colours(this).line); Refresh(); });
	}
	bool AcceptsFocus() const override { return false; }
};
}

namespace {
class DimText final : public wxStaticText {
	agi::signal::Connection theme_changed;
public:
	DimText(wxWindow *parent, wxString const& text) : wxStaticText(parent, -1, text) {
		SetForegroundColour(Colours(parent).dim);
		theme_changed = theme::AddChangeListener([this] { SetForegroundColour(Colours(GetParent()).dim); Refresh(); });
	}
};
}

wxStaticText *DimLabel(wxWindow *parent, wxString const& text) {
	return new DimText(parent, text);
}

wxString WithoutColon(wxString label) {
	label.Trim();
	if (label.EndsWith(":") || label.EndsWith(L"\uFF1A"))
		label.RemoveLast().Trim();
	return label;
}

wxString OneLine(wxString text) {
	text.Replace("\r", "");
	text.Trim().Trim(false);
	while (text.Replace("\n\n", "\n")) { }
	text.Replace("\n", " ");
	return text;
}

wxSizer *SectionTitle(wxWindow *parent, wxString const& title) {
	auto sizer = new wxBoxSizer(wxVERTICAL);
	auto text = new wxStaticText(parent, -1, title);
	text->SetFont(text->GetFont().Bold());
	sizer->Add(text, 0, wxBOTTOM, parent->FromDIP(3));
	sizer->Add(new Rule(parent), 0, wxEXPAND | wxBOTTOM, parent->FromDIP(6));
	return sizer;
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

bool GoHome(wxWindow *from) {
	if (!home || !home->IsShownOnScreen() || (from && wxGetTopLevelParent(home) != wxGetTopLevelParent(from))) return false;
	home->SetFocus();
	return true;
}

void LeaveField() {
	auto focus = wxWindow::FindFocus();
	if (!focus || !fields.count(focus)) return;
	// Home only from its own window: a field in a dialog stays in the dialog
	if (home && home->IsShownOnScreen() && wxGetTopLevelParent(home) == wxGetTopLevelParent(focus)) home->SetFocus();
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
	int icon = make_icon ? FromDIP(16) + FromDIP(6) : 0;
	if (label.empty() || !label_shown) return icon;
	return icon + GetTextExtent(label).x + FromDIP(6);
}

void FlatFrame::SetIcon(std::function<wxBitmapBundle()> make) {
	make_icon = std::move(make);
	Arrange();
	Refresh(false);
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
	int label_x = FromDIP(8);
	if (make_icon) {
		int size = FromDIP(16);
		wxBitmap icon = make_icon().GetBitmapFor(this);
		// Drawn at its size in DIP, whatever the scale of the bitmap
		wxImage image = icon.ConvertToImage();
		if (image.GetWidth() != size) image.Rescale(size, size, wxIMAGE_QUALITY_HIGH);
		dc.DrawBitmap(wxBitmap(image), label_x, (GetClientSize().y - size) / 2, true);
		label_x += size + FromDIP(6);
	}
	if (!label.empty() && label_shown) {
		dc.SetTextForeground(p.dim);
		dc.DrawText(label, label_x, (GetClientSize().y - dc.GetCharHeight()) / 2);
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

FlatChoice::FlatChoice(wxWindow *parent, wxString const& tip, bool editable)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetToolTip(tip);
	SetCursor(wxCursor(wxCURSOR_HAND));
	Bind(wxEVT_PAINT, &FlatChoice::OnPaint, this);
	Bind(wxEVT_KEY_DOWN, &FlatChoice::OnKey, this);
	Bind(wxEVT_CHAR, &FlatChoice::OnChar, this);
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
	theme_changed = theme::AddChangeListener([this] { Refresh(false); if (editor) ApplyEditorColours(true); });
	SetMinSize(wxSize(FromDIP(60), FromDIP(field_height)));
	flat::AddWheelTarget(this);
	if (!editable) return;

	editor = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	editor->SetToolTip(tip);
	editor->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { OnEdited(); });
	editor->Bind(wxEVT_KEY_DOWN, &FlatChoice::OnEditorKey, this);
	// Enter is done with it, as in the other fields; Ctrl+Enter goes on
	editor->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
		int key = e.GetKeyCode();
		if ((key == WXK_RETURN || key == WXK_NUMPAD_ENTER) && !e.HasAnyModifiers()) {
			SyncEditor();
			flat::LeaveField();
		}
		else if (key == WXK_ESCAPE && !e.HasAnyModifiers()) {
			SyncEditor();
			editor->SelectAll();
		}
		else if (key == WXK_TAB && !e.ControlDown() && !e.AltDown()) {
			SyncEditor();
			Navigate(e.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
		}
		else e.Skip();
	});
	editor->Bind(wxEVT_MOUSEWHEEL, &FlatChoice::OnWheel, this);
	editor->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) { Refresh(false); e.Skip(); });
	editor->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) {
		// What was typed and matched nothing is let go
		CallAfter([this] { if (!editor->HasFocus()) SyncEditor(); });
		Refresh(false);
		e.Skip();
	});
	// The keyboard goes to the text, all of it chosen so typing replaces it
	Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) {
		editor->SetFocus();
		editor->SelectAll();
		e.Skip();
	});
	Bind(wxEVT_SIZE, [this](wxSizeEvent &e) { ArrangeEditor(); e.Skip(); });
	flat::AddField(editor);
	ApplyEditorColours(true);
}

void FlatChoice::ArrangeEditor() {
	if (!editor) return;
	wxSize size = GetClientSize();
	int pad = FromDIP(8), right = size.x - pad - FromDIP(7) - FromDIP(6);
	int height = std::min(editor->GetBestSize().y, size.y - FromDIP(4));
	editor->SetSize(pad, (size.y - height) / 2, std::max(1, right - pad), std::max(1, height));
}

void FlatChoice::ApplyEditorColours(bool matched) {
	auto p = flat::Colours(this);
	editor->SetBackgroundColour(matched ? p.field : theme::Mix(p.field, theme::Colour("Colour/Subtitle/Syntax/Background/Error"), 0.6));
	editor->SetForegroundColour(p.text);
	editor->Refresh();
}

void FlatChoice::SyncEditor() {
	if (!editor || completing) return;
	if (editor->GetValue() != GetStringSelection()) editor->ChangeValue(GetStringSelection());
	if (editor->HasFocus()) editor->SelectAll();
	else editor->SetSelection(0, 0);
	ApplyEditorColours(true);
}

void FlatChoice::OnEdited() {
	wxString prefix = editor->GetValue().Lower();
	if (prefix.empty()) {
		ApplyEditorColours(true);
		return;
	}
	for (size_t i = 0; i < items.size(); ++i) {
		if (!items[i].Lower().StartsWith(prefix)) continue;
		completing = true;
		Choose(static_cast<int>(i));
		completing = false;
		// The rest of the name, chosen so the next letter types over it.
		// After the event: the field puts the caret after what was typed
		// once it has told of the change.
		long typed_length = static_cast<long>(prefix.size());
		wxString name = items[i];
		editor->ChangeValue(name);
		editor->SetSelection(typed_length, static_cast<long>(name.size()));
		CallAfter([this, name, typed_length] {
			if (editor->GetValue() == name)
				editor->SetSelection(typed_length, static_cast<long>(name.size()));
		});
		ApplyEditorColours(true);
		return;
	}
	ApplyEditorColours(false);
}

void FlatChoice::OnEditorKey(wxKeyEvent &event) {
	switch (event.GetKeyCode()) {
		case WXK_UP: case WXK_NUMPAD_UP: Step(-1); break;
		case WXK_DOWN: case WXK_NUMPAD_DOWN:
			if (event.AltDown()) ShowList(); else Step(1);
			break;
		case WXK_F4: ShowList(); break;
		case WXK_BACK: {
			// With the completed rest chosen, Backspace takes off a letter
			// typed rather than only the rest
			long from, to;
			editor->GetSelection(&from, &to);
			if (!event.HasAnyModifiers() && from > 0 && to > from && to == editor->GetLastPosition())
				editor->SetSelection(from - 1, to);
			event.Skip();
			break;
		}
		default: event.Skip();
	}
}

bool FlatChoice::Enable(bool enable) {
	bool changed = wxWindow::Enable(enable);
	if (changed) Refresh(false);
	if (editor) editor->Enable(enable);
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
	if (index != selection) {
		selection = index;
		Refresh(false);
	}
	SyncEditor();
}

wxString FlatChoice::GetStringSelection() const {
	return selection >= 0 && selection < static_cast<int>(items.size()) ? items[selection] : wxString();
}

void FlatChoice::Choose(int index) {
	if (index < 0 || index >= static_cast<int>(items.size()) || index == selection) return;
	selection = index;
	Refresh(false);
	SyncEditor();
	if (on_choose) on_choose(index);
}

void FlatChoice::Step(int delta) {
	if (items.empty()) return;
	int from = selection < 0 ? (delta > 0 ? -1 : static_cast<int>(items.size())) : selection;
	Choose(std::clamp(from + delta, 0, static_cast<int>(items.size()) - 1));
}

bool FlatChoice::Typing() const {
	return !typed.empty() && wxGetLocalTimeMillis() - typed_at < 1000;
}

void FlatChoice::TypeAhead(wxString const& text) {
	if (text.empty() || items.empty() || !IsEnabled()) return;
	if (!Typing()) typed.clear();
	typed += text.Lower();
	typed_at = wxGetLocalTimeMillis();
	int count = static_cast<int>(items.size());
	auto begins = [&](int i, wxString const& prefix) { return items[i].Lower().StartsWith(prefix); };
	for (int i = 0; i < count; ++i) {
		if (begins(i, typed)) {
			Choose(i);
			return;
		}
	}
	// The same letter again, with no name beginning so: the next item
	// beginning with the letter, round to the first
	if (typed.size() > 1 && typed.find_first_not_of(typed[0]) == wxString::npos) {
		wxString letter(typed[0]);
		for (int n = 1; n <= count; ++n) {
			int i = (std::max(selection, 0) + n) % count;
			if (begins(i, letter)) {
				Choose(i);
				return;
			}
		}
	}
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
	DrawField(dc, this, p, HasFocus() || (editor && editor->HasFocus()), hover);
	dc.SetFont(GetFont());
	wxSize size = GetClientSize();
	int pad = FromDIP(8), chevron = FromDIP(7);
	int right = size.x - pad - chevron - FromDIP(6);
	wxString text = editor ? wxString() : GetStringSelection();
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
		case WXK_SPACE:
			// A space in a name being typed is part of it
			if (Typing() && !event.HasAnyModifiers()) event.Skip(); else ShowList();
			break;
		case WXK_RETURN: case WXK_NUMPAD_ENTER: case WXK_F4: ShowList(); break;
		case WXK_TAB:
			Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
			break;
		default: event.Skip();
	}
}

void FlatChoice::OnChar(wxKeyEvent &event) {
	wxChar ch = event.GetUnicodeKey();
	if (ch == WXK_NONE || ch < 32 || ch == 127 || event.ControlDown() || event.AltDown() || event.MetaDown()) {
		event.Skip();
		return;
	}
	TypeAhead(wxString(ch));
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

// FlatSwitch

namespace {
int MnemonicOf(wxString const& label) {
	for (size_t i = 0; i + 1 < label.size(); ++i) {
		if (label[i] != '&') continue;
		if (label[i + 1] == '&') { ++i; continue; }
		return wxToupper(label[i + 1]);
	}
	return 0;
}
}

FlatSwitch::FlatSwitch(wxWindow *parent, std::vector<wxString> labels)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS)
, labels(std::move(labels))
, available(this->labels.size(), true)
, tips(this->labels.size())
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetCursor(wxCursor(wxCURSOR_HAND));
	Bind(wxEVT_PAINT, &FlatSwitch::OnPaint, this);
	// Not taking the keyboard from a field on a click, as FlatButton does
	Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) { Choose(HitSegment(event.GetPosition())); });
	Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent &event) { Choose(HitSegment(event.GetPosition())); });
	Bind(wxEVT_MOTION, [this](wxMouseEvent &event) {
		int index = HitSegment(event.GetPosition());
		if (index != hover) {
			hover = index;
			SetToolTip(index >= 0 ? tips[index] : wxString());
			Refresh(false);
		}
		event.Skip();
	});
	Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &event) { hover = -1; Refresh(false); event.Skip(); });
	Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &event) {
		switch (event.GetKeyCode()) {
			case WXK_LEFT: case WXK_NUMPAD_LEFT: case WXK_UP: case WXK_NUMPAD_UP: Step(-1); break;
			case WXK_RIGHT: case WXK_NUMPAD_RIGHT: case WXK_DOWN: case WXK_NUMPAD_DOWN: Step(1); break;
			case WXK_TAB:
				Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
				break;
			default: event.Skip();
		}
	});
	Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &event) { Refresh(false); event.Skip(); });
	Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &event) { Refresh(false); event.Skip(); });
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
}

wxRect FlatSwitch::Segment(int index) const {
	wxSize size = GetClientSize();
	int count = static_cast<int>(labels.size());
	int width = size.x / count;
	return wxRect(index * width, 0, index + 1 == count ? size.x - index * width : width, size.y);
}

int FlatSwitch::HitSegment(wxPoint pos) const {
	for (int i = 0; i < static_cast<int>(labels.size()); ++i)
		if (Segment(i).Contains(pos)) return i;
	return -1;
}

void FlatSwitch::Choose(int index) {
	if (index < 0 || index >= static_cast<int>(labels.size()) || !available[index] || !IsEnabled() || index == selection) return;
	selection = index;
	Refresh(false);
	if (on_choose) on_choose(index);
}

void FlatSwitch::SetSelection(int index) {
	if (index < 0 || index >= static_cast<int>(labels.size()) || index == selection) return;
	selection = index;
	Refresh(false);
}

void FlatSwitch::Step(int delta) {
	for (int i = selection + delta; i >= 0 && i < static_cast<int>(labels.size()); i += delta) {
		if (!available[i]) continue;
		Choose(i);
		return;
	}
}

void FlatSwitch::EnableItem(int index, bool enable) {
	if (index < 0 || index >= static_cast<int>(labels.size()) || available[index] == enable) return;
	available[index] = enable;
	Refresh(false);
}

void FlatSwitch::SetItemToolTip(int index, wxString const& tip) {
	if (index >= 0 && index < static_cast<int>(tips.size())) tips[index] = tip;
}

int FlatSwitch::MnemonicIndex(int key) const {
	for (int i = 0; i < static_cast<int>(labels.size()); ++i)
		if (MnemonicOf(labels[i]) == std::toupper(key)) return i;
	return -1;
}

bool FlatSwitch::Enable(bool enable) {
	bool changed = wxWindow::Enable(enable);
	if (changed) Refresh(false);
	return changed;
}

wxSize FlatSwitch::DoGetBestClientSize() const {
	int widest = 0;
	for (auto const& label : labels)
		widest = std::max(widest, GetTextExtent(wxStripMenuCodes(label, wxStrip_Menu)).x);
	return wxSize(static_cast<int>(labels.size()) * (widest + FromDIP(20)), FromDIP(field_height));
}

void FlatSwitch::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	auto p = flat::Colours(this);
	wxSize size = GetClientSize();
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(p.surface));
	dc.DrawRectangle(wxPoint(0, 0), size);
	int radius = FromDIP(5), inset = FromDIP(2);
	dc.SetBrush(wxBrush(p.field));
	dc.SetPen(wxPen(HasFocus() ? p.accent : p.field, std::max(1, FromDIP(1))));
	dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), radius);
	dc.SetFont(GetFont());
	for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
		wxRect segment = Segment(i).Deflate(inset);
		bool on = i == selection;
		bool usable = available[i] && IsEnabled();
		if (on) {
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(usable ? p.accent : theme::Mix(p.field, p.text, 0.15)));
			dc.DrawRoundedRectangle(segment, radius - 1);
		}
		else if (i == hover && usable) {
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(theme::Mix(p.field, p.text, 0.05)));
			dc.DrawRoundedRectangle(segment, radius - 1);
		}
		wxColour ink = on && usable ? *wxWHITE : usable ? p.text : theme::Mix(p.field, p.text, 0.35);
		dc.SetTextForeground(ink);
		wxString shown = wxStripMenuCodes(labels[i], wxStrip_Menu);
		wxSize extent = dc.GetTextExtent(shown);
		dc.DrawText(shown, segment.x + (segment.width - extent.x) / 2, segment.y + (segment.height - extent.y) / 2);
	}
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
	wxSize extent = GetTextExtent(wxStripMenuCodes(label, wxStrip_Menu));
	// Room at the left for the ring round the box when it has the keyboard
	return wxSize(FromDIP(3) + FromDIP(14) + FromDIP(6) + extent.x + FromDIP(2), std::max(FromDIP(20), extent.y + FromDIP(4)));
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
	wxRect check(FromDIP(3), (size.y - box) / 2, box, box);
	flat::DrawCheck(dc, this, check, value ? 1 : 0, p);
	if (HasFocus())
		flat::DrawOutline(dc, wxRect(check).Inflate(FromDIP(3)), FromDIP(5), std::max(1, FromDIP(1)), p.accent);

	dc.SetFont(GetFont());
	// A colour given to it (the preferences' search marking a match) wins
	dc.SetTextForeground(UseForegroundColour() && IsEnabled() ? GetForegroundColour() : p.text);
	wxString shown = wxStripMenuCodes(label, wxStrip_Menu);
	wxSize extent = dc.GetTextExtent(shown);
	dc.DrawText(shown, check.GetRight() + 1 + FromDIP(6), (size.y - extent.y) / 2);
}

// FlatButton

FlatButton::FlatButton(wxWindow *parent, wxString const& label, int id, bool accent)
: wxWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS)
, label(label.empty() && wxIsStockID(id) ? flat::StockLabel(id) : label)
, accent(accent)
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

void FlatButton::SetAccent(bool new_accent) {
	if (accent == new_accent) return;
	accent = new_accent;
	Refresh(false);
}

void FlatButton::SetLeastSize(wxSize size) {
	least = size;
	InvalidateBestSize();
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

void FlatButton::SetGlyph(FlatGlyph new_glyph) {
	if (glyph == new_glyph) return;
	glyph = new_glyph;
	InvalidateBestSize();
	Refresh(false);
}

namespace {
/// The icon's box and the gap after it, when there are words
int glyph_width(wxWindow const* w, FlatGlyph glyph, bool words) {
	if (glyph == FlatGlyph::None) return 0;
	return w->FromDIP(14) + (words ? w->FromDIP(6) : 0);
}

/// @p glyph in a square of @p size at @p origin, drawn on a 16-unit grid
void draw_glyph(wxGraphicsContext *gc, FlatGlyph glyph, wxPoint2DDouble origin, double size, wxColour const& ink) {
	double k = size / 16;
	bool mirror = glyph == FlatGlyph::Redo;
	auto at = [&](double x, double y) { return wxPoint2DDouble(origin.m_x + (mirror ? 16 - x : x) * k, origin.m_y + y * k); };
	wxGraphicsPath path = gc->CreatePath();
	auto polyline = [&](std::initializer_list<std::pair<double, double>> points) {
		bool first = true;
		for (auto [x, y] : points) {
			auto p = at(x, y);
			if (first) path.MoveToPoint(p); else path.AddLineToPoint(p);
			first = false;
		}
	};
	switch (glyph) {
		case FlatGlyph::Up: polyline({{4, 10}, {8, 6}, {12, 10}}); break;
		case FlatGlyph::Down: polyline({{4, 6}, {8, 10}, {12, 6}}); break;
		case FlatGlyph::Undo:
		case FlatGlyph::Redo: {
			polyline({{5.5, 3.5}, {2.5, 6.5}, {5.5, 9.5}});
			polyline({{2.8, 6.5}, {9.5, 6.5}});
			// a half turn back down to the baseline
			auto centre = at(9.5, 10);
			constexpr double pi = std::numbers::pi;
			path.AddArc(centre.m_x, centre.m_y, 3.5 * k, -pi / 2, pi / 2, !mirror);
			path.AddLineToPoint(at(7, 13.5));
			break;
		}
		case FlatGlyph::Swap:
			polyline({{3, 5.5}, {13, 5.5}});
			polyline({{10.5, 3}, {13, 5.5}, {10.5, 8}});
			polyline({{13, 10.5}, {3, 10.5}});
			polyline({{5.5, 8}, {3, 10.5}, {5.5, 13}});
			break;
		default: return;
	}
	gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(ink).Width(std::max(1.0, 1.6 * k)).Cap(wxCAP_ROUND).Join(wxJOIN_ROUND)));
	gc->SetBrush(*wxTRANSPARENT_BRUSH);
	gc->StrokePath(path);
}
}

wxSize FlatButton::DoGetBestClientSize() const {
	auto shown = wxStripMenuCodes(label, wxStrip_Menu);
	wxSize extent = shown.empty() ? wxSize(0, GetCharHeight()) : GetTextExtent(shown);
	int icon = glyph_width(this, glyph, !shown.empty());
	int pad = shown.empty() ? FromDIP(12) : FromDIP(20);
	return wxSize(std::max(least.x, extent.x + icon + pad), std::max({least.y, FromDIP(24), extent.y + FromDIP(8)}));
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
	wxColour ink = p.text;
	if (accent) {
		wxColour fill = IsEnabled() ? p.accent : theme::Mix(p.surface, p.text, 0.12);
		if (lit) fill = theme::Mix(fill, *wxWHITE, pressed ? 0.05 : 0.15);
		dc.SetBrush(wxBrush(fill));
		// Focus is a ring inside the fill
		dc.SetPen(HasFocus() ? wxPen(theme::Mix(fill, *wxWHITE, 0.6), std::max(1, FromDIP(1))) : wxPen(fill));
		ink = IsEnabled() ? *wxWHITE : theme::Mix(p.surface, p.text, 0.4);
	}
	else {
		dc.SetBrush(wxBrush(lit ? theme::Mix(p.field, p.text, pressed ? 0.1 : 0.05) : p.field));
		dc.SetPen(wxPen(HasFocus() ? p.accent : p.line, std::max(1, FromDIP(1))));
	}
	dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), FromDIP(5));

	dc.SetFont(GetFont());
	dc.SetTextForeground(ink);
	wxString shown = wxStripMenuCodes(label, wxStrip_Menu);
	wxSize extent = shown.empty() ? wxSize(0, 0) : dc.GetTextExtent(shown);
	int icon = glyph_width(this, glyph, !shown.empty());
	int x = (size.x - extent.x - icon) / 2;
	if (icon) {
		double s = FromDIP(14);
		draw_glyph(dc.GetGraphicsContext(), glyph, {double(x), (size.y - s) / 2}, s, ink);
	}
	if (!shown.empty()) dc.DrawText(shown, x + icon, (size.y - extent.y) / 2);
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

wxString flat::StockLabel(int id) {
	switch (id) {
		case wxID_OK: return wxGETTEXT_IN_CONTEXT("button", "&OK");
		case wxID_APPLY: return wxGETTEXT_IN_CONTEXT("button", "&Apply");
		case wxID_CANCEL: return wxGETTEXT_IN_CONTEXT("button", "&Cancel");
		case wxID_CLOSE: return wxGETTEXT_IN_CONTEXT("button", "&Close");
		case wxID_YES: return wxGETTEXT_IN_CONTEXT("button", "&Yes");
		case wxID_NO: return wxGETTEXT_IN_CONTEXT("button", "&No");
		case wxID_HELP: return wxGETTEXT_IN_CONTEXT("button", "&Help");
		case wxID_SAVE: return wxGETTEXT_IN_CONTEXT("button", "&Save");
		case wxID_OPEN: return wxGETTEXT_IN_CONTEXT("button", "&Open");
		case wxID_DELETE: return wxGETTEXT_IN_CONTEXT("button", "&Delete");
		case wxID_ADD: return wxGETTEXT_IN_CONTEXT("button", "&Add");
		case wxID_REMOVE: return wxGETTEXT_IN_CONTEXT("button", "&Remove");
		case wxID_EDIT: return wxGETTEXT_IN_CONTEXT("button", "&Edit");
		case wxID_NEW: return wxGETTEXT_IN_CONTEXT("button", "&New");
		case wxID_CLEAR: return wxGETTEXT_IN_CONTEXT("button", "&Clear");
		case wxID_RESET: return wxGETTEXT_IN_CONTEXT("button", "&Reset");
		case wxID_FIND: return wxGETTEXT_IN_CONTEXT("button", "&Find");
		case wxID_REPLACE: return wxGETTEXT_IN_CONTEXT("button", "Rep&lace");
		case wxID_COPY: return wxGETTEXT_IN_CONTEXT("button", "&Copy");
		case wxID_PASTE: return wxGETTEXT_IN_CONTEXT("button", "&Paste");
		case wxID_UNDO: return wxGETTEXT_IN_CONTEXT("button", "&Undo");
		case wxID_REDO: return wxGETTEXT_IN_CONTEXT("button", "&Redo");
		case wxID_STOP: return wxGETTEXT_IN_CONTEXT("button", "&Stop");
		case wxID_ABORT: return wxGETTEXT_IN_CONTEXT("button", "&Abort");
		case wxID_RETRY: return wxGETTEXT_IN_CONTEXT("button", "&Retry");
		case wxID_IGNORE: return wxGETTEXT_IN_CONTEXT("button", "&Ignore");
		case wxID_UP: return wxGETTEXT_IN_CONTEXT("button", "&Up");
		case wxID_DOWN: return wxGETTEXT_IN_CONTEXT("button", "&Down");
		case wxID_BACKWARD: return wxGETTEXT_IN_CONTEXT("button", "&Back");
		case wxID_FORWARD: return wxGETTEXT_IN_CONTEXT("button", "&Forward");
		case wxID_PREFERENCES: return wxGETTEXT_IN_CONTEXT("button", "&Preferences");
		case wxID_PROPERTIES: return wxGETTEXT_IN_CONTEXT("button", "&Properties");
		case wxID_SELECTALL: return wxGETTEXT_IN_CONTEXT("button", "Select &All");
		case wxID_REFRESH: return wxGETTEXT_IN_CONTEXT("button", "&Refresh");
	}
	return wxIsStockID(id) ? wxGetStockLabel(id, wxSTOCK_NOFLAGS) : wxString();
}

FlatFrame *flat::SearchField(wxWindow *parent, wxString const& hint, wxTextCtrl *&text, int width) {
	auto frame = new FlatFrame(parent, "");
	text = new wxTextCtrl(frame, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER | wxBORDER_NONE);
	text->SetHint(hint);
	frame->SetContent(text, width);
	frame->SetIcon([] { return GETBUNDLE(find_button, 16); });
	auto box = text;
	box->Bind(wxEVT_CHAR_HOOK, [box](wxKeyEvent &e) {
		if (e.GetKeyCode() == WXK_ESCAPE && !box->IsEmpty()) box->Clear();
		else e.Skip();
	});
	// The frame shows the box has the keyboard
	box->Bind(wxEVT_SET_FOCUS, [frame](wxFocusEvent &e) { frame->Refresh(false); e.Skip(); });
	box->Bind(wxEVT_KILL_FOCUS, [frame](wxFocusEvent &e) { frame->Refresh(false); e.Skip(); });
	return frame;
}

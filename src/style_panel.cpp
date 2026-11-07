// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "style_panel.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "compat.h"
#include "format.h"
#include "style_edit.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include "style_look.h"
#include "subtitle_project_access.h"
#include "text_selection_controller.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <tuple>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/fontenum.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/popupwin.h>
#include <wx/intl.h>
#include <wx/app.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

namespace {
struct Palette {
	wxColour surface, text, dim, field, line, accent;
};

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

wxString Number(double value) {
	double rounded = std::round(value * 100) / 100;
	if (rounded == 0) rounded = 0; // no "-0"
	return fmt_wx("%g", rounded);
}

/// Text cut to @p width with an ellipsis
wxString Ellipsize(wxDC &dc, wxString const& text, int width) {
	if (dc.GetTextExtent(text).x <= width) return text;
	wxString cut = text;
	while (!cut.empty() && dc.GetTextExtent(cut + wxS("…")).x > width)
		cut.RemoveLast();
	return cut + wxS("…");
}

void DrawChevron(wxDC &dc, wxColour const& colour, int x, int cy, int size, bool down, int thick) {
	dc.SetPen(wxPen(colour, thick));
	if (down) {
		dc.DrawLine(x, cy - size / 4, x + size / 2, cy + size / 4);
		dc.DrawLine(x + size / 2, cy + size / 4, x + size, cy - size / 4);
	}
	else {
		dc.DrawLine(x + size / 4, cy - size / 2, x + size * 3 / 4, cy);
		dc.DrawLine(x + size * 3 / 4, cy, x + size / 4, cy + size / 2);
	}
}

const wxString mixed = wxS("—");
}

// Every part of the panel is drawn: flat on the block colour, as the
// controls below the video are (docs/theme.md)
class StylePanel::Part : public wxWindow {
	agi::signal::Connection theme_changed;

protected:
	bool hover = false;

	virtual void Draw(wxDC &dc, Palette const& p) = 0;

	void Set(wxString &member, wxString const& value) {
		if (member == value) return;
		member = value;
		Refresh(false);
	}

public:
	Part(wxWindow *parent, wxSize min_size)
	: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(FromDIP(min_size));
		Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
			wxAutoBufferedPaintDC buffered(this);
			wxGCDC dc(buffered);
			Palette p = Colours(this);
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(p.surface));
			dc.DrawRectangle(wxPoint(0, 0), GetClientSize());
			dc.SetFont(GetFont());
			Draw(dc, p);
		});
		Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) { hover = true; Refresh(false); e.Skip(); });
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) { hover = false; Refresh(false); e.Skip(); });
		theme_changed = theme::AddChangeListener([this] { Refresh(false); });
	}

	/// What the part shows, for tests
	virtual wxString Shown() const = 0;

	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return false; }
};

/// A value in a rounded field: an optional dim label before it, a unit
/// after it and a chevron when it is chosen from a list. A number can be
/// dragged sideways, or clicked and typed in.
class StylePanel::Field final : public StylePanel::Part {
public:
	struct Range {
		double min, max;
		double step; ///< Change per two DIP dragged; a tenth with Shift
	};

private:
	wxString label, value, unit;
	bool chevron;
	std::function<void()> on_choose;
	std::optional<Range> range;
	std::function<void(double, bool)> on_number;
	double current = 0;
	bool known = false;
	wxTextCtrl *edit = nullptr;
	bool pressed = false, dragging = false;
	int press_x = 0;
	double press_value = 0;

	int TextLeft(wxDC &dc) const {
		int x = FromDIP(8);
		if (!label.empty()) x += dc.GetTextExtent(label).x + FromDIP(6);
		return x;
	}

	void Draw(wxDC &dc, Palette const& p) override {
		wxSize size = GetClientSize();
		int radius = FromDIP(5);
		bool active = (hover || pressed) && IsEnabled() && (on_choose || range);
		dc.SetBrush(wxBrush(active ? theme::Mix(p.field, p.text, 0.05) : p.field));
		dc.SetPen(wxPen(IsEditing() ? p.accent : p.line, std::max(1, FromDIP(1))));
		dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), radius);

		int pad = FromDIP(8);
		int right = size.x - pad;
		int chevron_size = FromDIP(7);
		if (chevron) {
			DrawChevron(dc, p.dim, right - chevron_size, size.y / 2 - FromDIP(1), chevron_size, true, std::max(1, FromDIP(3) / 2));
			right -= chevron_size + FromDIP(6);
		}
		int y = (size.y - dc.GetCharHeight()) / 2;
		if (!label.empty()) {
			dc.SetTextForeground(p.dim);
			dc.DrawText(label, pad, y);
		}
		if (IsEditing()) return;
		int x = TextLeft(dc);
		wxString shown_unit = value == mixed ? wxString() : unit;
		// A degree sign sits against its number
		int unit_gap = shown_unit == wxS("°") ? 0 : FromDIP(3);
		int unit_width = shown_unit.empty() ? 0 : dc.GetTextExtent(shown_unit).x + unit_gap;
		wxString text = Ellipsize(dc, value, std::max(0, right - x - unit_width));
		// Numbers that can be dragged are in the accent colour
		dc.SetTextForeground(range && IsEnabled() && value != mixed ? p.accent : p.text);
		dc.DrawText(text, x, y);
		if (!shown_unit.empty()) {
			dc.SetTextForeground(p.dim);
			dc.DrawText(shown_unit, x + dc.GetTextExtent(text).x + unit_gap, y);
		}
	}

	double Clamp(double v) const { return std::clamp(v, range->min, range->max); }

	void OnLeftDown(wxMouseEvent &event) {
		// Not skipped: the click must not move the focus (it would end typing)
		if (!IsEnabled() || IsEditing()) return;
		if (on_choose) {
			CallAfter([this] { if (on_choose) on_choose(); });
			return;
		}
		if (!range) return;
		pressed = true;
		dragging = false;
		press_x = event.GetX();
		press_value = current;
		CaptureMouse();
		Refresh(false);
	}

	void OnMotion(wxMouseEvent &event) {
		if (!pressed || !HasCapture()) return;
		int dx = event.GetX() - press_x;
		if (!dragging && std::abs(dx) < FromDIP(3)) return;
		dragging = true;
		double step = range->step * (event.ShiftDown() ? 0.1 : 1);
		double v = Clamp(press_value + std::round(dx / static_cast<double>(FromDIP(2))) * step);
		v = std::round(v / step) * step;
		if (v == current && known) return;
		SetNumber(v, true);
		if (on_number) on_number(v, false);
	}

	void OnLeftUp(wxMouseEvent &) {
		if (!pressed) return;
		pressed = false;
		if (HasCapture()) ReleaseMouse();
		Refresh(false);
		if (dragging) {
			dragging = false;
			if (on_number) on_number(current, true);
		}
		else
			CallAfter([this] { StartEditing(); });
	}

	void OnCaptureLost(wxMouseCaptureLostEvent &) {
		bool was_dragging = dragging;
		pressed = dragging = false;
		Refresh(false);
		if (was_dragging && on_number) on_number(current, true);
	}

public:
	Field(wxWindow *parent, wxString label, wxString unit, bool chevron, wxString const& tip)
	: Part(parent, wxSize(48, 28)), label(std::move(label)), unit(std::move(unit)), chevron(chevron)
	{
		SetToolTip(tip);
		Bind(wxEVT_LEFT_DOWN, &Field::OnLeftDown, this);
		Bind(wxEVT_LEFT_DCLICK, &Field::OnLeftDown, this);
		Bind(wxEVT_MOTION, &Field::OnMotion, this);
		Bind(wxEVT_LEFT_UP, &Field::OnLeftUp, this);
		Bind(wxEVT_MOUSE_CAPTURE_LOST, &Field::OnCaptureLost, this);
	}

	/// Clicking opens a list, from @p choose
	void SetChooser(std::function<void()> choose) {
		on_choose = std::move(choose);
		SetCursor(wxCursor(wxCURSOR_HAND));
	}

	/// A number in @p number_range: @p handler gets each value while it is
	/// dragged (false), and the last when the drag ends or a typed value
	/// is entered (true)
	void SetNumberHandler(Range number_range, std::function<void(double, bool)> handler) {
		range = number_range;
		on_number = std::move(handler);
		SetCursor(wxCursor(wxCURSOR_SIZEWE));
	}

	void SetValue(wxString const& new_value) { Set(value, new_value); }
	/// Show a number, or "—" when @p is_known is false
	void SetNumber(double number, bool is_known) {
		current = number;
		known = is_known;
		SetValue(is_known ? Number(number) : mixed);
	}
	double GetNumber() const { return current; }

	void StartEditing() {
		if (!range || IsEditing() || !IsEnabled()) return;
		if (!edit) {
			edit = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER | wxBORDER_NONE);
			edit->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { CommitEditing(); });
			edit->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) { e.Skip(); CallAfter([this] { if (IsEditing()) CommitEditing(); }); });
			edit->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
				if (e.GetKeyCode() == WXK_ESCAPE) CancelEditing();
				else e.Skip();
			});
		}
		wxClientDC dc(this);
		dc.SetFont(GetFont());
		int left = TextLeft(dc) - FromDIP(2);
		int height = std::min(GetClientSize().y - FromDIP(4), edit->GetBestSize().y);
		edit->SetSize(left, (GetClientSize().y - height) / 2, GetClientSize().x - left - FromDIP(6), height);
		edit->SetBackgroundColour(Colours(this).field);
		edit->SetForegroundColour(Colours(this).text);
		edit->ChangeValue(known ? Number(current) : wxString());
		edit->Show();
		edit->SetFocus();
		edit->SelectAll();
		Refresh(false);
	}
	bool IsEditing() const { return edit && edit->IsShown(); }
	wxTextCtrl *EditBox() const { return edit; }

	/// Enter the typed number; something else beeps and keeps the box open
	void CommitEditing() {
		if (!IsEditing()) return;
		double v;
		wxString text = edit->GetValue();
		text.Trim(true).Trim(false);
		text.Replace(",", ".");
		if (!text.ToCDouble(&v)) {
			if (edit->HasFocus()) { wxBell(); return; }
			CancelEditing();
			return;
		}
		v = Clamp(v);
		edit->Hide();
		SetNumber(v, true);
		Refresh(false);
		if (on_number) on_number(v, true);
	}

	void CancelEditing() {
		if (!IsEditing()) return;
		edit->Hide();
		Refresh(false);
	}

	/// What dragging or typing @p v does, for tests
	void Enter(double v, bool final) {
		SetNumber(Clamp(v), true);
		if (on_number) on_number(current, final);
	}

	wxString Shown() const override { return value; }
};

/// A square button that is on, off or on for only some of the lines
class StylePanel::Toggle final : public StylePanel::Part {
public:
	enum class Glyph { Bold, Italic, Underline, Strikeout, Left, Centre, Right, Top, Middle, Bottom };
	enum class State { Off, On, Mixed };

private:
	Glyph glyph;
	State state = State::Off;

	void DrawLetter(wxDC &dc, wxColour const& colour) {
		wxFont font = GetFont();
		font.SetPointSize(font.GetPointSize() + 1);
		wxString letter;
		switch (glyph) {
			case Glyph::Bold: font.MakeBold(); letter = "B"; break;
			case Glyph::Italic: font.MakeItalic(); letter = "I"; break;
			case Glyph::Underline: letter = "U"; break;
			default: letter = "S"; break;
		}
		dc.SetFont(font);
		dc.SetTextForeground(colour);
		wxSize size = GetClientSize(), extent = dc.GetTextExtent(letter);
		int x = (size.x - extent.x) / 2, y = (size.y - extent.y) / 2;
		dc.DrawText(letter, x, y);
		// Drawn lines: not every graphics context draws a font's decorations
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(colour));
		int thick = std::max(1, FromDIP(1));
		if (glyph == Glyph::Underline)
			dc.DrawRectangle(x - FromDIP(1), y + extent.y - thick, extent.x + FromDIP(2), thick);
		else if (glyph == Glyph::Strikeout)
			dc.DrawRectangle(x - FromDIP(2), y + extent.y / 2, extent.x + FromDIP(4), thick);
	}

	void DrawAlignment(wxDC &dc, Palette const& p, wxColour const& colour) {
		wxSize size = GetClientSize();
		int box = FromDIP(14), thick = std::max(1, FromDIP(3) / 2);
		int left = (size.x - box) / 2, top = (size.y - box) / 2;
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(colour));
		if (glyph == Glyph::Left || glyph == Glyph::Centre || glyph == Glyph::Right) {
			// Three lines of text set to the side
			int lengths[] = {box, box * 3 / 5, box * 4 / 5};
			for (int i = 0; i < 3; ++i) {
				int w = lengths[i];
				int x = glyph == Glyph::Left ? left : glyph == Glyph::Right ? left + box - w : left + (box - w) / 2;
				dc.DrawRectangle(x, top + FromDIP(2) + i * FromDIP(5), w, thick);
			}
		}
		else {
			// A frame with the text at its top, middle or bottom
			dc.SetBrush(wxBrush(theme::Mix(p.surface, colour, 0.45)));
			dc.DrawRectangle(left, top, box, thick);
			dc.DrawRectangle(left, top + box - thick, box, thick);
			int h = FromDIP(4);
			int y = glyph == Glyph::Top ? top + thick + FromDIP(1) : glyph == Glyph::Bottom ? top + box - thick - FromDIP(1) - h : top + (box - h) / 2;
			dc.SetBrush(wxBrush(colour));
			dc.DrawRectangle(left + FromDIP(3), y, box - FromDIP(6), h);
		}
	}

	void Draw(wxDC &dc, Palette const& p) override {
		wxSize size = GetClientSize();
		int radius = FromDIP(5);
		wxColour back = state == State::On ? theme::Mix(p.surface, p.accent, theme::IsDark() ? 0.4 : 0.25)
			: state == State::Mixed ? theme::Mix(p.surface, p.accent, 0.12)
			: hover ? p.field : p.surface;
		dc.SetBrush(wxBrush(back));
		dc.SetPen(state == State::Off && !hover ? *wxTRANSPARENT_PEN : wxPen(state == State::Off ? p.line : theme::Mix(back, p.accent, 0.5), std::max(1, FromDIP(1))));
		dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), radius);
		wxColour colour = state == State::Mixed ? p.dim : p.text;
		if (glyph <= Glyph::Strikeout)
			DrawLetter(dc, colour);
		else
			DrawAlignment(dc, p, colour);
	}

public:
	Toggle(wxWindow *parent, Glyph glyph, wxString const& tip)
	: Part(parent, wxSize(28, 28)), glyph(glyph)
	{
		SetToolTip(tip);
		SetCursor(wxCursor(wxCURSOR_HAND));
		auto click = [this](wxMouseEvent &) {
			// Not skipped: the editor keeps the focus and its selection
			if (IsEnabled() && on_click) CallAfter([this] { if (on_click) on_click(); });
		};
		Bind(wxEVT_LEFT_DOWN, click);
		Bind(wxEVT_LEFT_DCLICK, click);
	}

	std::function<void()> on_click;

	void SetState(State new_state) {
		if (state == new_state) return;
		state = new_state;
		Refresh(false);
	}
	State GetState() const { return state; }

	wxString Shown() const override {
		return state == State::On ? "on" : state == State::Mixed ? "mixed" : "off";
	}
};

/// A check box, a colour and a name, with a value at the right: fill,
/// outline, background and shadow
class StylePanel::ColourRow final : public StylePanel::Part {
	wxString label, value;
	Toggle::State checked = Toggle::State::Off;
	agi::Color colour;
	bool colour_known = false;

	void Draw(wxDC &dc, Palette const& p) override {
		wxSize size = GetClientSize();
		int thick = std::max(1, FromDIP(1));
		int cy = size.y / 2;

		// Check box
		int box = FromDIP(14);
		wxRect check(0, cy - box / 2, box, box);
		if (checked == Toggle::State::Off) {
			dc.SetBrush(wxBrush(p.field));
			dc.SetPen(wxPen(theme::Mix(p.surface, p.text, 0.45), thick));
			dc.DrawRoundedRectangle(check, FromDIP(3));
		}
		else {
			dc.SetBrush(wxBrush(p.accent));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawRoundedRectangle(check, FromDIP(3));
			wxColour mark = theme::IsDark() ? wxColour(20, 20, 20) : *wxWHITE;
			dc.SetPen(wxPen(mark, std::max(1, FromDIP(2))));
			if (checked == Toggle::State::Mixed)
				dc.DrawLine(check.x + FromDIP(4), cy, check.GetRight() - FromDIP(3), cy);
			else {
				dc.DrawLine(check.x + FromDIP(3), cy, check.x + FromDIP(6), cy + FromDIP(3));
				dc.DrawLine(check.x + FromDIP(6), cy + FromDIP(3), check.GetRight() - FromDIP(3), cy - FromDIP(3));
			}
		}

		// Colour: over a checkerboard, so that its transparency shows
		wxRect swatch(box + FromDIP(8), cy - FromDIP(9), FromDIP(24), FromDIP(18));
		dc.SetPen(*wxTRANSPARENT_PEN);
		if (colour_known) {
			int cell = FromDIP(6);
			for (int y = 0; y * cell < swatch.height; ++y)
				for (int x = 0; x * cell < swatch.width; ++x) {
					dc.SetBrush(wxBrush((x + y) % 2 ? wxColour(204, 204, 204) : *wxWHITE));
					dc.DrawRectangle(swatch.x + x * cell, swatch.y + y * cell,
						std::min(cell, swatch.width - x * cell), std::min(cell, swatch.height - y * cell));
				}
			dc.SetBrush(wxBrush(wxColour(colour.r, colour.g, colour.b, 255 - colour.a)));
			dc.DrawRectangle(swatch);
		}
		else {
			// The lines differ: a slash through an empty swatch
			dc.SetBrush(wxBrush(p.field));
			dc.DrawRectangle(swatch);
			dc.SetPen(wxPen(p.dim, thick));
			dc.DrawLine(swatch.x, swatch.GetBottom(), swatch.GetRight(), swatch.y);
		}
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.SetPen(wxPen(p.line, thick));
		dc.DrawRectangle(swatch);

		// Name, and the value at the right
		int y = cy - dc.GetCharHeight() / 2;
		int value_width = value.empty() ? 0 : dc.GetTextExtent(value).x;
		int x = swatch.GetRight() + FromDIP(10);
		dc.SetTextForeground(checked == Toggle::State::Off ? p.dim : p.text);
		dc.DrawText(Ellipsize(dc, label, std::max(0, size.x - x - value_width - FromDIP(8))), x, y);
		if (!value.empty()) {
			dc.SetTextForeground(checked == Toggle::State::Off ? p.dim : p.text);
			dc.DrawText(value, size.x - value_width - FromDIP(2), y);
		}
	}

public:
	ColourRow(wxWindow *parent, wxString label, wxString const& tip)
	: Part(parent, wxSize(120, 28)), label(std::move(label))
	{
		SetToolTip(tip);
	}

	void SetState(Toggle::State state, std::optional<agi::Color> new_colour, wxString const& new_value) {
		checked = state;
		colour_known = new_colour.has_value();
		if (new_colour) colour = *new_colour;
		value = new_value;
		Refresh(false);
	}

	wxString Shown() const override {
		wxString shown = checked == Toggle::State::On ? "on" : checked == Toggle::State::Mixed ? "mixed" : "off";
		shown += " ";
		shown += colour_known ? to_wx(colour.GetHexFormatted()) : mixed;
		if (!value.empty()) shown += " " + value;
		return shown;
	}
};

/// Dim text: what the panel describes
class StylePanel::Caption final : public StylePanel::Part {
	wxString text;

	void Draw(wxDC &dc, Palette const& p) override {
		dc.SetTextForeground(p.dim);
		dc.DrawText(Ellipsize(dc, text, GetClientSize().x), 0, (GetClientSize().y - dc.GetCharHeight()) / 2);
	}

public:
	explicit Caption(wxWindow *parent) : Part(parent, wxSize(40, 20)) { }
	void SetText(wxString const& new_text) { Set(text, new_text); }
	wxString Shown() const override { return text; }
};

/// A section's title; clicking it folds the section away
class StylePanel::Header final : public StylePanel::Part {
	wxString title;
	bool divider;
	std::function<void()> on_click;

	void Draw(wxDC &dc, Palette const& p) override {
		wxSize size = GetClientSize();
		if (divider) {
			dc.SetBrush(wxBrush(p.line));
			dc.DrawRectangle(0, 0, size.x, std::max(1, FromDIP(1)));
		}
		int chevron = FromDIP(8);
		int cy = size.y / 2 + (divider ? FromDIP(2) : 0);
		DrawChevron(dc, hover ? p.text : p.dim, 0, cy, chevron, expanded, std::max(1, FromDIP(3) / 2));
		wxFont font = GetFont();
		font.MakeBold();
		dc.SetFont(font);
		dc.SetTextForeground(p.text);
		dc.DrawText(title, chevron + FromDIP(8), cy - dc.GetCharHeight() / 2);
	}

public:
	bool expanded = true;

	Header(wxWindow *parent, wxString title, bool divider, std::function<void()> on_click)
	: Part(parent, wxSize(40, divider ? 36 : 28)), title(std::move(title)), divider(divider), on_click(std::move(on_click))
	{
		SetCursor(wxCursor(wxCURSOR_HAND));
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { if (this->on_click) this->on_click(); });
		Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent &) { if (this->on_click) this->on_click(); });
	}

	wxString Shown() const override { return expanded ? "expanded" : "collapsed"; }
};

namespace {
/// Installed fonts, without the vertical "@" variants of Windows
wxArrayString const& InstalledFonts() {
	static wxArrayString fonts;
	if (fonts.empty()) {
		for (auto const& face : wxFontEnumerator::GetFacenames())
			if (!face.StartsWith("@")) fonts.Add(face);
		fonts.Sort([](wxString const& a, wxString const& b) { return a.CmpNoCase(b); });
	}
	return fonts;
}

/// The font list below the font field: type to narrow it down, Enter or a
/// click to choose; Enter on a name that is not in the list uses it as typed
class FontPopup final : public wxPopupTransientWindow {
	wxTextCtrl *search;
	wxListBox *list;
	wxString current;
	std::function<void(wxString const&)> on_pick;

	void Filter() {
		wxString query = search->GetValue().Lower();
		query.Trim(true).Trim(false);
		wxArrayString shown;
		for (auto const& font : InstalledFonts())
			if (query.empty() || font.Lower().Contains(query)) shown.Add(font);
		list->Freeze();
		list->Set(shown);
		int selected = query.empty() ? list->FindString(current) : 0;
		if (selected == wxNOT_FOUND && !shown.empty()) selected = 0;
		if (selected != wxNOT_FOUND && !shown.empty()) {
			list->SetSelection(selected);
			list->EnsureVisible(selected);
		}
		list->Thaw();
	}

	void Pick(wxString font) {
		font.Trim(true).Trim(false);
		if (font.empty()) return;
		auto pick = on_pick;
		Dismiss();
		pick(font);
	}

	void PickSelected() {
		int typed_match = list->FindString(search->GetValue().Trim(true).Trim(false));
		if (list->GetSelection() != wxNOT_FOUND && (typed_match != wxNOT_FOUND || list->GetCount() > 0))
			Pick(typed_match != wxNOT_FOUND ? list->GetString(typed_match) : list->GetStringSelection());
		else
			Pick(search->GetValue());
	}

	void OnDismiss() override {
		wxTheApp->ScheduleForDestruction(this);
	}

public:
	FontPopup(wxWindow *parent, wxString current, std::function<void(wxString const&)> pick)
	: wxPopupTransientWindow(parent, wxBORDER_SIMPLE)
	, current(std::move(current))
	, on_pick(std::move(pick))
	{
		auto panel = new wxPanel(this);
		search = new wxTextCtrl(panel, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
		search->SetHint(_("Search fonts"));
		list = new wxListBox(panel, -1, wxDefaultPosition, wxSize(-1, FromDIP(280)));
		auto sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(search, 0, wxEXPAND | wxALL, FromDIP(4));
		sizer->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
		panel->SetSizer(sizer);
		Filter();

		search->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { Filter(); });
		search->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { PickSelected(); });
		search->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
			int n = list->GetSelection(), count = list->GetCount();
			switch (e.GetKeyCode()) {
				case WXK_DOWN: if (count) { n = std::min(count - 1, n + 1); list->SetSelection(n); list->EnsureVisible(n); } break;
				case WXK_UP: if (count) { n = std::max(0, n - 1); list->SetSelection(n); list->EnsureVisible(n); } break;
				case WXK_ESCAPE: Dismiss(); break;
				default: e.Skip();
			}
		});
		list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &e) { Pick(e.GetString()); });
	}

	void Open(wxWindow *field) {
		wxSize size(std::max(field->GetSize().x, FromDIP(220)), -1);
		GetChildren().front()->SetInitialSize(wxSize(size.x, -1));
		GetChildren().front()->Fit();
		SetClientSize(GetChildren().front()->GetSize());
		Position(field->ClientToScreen(wxPoint(0, 0)), wxSize(0, field->GetSize().y));
		Popup(search);
	}
};
}

template<class T, class... Args>
T *StylePanel::Add(std::string const& name, Args&&... args) {
	auto part = new T(this, std::forward<Args>(args)...);
	parts[name] = part;
	return part;
}

StylePanel::Section &StylePanel::AddSection(wxSizer *sizer, std::string const& name, wxString const& title) {
	int pad = FromDIP(12);
	bool first = sections.empty();
	auto header = new Header(this, title, !first, [this, name] { ShowSection(name, !IsSectionShown(name)); });
	sizer->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT | (first ? wxTOP : 0), first ? pad - FromDIP(4) : 0);
	auto body = new wxBoxSizer(wxVERTICAL);
	sizer->Add(body, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
	return sections[name] = Section{header, body};
}

StylePanel::StylePanel(wxWindow *parent, agi::Context *c)
: wxScrolledWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE)
, c(c)
{
	theme::UseSurface(this);
	SetScrollRate(0, FromDIP(12));
	const int gap = FromDIP(6);
	using Glyph = Toggle::Glyph;

	auto outer = new wxBoxSizer(wxVERTICAL);

	auto &style = AddSection(outer, "style", _("Style"));
	style.body->Add(Add<Field>("style", "", "", true, _("The line's style: what the text looks like without override tags")), 0, wxEXPAND);
	style.body->Add(Add<Caption>("scope"), 0, wxEXPAND | wxTOP, gap / 2);

	auto &text = AddSection(outer, "text", _("Text"));
	text.body->Add(Add<Field>("font", "", "", true, _("Font (\\fn)")), 0, wxEXPAND);
	auto row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(Add<Field>("weight", "", "", true, _("Weight and slant (\\b, \\i)")), 3, wxEXPAND);
	row->Add(Add<Field>("size", _("Size"), "", false, _("Font size (\\fs)")), 2, wxEXPAND | wxLEFT, gap);
	text.body->Add(row, 0, wxEXPAND | wxTOP, gap);

	row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(Add<Toggle>("align_left", Glyph::Left, _("Align left (\\an1, \\an4, \\an7)")));
	row->Add(Add<Toggle>("align_centre", Glyph::Centre, _("Centre horizontally (\\an2, \\an5, \\an8)")), 0, wxLEFT, gap / 3);
	row->Add(Add<Toggle>("align_right", Glyph::Right, _("Align right (\\an3, \\an6, \\an9)")), 0, wxLEFT, gap / 3);
	row->AddStretchSpacer();
	row->Add(Add<Toggle>("align_top", Glyph::Top, _("Align to the top (\\an7, \\an8, \\an9)")));
	row->Add(Add<Toggle>("align_middle", Glyph::Middle, _("Centre vertically (\\an4, \\an5, \\an6)")), 0, wxLEFT, gap / 3);
	row->Add(Add<Toggle>("align_bottom", Glyph::Bottom, _("Align to the bottom (\\an1, \\an2, \\an3)")), 0, wxLEFT, gap / 3);
	text.body->Add(row, 0, wxEXPAND | wxTOP, gap);

	row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(Add<Toggle>("bold", Glyph::Bold, _("Bold (\\b)")));
	row->Add(Add<Toggle>("italic", Glyph::Italic, _("Italic (\\i)")), 0, wxLEFT, gap / 3);
	row->Add(Add<Toggle>("underline", Glyph::Underline, _("Underline (\\u)")), 0, wxLEFT, gap / 3);
	row->Add(Add<Toggle>("strikeout", Glyph::Strikeout, _("Strikeout (\\s)")), 0, wxLEFT, gap / 3);
	row->Add(Add<Field>("spacing", _("Spacing"), "px", false, _("Extra space between letters, in pixels (\\fsp)")), 1, wxEXPAND | wxLEFT, gap);
	text.body->Add(row, 0, wxEXPAND | wxTOP, gap);

	auto &look = AddSection(outer, "appearance", _("Appearance"));
	look.body->Add(Add<ColourRow>("fill", _("Fill"), _("Colour and opacity of the letters (\\1c, \\1a)")), 0, wxEXPAND);
	look.body->Add(Add<ColourRow>("outline", _("Outline"), _("Colour and width of the outline (\\3c, \\3a, \\bord)")), 0, wxEXPAND | wxTOP, gap / 2);
	look.body->Add(Add<ColourRow>("background", _("Background"),
		_("A box behind the text in the outline colour. Only a style can have one: its border style is \"opaque box\"")), 0, wxEXPAND | wxTOP, gap / 2);
	look.body->Add(Add<ColourRow>("shadow", _("Shadow"), _("Colour and opacity of the shadow (\\4c, \\4a)")), 0, wxEXPAND | wxTOP, gap / 2);
	row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(Add<Field>("shadow_distance", _("Distance"), "", false, _("How far the shadow falls from the text (\\xshad, \\yshad)")), 1, wxEXPAND);
	row->Add(Add<Field>("shadow_angle", _("Angle"), wxS("°"), false, _("Which way the shadow falls: 90° is right, 180° down (\\xshad, \\yshad)")), 1, wxEXPAND | wxLEFT, gap);
	look.body->Add(row, 0, wxEXPAND | wxTOP, gap / 2);
	look.body->Add(Add<Field>("blur", _("Blur"), "", false, _("Softens the edges of the outline, or the letters without one (\\blur)")), 0, wxEXPAND | wxTOP, gap);

	SetSizer(outer);
	FitInside();
	BindEditing();
	SetMinSize(wxSize(FromDIP(min_width), -1));

	connections.push_back(c->subtitleAccess->Read().AddCommitListener("style_panel.AddCommitListener",
		[this](int, AssDialogue const*, SubtitleModelRebindTicket ticket) { ScheduleUpdate(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); },
		std::weak_ptr<void const>(model_observer_lifetime)));
	connections.push_back(c->selectionController->AddSelectionListener("style_panel.AddSelectionListener",
		[this](SubtitleModelRebindTicket ticket) { ScheduleUpdate(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); },
		std::weak_ptr<void const>(model_observer_lifetime)));
	connections.push_back(c->selectionController->AddActiveLineListener("style_panel.AddActiveLineListener",
		[this](SubtitleModelRebindTicket ticket) { ScheduleUpdate(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); },
		std::weak_ptr<void const>(model_observer_lifetime)));
	connections.push_back(c->textSelectionController->AddSelectionListener([this] { ScheduleUpdate(); }));
	theme_changed = theme::AddChangeListener([this] { Refresh(); });

	UpdateNow();
}

void StylePanel::BindEditing() {
	using style_edit::Property;
	using State = Toggle::State;
	auto toggle = [this](std::string const& name) { return static_cast<Toggle*>(parts[name]); };
	auto apply = [this](Property property, std::function<void(StyleLook&)> change) {
		style_edit::Apply(c, property, change);
	};

	toggle("bold")->on_click = [=, this] {
		bool on = toggle("bold")->GetState() == State::On;
		apply(Property::Weight, [on](StyleLook &l) { l.weight = on ? 400 : 700; });
	};
	auto flag = [=](std::string const& name, Property property, bool StyleLook::*member) {
		toggle(name)->on_click = [=] {
			bool on = toggle(name)->GetState() == State::On;
			apply(property, [=](StyleLook &l) { l.*member = !on; });
		};
	};
	flag("italic", Property::Italic, &StyleLook::italic);
	flag("underline", Property::Underline, &StyleLook::underline);
	flag("strikeout", Property::Strikeout, &StyleLook::strikeout);

	// Each line keeps the other direction of its alignment
	auto column = [=](std::string const& name, int col) {
		toggle(name)->on_click = [=] { apply(Property::Alignment, [col](StyleLook &l) { l.alignment = (l.alignment - 1) / 3 * 3 + col + 1; }); };
	};
	auto row = [=](std::string const& name, int r) {
		toggle(name)->on_click = [=] { apply(Property::Alignment, [r](StyleLook &l) { l.alignment = r * 3 + (l.alignment - 1) % 3 + 1; }); };
	};
	column("align_left", 0); column("align_centre", 1); column("align_right", 2);
	row("align_bottom", 0); row("align_middle", 1); row("align_top", 2);

	auto number = [this](std::string const& name, Property property, Field::Range range, std::function<void(StyleLook&, double)> set) {
		static_cast<Field*>(parts[name])->SetNumberHandler(range, [this, property, set](double v, bool final) {
			// One undo step for a whole drag
			drag_commit = style_edit::Apply(c, property, [&](StyleLook &l) { set(l, v); }, drag_commit);
			if (final) drag_commit = -1;
		});
	};
	number("size", Property::Size, {1, 1000, 1}, [](StyleLook &l, double v) { l.size = v; });
	number("spacing", Property::Spacing, {-100, 100, 0.1}, [](StyleLook &l, double v) { l.spacing = v; });

	static_cast<Field*>(parts["font"])->SetChooser([this] {
		auto field = parts["font"];
		auto popup = new FontPopup(this, field->Shown() == mixed ? wxString() : field->Shown(),
			[this](wxString const& font) { ChooseFont(font); });
		popup->Open(field);
	});
	static_cast<Field*>(parts["weight"])->SetChooser([this] { ShowWeightMenu(); });
}

void StylePanel::ShowWeightMenu() {
	struct Choice { int weight; bool italic; wxString name; };
	std::vector<Choice> choices = {
		{400, false, _("Regular")}, {400, true, _("Italic")}, {700, false, _("Bold")}, {700, true, _("Bold Italic")},
		{100, false, _("Thin (100)")}, {200, false, _("Extra Light (200)")}, {300, false, _("Light (300)")},
		{500, false, _("Medium (500)")}, {600, false, _("Semibold (600)")}, {800, false, _("Extra Bold (800)")},
		{900, false, _("Black (900)")},
	};
	wxMenu menu;
	wxString shown = parts["weight"]->Shown();
	for (size_t i = 0; i < choices.size(); ++i) {
		if (i == 4) menu.AppendSeparator();
		auto item = menu.AppendCheckItem(wxID_HIGHEST + 1 + i, choices[i].name);
		item->Check(i < 4 && choices[i].name == shown);
	}
	auto field = parts["weight"];
	int chosen = field->GetPopupMenuSelectionFromUser(menu, 0, field->GetSize().y);
	if (chosen <= wxID_HIGHEST) return;
	size_t index = chosen - wxID_HIGHEST - 1;
	if (index >= choices.size()) return;
	// The extra weights keep each line's slant
	if (index < 4) ChooseWeight(choices[index].weight, choices[index].italic);
	else style_edit::Apply(c, style_edit::Property::Weight, [&](StyleLook &l) { l.weight = choices[index].weight; });
}

void StylePanel::ChooseFont(wxString const& font) {
	style_edit::Apply(c, style_edit::Property::Font, [&](StyleLook &l) { l.font = from_wx(font); });
}

void StylePanel::ChooseWeight(int weight, bool italic) {
	// One undo step for both
	int id = style_edit::Apply(c, style_edit::Property::Weight, [&](StyleLook &l) { l.weight = weight; });
	style_edit::Apply(c, style_edit::Property::Italic, [&](StyleLook &l) { l.italic = italic; }, id);
}

void StylePanel::Press(std::string const& name) {
	if (auto toggle = dynamic_cast<Toggle*>(parts[name]); toggle && toggle->on_click)
		toggle->on_click();
}

void StylePanel::EnterNumber(std::string const& name, double value, bool final) {
	if (auto field = dynamic_cast<Field*>(parts[name]))
		field->Enter(value, final);
}

void StylePanel::StartEditing(std::string const& name) {
	if (auto field = dynamic_cast<Field*>(parts[name]))
		field->StartEditing();
}

wxTextCtrl *StylePanel::EditBox(std::string const& name) {
	auto field = dynamic_cast<Field*>(parts[name]);
	return field ? field->EditBox() : nullptr;
}

StylePanel::~StylePanel() = default;

void StylePanel::ShowSection(std::string const& name, bool show) {
	auto it = sections.find(name);
	if (it == sections.end() || it->second.header->expanded == show) return;
	it->second.header->expanded = show;
	it->second.header->Refresh(false);
	GetSizer()->Show(it->second.body, show, true);
	Layout();
	FitInside();
}

bool StylePanel::IsSectionShown(std::string const& name) const {
	auto it = sections.find(name);
	return it != sections.end() && it->second.header->expanded;
}

void StylePanel::ScheduleUpdate() {
	// Typing commits on every key: read the lines once when idle
	if (update_pending) return;
	update_pending = true;
	CallAfter([this] {
		update_pending = false;
		UpdateNow();
	});
}

void StylePanel::UpdateNow() {
	auto const& file = c->subtitleAccess->Read();
	StyleLookup find = [&](std::string const& name) { return file.GetStyle(name); };

	std::vector<StyleLook> looks;
	wxString scope;
	auto active = c->selectionController->ReadActiveLine();
	auto selected = c->selectionController->ReadSelectedSet();
	int start = c->textSelectionController->GetSelectionStart();
	int end = c->textSelectionController->GetSelectionEnd();
	std::string const* text = active ? &active->Text.get() : nullptr;
	int plain_start = text ? PlainPosition(*text, std::min(start, end)) : 0;
	if (text && selected.size() <= 1 && plain_start != PlainPosition(*text, std::max(start, end))) {
		// Text selected in the editor: how that text begins
		looks.push_back(LookAt(*active, plain_start, find));
		scope = _("The selected text");
	}
	else {
		for (auto line : selected)
			looks.push_back(LookAt(*line, 0, find));
		if (looks.empty() && active)
			looks.push_back(LookAt(*active, 0, find));
		if (looks.empty())
			scope = _("No line selected");
		else if (looks.size() == 1)
			scope = _("The current line");
		else
			scope = fmt_plural(looks.size(), "%d selected line", "%d selected lines", looks.size());
	}
	Display(looks, scope);
}

void StylePanel::Display(std::vector<StyleLook> const& looks, wxString const& scope) {
	using State = Toggle::State;
	// Whether every look agrees on @p get
	auto same = [&](auto get) {
		if (looks.empty()) return false;
		for (auto const& look : looks)
			if (!(get(look) == get(looks.front()))) return false;
		return true;
	};
	// On when every look has it, mixed when some do
	auto state = [&](auto has) {
		size_t count = std::count_if(looks.begin(), looks.end(), has);
		return count == 0 ? State::Off : count == looks.size() ? State::On : State::Mixed;
	};
	auto field = [&](std::string const& name, auto get, auto text) {
		static_cast<Field*>(parts[name])->SetValue(same(get) ? text(get(looks.front())) : mixed);
	};
	auto toggle = [&](std::string const& name, auto has) {
		static_cast<Toggle*>(parts[name])->SetState(state(has));
	};

	static_cast<Caption*>(parts["scope"])->SetText(scope);
	field("style", [](StyleLook const& l) { return l.style; }, [](std::string const& s) { return to_wx(s); });
	field("font", [](StyleLook const& l) { return l.font; }, [](std::string const& s) { return to_wx(s); });
	field("weight", [](StyleLook const& l) { return std::make_pair(l.weight, l.italic); }, [](std::pair<int, bool> w) -> wxString {
		if (w.first == 400) return w.second ? _("Italic") : _("Regular");
		if (w.first == 700) return w.second ? _("Bold Italic") : _("Bold");
		return w.second ? fmt_tl("Weight %d, italic", w.first) : fmt_tl("Weight %d", w.first);
	});
	// Numbers: from the first line even when they differ, to drag from
	auto number_field = [&](std::string const& name, auto get) {
		static_cast<Field*>(parts[name])->SetNumber(looks.empty() ? 0 : get(looks.front()), same(get));
	};
	number_field("size", [](StyleLook const& l) { return l.size; });
	number_field("spacing", [](StyleLook const& l) { return l.spacing; });

	auto column = [](StyleLook const& l) { return (l.alignment - 1) % 3; };
	auto row = [](StyleLook const& l) { return (l.alignment - 1) / 3; }; // 0 bottom, 2 top
	toggle("align_left", [&](StyleLook const& l) { return column(l) == 0; });
	toggle("align_centre", [&](StyleLook const& l) { return column(l) == 1; });
	toggle("align_right", [&](StyleLook const& l) { return column(l) == 2; });
	toggle("align_top", [&](StyleLook const& l) { return row(l) == 2; });
	toggle("align_middle", [&](StyleLook const& l) { return row(l) == 1; });
	toggle("align_bottom", [&](StyleLook const& l) { return row(l) == 0; });
	toggle("bold", [](StyleLook const& l) { return l.Bold(); });
	toggle("italic", [](StyleLook const& l) { return l.italic; });
	toggle("underline", [](StyleLook const& l) { return l.underline; });
	toggle("strikeout", [](StyleLook const& l) { return l.strikeout; });

	auto colour_row = [&](std::string const& name, auto has, auto colour, auto value) {
		auto rgb = [&](StyleLook const& l) { agi::Color c = colour(l); return std::make_tuple(c.r, c.g, c.b, c.a); };
		std::optional<agi::Color> shown;
		if (same(rgb)) shown = colour(looks.front());
		static_cast<ColourRow*>(parts[name])->SetState(state(has), shown, value);
	};
	auto opacity = [](agi::Color const& c) { return fmt_wx("%d%%", static_cast<int>(std::lround((255 - c.a) * 100 / 255.))); };
	auto box = [](StyleLook const& l) { return l.border_style == 3; };

	colour_row("fill", [](StyleLook const& l) { return l.primary.a != 255; },
		[](StyleLook const& l) { return l.primary; },
		same([](StyleLook const& l) { return l.primary.a; }) ? opacity(looks.front().primary) : looks.empty() ? wxString() : mixed);
	colour_row("outline", [&](StyleLook const& l) { return !box(l) && l.border > 0 && l.outline.a != 255; },
		[](StyleLook const& l) { return l.outline; },
		same([](StyleLook const& l) { return l.border; }) ? Number(looks.front().border) : looks.empty() ? wxString() : mixed);
	colour_row("background", box, [](StyleLook const& l) { return l.outline; }, wxString());
	colour_row("shadow", [](StyleLook const& l) { return (l.shadow_x != 0 || l.shadow_y != 0) && l.shadow.a != 255; },
		[](StyleLook const& l) { return l.shadow; },
		same([](StyleLook const& l) { return l.shadow.a; }) ? opacity(looks.front().shadow) : looks.empty() ? wxString() : mixed);

	// Distance and angle as in a design program: 0° is up, clockwise
	number_field("shadow_distance", [](StyleLook const& l) { return std::hypot(l.shadow_x, l.shadow_y); });
	number_field("shadow_angle", [](StyleLook const& l) {
		if (l.shadow_x == 0 && l.shadow_y == 0) return 0.;
		double degrees = std::atan2(l.shadow_x, -l.shadow_y) * 180 / 3.14159265358979323846;
		return std::round(degrees < 0 ? degrees + 360 : degrees);
	});
	number_field("blur", [](StyleLook const& l) { return l.blur; });
}

std::map<std::string, wxString> StylePanel::Shown() const {
	std::map<std::string, wxString> shown;
	for (auto const& [name, part] : parts)
		shown[name] = part->Shown();
	return shown;
}

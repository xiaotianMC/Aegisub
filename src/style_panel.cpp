// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "style_panel.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "compat.h"
#include "dialog_style_editor.h"
#include "dialogs.h"
#include "format.h"
#include "options.h"
#include "style_edit.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include "subs_controller.h"
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
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
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
constexpr double pi = 3.14159265358979323846;

/// Opacity in percent for an ASS alpha (0 opaque)
double Opacity(agi::Color const& colour) {
	return std::round((255 - colour.a) * 100 / 255.);
}
unsigned char AlphaFor(double opacity) {
	return static_cast<unsigned char>(std::lround((100 - std::clamp(opacity, 0., 100.)) * 255 / 100));
}

/// The way the shadow falls as in a design program: 0° is up, clockwise
double ShadowAngle(StyleLook const& look) {
	if (look.shadow_x == 0 && look.shadow_y == 0) return 0;
	double degrees = std::atan2(look.shadow_x, -look.shadow_y) * 180 / pi;
	return degrees < 0 ? degrees + 360 : degrees;
}
void SetShadow(StyleLook &look, double distance, double degrees) {
	double radians = degrees * pi / 180;
	look.shadow_x = std::round(distance * std::sin(radians) * 100) / 100;
	look.shadow_y = std::round(-distance * std::cos(radians) * 100) / 100;
}
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
		// Degree and percent signs sit against their number
		int unit_gap = shown_unit == wxS("°") || shown_unit == "%" ? 0 : FromDIP(3);
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
		edit->SetSize(left, (GetClientSize().y - height) / 2, std::max(FromDIP(24), GetClientSize().x - left - FromDIP(6)), std::max(1, height));
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
		wxRect swatch = SwatchRect();
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
		SetCursor(wxCursor(wxCURSOR_HAND));
		auto click = [this](wxMouseEvent &event) {
			// Not skipped: the editor keeps the focus and its selection.
			// The colour opens the colour picker; the rest is the check box.
			if (!IsEnabled()) return;
			bool colour = event.GetX() >= SwatchRect().x - FromDIP(2) && event.GetX() <= SwatchRect().GetRight() + FromDIP(2);
			auto handler = colour ? on_pick : on_check;
			if (handler) CallAfter([handler] { handler(); });
		};
		Bind(wxEVT_LEFT_DOWN, click);
		Bind(wxEVT_LEFT_DCLICK, click);
	}

	std::function<void()> on_check, on_pick;

	wxRect SwatchRect() const {
		int cy = GetClientSize().y / 2;
		return wxRect(FromDIP(14) + FromDIP(8), cy - FromDIP(9), FromDIP(24), FromDIP(18));
	}
	Toggle::State GetState() const { return checked; }
	std::optional<agi::Color> GetColour() const { return colour_known ? std::optional<agi::Color>(colour) : std::nullopt; }

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

/// A small button with a drawn symbol: the style row's actions
class StylePanel::IconButton final : public StylePanel::Part {
public:
	enum class Glyph { Up, Down, Plus, More };

private:
	Glyph glyph;
	bool pressed = false;

	void Draw(wxDC &dc, Palette const& p) override {
		wxSize size = GetClientSize();
		if (hover && IsEnabled()) {
			dc.SetBrush(wxBrush(pressed ? theme::Mix(p.field, p.text, 0.1) : p.field));
			dc.SetPen(wxPen(p.line, std::max(1, FromDIP(1))));
			dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), FromDIP(5));
		}
		wxPoint centre(size.x / 2, size.y / 2);
		int arm = FromDIP(5), thick = std::max(1, FromDIP(3) / 2);
		dc.SetPen(wxPen(p.text, thick));
		dc.SetBrush(wxBrush(p.text));
		switch (glyph) {
			case Glyph::Up:
			case Glyph::Down: {
				int dir = glyph == Glyph::Up ? -1 : 1;
				dc.DrawLine(centre.x, centre.y - dir * arm, centre.x, centre.y + dir * arm);
				dc.DrawLine(centre.x - arm * 3 / 4, centre.y + dir * arm / 4, centre.x, centre.y + dir * arm);
				dc.DrawLine(centre.x + arm * 3 / 4, centre.y + dir * arm / 4, centre.x, centre.y + dir * arm);
				break;
			}
			case Glyph::Plus:
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.DrawRectangle(centre.x - arm, centre.y - thick / 2, 2 * arm, thick);
				dc.DrawRectangle(centre.x - thick / 2, centre.y - arm, thick, 2 * arm);
				break;
			case Glyph::More: {
				dc.SetPen(*wxTRANSPARENT_PEN);
				int r = std::max(1, FromDIP(3) / 2);
				for (int i = -1; i <= 1; ++i)
					dc.DrawCircle(centre.x + i * FromDIP(5), centre.y, r);
				break;
			}
		}
	}

public:
	std::function<void()> on_click;

	IconButton(wxWindow *parent, Glyph glyph, wxString const& tip)
	: Part(parent, wxSize(28, 28)), glyph(glyph)
	{
		SetToolTip(tip);
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
			if (!IsEnabled()) return;
			pressed = true;
			Refresh(false);
			if (on_click) CallAfter([this] { pressed = false; Refresh(false); if (on_click) on_click(); });
		});
	}

	wxString Shown() const override { return IsEnabled() ? "enabled" : "disabled"; }
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
	auto style_row = new wxBoxSizer(wxHORIZONTAL);
	style_row->Add(Add<Field>("style", "", "", true, _("The line's style: what the text looks like without override tags")), 1, wxEXPAND);
	style_row->Add(Add<IconButton>("style_update", IconButton::Glyph::Up,
		_("Update the style from the current line: the style takes on how the line looks, and the line's tags it makes unneeded go")), 0, wxLEFT, gap / 3);
	style_row->Add(Add<IconButton>("style_reset", IconButton::Glyph::Down,
		_("Reset to the style: remove the tags the panel sets from the selected lines")), 0, wxLEFT, gap / 3);
	style_row->Add(Add<IconButton>("style_new", IconButton::Glyph::Plus,
		_("New style that looks as the current line does, for the selected lines")), 0, wxLEFT, gap / 3);
	style_row->Add(Add<IconButton>("style_edit", IconButton::Glyph::More,
		_("Edit the style in the style editor, with every setting")), 0, wxLEFT, gap / 3);
	style.body->Add(style_row, 0, wxEXPAND);
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
	// Each colour with its number at the right
	const int value_width = FromDIP(84);
	auto colour_row = [&](std::string const& name, wxString const& label, wxString const& tip, std::string const& value_name,
		wxString const& value_label, wxString const& unit, wxString const& value_tip, bool first = false)
	{
		auto line = new wxBoxSizer(wxHORIZONTAL);
		line->Add(Add<ColourRow>(name, label, tip), 1, wxEXPAND);
		if (!value_name.empty()) {
			auto value = Add<Field>(value_name, value_label, unit, false, value_tip);
			value->SetMinSize(wxSize(value_width, -1));
			line->Add(value, 0, wxEXPAND | wxLEFT, gap);
		}
		else
			line->AddSpacer(value_width + gap);
		look.body->Add(line, 0, wxEXPAND | (first ? 0 : wxTOP), gap / 2);
	};
	colour_row("fill", _("Fill"), _("Colour of the letters (\\1c): click the colour to change it, the box to hide the letters"),
		"fill_opacity", "", "%", _("Opacity of the letters (\\1a)"), true);
	colour_row("outline", _("Outline"), _("Colour of the outline (\\3c, \\3a): click the colour to change it, the box to remove the outline"),
		"outline_width", "", "", _("Width of the outline, in pixels (\\bord)"));
	colour_row("background", _("Background"),
		_("A box behind the text in the outline colour. Only a style can have one: the box turns it on or off for the styles of the selected lines"), "", "", "", "");
	colour_row("shadow", _("Shadow"), _("Colour of the shadow (\\4c): click the colour to change it, the box to remove the shadow"),
		"shadow_opacity", "", "%", _("Opacity of the shadow (\\4a)"));
	row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(Add<Field>("shadow_distance", _("Distance"), "", false, _("How far the shadow falls from the text (\\xshad, \\yshad)")), 1, wxEXPAND);
	row->Add(Add<Field>("shadow_angle", _("Angle"), wxS("°"), false, _("Which way the shadow falls: 90° is right, 180° down (\\xshad, \\yshad)")), 1, wxEXPAND | wxLEFT, gap);
	look.body->Add(row, 0, wxEXPAND | wxTOP, gap / 2);
	look.body->Add(Add<Field>("blur", _("Blur"), "", false, _("Softens the edges of the outline, or the letters without one (\\blur)")), 0, wxEXPAND | wxTOP, gap);

	auto &transform = AddSection(outer, "transform", _("Transform"));
	auto pair = [&](std::string const& a, wxString const& a_label, wxString const& a_unit, wxString const& a_tip,
		std::string const& b, wxString const& b_label, wxString const& b_unit, wxString const& b_tip, bool first = false)
	{
		auto line = new wxBoxSizer(wxHORIZONTAL);
		line->Add(Add<Field>(a, a_label, a_unit, false, a_tip), 1, wxEXPAND);
		line->Add(Add<Field>(b, b_label, b_unit, false, b_tip), 1, wxEXPAND | wxLEFT, gap);
		transform.body->Add(line, 0, wxEXPAND | (first ? 0 : wxTOP), gap);
	};
	pair("position_x", "X", "", _("Where the line is, across (\\pos). Lines that move (\\move) are left as they are"),
		"position_y", "Y", "", _("Where the line is, down (\\pos). Lines that move (\\move) are left as they are"), true);
	pair("scale_x", _("Scale X"), "%", _("Stretches the letters across (\\fscx)"),
		"scale_y", _("Scale Y"), "%", _("Stretches the letters up and down (\\fscy)"));
	pair("rotation", _("Rotation"), wxS("°"), _("Turns the line, anticlockwise (\\frz)"),
		"opacity", _("Opacity"), "%", _("Opacity of fill, outline and shadow together (\\alpha)"));

	SetSizer(outer);
	ShowSection("transform", false);
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

	number("fill_opacity", Property::FillAlpha, {0, 100, 1}, [](StyleLook &l, double v) { l.primary.a = AlphaFor(v); });
	number("shadow_opacity", Property::ShadowAlpha, {0, 100, 1}, [](StyleLook &l, double v) { l.shadow.a = AlphaFor(v); });
	number("outline_width", Property::Border, {0, 100, 0.1}, [](StyleLook &l, double v) { l.border = v; });
	number("blur", Property::Blur, {0, 100, 0.1}, [](StyleLook &l, double v) { l.blur = v; });
	// Each line keeps its angle, or its distance
	number("shadow_distance", Property::Shadow, {0, 100, 0.1}, [](StyleLook &l, double v) {
		SetShadow(l, v, l.shadow_x == 0 && l.shadow_y == 0 ? 135 : ShadowAngle(l));
	});
	number("shadow_angle", Property::Shadow, {0, 360, 1}, [](StyleLook &l, double v) {
		SetShadow(l, std::hypot(l.shadow_x, l.shadow_y), v);
	});

	number("position_x", Property::Position, {-10000, 10000, 1}, [](StyleLook &l, double v) { l.pos_x = v; });
	number("position_y", Property::Position, {-10000, 10000, 1}, [](StyleLook &l, double v) { l.pos_y = v; });
	number("scale_x", Property::ScaleX, {0, 1000, 1}, [](StyleLook &l, double v) { l.scale_x = v; });
	number("scale_y", Property::ScaleY, {0, 1000, 1}, [](StyleLook &l, double v) { l.scale_y = v; });
	number("rotation", Property::Rotation, {-360, 360, 1}, [](StyleLook &l, double v) { l.angle = v; });
	number("opacity", Property::Opacity, {0, 100, 1}, [](StyleLook &l, double v) {
		l.primary.a = l.outline.a = l.shadow.a = AlphaFor(v);
	});

	auto colour = [this](std::string const& name) { return static_cast<ColourRow*>(parts[name]); };
	for (auto name : {"fill", "outline", "background", "shadow"}) {
		colour(name)->on_check = [this, name, colour] { Check(name, colour(name)->GetState() != State::On); };
		colour(name)->on_pick = [this, name] { PickColour(name); };
	}

	auto button = [this](std::string const& name) { return static_cast<IconButton*>(parts[name]); };
	button("style_update")->on_click = [this] { style_edit::UpdateStyleFromLine(c); };
	button("style_reset")->on_click = [this] { style_edit::ResetToStyle(c); };
	button("style_new")->on_click = [this] { AskNewStyle(); };
	button("style_edit")->on_click = [this] { OpenStyleEditor(); };
	static_cast<Field*>(parts["style"])->SetChooser([this] { ShowStyleMenu(); });

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

namespace {
struct ColourProperties {
	style_edit::Property colour, alpha;
	agi::Color StyleLook::*member;
};
ColourProperties PropertiesOf(std::string const& row) {
	using style_edit::Property;
	if (row == "fill") return {Property::FillColour, Property::FillAlpha, &StyleLook::primary};
	if (row == "shadow") return {Property::ShadowColour, Property::ShadowAlpha, &StyleLook::shadow};
	// The background box is drawn in the outline colour
	return {Property::OutlineColour, Property::OutlineAlpha, &StyleLook::outline};
}
}

void StylePanel::Check(std::string const& row, bool on) {
	using style_edit::Property;
	if (row == "background") {
		style_edit::SetBorderStyle(c, on ? 3 : 1);
		return;
	}
	auto props = PropertiesOf(row);
	auto member = props.member;
	// Showing: the style's value when it has one, so nothing is lost
	auto visible = [member](StyleLook &l, StyleLook const& style) {
		if ((l.*member).a == 255) (l.*member).a = (style.*member).a != 255 ? (style.*member).a : 0;
	};
	if (row == "fill") {
		style_edit::ApplyWithStyle(c, Property::FillAlpha, [&](StyleLook &l, StyleLook const& style) {
			if (on) visible(l, style); else l.primary.a = 255;
		});
	}
	else if (row == "outline") {
		if (on && box_style) { wxBell(); return; } // a box has no outline
		int id = style_edit::ApplyWithStyle(c, Property::Border, [&](StyleLook &l, StyleLook const& style) {
			if (!on) l.border = 0;
			else if (l.border == 0) l.border = style.border > 0 ? style.border : 2;
		});
		if (on) style_edit::ApplyWithStyle(c, Property::OutlineAlpha, visible, id);
	}
	else if (row == "shadow") {
		int id = style_edit::ApplyWithStyle(c, Property::Shadow, [&](StyleLook &l, StyleLook const& style) {
			if (!on) l.shadow_x = l.shadow_y = 0;
			else if (l.shadow_x == 0 && l.shadow_y == 0) {
				bool styled = style.shadow_x != 0 || style.shadow_y != 0;
				l.shadow_x = styled ? style.shadow_x : 2;
				l.shadow_y = styled ? style.shadow_y : 2;
			}
		});
		if (on) style_edit::ApplyWithStyle(c, Property::ShadowAlpha, visible, id);
	}
}

void StylePanel::SetColour(std::string const& row, agi::Color colour, bool rgb, bool alpha, Writer const& write) {
	auto props = PropertiesOf(row);
	auto member = props.member;
	// Only what changed: lines that differ keep what was not touched
	if (rgb)
		write(props.colour, [&](StyleLook &l) {
			(l.*member).r = colour.r; (l.*member).g = colour.g; (l.*member).b = colour.b;
		});
	if (alpha)
		write(props.alpha, [&](StyleLook &l) { (l.*member).a = colour.a; });
}

void StylePanel::ChooseColour(std::string const& row, agi::Color colour) {
	auto before = static_cast<ColourRow*>(parts[row])->GetColour();
	// Lines in different colours all take the new one
	bool rgb = !before || colour.r != before->r || colour.g != before->g || colour.b != before->b;
	bool alpha = !before || colour.a != before->a;
	int commit = -1;
	SetColour(row, colour, rgb, alpha, [&](style_edit::Property property, std::function<void(StyleLook&)> change) {
		commit = style_edit::Apply(c, property, change, commit);
	});
}

void StylePanel::PickColour(std::string const& row) {
	auto before = static_cast<ColourRow*>(parts[row])->GetColour();
	agi::Color initial = before ? *before : agi::Color();
	// The preview runs inside the picker's modal loop: as with the colour
	// buttons of the editor, it is an interaction over the selected rows,
	// so nothing else changes them meanwhile and Cancel undoes only it
	std::vector<int> targets;
	for (auto line : c->selectionController->ReadSelectedSet()) targets.push_back(line->Id);
	if (targets.empty()) return;
	auto session = c->subtitleAccess->TryBeginInteraction(targets);
	if (!session) return;
	const int start = c->textSelectionController->GetSelectionStart();
	const int end = c->textSelectionController->GetSelectionEnd();
	int commit = -1;
	bool previewed = false, rgb = !before, alpha = !before;
	auto write = [&](style_edit::Property property, std::function<void(StyleLook&)> change) {
		auto staged = style_edit::Stage(c, property, [&](StyleLook &l, StyleLook const&) { change(l); });
		if (!staged.changed) return;
		session->UpdateRows(staged.rows);
		commit = session->Commit(staged.name, AssFile::COMMIT_DIAG_TEXT, commit, staged.single_line_id);
		style_edit::Select(c, staged);
		previewed = true;
	};
	// The picker's screen dropper also takes colours from the video
	bool ok = GetColorFromUser(c->parent, initial, true, [&](agi::Color colour) {
		if (!session->Current()) return;
		rgb |= colour.r != initial.r || colour.g != initial.g || colour.b != initial.b;
		alpha |= colour.a != initial.a;
		SetColour(row, colour, rgb, alpha, write);
	});
	// Current(): nothing else has changed the rows since the last preview
	const bool undo_preview = !ok && previewed && session->Current();
	session->End();
	if (undo_preview) {
		c->subsController->Undo();
		c->textSelectionController->SetSelection(start, end);
	}
}

void StylePanel::ShowStyleMenu() {
	wxMenu menu;
	std::vector<std::string> names;
	wxString shown = parts["style"]->Shown();
	for (auto const& style : c->subtitleAccess->Read().Styles) {
		auto item = menu.AppendCheckItem(wxID_HIGHEST + 1 + names.size(), to_wx(style.name));
		item->Check(to_wx(style.name) == shown);
		names.push_back(style.name);
	}
	if (names.empty()) return;
	auto field = parts["style"];
	int chosen = field->GetPopupMenuSelectionFromUser(menu, 0, field->GetSize().y);
	size_t index = chosen - wxID_HIGHEST - 1;
	if (chosen > wxID_HIGHEST && index < names.size())
		ChooseStyle(names[index]);
}

void StylePanel::ChooseStyle(std::string const& name) {
	style_edit::SetLineStyle(c, name);
}

void StylePanel::AskNewStyle() {
	auto active = c->selectionController->ReadActiveLine();
	if (!active) return;
	auto const& file = c->subtitleAccess->Read();
	// A free name after the line's style: "Default 2", "Default 3"…
	std::string base = active->Style.get();
	std::string suggestion;
	for (int n = 2; suggestion.empty() || file.GetStyle(suggestion); ++n)
		suggestion = base + " " + std::to_string(n);
	wxString name = to_wx(suggestion);
	while (true) {
		wxTextEntryDialog dialog(c->parent, _("Name of the new style:"), _("New Style"), name);
		if (dialog.ShowModal() != wxID_OK) return;
		name = dialog.GetValue().Trim(true).Trim(false);
		if (name.empty()) continue;
		if (!c->subtitleAccess->Read().GetStyle(from_wx(name))) break;
		wxMessageBox(_("There is already a style with this name. Please choose another name."), _("Style name conflict"), wxOK | wxICON_ERROR | wxCENTER, c->parent);
	}
	NewStyle(name);
}

bool StylePanel::NewStyle(wxString const& name) {
	return style_edit::NewStyleFromLine(c, from_wx(name)) != -1;
}

void StylePanel::OpenStyleEditor() {
	auto active = c->selectionController->ReadActiveLine();
	if (!active) return;
	auto style = c->subtitleAccess->Read().GetStyle(active->Style.get());
	if (!style) return;
	wxArrayString fonts = InstalledFonts();
	DialogStyleEditor(c->parent, style, c, nullptr, "", fonts).ShowModal();
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
	if (auto icon = dynamic_cast<IconButton*>(parts[name]); icon && icon->on_click && icon->IsEnabled())
		icon->on_click();
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
	int width = 0, height = 0;
	file.GetResolution(width, height);
	// With where lines without \pos are, for the position fields
	auto look_at = [&](AssDialogue const& line, int plain) {
		StyleLook look = LookAt(line, plain, find);
		DefaultPosition(look, line, find(line.Style.get()), width, height);
		return look;
	};

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
		looks.push_back(look_at(*active, plain_start));
		scope = _("The selected text");
	}
	else {
		for (auto line : selected)
			looks.push_back(look_at(*line, 0));
		if (looks.empty() && active)
			looks.push_back(look_at(*active, 0));
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
	bool line = c->selectionController->ReadActiveLine() != nullptr;
	for (auto name : {"style_update", "style_new", "style_edit"})
		parts[name]->Enable(line && looks.size() >= 1);
	parts["style_reset"]->Enable(!looks.empty());
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

	auto colour_row = [&](std::string const& name, auto has, auto colour) {
		auto rgb = [&](StyleLook const& l) { agi::Color c = colour(l); return std::make_tuple(c.r, c.g, c.b, c.a); };
		std::optional<agi::Color> shown;
		if (same(rgb)) shown = colour(looks.front());
		static_cast<ColourRow*>(parts[name])->SetState(state(has), shown, wxString());
	};
	auto box = [](StyleLook const& l) { return l.border_style == 3; };
	box_style = !looks.empty() && std::all_of(looks.begin(), looks.end(), box);
	colour_row("fill", [](StyleLook const& l) { return l.primary.a != 255; }, [](StyleLook const& l) { return l.primary; });
	colour_row("outline", [&](StyleLook const& l) { return !box(l) && l.border > 0 && l.outline.a != 255; }, [](StyleLook const& l) { return l.outline; });
	colour_row("background", box, [](StyleLook const& l) { return l.outline; });
	colour_row("shadow", [](StyleLook const& l) { return (l.shadow_x != 0 || l.shadow_y != 0) && l.shadow.a != 255; }, [](StyleLook const& l) { return l.shadow; });
	number_field("fill_opacity", [](StyleLook const& l) { return Opacity(l.primary); });
	number_field("outline_width", [](StyleLook const& l) { return l.border; });
	number_field("shadow_opacity", [](StyleLook const& l) { return Opacity(l.shadow); });

	// Distance and angle as in a design program: 0° is up, clockwise
	number_field("shadow_distance", [](StyleLook const& l) { return std::hypot(l.shadow_x, l.shadow_y); });
	number_field("shadow_angle", [](StyleLook const& l) {
		if (l.shadow_x == 0 && l.shadow_y == 0) return 0.;
		return std::round(ShadowAngle(l));
	});
	number_field("blur", [](StyleLook const& l) { return l.blur; });
	number_field("position_x", [](StyleLook const& l) { return l.pos_x; });
	number_field("position_y", [](StyleLook const& l) { return l.pos_y; });
	number_field("scale_x", [](StyleLook const& l) { return l.scale_x; });
	number_field("scale_y", [](StyleLook const& l) { return l.scale_y; });
	number_field("rotation", [](StyleLook const& l) { return l.angle; });
	// One opacity only when fill, outline and shadow agree
	auto opacity = [](StyleLook const& l) {
		return l.primary.a == l.outline.a && l.primary.a == l.shadow.a ? Opacity(l.primary) : -1.;
	};
	bool one_opacity = !looks.empty() && same(opacity) && opacity(looks.front()) >= 0;
	static_cast<Field*>(parts["opacity"])->SetNumber(one_opacity ? opacity(looks.front()) : 100, one_opacity);
}

std::map<std::string, wxString> StylePanel::Shown() const {
	std::map<std::string, wxString> shown;
	for (auto const& [name, part] : parts)
		shown[name] = part->Shown();
	return shown;
}

StylePanelEdge::StylePanelEdge(wxWindow *parent, StylePanel *panel)
: wxWindow(parent, -1, wxDefaultPosition, wxSize(theme::BlockGap(parent), -1), wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
, panel(panel)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetWidth(panel->FromDIP(OPT_GET("App/Style Panel/Width")->GetInt()));
	option_changed = OPT_SUB("App/Style Panel/Show", [this](agi::OptionValue const& opt) {
		// Asked for: open even in a narrow window
		opened = opt.GetBool();
		Update();
	});
	theme_changed = theme::AddChangeListener([this] { Refresh(); });
	parent->Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
		event.Skip();
		Update();
	});

	Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
		wxAutoBufferedPaintDC buffered(this);
		wxGCDC dc(buffered);
		wxSize size = GetClientSize();
		wxColour back = theme::Backdrop();
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(back));
		dc.DrawRectangle(wxPoint(0, 0), size);
		if (!Folded()) return;
		// Folded: an arrow pointing to where the panel opens
		wxColour text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
		if (hover) {
			dc.SetBrush(wxBrush(theme::Mix(back, text, 0.1)));
			dc.DrawRoundedRectangle(wxRect(FromDIP(1), size.y / 2 - FromDIP(20), size.x - FromDIP(2), FromDIP(40)), FromDIP(3));
		}
		int arm = FromDIP(4);
		dc.SetPen(wxPen(theme::Mix(back, text, hover ? 0.9 : 0.5), std::max(1, FromDIP(3) / 2)));
		int cx = size.x / 2 - arm / 2, cy = size.y / 2;
		dc.DrawLine(cx + arm, cy - arm, cx, cy);
		dc.DrawLine(cx, cy, cx + arm, cy + arm);
	});
	Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) { hover = true; Refresh(false); });
	Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { hover = false; Refresh(false); });
	Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
		if (Folded()) {
			CallAfter([this] { Open(); });
			return;
		}
		start_x = ClientToScreen(event.GetPosition()).x;
		start_width = this->panel->GetSize().x;
		CaptureMouse();
	});
	Bind(wxEVT_MOTION, [this](wxMouseEvent &event) {
		if (!HasCapture() || !event.LeftIsDown()) return;
		SetWidth(start_width - (ClientToScreen(event.GetPosition()).x - start_x));
		GetParent()->Layout();
	});
	Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
		if (!HasCapture()) return;
		ReleaseMouse();
		OPT_SET("App/Style Panel/Width")->SetInt(this->panel->ToDIP(this->panel->GetSize().x));
	});
	Bind(wxEVT_MOUSE_CAPTURE_LOST, [](wxMouseCaptureLostEvent &) { });
}

void StylePanelEdge::SetWidth(int width) {
	width = std::clamp(width, panel->FromDIP(StylePanel::min_width), panel->FromDIP(StylePanel::max_width));
	panel->SetMinSize(wxSize(width, -1));
	panel->SetInitialSize(wxSize(width, -1));
}

bool StylePanelEdge::Folded() const {
	return !panel->IsShown();
}

void StylePanelEdge::Update() {
	bool narrow = GetParent()->GetClientSize().x < FromDIP(narrow_width);
	if (!narrow) opened = false;
	bool show = OPT_GET("App/Style Panel/Show")->GetBool() && (!narrow || opened);
	// Folded, the strip is wide enough to aim at
	int strip = show ? theme::BlockGap(GetParent()) : FromDIP(14);
	if (show == panel->IsShown() && GetMinSize().x == strip) return;
	SetMinSize(wxSize(strip, -1));
	SetCursor(show ? wxCursor(wxCURSOR_SIZEWE) : wxCursor(wxCURSOR_HAND));
	SetToolTip(show ? _("Drag to change the width of the style panel") : _("Show the style panel"));
	if (auto sizer = GetContainingSizer()) sizer->Show(panel, show);
	else panel->Show(show);
	GetParent()->Layout();
	Refresh();
	if (show) panel->UpdateNow();
}

void StylePanelEdge::Open() {
	if (!OPT_GET("App/Style Panel/Show")->GetBool()) {
		OPT_SET("App/Style Panel/Show")->SetBool(true); // Update follows
		return;
	}
	opened = true;
	Update();
}

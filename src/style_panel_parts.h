// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The drawn parts the style panel is made of (docs/style-panel.md): flat
// on the block colour, as the controls below the video are. Shared by its
// pages: the style's sections and the animation page.
#pragma once

#include "compat.h"
#include "flat_controls.h"
#include "format.h"
#include "style_panel.h"
#include "theme.h"
#include "libresrc/libresrc.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

#include <wx/bmpbndl.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/settings.h>
#include <wx/textctrl.h>

namespace style_parts {
struct Palette {
	wxColour surface, text, dim, field, line, accent;
};

inline Palette Colours(wxWindow const *window) {
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

inline wxString Number(double value) {
	double rounded = std::round(value * 100) / 100;
	if (rounded == 0) rounded = 0; // no "-0"
	return fmt_wx("%g", rounded);
}

/// Text cut to @p width with an ellipsis
inline wxString Ellipsize(wxDC &dc, wxString const& text, int width) {
	if (dc.GetTextExtent(text).x <= width) return text;
	wxString cut = text;
	while (!cut.empty() && dc.GetTextExtent(cut + wxS("…")).x > width)
		cut.RemoveLast();
	return cut + wxS("…");
}

inline void DrawChevron(wxDC &dc, wxColour const& colour, int x, int cy, int size, bool down, int thick) {
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

inline const wxString mixed = wxS("—");
}

using style_parts::Palette;
using style_parts::Colours;
using style_parts::Ellipsize;
using style_parts::DrawChevron;
using style_parts::mixed;

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
		// The wheel scrolls the panel, unless the part takes it: a turn
		// that has only brought a list under the pointer goes on scrolling
		Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent &e) {
			if (!WantsWheel()) flat::NoteScroll();
			e.Skip();
		});
	}

	/// What the part shows, for tests
	virtual wxString Shown() const = 0;
	/// Whether the wheel changes the part rather than scrolling the panel
	virtual bool WantsWheel() const { return false; }

	/// Drawn again at once: a part left looking disabled reads as one
	bool Enable(bool enable = true) override {
		bool changed = wxWindow::Enable(enable);
		if (changed) Refresh(false);
		return changed;
	}

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
	std::function<void(int)> on_wheel;
	int wheel_remainder = 0;
	double current = 0;
	bool known = false;
	bool inherited = false; ///< The value is the style's, not the line's
	wxTextCtrl *edit = nullptr;
	bool pressed = false, dragging = false;
	int press_x = 0;
	double press_value = 0;
	/// How a number is shown and read back when typed; plain by default
	std::function<wxString(double)> format;
	std::function<bool(wxString const&, double&)> parse;

	wxString Format(double v) const { return format ? format(v) : style_parts::Number(v); }

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
		// Numbers that can be dragged are in the accent colour; a value
		// taken from the style lighter, still not the grey of a disabled one
		wxColour number = range && IsEnabled() && value != mixed ? p.accent : p.text;
		dc.SetTextForeground(inherited && IsEnabled() ? theme::Mix(p.field, p.accent, 0.55) : number);
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
		Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent &e) {
			if (!WantsWheel() || flat::Scrolling()) { e.Skip(); return; }
			if (int steps = flat::WheelSteps(e, wheel_remainder)) on_wheel(steps);
		});
	}

	/// Turning the wheel over the field goes through its list: @p step gets
	/// the notches, up positive
	void SetWheel(std::function<void(int)> step) {
		on_wheel = std::move(step);
		flat::AddWheelTarget(this);
	}
	bool WantsWheel() const override { return on_wheel && IsEnabled() && !IsEditing(); }
	/// What turning the wheel does, for tests
	void Wheel(int steps) { if (on_wheel) on_wheel(steps); }

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

	/// The numbers it takes, once it has a handler
	void SetRange(double min, double max) {
		if (range) { range->min = min; range->max = max; }
	}
	/// Show numbers as @p show makes them, and read typed ones with @p read
	void SetFormat(std::function<wxString(double)> show, std::function<bool(wxString const&, double&)> read) {
		format = std::move(show);
		parse = std::move(read);
	}

	void SetValue(wxString const& new_value) { Set(value, new_value); }
	/// Show a number, or "—" when @p is_known is false
	void SetNumber(double number, bool is_known) {
		current = number;
		known = is_known;
		SetValue(is_known ? Format(number) : mixed);
	}
	double GetNumber() const { return current; }
	/// Show the number dim: the line leaves it to its style
	void SetInherited(bool on) {
		if (on == inherited) return;
		inherited = on;
		Refresh(false);
	}

	void StartEditing() {
		if (!range || IsEditing() || !IsEnabled()) return;
		if (!edit) {
			edit = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER | wxBORDER_NONE);
			// A click anywhere else enters the number and leaves it
			flat::AddField(edit);
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
		edit->ChangeValue(known ? Format(current) : wxString());
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
		bool read;
		if (parse) read = parse(text, v);
		else {
			text.Replace(",", ".");
			read = text.ToCDouble(&v);
		}
		if (!read) {
			if (edit->HasFocus()) { wxBell(); return; }
			CancelEditing();
			return;
		}
		v = Clamp(v);
		// Back to the text, as Enter in the edit box's fields goes
		if (edit->HasFocus()) flat::LeaveField();
		edit->Hide();
		SetNumber(v, true);
		Refresh(false);
		if (on_number) on_number(v, true);
	}

	void CancelEditing() {
		if (!IsEditing()) return;
		if (edit->HasFocus()) flat::LeaveField();
		edit->Hide();
		Refresh(false);
	}

	/// What dragging or typing @p v does, for tests
	void Enter(double v, bool final) {
		SetNumber(Clamp(v), true);
		if (on_number) on_number(current, final);
	}

	/// The value, with " (style)" when it is the style's
	wxString Shown() const override { return inherited ? value + " (style)" : value; }
};

/// A square button that is on, off or on for only some of the lines
class StylePanel::Toggle final : public StylePanel::Part {
public:
	enum class State { Off, On, Mixed };
	/// The icon, made again when the appearance changes
	using IconMaker = std::function<wxBitmapBundle()>;

private:
	IconMaker make_icon;
	wxBitmapBundle icon;
	int icon_made_for = -1; ///< The appearance it was made for
	State state = State::Off;

	void DrawIcon(wxDC &dc) {
		int appearance = (theme::IsDark() ? 1 : 0) + (libresrc_classic() ? 2 : 0);
		if (!icon.IsOk() || icon_made_for != appearance) {
			icon = make_icon();
			icon_made_for = appearance;
		}
		wxBitmap bitmap = icon.GetBitmapFor(this);
		if (!bitmap.IsOk()) return;
		// Dimmed when only some lines have it
		if (state == State::Mixed) bitmap = bitmap.ConvertToDisabled(theme::IsDark() ? 0 : 255);
		wxSize size = GetClientSize(), bitmap_size = bitmap.GetLogicalSize();
		dc.DrawBitmap(bitmap, (size.x - bitmap_size.x) / 2, (size.y - bitmap_size.y) / 2, true);
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
		DrawIcon(dc);
	}

public:
	Toggle(wxWindow *parent, IconMaker make_icon, wxString const& tip)
	: Part(parent, wxSize(28, 28)), make_icon(std::move(make_icon))
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
		flat::DrawCheck(dc, this, wxRect(0, cy - box / 2, box, box),
			checked == Toggle::State::Off ? 0 : checked == Toggle::State::Mixed ? 2 : 1,
			flat::Palette{p.surface, p.text, p.dim, p.field, p.line, p.accent});

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
	enum class Glyph { Up, Down, Plus, More, Collapse, Close, Now, Pick };

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
			case Glyph::Collapse: {
				// » : the panel goes to the right
				int half = FromDIP(4);
				for (int x : {centre.x - FromDIP(4), centre.x + FromDIP(1)}) {
					dc.DrawLine(x, centre.y - half, x + half, centre.y);
					dc.DrawLine(x + half, centre.y, x, centre.y + half);
				}
				break;
			}
			case Glyph::Close: {
				// ✕ : take it away
				int half = FromDIP(4);
				dc.DrawLine(centre.x - half, centre.y - half, centre.x + half + 1, centre.y + half + 1);
				dc.DrawLine(centre.x - half, centre.y + half, centre.x + half + 1, centre.y - half - 1);
				break;
			}
			case Glyph::Now: {
				// A clock: the time it is now, in the video
				dc.SetBrush(*wxTRANSPARENT_BRUSH);
				int r = FromDIP(6);
				dc.DrawCircle(centre, r);
				dc.DrawLine(centre.x, centre.y, centre.x, centre.y - r + FromDIP(2));
				dc.DrawLine(centre.x, centre.y, centre.x + r - FromDIP(3), centre.y);
				break;
			}
			case Glyph::Pick: {
				// ⌖ : a point on the video
				dc.SetBrush(*wxTRANSPARENT_BRUSH);
				int r = FromDIP(4);
				dc.DrawCircle(centre, r);
				dc.DrawLine(centre.x, centre.y - arm - 2, centre.x, centre.y - r + 1);
				dc.DrawLine(centre.x, centre.y + r, centre.x, centre.y + arm + 3);
				dc.DrawLine(centre.x - arm - 2, centre.y, centre.x - r + 1, centre.y);
				dc.DrawLine(centre.x + r, centre.y, centre.x + arm + 3, centre.y);
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

/// The switch at the top of the panel between its pages: two halves of a
/// rounded field, the one shown filled with the accent colour
class StylePanel::PageSwitch final : public StylePanel::Part {
	std::vector<wxString> labels;
	int selected = 0;

	wxRect Half(int index) const {
		wxSize size = GetClientSize();
		int width = size.x / static_cast<int>(labels.size());
		return wxRect(index * width, 0, index + 1 == static_cast<int>(labels.size()) ? size.x - index * width : width, size.y);
	}

	void Draw(wxDC &dc, Palette const& p) override {
		wxSize size = GetClientSize();
		int radius = FromDIP(4), inset = FromDIP(2);
		dc.SetBrush(wxBrush(p.field));
		dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), radius);
		for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
			wxRect half = Half(i).Deflate(inset);
			bool on = i == selected;
			if (on) {
				dc.SetBrush(wxBrush(p.accent));
				dc.DrawRoundedRectangle(half, radius - 1);
			}
			wxColour mark = theme::IsDark() ? wxColour(20, 20, 20) : *wxWHITE;
			dc.SetTextForeground(on ? mark : hover ? p.text : p.dim);
			wxSize extent = dc.GetTextExtent(labels[i]);
			dc.DrawText(labels[i], half.x + (half.width - extent.x) / 2, half.y + (half.height - extent.y) / 2);
		}
	}

public:
	std::function<void(int)> on_choose;

	PageSwitch(wxWindow *parent, std::vector<wxString> labels)
	: Part(parent, wxSize(120, 26)), labels(std::move(labels))
	{
		SetCursor(wxCursor(wxCURSOR_HAND));
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
			for (int i = 0; i < static_cast<int>(this->labels.size()); ++i)
				if (Half(i).Contains(event.GetPosition()) && i != selected && on_choose) on_choose(i);
		});
	}

	void Select(int index) {
		if (index == selected) return;
		selected = index;
		Refresh(false);
	}
	wxString Shown() const override { return labels[selected]; }
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
		// Lined up with the fields below, the divider across the panel
		int left = FromDIP(12);
		int chevron = FromDIP(8);
		int cy = size.y / 2 + (divider ? FromDIP(2) : 0);
		wxFont font = GetFont();
		font.MakeBold();
		// The title's ink and the chevron both centred on the same line: a
		// glyph box leaves room below the letters, so it would sit high
		auto ink = theme::TextInk(font, title);
		dc.SetFont(font);
		dc.SetTextForeground(p.text);
		dc.DrawText(title, left + chevron + FromDIP(8), cy - (ink.first + ink.second) / 2);
		DrawChevron(dc, hover ? p.text : p.dim, left, cy, chevron, expanded, std::max(1, FromDIP(3) / 2));
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

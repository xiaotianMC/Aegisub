// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "style_panel.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "compat.h"
#include "format.h"
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
#include <wx/intl.h>
#include <wx/settings.h>
#include <wx/sizer.h>

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
/// after it and a chevron when it is chosen from a list
class StylePanel::Field final : public StylePanel::Part {
	wxString label, value, unit;
	bool chevron;

	void Draw(wxDC &dc, Palette const& p) override {
		wxSize size = GetClientSize();
		int radius = FromDIP(5);
		dc.SetBrush(wxBrush(hover ? theme::Mix(p.field, p.text, 0.05) : p.field));
		dc.SetPen(wxPen(p.line, std::max(1, FromDIP(1))));
		dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), radius);

		int pad = FromDIP(8);
		int right = size.x - pad;
		int chevron_size = FromDIP(7);
		if (chevron) {
			DrawChevron(dc, p.dim, right - chevron_size, size.y / 2 - FromDIP(1), chevron_size, true, std::max(1, FromDIP(3) / 2));
			right -= chevron_size + FromDIP(6);
		}
		int x = pad;
		int y = (size.y - dc.GetCharHeight()) / 2;
		if (!label.empty()) {
			dc.SetTextForeground(p.dim);
			dc.DrawText(label, x, y);
			x += dc.GetTextExtent(label).x + FromDIP(6);
		}
		wxString shown_unit = value == mixed ? wxString() : unit;
		// A degree sign sits against its number
		int unit_gap = shown_unit == wxS("°") ? 0 : FromDIP(3);
		int unit_width = shown_unit.empty() ? 0 : dc.GetTextExtent(shown_unit).x + unit_gap;
		wxString text = Ellipsize(dc, value, std::max(0, right - x - unit_width));
		dc.SetTextForeground(p.text);
		dc.DrawText(text, x, y);
		if (!shown_unit.empty()) {
			dc.SetTextForeground(p.dim);
			dc.DrawText(shown_unit, x + dc.GetTextExtent(text).x + unit_gap, y);
		}
	}

public:
	Field(wxWindow *parent, wxString label, wxString unit, bool chevron, wxString const& tip)
	: Part(parent, wxSize(48, 28)), label(std::move(label)), unit(std::move(unit)), chevron(chevron)
	{
		SetToolTip(tip);
	}

	void SetValue(wxString const& new_value) { Set(value, new_value); }
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
	}

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
	auto number = [](double value) { return Number(value); };

	static_cast<Caption*>(parts["scope"])->SetText(scope);
	field("style", [](StyleLook const& l) { return l.style; }, [](std::string const& s) { return to_wx(s); });
	field("font", [](StyleLook const& l) { return l.font; }, [](std::string const& s) { return to_wx(s); });
	field("weight", [](StyleLook const& l) { return std::make_pair(l.weight, l.italic); }, [](std::pair<int, bool> w) -> wxString {
		if (w.first == 400) return w.second ? _("Italic") : _("Regular");
		if (w.first == 700) return w.second ? _("Bold Italic") : _("Bold");
		return w.second ? fmt_tl("Weight %d, italic", w.first) : fmt_tl("Weight %d", w.first);
	});
	field("size", [](StyleLook const& l) { return l.size; }, number);
	field("spacing", [](StyleLook const& l) { return l.spacing; }, number);

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
	field("shadow_distance", [](StyleLook const& l) { return std::hypot(l.shadow_x, l.shadow_y); }, number);
	field("shadow_angle", [](StyleLook const& l) {
		if (l.shadow_x == 0 && l.shadow_y == 0) return 0.;
		double degrees = std::atan2(l.shadow_x, -l.shadow_y) * 180 / 3.14159265358979323846;
		return std::round(degrees < 0 ? degrees + 360 : degrees);
	}, number);
	field("blur", [](StyleLook const& l) { return l.blur; }, number);
}

std::map<std::string, wxString> StylePanel::Shown() const {
	std::map<std::string, wxString> shown;
	for (auto const& [name, part] : parts)
		shown[name] = part->Shown();
	return shown;
}

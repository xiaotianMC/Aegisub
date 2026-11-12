// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "style_panel.h"
#include "style_panel_parts.h"

#include "animation_page.h"
#include "script_panel.h"
#include "timing_page.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "compat.h"
#include "dialog_rectangle.h"
#include "dialog_style_editor.h"
#include "dialogs.h"
#include "flat_controls.h"
#include "font_popup.h"
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
#include "command/command.h"
#include "libresrc/libresrc.h"

#include <algorithm>
#include <boost/scope_exit.hpp>
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
#include <wx/time.h>
#include <wx/utils.h>

namespace {
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
	sizer->Add(header, 0, wxEXPAND);
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
	Bind(wxEVT_MOUSEWHEEL, [](wxMouseEvent &e) { flat::NoteScroll(); e.Skip(); });
	const int gap = FromDIP(6);
	// The edit box's icons, as its buttons show them
	auto command_icon = [](const char *name) -> Toggle::IconMaker {
		return [name] { return cmd::get(name)->Icon(); };
	};
	auto own_icon = [](const char *name) -> Toggle::IconMaker {
		return [name] { return libresrc_modern_bundle(libresrc_modern_icon(name), 16); };
	};

	auto outer = new wxBoxSizer(wxVERTICAL);

	// The pages, and the button folding the panel away, on the first line
	{
		const int pad = FromDIP(12);
		auto line = new wxBoxSizer(wxHORIZONTAL);
		auto pages = Add<PageSwitch>("page", std::vector<wxString>{_("Style"), _("Animation"), wxGETTEXT_IN_CONTEXT("page", "Timing")});
		pages->SetToolTip(_("Style: what the text looks like. Animation: make it move and change over time, as \\move does. Timing: when it shows and goes"));
		pages->on_choose = [this](int index) { ShowPage(static_cast<Page>(index)); };
		line->AddSpacer(pad);
		line->Add(pages, 1, wxALIGN_CENTER_VERTICAL);
		// The script panel, apart from the switch: it is the video's, not the line's
		auto script_button = Add<IconButton>("script", IconButton::Glyph::Script, _("Script: the video's whole script, typed or pasted in to check the subtitles against"));
		script_button->on_click = [this] { if (on_script) on_script(); };
		line->Add(script_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, pad - FromDIP(6));
		auto collapse = Add<IconButton>("collapse", IconButton::Glyph::Collapse, _("Hide the style panel"));
		collapse->on_click = [this] {
			// The keyboard on it would be left in a hidden window
			bool keyboard = HasKeyboard();
			if (on_collapse) on_collapse();
			if (keyboard) flat::GoHome(this);
		};
		line->Add(collapse, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, pad - FromDIP(4));
		outer->Add(line, 0, wxEXPAND | wxTOP | wxBOTTOM, pad - FromDIP(4));
	}

	auto &style = AddSection(outer, "style", _("Style"));
	// The name on a line of its own, what is described and the buttons below
	style.body->Add(Add<Field>("style", "", "", true, _("The line's style: what the text looks like without override tags")), 0, wxEXPAND);
	auto style_row = new wxBoxSizer(wxHORIZONTAL);
	style_row->Add(Add<Caption>("scope"), 1, wxALIGN_CENTER_VERTICAL);
	style_row->Add(Add<IconButton>("style_update", IconButton::Glyph::Up,
		_("Update the style from the current line: the style takes on how the line looks, and the line's tags it makes unneeded go")), 0, wxLEFT, gap);
	style_row->Add(Add<IconButton>("style_reset", IconButton::Glyph::Down,
		_("Reset to the style: remove the tags the panel sets from the selected lines")), 0, wxLEFT, gap / 3);
	style_row->Add(Add<IconButton>("style_new", IconButton::Glyph::Plus,
		_("New style that looks as the current line does, for the selected lines")), 0, wxLEFT, gap / 3);
	style_row->Add(Add<IconButton>("style_edit", IconButton::Glyph::More,
		_("Edit the style in the style editor, with every setting")), 0, wxLEFT, gap / 3);
	style.body->Add(style_row, 0, wxEXPAND | wxTOP, gap / 2);

	wxBoxSizer *row;
	// Only for a shape (\p) line: its size; where it is, its colour and
	// opacity are in Transform and Appearance as for text
	auto &shape = AddSection(outer, "shape", _("Shape"));
	row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(Add<Field>("shape_width", _("W"), "", false, _("Width of the shape: its points are scaled, so no \\fscx is needed")), 1, wxEXPAND);
	row->Add(Add<Toggle>("shape_lock", own_icon("toggle_audio_link"),
		_("Keep the shape's proportions: changing its width changes its height with it, and the other way round")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, gap / 3);
	row->Add(Add<Field>("shape_height", _("H"), "", false, _("Height of the shape: its points are scaled, so no \\fscy is needed")), 1, wxEXPAND | wxLEFT, gap / 3);
	shape.body->Add(row, 0, wxEXPAND);

	auto &text = AddSection(outer, "text", _("Text"));
	text.body->Add(Add<Field>("font", "", "", true, _("Font (\\fn)")), 0, wxEXPAND);
	row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(Add<Field>("weight", "", "", true, _("Weight and slant (\\b, \\i)")), 3, wxEXPAND);
	row->Add(Add<Field>("size", _("Size"), "", false, _("Font size (\\fs)")), 2, wxEXPAND | wxLEFT, gap);
	text.body->Add(row, 0, wxEXPAND | wxTOP, gap);

	row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(Add<Toggle>("align_left", own_icon("align_left"), _("Align left (\\an1, \\an4, \\an7)")));
	row->Add(Add<Toggle>("align_centre", own_icon("align_centre"), _("Centre horizontally (\\an2, \\an5, \\an8)")), 0, wxLEFT, gap / 3);
	row->Add(Add<Toggle>("align_right", own_icon("align_right"), _("Align right (\\an3, \\an6, \\an9)")), 0, wxLEFT, gap / 3);
	row->AddStretchSpacer();
	row->Add(Add<Toggle>("align_top", own_icon("align_top"), _("Align to the top (\\an7, \\an8, \\an9)")));
	row->Add(Add<Toggle>("align_middle", own_icon("align_middle"), _("Centre vertically (\\an4, \\an5, \\an6)")), 0, wxLEFT, gap / 3);
	row->Add(Add<Toggle>("align_bottom", own_icon("align_bottom"), _("Align to the bottom (\\an1, \\an2, \\an3)")), 0, wxLEFT, gap / 3);
	text.body->Add(row, 0, wxEXPAND | wxTOP, gap);

	row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(Add<Toggle>("bold", command_icon("edit/style/bold"), _("Bold (\\b)")));
	row->Add(Add<Toggle>("italic", command_icon("edit/style/italic"), _("Italic (\\i)")), 0, wxLEFT, gap / 3);
	row->Add(Add<Toggle>("underline", command_icon("edit/style/underline"), _("Underline (\\u)")), 0, wxLEFT, gap / 3);
	row->Add(Add<Toggle>("strikeout", command_icon("edit/style/strikeout"), _("Strikeout (\\s)")), 0, wxLEFT, gap / 3);
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
	colour_row("secondary", _("Karaoke"), _("Colour of karaoke text before it is sung (\\2c, \\2a): click the colour to change it, the box to hide that text"),
		"secondary_opacity", "", "%", _("Opacity of karaoke text before it is sung (\\2a)"));
	colour_row("outline", _("Outline"), _("Colour of the outline (\\3c, \\3a): click the colour to change it, the box to remove the outline"),
		"outline_width", "", "", _("Width of the outline, in pixels (\\bord)"));
	colour_row("background", _("Background"),
		_("A box behind the text in the outline colour. Only a style can have one, so ticking it gives the selected lines a copy of their style with the box (\"Default Box\"); the style itself and its other lines stay as they are"), "", "", "", "");
	colour_row("shadow", _("Shadow"), _("Colour of the shadow (\\4c): click the colour to change it, the box to remove the shadow"),
		"shadow_opacity", "", "%", _("Opacity of the shadow (\\4a)"));
	row = new wxBoxSizer(wxHORIZONTAL);
	row->Add(Add<Field>("shadow_distance", _("Distance"), "", false, _("How far the shadow falls from the text (\\xshad, \\yshad)")), 1, wxEXPAND);
	row->Add(Add<Field>("shadow_angle", _("Angle"), wxS("°"), false, wxString::Format(_("Which way the shadow falls: 90%s is right, 180%s down (\\xshad, \\yshad)"), wxS("°"), wxS("°"))), 1, wxEXPAND | wxLEFT, gap);
	// Under the shadow's colour, as its own settings: in as far as the
	// colour swatch, past the check box
	row->PrependSpacer(FromDIP(14) + FromDIP(8));
	look.body->Add(row, 0, wxEXPAND | wxTOP, gap / 2);
	row = new wxBoxSizer(wxHORIZONTAL);
	row->AddSpacer(FromDIP(14) + FromDIP(8));
	row->Add(Add<Field>("blur", _("Blur"), "", false, _("Softens the edges of the outline, or the letters without one (\\blur)")), 1, wxEXPAND);
	look.body->Add(row, 0, wxEXPAND | wxTOP, gap / 2);

	auto &transform = AddSection(outer, "transform", _("Transform"));
	// Each group under a small title of its own
	auto caption = [&](std::string const& name, wxString const& title) {
		auto part = Add<Caption>(name);
		part->SetText(title);
		return part;
	};
	auto group = [&](std::string const& name, wxString const& title, bool first) {
		transform.body->Add(caption(name, title), 0, wxEXPAND | (first ? 0 : wxTOP), first ? 0 : gap);
	};
	auto pair = [&](std::string const& a, wxString const& a_label, wxString const& a_unit, wxString const& a_tip,
		std::string const& b, wxString const& b_label, wxString const& b_unit, wxString const& b_tip)
	{
		auto line = new wxBoxSizer(wxHORIZONTAL);
		line->Add(Add<Field>(a, a_label, a_unit, false, a_tip), 1, wxEXPAND);
		line->Add(Add<Field>(b, b_label, b_unit, false, b_tip), 1, wxEXPAND | wxLEFT, gap);
		transform.body->Add(line, 0, wxEXPAND);
	};
	group("title_position", _("Position"), true);
	pair("position_x", "X", "", _("Where the line is, across (\\pos). Lines that move (\\move) are left as they are"),
		"position_y", "Y", "", _("Where the line is, down (\\pos). Lines that move (\\move) are left as they are"));
	{
		// The line's margins: 0 leaves it to the style, whose margin shows dim
		group("title_margins", _("Margins"), false);
		auto line = new wxBoxSizer(wxHORIZONTAL);
		auto tip = _("%s of the line, in pixels. 0 uses the style's, shown dim. Lines placed by \\pos or \\move do not use their margins to be placed");
		line->Add(Add<Field>("margin_left", wxGETTEXT_IN_CONTEXT("margin", "L"), "", false, wxString::Format(tip, _("Left margin"))), 1, wxEXPAND);
		line->Add(Add<Field>("margin_right", wxGETTEXT_IN_CONTEXT("margin", "R"), "", false, wxString::Format(tip, _("Right margin"))), 1, wxEXPAND | wxLEFT, gap);
		line->Add(Add<Field>("margin_vertical", wxGETTEXT_IN_CONTEXT("margin", "V"), "", false, wxString::Format(tip, _("Vertical margin"))), 1, wxEXPAND | wxLEFT, gap);
		transform.body->Add(line, 0, wxEXPAND);
	}
	group("title_scale", _("Scale"), false);
	pair("scale_x", "X", "%", _("Stretches the letters across (\\fscx)"),
		"scale_y", "Y", "%", _("Stretches the letters up and down (\\fscy)"));
	{
		// Rotation and opacity side by side, each under its own title
		auto titles = new wxBoxSizer(wxHORIZONTAL);
		titles->Add(caption("title_rotation", _("Rotation")), 1, wxEXPAND);
		titles->Add(caption("title_opacity", _("Opacity")), 1, wxEXPAND | wxLEFT, gap);
		transform.body->Add(titles, 0, wxEXPAND | wxTOP, gap);
		pair("rotation", "", wxS("°"), _("Turns the line, anticlockwise (\\frz)"),
			"opacity", "", "%", _("Opacity of fill, outline and shadow together (\\alpha)"));
	}

	// The animation page: the current line's \move, \fad, \t and \org
	animation_body = new wxBoxSizer(wxVERTICAL);
	animation = new AnimationPage(this, c);
	animation->on_layout = [this] {
		if (page != Page::Animation) return;
		Layout();
		FitInside();
	};
	animation_body->Add(animation, 0, wxEXPAND);
	outer->Add(animation_body, 0, wxEXPAND);
	outer->Show(animation_body, false);

	// The timing page: the current line's times, scene changes and lead
	timing_body = new wxBoxSizer(wxVERTICAL);
	timing = new TimingPage(this, c);
	timing->on_layout = [this] {
		if (page != Page::Timing) return;
		Layout();
		FitInside();
	};
	timing_body->Add(timing, 0, wxEXPAND);
	outer->Add(timing_body, 0, wxEXPAND);
	outer->Show(timing_body, false);

	SetSizer(outer);
	SetSectionVisible("shape", false);
	FitInside();
	BindEditing();
	SetMinSize(wxSize(FromDIP(min_width), -1));
	// Back on the page last shown. 3 was the script, a page of this panel
	// once: now the script panel, shown in its place
	if (OPT_GET("App/Style Panel/Page")->GetInt() == 3) {
		OPT_SET("App/Script Panel/Show")->SetBool(true);
		OPT_SET("App/Style Panel/Page")->SetInt(0);
	}
	ShowPage(static_cast<Page>(std::clamp<int>(OPT_GET("App/Style Panel/Page")->GetInt(), 0, 2)));

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
	number("secondary_opacity", Property::SecondaryAlpha, {0, 100, 1}, [](StyleLook &l, double v) { l.secondary.a = AlphaFor(v); });
	for (int i = 0; i < 3; ++i) {
		static const char *names[] = {"margin_left", "margin_right", "margin_vertical"};
		static_cast<Field*>(parts[names[i]])->SetNumberHandler({0, 9999, 1}, [this, i](double v, bool final) {
			drag_commit = style_edit::SetMargin(c, i, static_cast<int>(std::lround(v)), drag_commit);
			if (final) drag_commit = -1;
		});
	}
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

	auto shape_number = [this](std::string const& name, bool width) {
		static_cast<Field*>(parts[name])->SetNumberHandler({0.1, 100000, 1}, [this, width](double v, bool final) {
			std::optional<double> w, h;
			(width ? w : h) = v;
			// Locked: the other side follows, at the proportions it had
			// before the change began
			if (OPT_GET("App/Style Panel/Lock Shape Proportions")->GetBool() && shape_aspect > 0) {
				if (width) h = v / shape_aspect;
				else w = v * shape_aspect;
			}
			resizing_shape = true;
			BOOST_SCOPE_EXIT_ALL(this) { resizing_shape = false; };
			drag_commit = ResizeShape(c, w, h, drag_commit);
			if (final) drag_commit = -1;
		});
	};
	toggle("shape_lock")->SetState(OPT_GET("App/Style Panel/Lock Shape Proportions")->GetBool() ? State::On : State::Off);
	toggle("shape_lock")->on_click = [this, toggle] {
		bool on = !OPT_GET("App/Style Panel/Lock Shape Proportions")->GetBool();
		OPT_SET("App/Style Panel/Lock Shape Proportions")->SetBool(on);
		toggle("shape_lock")->SetState(on ? State::On : State::Off);
	};
	shape_number("shape_width", true);
	shape_number("shape_height", false);

	auto colour = [this](std::string const& name) { return static_cast<ColourRow*>(parts[name]); };
	for (auto name : {"fill", "secondary", "outline", "background", "shadow"}) {
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
		auto popup = new FontPopup(this, InstalledFonts(), field->Shown() == mixed ? wxString() : field->Shown(),
			[this](wxString const& font) { ChooseFont(font); });
		popup->Open(field);
	});
	static_cast<Field*>(parts["weight"])->SetChooser([this] { ShowWeightMenu(); });

	// The wheel goes through the lists: up to the style or font above, up
	// to a heavier weight. Turns close together are one undo step.
	auto wheel_amend = [this] {
		bool close = wxGetLocalTimeMillis() - last_wheel < 1000;
		last_wheel = wxGetLocalTimeMillis();
		return close ? wheel_commit : -1;
	};
	auto step_in = [](std::vector<wxString> const& list, wxString const& current, int steps) -> std::optional<size_t> {
		if (list.empty()) return std::nullopt;
		auto it = std::find_if(list.begin(), list.end(), [&](wxString const& item) { return item.CmpNoCase(current) == 0; });
		long from;
		if (it != list.end()) from = it - list.begin();
		// Not in the list ("—", a font not installed): from where it would be
		else from = std::lower_bound(list.begin(), list.end(), current, [](wxString const& a, wxString const& b) { return a.CmpNoCase(b) < 0; }) - list.begin() - (steps > 0 ? 0 : 1);
		long to = std::clamp<long>(from - steps, 0, static_cast<long>(list.size()) - 1);
		if (it != list.end() && to == from) return std::nullopt;
		return static_cast<size_t>(to);
	};
	static_cast<Field*>(parts["style"])->SetWheel([=, this](int steps) {
		std::vector<wxString> names;
		for (auto const& style : c->subtitleAccess->Read().Styles) names.push_back(to_wx(style.name));
		// The file's order, as the list shows them
		wxString current = parts["style"]->Shown();
		auto it = std::find(names.begin(), names.end(), current);
		long from = it == names.end() ? (steps > 0 ? static_cast<long>(names.size()) : -1) : it - names.begin();
		long to = std::clamp<long>(from - steps, 0, static_cast<long>(names.size()) - 1);
		if (names.empty() || to == from) return;
		int amend = wheel_amend();
		wheel_commit = style_edit::SetLineStyle(c, from_wx(names[to]), amend);
		UpdateNow();
	});
	static_cast<Field*>(parts["font"])->SetWheel([=, this](int steps) {
		auto const& fonts = InstalledFonts();
		std::vector<wxString> list(fonts.begin(), fonts.end());
		auto to = step_in(list, parts["font"]->Shown(), steps);
		if (!to) return;
		int amend = wheel_amend();
		wheel_commit = style_edit::Apply(c, style_edit::Property::Font, [&](StyleLook &l) { l.font = from_wx(list[*to]); }, amend);
		UpdateNow();
	});
	static_cast<Field*>(parts["weight"])->SetWheel([=, this](int steps) {
		// Each line from its own weight, keeping its slant
		int amend = wheel_amend();
		wheel_commit = style_edit::Apply(c, style_edit::Property::Weight, [&](StyleLook &l) {
			l.weight = std::clamp((l.weight + 50) / 100 * 100 + steps * 100, 100, 900);
		}, amend);
		UpdateNow();
	});
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
	if (row == "secondary") return {Property::SecondaryColour, Property::SecondaryAlpha, &StyleLook::secondary};
	// The background box is drawn in the outline colour
	return {Property::OutlineColour, Property::OutlineAlpha, &StyleLook::outline};
}
}

void StylePanel::Check(std::string const& row, bool on) {
	using style_edit::Property;
	if (row == "background") {
		style_edit::SetBackground(c, on);
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
	else if (row == "secondary") {
		style_edit::ApplyWithStyle(c, Property::SecondaryAlpha, [&](StyleLook &l, StyleLook const& style) {
			if (on) visible(l, style); else l.secondary.a = 255;
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

void StylePanel::Wheel(std::string const& name, int steps) {
	if (auto field = dynamic_cast<Field*>(parts[name]))
		field->Wheel(steps);
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
	ApplySection(it->second);
	Layout();
	FitInside();
}

void StylePanel::ApplySection(Section const& section) {
	bool on_page = page == Page::Style;
	section.header->Show(on_page && section.visible);
	GetSizer()->Show(section.body, on_page && section.visible && section.header->expanded, true);
}

bool StylePanel::ShowScrollEffect() {
	if (!IsShownOnScreen()) return false;
	ShowPage(Page::Animation);
	animation->UpdateNow();
	if (animation->CanAdd(AnimationPage::Effect::Scroll)) {
		animation->AddEffect(AnimationPage::Effect::Scroll);
		animation->UpdateNow();
	}
	// Its card is the first
	Scroll(0, 0);
	return true;
}

void StylePanel::ShowPage(Page new_page) {
	if (page == new_page) return;
	page = new_page;
	static_cast<PageSwitch*>(parts["page"])->Select(static_cast<int>(page));
	OPT_SET("App/Style Panel/Page")->SetInt(static_cast<int>(page));
	Freeze();
	for (auto const& [name, section] : sections) ApplySection(section);
	GetSizer()->Show(animation_body, page == Page::Animation, true);
	GetSizer()->Show(timing_body, page == Page::Timing, true);
	if (page == Page::Timing) timing->UpdateNow();
	Layout();
	FitInside();
	Scroll(0, 0);
	Thaw();
}

std::vector<StylePanel::Part*> StylePanel::KeyboardParts() const {
	std::vector<Part*> found;
	std::function<void(wxWindow const*)> walk = [&](wxWindow const* window) {
		for (auto child : window->GetChildren()) {
			if (auto part = dynamic_cast<Part*>(child)) {
				if (part->Focusable() && part->IsEnabled() && part->IsShownOnScreen()) found.push_back(part);
			}
			else if (child->IsShown())
				walk(child);
		}
	};
	walk(this);
	return found;
}

void StylePanel::MoveFocus(wxWindow *from, bool forward) {
	std::vector<wxWindow*> list;
	for (auto part : KeyboardParts()) list.push_back(part);
	if (list.empty()) {
		flat::GoHome(this);
		return;
	}
	auto it = std::find(list.begin(), list.end(), from);
	size_t n = list.size(), i;
	if (it == list.end()) i = forward ? 0 : n - 1;
	else i = (static_cast<size_t>(it - list.begin()) + (forward ? 1 : n - 1)) % n;
	list[i]->SetFocus();
}

bool StylePanel::HasKeyboard() const {
	for (wxWindow *w = FindFocus(); w; w = w->GetParent())
		if (w == this) return true;
	return false;
}

bool StylePanel::TakeKeyboard() {
	if (!OPT_GET("App/Style Panel/Show")->GetBool()) OPT_SET("App/Style Panel/Show")->SetBool(true);
	if (request_open) request_open();
	if (!IsShownOnScreen()) return false;
	auto list = KeyboardParts();
	if (list.empty()) return false;
	list.front()->SetFocus();
	return true;
}

void StylePanel::SetSectionVisible(std::string const& name, bool visible) {
	auto it = sections.find(name);
	if (it == sections.end() || it->second.visible == visible) return;
	it->second.visible = visible;
	ApplySection(it->second);
	Layout();
	FitInside();
}

bool StylePanel::IsSectionVisible(std::string const& name) const {
	auto it = sections.find(name);
	return it != sections.end() && it->second.visible;
}

void StylePanel::EditShape() {
	if (request_open) request_open();
	UpdateNow();
	if (!IsSectionVisible("shape")) return;
	ShowSection("shape", true);
	// The section in sight
	int y = sections["shape"].header->GetPosition().y;
	int step = 1;
	GetScrollPixelsPerUnit(nullptr, &step);
	if (y < 0 || y > GetClientSize().y - FromDIP(80))
		Scroll(-1, GetViewStart().y + y / std::max(1, step));
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
	// Shown again, the timing page's check may be out of date
	if (page == Page::Timing && timing) timing->UpdateNow();
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

	// Margins: the lines', or their styles' where they leave it at 0
	std::vector<AssDialogue const*> lines(selected.begin(), selected.end());
	if (lines.empty() && active) lines.push_back(active);
	for (int i = 0; i < 3; ++i) {
		static const char *names[] = {"margin_left", "margin_right", "margin_vertical"};
		auto field = static_cast<Field*>(parts[names[i]]);
		std::optional<int> value;
		bool same = !lines.empty(), inherited = !lines.empty();
		for (auto line : lines) {
			int margin = line->Margin[i];
			if (margin == 0) {
				if (auto style = find(line->Style.get())) margin = style->Margin[i];
			}
			else
				inherited = false;
			if (value && *value != margin) same = false;
			value = margin;
		}
		field->SetNumber(value.value_or(0), same);
		field->SetInherited(same && inherited);
	}
	// Lines placed by \pos or \move do not use their margins, but they are
	// still theirs to change; the tooltip says so
	for (auto name : {"margin_left", "margin_right", "margin_vertical"})
		parts[name]->Enable(!lines.empty());

	// The shape's size, when the line is one and it alone is selected
	auto size = selected.size() <= 1 ? ShapeSize(c) : std::nullopt;
	SetSectionVisible("shape", size.has_value());
	static_cast<Field*>(parts["shape_width"])->SetNumber(size ? size->first : 0, size.has_value());
	static_cast<Field*>(parts["shape_height"])->SetNumber(size ? size->second : 0, size.has_value());
	// The proportions a locked resize keeps: the shape's own, not those its
	// rounded sizes come to while it is being resized
	if (!resizing_shape && drag_commit < 0)
		shape_aspect = size && size->first > 0 && size->second > 0 ? size->first / size->second : 0;
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
	colour_row("secondary", [](StyleLook const& l) { return l.secondary.a != 255; }, [](StyleLook const& l) { return l.secondary; });
	number_field("secondary_opacity", [](StyleLook const& l) { return Opacity(l.secondary); });
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
	for (auto const& [name, part] : parts) {
		shown[name] = part->Shown();
		if (!part->IsThisEnabled()) shown[name + " disabled"] = "yes";
	}
	return shown;
}

StylePanelEdge::StylePanelEdge(wxWindow *parent, StylePanel *panel, ScriptPanel *script)
: wxWindow(parent, -1, wxDefaultPosition, wxSize(theme::BlockGap(parent), -1), wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
, panel(panel)
, script(script)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetWidth(panel->FromDIP(OPT_GET("App/Style Panel/Width")->GetInt()));
	panel->on_collapse = [this] { Collapse(); };
	panel->request_open = [this] {
		ShowScript(false);
		if (Folded()) Open();
	};
	panel->on_script = [this] { OpenScript(); };
	if (script) {
		script->on_collapse = [this] { Collapse(); };
		script->on_style = [this] { ShowScript(false); };
	}
	option_changed = OPT_SUB("App/Style Panel/Show", [this](agi::OptionValue const& opt) {
		// Asked for: open even in a narrow window
		opened = opt.GetBool();
		Update();
	});
	script_changed = OPT_SUB("App/Script Panel/Show", [this](agi::OptionValue const&) { Update(); });
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
		// Folded: a narrow block of its own, as a side bar folded away is,
		// with a « pointing to where the panel opens and its name down it
		int gap = theme::BlockGap(GetParent());
		wxRect block(gap, 0, size.x - gap, size.y);
		wxColour surface = theme::Surface(), accent = theme::UiColours().accent;
		wxColour text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
		dc.SetBrush(wxBrush(hover ? theme::Mix(surface, accent, 0.12) : surface));
		dc.DrawRectangle(block);
		// A line in the accent colour along its inner edge: something is here
		dc.SetBrush(wxBrush(hover ? accent : theme::Mix(surface, accent, 0.6)));
		dc.DrawRectangle(block.x, 0, std::max(2, FromDIP(2)), size.y);

		wxColour ink = hover ? accent : theme::Mix(surface, text, 0.75);
		int cx = block.x + block.width / 2, top = FromDIP(18), arm = FromDIP(4);
		wxPen pen(ink, std::max(1, FromDIP(3) / 2));
		pen.SetCap(wxCAP_ROUND);
		dc.SetPen(pen);
		for (int x : {cx - FromDIP(3), cx + FromDIP(2)}) {
			dc.DrawLine(x + arm / 2, top - arm, x - arm / 2, top);
			dc.DrawLine(x - arm / 2, top, x + arm / 2, top + arm);
		}

		// The pages' names, read from the top down as a folded side bar's
		// name is: the one the panel shows in the accent colour on a pale
		// accent pill, the one under the pointer in the accent colour.
		// Not bold: Windows draws turned bold CJK text smeared.
		dc.SetFont(GetFont());
		wxColour plain = theme::Mix(surface, text, 0.75);
		int current = ScriptShown() ? script_name : static_cast<int>(this->panel->ShownPage());
		auto names = PageNames();
		for (int i = 0; i < static_cast<int>(names.size()); ++i) {
			auto const& [label, rect] = names[i];
			int height = dc.GetTextExtent(label).y;
			if (i == current) {
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(theme::Mix(surface, accent, 0.16)));
				int half = std::min(height / 2 + FromDIP(2), block.width / 2 - FromDIP(1));
				dc.DrawRoundedRectangle(cx - half, rect.y - FromDIP(5), 2 * half, rect.height + FromDIP(10), FromDIP(4));
			}
			dc.SetTextForeground(i == current || i == hover_page ? accent : plain);
			dc.DrawRotatedText(label, cx + height / 2, rect.y, 270);
		}
	});
	Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) { hover = true; Refresh(false); });
	Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { hover = false; hover_page = -1; Refresh(false); });
	Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
		if (Folded()) {
			int page = PageAt(event.GetPosition());
			CallAfter([this, page] {
				if (page < 0) Open();
				else if (page == script_name) OpenScript();
				else Open(static_cast<StylePanel::Page>(page));
			});
			return;
		}
		start_x = ClientToScreen(event.GetPosition()).x;
		start_width = Shown()->GetSize().x;
		CaptureMouse();
	});
	Bind(wxEVT_MOTION, [this](wxMouseEvent &event) {
		if (Folded()) {
			int page = PageAt(event.GetPosition());
			if (page != hover_page) {
				hover_page = page;
				Refresh(false);
			}
			return;
		}
		if (!HasCapture() || !event.LeftIsDown()) return;
		SetWidth(start_width - (ClientToScreen(event.GetPosition()).x - start_x));
		GetParent()->Layout();
	});
	Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
		if (!HasCapture()) return;
		ReleaseMouse();
		OPT_SET("App/Style Panel/Width")->SetInt(Shown()->ToDIP(Shown()->GetSize().x));
	});
	Bind(wxEVT_MOUSE_CAPTURE_LOST, [](wxMouseCaptureLostEvent &) { });
}

std::vector<std::pair<wxString, wxRect>> StylePanelEdge::PageNames() const {
	std::vector<std::pair<wxString, wxRect>> names;
	wxClientDC dc(const_cast<StylePanelEdge*>(this));
	dc.SetFont(GetFont());
	int gap = theme::BlockGap(GetParent());
	int y = FromDIP(40); // below the «
	std::vector<wxString> labels{_("Style"), _("Animation"), wxGETTEXT_IN_CONTEXT("page", "Timing")};
	if (script) labels.push_back(wxGETTEXT_IN_CONTEXT("page", "Script"));
	for (wxString const& label : labels) {
		int length = dc.GetTextExtent(label).x;
		// Taller than the text, for an easier aim
		names.emplace_back(label, wxRect(gap, y, std::max(0, GetClientSize().x - gap), length));
		y += length + FromDIP(16);
	}
	return names;
}

int StylePanelEdge::PageAt(wxPoint point) const {
	auto names = PageNames();
	for (int i = 0; i < static_cast<int>(names.size()); ++i)
		if (names[i].second.Inflate(0, FromDIP(7)).Contains(point)) return i;
	return -1;
}

void StylePanelEdge::Open(StylePanel::Page page) {
	ShowScript(false);
	panel->ShowPage(page);
	if (Folded()) Open();
}

void StylePanelEdge::SetWidth(int width) {
	// The two panels one width: one takes the other's place
	width = std::clamp(width, panel->FromDIP(StylePanel::min_width), panel->FromDIP(StylePanel::max_width));
	for (wxWindow *w : {static_cast<wxWindow*>(panel), static_cast<wxWindow*>(script)}) {
		if (!w) continue;
		w->SetMinSize(wxSize(width, -1));
		w->SetInitialSize(wxSize(width, -1));
	}
}

wxWindow *StylePanelEdge::Shown() const {
	if (ScriptShown()) return script;
	return panel;
}

bool StylePanelEdge::ScriptShown() const {
	return script && OPT_GET("App/Script Panel/Show")->GetBool();
}

bool StylePanelEdge::Folded() const {
	return !panel->IsShown() && !(script && script->IsShown());
}

void StylePanelEdge::Update() {
	bool narrow = GetParent()->GetClientSize().x < FromDIP(narrow_width);
	if (!narrow) opened = false;
	bool show = OPT_GET("App/Style Panel/Show")->GetBool() && (!narrow || opened);
	bool show_script = show && ScriptShown();
	bool show_style = show && !show_script;
	// Folded, the strip is wide enough to aim at
	int strip = show ? theme::BlockGap(GetParent()) : theme::BlockGap(GetParent()) + FromDIP(24);
	if (show_style == panel->IsShown() && (!script || show_script == script->IsShown()) && GetMinSize().x == strip) return;
	SetMinSize(wxSize(strip, -1));
	SetCursor(show ? wxCursor(wxCURSOR_SIZEWE) : wxCursor(wxCURSOR_HAND));
	SetToolTip(!show ? _("Show the style panel: click a page's name to open it on that page")
		: show_script ? _("Drag to change the width of the script panel")
		: _("Drag to change the width of the style panel"));
	auto sizer = GetContainingSizer();
	auto show_window = [&](wxWindow *w, bool on) {
		if (!sizer || !sizer->Show(w, on)) w->Show(on);
	};
	// Hidden first: the two never side by side, even for a moment
	if (script && !show_script) show_window(script, false);
	show_window(panel, show_style);
	if (script && show_script) show_window(script, true);
	GetParent()->Layout();
	Refresh();
	if (show_style) panel->UpdateNow();
	if (show_script) script->UpdateNow();
}

void StylePanelEdge::ShowScript(bool show) {
	if (show == ScriptShown() || (show && !script)) return;
	wxWindow *going = Shown();
	bool keyboard = false;
	for (wxWindow *w = FindFocus(); w && !keyboard; w = w->GetParent())
		keyboard = w == going;
	OPT_SET("App/Script Panel/Show")->SetBool(show); // Update follows
	if (!keyboard) return;
	// The button pressed went with the panel: the keyboard goes to the other
	bool taken = show ? script->TakeKeyboard() : panel->TakeKeyboard();
	if (!taken) flat::GoHome(GetParent());
}

void StylePanelEdge::OpenScript() {
	ShowScript(true);
	if (Folded()) Open();
}

void StylePanelEdge::ToggleScript() {
	if (!script) return;
	if (ScriptShown() && script->IsShownOnScreen()) {
		ShowScript(false);
		return;
	}
	OpenScript();
	script->TakeKeyboard();
}
void StylePanelEdge::Collapse() {
	if (opened) {
		// Opened by hand in a narrow window: back to folded, as it was
		opened = false;
		Update();
		return;
	}
	OPT_SET("App/Style Panel/Show")->SetBool(false); // Update follows
}

void StylePanelEdge::Open() {
	if (!OPT_GET("App/Style Panel/Show")->GetBool()) {
		OPT_SET("App/Style Panel/Show")->SetBool(true); // Update follows
		return;
	}
	opened = true;
	Update();
}

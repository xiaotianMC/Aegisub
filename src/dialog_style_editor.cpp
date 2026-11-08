// Copyright (c) 2005, Rodrigo Braz Monteiro
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file dialog_style_editor.cpp
/// @brief Style Editor dialogue box
/// @ingroup style_editor
///

#include "dialog_style_editor.h"
#include "theme.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "subtitle_value_edit.h"
#include "ass_style.h"
#include "ass_style_storage.h"
#include "colour_button.h"
#include "compat.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "font_popup.h"
#include "style_panel_parts.h"
#include "command/command.h"
#include "help_button.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "persist_location.h"
#include "selection_controller.h"
#include "subs_preview.h"
#include "utils.h"
#include "validators.h"

#include <libaegisub/of_type_adaptor.h>

#include <algorithm>

#include <cmath>
#include <optional>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

/// Style rename helper that walks a file searching for a style and optionally
/// updating references to it
class StyleRenamer {
	AssFile *file;
	bool found_any = false;
	bool do_replace = false;
	std::string source_name;
	std::string new_name;

	/// Process a single override parameter to check if it's \r with this style name
	static void ProcessTag(std::string const& tag, AssOverrideParameter* param, void *userData) {
		StyleRenamer *self = static_cast<StyleRenamer*>(userData);
		if (tag == "\\r" && param->GetType() == VariableDataType::TEXT && param->Get<std::string>() == self->source_name) {
			if (self->do_replace)
				param->Set(self->new_name);
			else
				self->found_any = true;
		}
	}

	void Walk(bool replace) {
		found_any = false;
		do_replace = replace;

		for (auto& diag : file->Events) {
			if (diag.Style == source_name) {
				if (replace)
					diag.Style = new_name;
				else
					found_any = true;
			}

			auto blocks = diag.ParseTags();
			for (auto block : blocks | agi::of_type<AssDialogueBlockOverride>())
				block->ProcessParameters(&StyleRenamer::ProcessTag, this);
			if (replace)
				diag.UpdateText(blocks);

			if (found_any) return;
		}
	}

public:
	StyleRenamer(AssFile *file, std::string source_name, std::string new_name)
	: file(file)
	, source_name(std::move(source_name))
	, new_name(std::move(new_name))
	{
	}

	/// Check if there are any uses of the original style name in the file
	bool NeedsReplace() {
		Walk(false);
		return found_any;
	}

	/// Replace all uses of the original style name with the new one
	void Replace() {
		Walk(true);
	}
};

namespace {
/// The nine places text is anchored to, as the numeric keypad lays them
/// out: a rounded field cut into three by three, the one chosen filled
/// with the accent colour and short lines showing text against that side
class AlignGrid final : public wxWindow {
	int value;
	int hover = 0;
	agi::signal::Connection theme_changed;

	wxRect Cell(int an) const {
		wxSize size = GetClientSize();
		int col = (an - 1) % 3, row = 2 - (an - 1) / 3;
		int w = size.x / 3, h = size.y / 3;
		return wxRect(col * w, row * h, col == 2 ? size.x - 2 * w : w, row == 2 ? size.y - 2 * h : h);
	}
	int At(wxPoint pos) const {
		for (int an = 1; an <= 9; ++an)
			if (Cell(an).Contains(pos)) return an;
		return 0;
	}

	void OnPaint(wxPaintEvent &) {
		wxAutoBufferedPaintDC buffered(this);
		wxGCDC dc(buffered);
		auto p = flat::Colours(this);
		wxSize size = GetClientSize();
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(GetParent()->GetBackgroundColour()));
		dc.DrawRectangle(wxPoint(0, 0), size);
		dc.SetBrush(wxBrush(p.field));
		dc.SetPen(wxPen(p.line, std::max(1, FromDIP(1))));
		dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), FromDIP(6));
		int thick = std::max(1, FromDIP(2));
		for (int an = 1; an <= 9; ++an) {
			wxRect cell = Cell(an).Deflate(FromDIP(3));
			bool on = an == value;
			dc.SetPen(*wxTRANSPARENT_PEN);
			if (on || an == hover) {
				dc.SetBrush(wxBrush(on ? p.accent : theme::Mix(p.field, p.text, 0.08)));
				dc.DrawRoundedRectangle(cell, FromDIP(4));
			}
			// Two short lines of text, against the side it keeps to
			wxColour ink = on ? (theme::IsDark() ? wxColour(20, 20, 20) : *wxWHITE) : p.dim;
			dc.SetBrush(wxBrush(ink));
			int col = (an - 1) % 3, row = 2 - (an - 1) / 3;
			int long_line = cell.width / 2, short_line = cell.width / 3;
			int gap = FromDIP(3);
			int top = cell.y + (row == 0 ? FromDIP(5) : row == 1 ? cell.height / 2 - gap : cell.height - FromDIP(5) - gap - thick);
			for (int i = 0; i < 2; ++i) {
				int width = i ? short_line : long_line;
				int x = col == 0 ? cell.x + FromDIP(5) : col == 1 ? cell.x + (cell.width - width) / 2 : cell.GetRight() - FromDIP(5) - width;
				dc.DrawRoundedRectangle(wxRect(x, top + i * (gap + thick) - (i ? 0 : 0), width, thick), thick / 2.0);
			}
		}
	}

public:
	std::function<void(int)> on_choose;

	AlignGrid(wxWindow *parent, int value, wxString const& tip)
	: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
	, value(value)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(FromDIP(wxSize(84, 72)));
		SetToolTip(tip);
		SetCursor(wxCursor(wxCURSOR_HAND));
		Bind(wxEVT_PAINT, &AlignGrid::OnPaint, this);
		Bind(wxEVT_MOTION, [this](wxMouseEvent &e) { int now = At(e.GetPosition()); if (now != hover) { hover = now; Refresh(false); } });
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { hover = 0; Refresh(false); });
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { if (int an = At(e.GetPosition())) Choose(an); });
		theme_changed = theme::AddChangeListener([this] { Refresh(false); });
	}

	/// Choose as a click does
	void Choose(int an) {
		if (an == value) return;
		value = an;
		Refresh(false);
		if (on_choose) on_choose(an);
	}
	int GetValue() const { return value; }
	bool AcceptsFocus() const override { return false; }
};


}

DialogStyleEditor::DialogStyleEditor(wxWindow *parent, AssStyle const* style, agi::Context *c, AssStyleStorage *store, std::string const& new_name, wxArrayString const& font_list)
: wxDialog (parent, -1, _("Style Editor"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
, c(c)
, style(style)
, source_revision(store ? std::optional<agi::SubtitleEditGate::RevisionToken>{} : c->subtitleEditGate.Capture())
, store(store)
{
	if (new_name.size()) {
		is_new = true;
        pending = std::make_unique<AssStyle>(*style);
        pending->name = new_name;
        style = this->style = pending.get();
	}
	else if (!style) {
		is_new = true;
        pending = std::make_unique<AssStyle>();
        style = this->style = pending.get();
	}

	work = std::make_unique<AssStyle>(*style);
    style_name = style->name;

	SetIcons(GETICONS(style_toolbutton));
	theme::UseSurface(this);
	const int gap = FromDIP(8);

	// A number in a rounded field, its name dim before it (docs/main-window.md):
	// typed, dragged by the name, or a step at a time with the wheel
	auto number = [&](wxString const& label, double value, double min, double max, double step, int precision, std::function<void(double)> set, wxString const& tip) {
		auto field = flat::NumberField::Create(this, label, value, min, max, step, precision);
		field->SetToolTip(tip);
		field->on_change = [this, set](double v) { set(v); Changed(); };
		return field->Frame();
	};

	// Preview: the style as it will look, on a background of one's choice
	SubsPreview = new SubtitlesPreview(this, DipSize(100, 90), wxBORDER_NONE, OPT_GET("Colour/Style Editor/Background/Preview")->GetColor());
	SubsPreview->SetToolTip(_("Preview of current style"));
	SubsPreview->SetStyle(*style);
	FlatFrame *preview_frame = new FlatFrame(this, _("Preview"));
	PreviewText = new wxTextCtrl(preview_frame, -1, to_wx(OPT_GET("Tool/Style Editor/Preview Text")->GetString()), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	preview_frame->SetContent(PreviewText, 120);
	PreviewText->SetToolTip(_("Text to be used for the preview"));
	SubsPreview->SetText(from_wx(PreviewText->GetValue()));
	auto previewButton = new ColourButton(this, wxSize(28, 14), false, OPT_GET("Colour/Style Editor/Background/Preview")->GetColor());
	previewButton->SetToolTip(_("Color of preview background"));

	// Name, on one row with the preview text and its background
	FlatFrame *name_frame = new FlatFrame(this, _("Name"));
	StyleName = new wxTextCtrl(name_frame, -1, to_wx(style->name), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	name_frame->SetContent(StyleName, 120);
	StyleName->SetToolTip(_("Style name"));
	auto top_row = new wxBoxSizer(wxHORIZONTAL);
	top_row->Add(name_frame, wxSizerFlags(1).Expand());
	top_row->Add(preview_frame, wxSizerFlags(1).Expand().Border(wxLEFT, FromDIP(6)));
	top_row->Add(previewButton, wxSizerFlags().Expand().Border(wxLEFT, FromDIP(4)));

	// Font: the face from the list or typed, the size, and the four flags
	auto left = new wxBoxSizer(wxVERTICAL);
	FlatFrame *font_frame = new FlatFrame(this, _("Font"));
	FontName = new wxTextCtrl(font_frame, -1, to_wx(style->font), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	font_frame->SetContent(FontName, 120);
	FontName->SetToolTip(_("Font face"));
	wxArrayString fonts = font_list.empty() ? InstalledFonts() : font_list;
	font_frame->SetChevron([this, font_frame, fonts] {
		auto popup = new FontPopup(this, fonts, FontName->GetValue(), [this](wxString const& font) {
			FontName->ChangeValue(font);
			work->font = from_wx(font);
			Changed();
		});
		popup->Open(font_frame);
	});
	auto font_row = new wxBoxSizer(wxHORIZONTAL);
	font_row->Add(font_frame, wxSizerFlags(1).Expand());
	font_row->Add(number(_("Size"), style->fontsize, 0, 10000, 1, 1, [this](double v) { work->fontsize = v; }, _("Font size")),
		wxSizerFlags().Expand().Border(wxLEFT, FromDIP(4)));
	left->Add(font_row, 0, wxEXPAND);
	auto flags_row = new wxBoxSizer(wxHORIZONTAL);
	struct Flag { const char *command; bool AssStyle::*member; wxString tip; };
	for (auto const& flag : {Flag{"edit/style/bold", &AssStyle::bold, _("Bold")}, Flag{"edit/style/italic", &AssStyle::italic, _("Italic")},
	                         Flag{"edit/style/underline", &AssStyle::underline, _("Underline")}, Flag{"edit/style/strikeout", &AssStyle::strikeout, _("Strikeout")}}) {
		const char *command = flag.command;
		auto toggle = new StylePanel::Toggle(this, [command] { return cmd::get(command)->Icon(); }, flag.tip);
		auto member = flag.member;
		toggle->SetState(style->*member ? StylePanel::Toggle::State::On : StylePanel::Toggle::State::Off);
		toggle->on_click = [this, toggle, member] {
			work.get()->*member = !(work.get()->*member);
			toggle->SetState(work.get()->*member ? StylePanel::Toggle::State::On : StylePanel::Toggle::State::Off);
			Changed();
		};
		flags_row->Add(toggle, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(2));
	}

	// Colours: four swatches beside the flags, their names small under them
	ColourButton *colorButton[] = {
		new ColourButton(this, wxSize(30, 14), true, style->primary, ColorValidator(&work->primary)),
		new ColourButton(this, wxSize(30, 14), true, style->secondary, ColorValidator(&work->secondary)),
		new ColourButton(this, wxSize(30, 14), true, style->outline, ColorValidator(&work->outline)),
		new ColourButton(this, wxSize(30, 14), true, style->shadow, ColorValidator(&work->shadow))
	};
	colorButton[0]->SetToolTip(_("Choose primary color"));
	colorButton[1]->SetToolTip(_("Choose secondary color"));
	colorButton[2]->SetToolTip(_("Choose outline color"));
	colorButton[3]->SetToolTip(_("Choose shadow color"));
	wxString colorLabels[] = { _("Primary"), _("Secondary"), _("Outline"), _("Shadow") };
	auto colours_row = new wxGridSizer(4, 0, FromDIP(4));
	for (int i = 0; i < 4; ++i) {
		auto cell = new wxBoxSizer(wxVERTICAL);
		cell->Add(colorButton[i], 0, wxEXPAND);
		auto name = new wxStaticText(this, -1, colorLabels[i]);
		name->SetFont(name->GetFont().Smaller());
		name->SetForegroundColour(flat::Colours(this).dim);
		cell->Add(name, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(1));
		colours_row->Add(cell, 0, wxEXPAND);
	}
	flags_row->Add(colours_row, 1, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(6));
	left->Add(flags_row, 0, wxEXPAND | wxTOP, FromDIP(4));

	// Where it goes: the alignment, and the margins beside it
	auto align = new AlignGrid(this, work->alignment, _("Alignment in screen, in numpad style"));
	align->on_choose = [this](int an) { work->alignment = an; Changed(); };
	auto margins = new wxBoxSizer(wxVERTICAL);
	wxString marginLabels[] = { _("Left"), _("Right"), _("Vert") };
	wxString marginTips[] = { _("Distance from left edge, in pixels"), _("Distance from right edge, in pixels"), _("Distance from top/bottom edge, in pixels") };
	for (int i = 0; i < 3; ++i)
		margins->Add(number(marginLabels[i], style->Margin[i], -9999, 99999, 1, 0, [this, i](double v) { work->Margin[i] = static_cast<int>(v); }, marginTips[i]),
			0, wxEXPAND | (i ? wxTOP : 0), FromDIP(4));
	auto place_row = new wxBoxSizer(wxHORIZONTAL);
	place_row->Add(align, 0, wxEXPAND);
	place_row->Add(margins, 1, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(6));
	left->Add(place_row, 0, wxEXPAND | wxTOP, FromDIP(6));

	// Border: outline or a box, and how wide
	auto right = new wxBoxSizer(wxVERTICAL);
	auto border = new StylePanel::PageSwitch(this, std::vector<wxString>{_("Outline"), wxGETTEXT_IN_CONTEXT("border style", "Per line"), wxGETTEXT_IN_CONTEXT("border style", "Whole")});
	// What libass draws for border styles 1, 3 and 4 (docs/main-window.md)
	border->SetToolTip(_("Outline: the text is outlined in the outline colour, with a shadow in the shadow colour.\n"
		"Background per line: each line is set on a solid rectangle in the outline colour, the outline width around the text, "
		"with the shadow as the rectangle's shadow; the text has no outline.\n"
		"One background: a single solid rectangle in the shadow colour behind the whole subtitle, the shadow distance around it; "
		"the text keeps its outline but has no shadow. Drawn by libass only."));
	{
		// Wide enough for the longest name in each third
		int widest = 0;
		for (auto const& name : {_("Outline"), wxGETTEXT_IN_CONTEXT("border style", "Per line"), wxGETTEXT_IN_CONTEXT("border style", "Whole")})
			widest = std::max(widest, border->GetTextExtent(name).x);
		border->SetMinSize(wxSize(std::max(FromDIP(240), 3 * (widest + FromDIP(16))), FromDIP(28)));
	}
	border->Select(work->borderstyle == 3 ? 1 : work->borderstyle == 4 ? 2 : 0);
	border->on_choose = [this, border](int index) {
		border->Select(index);
		work->borderstyle = index == 1 ? 3 : index == 2 ? 4 : 1;
		Changed();
	};
	right->Add(border, 0, wxEXPAND);
	auto widths = new wxGridSizer(2, FromDIP(4), FromDIP(4));
	widths->Add(number(_("Outline"), style->outline_w, 0, 1000, 0.1, 2, [this](double v) { work->outline_w = v; }, _("Outline width, in pixels")), 0, wxEXPAND);
	widths->Add(number(_("Shadow"), style->shadow_w, 0, 1000, 0.1, 2, [this](double v) { work->shadow_w = v; }, _("Shadow distance, in pixels")), 0, wxEXPAND);
	right->Add(widths, 0, wxEXPAND | wxTOP, FromDIP(4));

	// Transform
	auto transform = new wxGridSizer(2, FromDIP(4), FromDIP(4));
	transform->Add(number(_("Scale X"), style->scalex, 0, 10000, 1, 2, [this](double v) { work->scalex = v; }, _("Scale X, in percentage")), 0, wxEXPAND);
	transform->Add(number(_("Scale Y"), style->scaley, 0, 10000, 1, 2, [this](double v) { work->scaley = v; }, _("Scale Y, in percentage")), 0, wxEXPAND);
	transform->Add(number(_("Rotation"), style->angle, -360, 360, 1, 2, [this](double v) { work->angle = v; }, _("Angle to rotate in Z axis, in degrees")), 0, wxEXPAND);
	transform->Add(number(_("Spacing"), style->spacing, -1000, 1000, 0.1, 3, [this](double v) { work->spacing = v; }, _("Character spacing, in pixels")), 0, wxEXPAND);
	right->Add(transform, 0, wxEXPAND | wxTOP, FromDIP(4));

	// Encoding, named beside the list
	wxArrayString encodingStrings;
	AssStyle::GetEncodings(encodingStrings);
	auto encoding = new FlatChoice(this, _("Encoding, only useful in unicode if the font doesn't have the proper unicode mapping"));
	encoding->Set(std::vector<wxString>(encodingStrings.begin(), encodingStrings.end()));
	{
		wxString value = std::to_wstring(style->encoding);
		int chosen = -1, fallback = -1;
		for (size_t i = 0; i < encodingStrings.size(); ++i) {
			wxString number = encodingStrings[i].BeforeFirst(' ');
			if (number == value && chosen < 0) chosen = static_cast<int>(i);
			if (number == "1" && fallback < 0) fallback = static_cast<int>(i);
		}
		if (chosen < 0) chosen = fallback;
		if (chosen >= 0) encoding->SetStringSelection(encodingStrings[chosen]);
	}
	encoding->on_choose = [this, encoding](int) {
		long value = 1;
		encoding->GetStringSelection().BeforeFirst(' ').ToLong(&value);
		work->encoding = static_cast<int>(value);
		Changed();
	};
	auto encoding_row = new wxBoxSizer(wxHORIZONTAL);
	auto encoding_label = new wxStaticText(this, -1, _("Encoding"));
	encoding_label->SetForegroundColour(flat::Colours(this).dim);
	encoding_row->Add(encoding_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	encoding_row->Add(encoding, 1, wxEXPAND);
	right->Add(encoding_row, 0, wxEXPAND | wxTOP, FromDIP(4));

	auto columns = new wxBoxSizer(wxHORIZONTAL);
	columns->Add(left, 1, wxEXPAND);
	columns->AddSpacer(FromDIP(12));
	columns->Add(right, 1, wxEXPAND);

	// Buttons
	auto ButtonSizer = new flat::DialogButtons(this, wxOK | wxCANCEL | wxAPPLY | wxHELP);

	// General Layout
	auto content = new wxBoxSizer(wxVERTICAL);
	content->Add(top_row, 0, wxEXPAND);
	content->Add(SubsPreview, 1, wxEXPAND | wxTOP, FromDIP(4));
	content->Add(columns, 0, wxEXPAND | wxTOP, FromDIP(8));
	wxSizer *MainSizer = new wxBoxSizer(wxVERTICAL);
	MainSizer->Add(content, 1, wxEXPAND | wxALL, FromDIP(8));
	MainSizer->Add(ButtonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, gap);

	SetSizerAndFit(MainSizer);

	// Force the style name text field to scroll based on its final size, rather
	// than its initial size
	StyleName->SetInsertionPoint(0);
	StyleName->SetInsertionPoint(-1);

	persist = std::make_unique<PersistLocation>(this, "Tool/Style Editor", true);

	Bind(wxEVT_CHILD_FOCUS, &DialogStyleEditor::OnChildFocus, this);

	previewButton->Bind(EVT_COLOR, &DialogStyleEditor::OnPreviewColourChange, this);
	FontName->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
		event.Skip();
		work->font = from_wx(FontName->GetValue());
		Changed();
	});
	PreviewText->Bind(wxEVT_TEXT, &DialogStyleEditor::OnPreviewTextChange, this);

	Bind(wxEVT_BUTTON, std::bind(&DialogStyleEditor::Apply, this, true, true), wxID_OK);
	Bind(wxEVT_BUTTON, std::bind(&DialogStyleEditor::Apply, this, true, false), wxID_APPLY);
	Bind(wxEVT_BUTTON, std::bind(&DialogStyleEditor::Apply, this, false, true), wxID_CANCEL);
	Bind(wxEVT_BUTTON, std::bind(&HelpButton::OpenPage, "Style Editor"), wxID_HELP);

	for (auto const& elem : colorButton)
		elem->Bind(EVT_COLOR, &DialogStyleEditor::OnSetColor, this);
}

DialogStyleEditor::~DialogStyleEditor() = default;

std::string DialogStyleEditor::GetStyleName() const {
	return style_name;
}

void DialogStyleEditor::Apply(bool apply, bool close) {
	if (apply) {
        bool source_current = store || is_new || (source_revision && source_revision->Current());
        // C1-C1c-13 (audit B6): refuse (e.g. during a comparison) before any
        // question, but never hold the write lease across a message box: the
        // rename question is asked first, against an independent snapshot, and
        // every error below releases the lease before reporting.
        if (!store && !c->subtitleAccess->TryWrite()) return;
        if (!source_current) {
            wxMessageBox(_("The subtitles changed while this style was being edited. Please reopen the style editor."),
                _("Style Editor"), wxOK | wxICON_ERROR, this);
            return;
        }
		std::string new_name = from_wx(StyleName->GetValue());
		int rename_answer = wxNO;
		if (!store && !is_new && work->name != new_name) {
			auto snapshot = c->subtitleAccess->Snapshot();
			StyleRenamer probe(snapshot.get(), work->name, new_name);
			if (probe.NeedsReplace()) {
				// See if user wants to update style name through script
				rename_answer = wxMessageBox(
					_("Do you want to change all instances of this style in the script to this new name?"),
					_("Update script?"),
					wxYES_NO | wxCANCEL);
				if (rename_answer == wxCANCEL) return;
			}
		}

        std::optional<SubtitleProjectAccess::WriteScope> edit_scope;
        std::unique_ptr<SubtitleValueEdit> staged;
        if (!store) {
            edit_scope = c->subtitleAccess->TryWrite();
            if (!edit_scope) return;
        }
        auto refuse = [&](wxString const& message, wxString const& title, long style_flags) {
            staged.reset(); edit_scope.reset();
            wxMessageBox(message, title, style_flags, this);
        };
        // Never retain a mutable live style across callbacks. Resolve the
        // original object by identity, preserving behavior for duplicate names.
        // The question above ran without the lease, so the edited style must
        // still be the live node of that name before it is resolved.
        AssStyle *target = is_new ? pending.get() : nullptr;
        if (!is_new && store) {
            for (auto const& entry : *store) if (entry.get() == style) target = entry.get();
        }
        else if (!is_new) {
            bool live = false;
            for (auto const& candidate : c->subtitleAccess->Read().Styles)
                if (&candidate == style && candidate.name == style_name) live = true;
            if (live) {
                staged = std::make_unique<SubtitleValueEdit>(c);
                target = staged->Source(style);
            }
        }
        else if (!store) staged = std::make_unique<SubtitleValueEdit>(c);
        if (!target) {
            refuse(_("The style being edited no longer exists."), _("Style Editor"), wxOK | wxICON_ERROR);
            return;
        }

		// Check if style name is unique
		AssStyle *existing = store ? store->GetStyle(new_name) : staged->file.GetStyle(new_name);
		if (existing && existing != target) {
			refuse(_("There is already a style with this name. Please choose another name."), _("Style name conflict"), wxOK | wxICON_ERROR | wxCENTER);
			return;
		}

		// Style name change
		bool did_rename = false;
		if (work->name != new_name) {
			if (!store && !is_new && rename_answer == wxYES) {
				StyleRenamer renamer(&staged->file, work->name, new_name);
				if (renamer.NeedsReplace()) {
					did_rename = true;
					renamer.Replace();
				}
			}

			work->name = new_name;
		}

		UpdateWorkStyle();

		*target = *work;
		target->UpdateData();
		if (is_new) {
			if (store)
				store->push_back(std::move(pending));
			else
                { staged->file.Styles.push_back(*pending); pending.release(); }
			is_new = false;
		}
        if(store)style=target;
        else {staged->Publish(AssFile::COMMIT_STYLES | (did_rename?AssFile::COMMIT_DIAG_FULL:0));style=staged->LiveStyle(target);}
        style_name = target->name;
		if (!store)
			staged->Commit(_("style change"), AssFile::COMMIT_STYLES | (did_rename ? AssFile::COMMIT_DIAG_FULL : 0));
        if (!store) source_revision = c->subtitleEditGate.Capture();

		// Update preview
		if (!close) SubsPreview->SetStyle(*work);
	}

	if (close) {
		EndModal(apply);
		if (PreviewText)
			OPT_SET("Tool/Style Editor/Preview Text")->SetString(from_wx(PreviewText->GetValue()));
	}
}

void DialogStyleEditor::UpdateWorkStyle() {
	// The colours, through their validators
	updating = true;
	TransferDataFromWindow();
	updating = false;

	work->font = from_wx(FontName->GetValue());
}

void DialogStyleEditor::Changed() {
	if (SubsPreview) SubsPreview->SetStyle(*work);
}

void DialogStyleEditor::OnSetColor(ValueEvent<agi::Color>&) {
	TransferDataFromWindow();
	Changed();
}

void DialogStyleEditor::OnChildFocus(wxChildFocusEvent &event) {
	UpdateWorkStyle();
	Changed();
	event.Skip();
}

void DialogStyleEditor::OnPreviewTextChange (wxCommandEvent &event) {
	SubsPreview->SetText(from_wx(PreviewText->GetValue()));
	event.Skip();
}

void DialogStyleEditor::OnPreviewColourChange(ValueEvent<agi::Color> &evt) {
	SubsPreview->SetColour(evt.Get());
	OPT_SET("Colour/Style Editor/Background/Preview")->SetColor(evt.Get());
}

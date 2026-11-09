// Copyright (c) 2011 Niels Martin Hansen <nielsm@aegisub.org>
// Copyright (c) 2012 Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file dialog_export_ebu3264.cpp
/// @see dialog_export_ebu3264.h
/// @ingroup subtitle_io export

#include "dialog_export_ebu3264.h"

#include "compat.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "format.h"
#include "options.h"
#include "theme.h"

#include <libaegisub/charset_conv.h>

#include <algorithm>

#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>

#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
	const boost::regex timecode_regex("([[:digit:]]{2}):([[:digit:]]{2}):([[:digit:]]{2}):([[:digit:]]{2})");

	/// @p text as a time code into @p value, if it is one
	bool ParseTimecode(wxString const& text, EbuTimecode &value) {
		std::string str = from_wx(text);
		boost::smatch result;
		if (!regex_match(str, result, timecode_regex))
			return false;

		value.h = boost::lexical_cast<int>(result.str(1));
		value.m = boost::lexical_cast<int>(result.str(2));
		value.s = boost::lexical_cast<int>(result.str(3));
		value.f = boost::lexical_cast<int>(result.str(4));
		return true;
	}
} // namespace {

int ShowEbuExportConfigurationDialog(wxWindow *owner, EbuExportSettings &s) {
	wxDialog d(owner, -1, _("Export to EBU STL format"));
	theme::UseSurface(&d);
	int gap = d.FromDIP(10), row = d.FromDIP(6);

	auto choice = [&](wxString const& name, std::vector<wxString> items, int chosen) {
		auto ctrl = new FlatChoice(&d, name);
		ctrl->Set(items);
		// Wide enough for the longest, with room for the arrow
		int widest = 0;
		for (auto const& item : items) widest = std::max(widest, ctrl->GetTextExtent(item).x);
		ctrl->SetMinSize(wxSize(widest + d.FromDIP(40), -1));
		if (chosen >= 0 && chosen < static_cast<int>(items.size()))
			ctrl->SetStringSelection(items[chosen]);
		return ctrl;
	};

	// What the file is for: the frame rate, the characters and the display
	auto tv_standard = choice(_("TV standard"), {
		_("23.976 fps (non-standard, STL24.01)"),
		_("24 fps (non-standard, STL24.01)"),
		_("25 fps (STL25.01)"),
		_("29.97 fps (non-dropframe, STL30.01)"),
		_("29.97 fps (dropframe, STL30.01)"),
		_("30 fps (STL30.01)")
	}, s.tv_standard);
	auto text_encoding = choice(_("Text encoding"), {
		_("ISO 6937-2 (Latin/Western Europe)"),
		_("ISO 8859-5 (Cyrillic)"),
		_("ISO 8859-6 (Arabic)"),
		_("ISO 8859-7 (Greek)"),
		_("ISO 8859-8 (Hebrew)"),
		_("UTF-8 Unicode (non-standard)")
	}, s.text_encoding);
	auto display_standard = new FlatSwitch(&d, {_("Open subtitles"), _("Level-1 teletext")});
	// Level-2 teletext, not offered, is kept unless another is chosen
	int display_shown = std::clamp(static_cast<int>(s.display_standard), 0, 1);
	display_standard->SetSelection(display_shown);

	// How the text is laid out
	auto max_line_length = flat::NumberField::Create(&d, wxString(), s.max_line_length, 10, 99, 1, 0, 32);
	auto wrap_mode = choice(_("Text formatting"), {
		_("Automatically wrap long lines (ASS)"),
		_("Automatically wrap long lines (Balanced)"),
		_("Abort if any lines are too long"),
		_("Skip lines that are too long")
	}, s.line_wrapping_mode);
	auto translate_alignments = new FlatCheck(&d, _("Translate alignments"));
	translate_alignments->SetValue(s.translate_alignments);

	// When the lines are shown
	auto offset_frame = new FlatFrame(&d, wxString());
	auto timecode_offset = new wxTextCtrl(offset_frame, -1, fmt_wx("%02d:%02d:%02d:%02d", s.timecode_offset.h, s.timecode_offset.m, s.timecode_offset.s, s.timecode_offset.f),
		wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	offset_frame->SetContent(timecode_offset, d.ToDIP(timecode_offset->GetSizeFromText("00:00:00:00").x));
	auto inclusive_end_times = new FlatCheck(&d, _("Out-times are inclusive"));
	inclusive_end_times->SetValue(s.inclusive_end_times);

	// Grey names at the left, under a section's title
	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer *grid = nullptr;
	auto section = [&](wxString const& title) {
		if (!title.empty())
			main_sizer->Add(flat::SectionTitle(&d, title), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
		grid = new wxFlexGridSizer(2, row, gap);
		grid->AddGrowableCol(1, 1);
		main_sizer->Add(grid, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	};
	// The names one column wide through the sections
	std::vector<wxWindow*> names;
	auto add = [&](wxString const& name, wxWindow *ctrl, bool expand = true) {
		names.push_back(flat::DimLabel(&d, flat::WithoutColon(name)));
		grid->Add(names.back(), wxSizerFlags().CenterVertical());
		grid->Add(ctrl, expand ? wxSizerFlags().Expand() : wxSizerFlags());
	};
	auto add_unnamed = [&](wxWindow *ctrl, bool expand = false) {
		names.push_back(new wxWindow(&d, -1, wxDefaultPosition, wxSize(0, 0)));
		grid->Add(names.back());
		grid->Add(ctrl, expand ? wxSizerFlags().Expand() : wxSizerFlags());
	};
	// What the file is for needs no title
	section(wxString());
	add(_("TV standard"), tv_standard);
	add(_("Text encoding"), text_encoding);
	add(_("Display standard"), display_standard, false);
	section(_("Text formatting"));
	add(_("Max. line length:"), max_line_length->Frame(), false);
	add_unnamed(wrap_mode, true);
	add_unnamed(translate_alignments);
	section(_("Time codes"));
	add(_("Time code offset:"), offset_frame, false);
	add_unnamed(inclusive_end_times);

	int name_width = 0;
	for (auto name : names) name_width = std::max(name_width, name->GetBestSize().x);
	for (auto name : names) name->SetMinSize(wxSize(name_width, -1));

	// Developers are requested to leave this message in! Intentionally not translatable.
	auto sponsor_label = flat::DimLabel(&d, "EBU STL format writing sponsored by Bandai");
	main_sizer->Add(sponsor_label, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	main_sizer->Add(new flat::DialogButtons(&d, wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL, gap));

	d.SetSizerAndFit(main_sizer);
	d.CenterOnParent();

	d.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
		EbuTimecode offset;
		if (!ParseTimecode(timecode_offset->GetValue(), offset)) {
			wxMessageBox(_("Time code offset in incorrect format. Ensure it is entered as four groups of two digits separated by colons."), _("EBU STL export"), wxICON_EXCLAMATION);
			return;
		}
		s.timecode_offset = offset;
		s.tv_standard = static_cast<EbuExportSettings::TvStandard>(tv_standard->GetSelection());
		s.text_encoding = static_cast<EbuExportSettings::TextEncoding>(text_encoding->GetSelection());
		if (display_standard->GetSelection() != display_shown || s.display_standard != EbuExportSettings::DSC_Level2)
			s.display_standard = static_cast<EbuExportSettings::DisplayStandard>(display_standard->GetSelection());
		s.max_line_length = max_line_length->GetInt();
		s.line_wrapping_mode = static_cast<EbuExportSettings::LineWrappingMode>(wrap_mode->GetSelection());
		s.translate_alignments = translate_alignments->GetValue();
		s.inclusive_end_times = inclusive_end_times->GetValue();
		d.EndModal(wxID_OK);
	}, wxID_OK);

	return d.ShowModal();
}

agi::vfr::Framerate EbuExportSettings::GetFramerate() const {
	switch (tv_standard) {
		case STL24:     return agi::vfr::Framerate(24, 1);
		case STL25:     return agi::vfr::Framerate(25, 1);
		case STL30:     return agi::vfr::Framerate(30, 1);
		case STL23:     return agi::vfr::Framerate(24000, 1001, false);
		case STL29:     return agi::vfr::Framerate(30000, 1001, false);
		case STL29drop: return agi::vfr::Framerate(30000, 1001);
		default:        return agi::vfr::Framerate(25, 1);
	}
}

std::unique_ptr<agi::charset::IconvWrapper> EbuExportSettings::GetTextEncoder() const {
	const char *encoding = "ISO-8859-1";
	switch (text_encoding) {
		case iso6937_2: encoding = "ISO-6937-2"; break;
		case iso8859_5: encoding = "ISO-8859-5"; break;
		case iso8859_6: encoding = "ISO-8859-6"; break;
		case iso8859_7: encoding = "ISO-8859-7"; break;
		case iso8859_8: encoding = "ISO-8859-8"; break;
		case utf8:      encoding = "utf-8"; break;
	}
	return std::make_unique<agi::charset::IconvWrapper>("utf-8", encoding);
}

EbuExportSettings::EbuExportSettings(std::string const& prefix)
: prefix(prefix)
, tv_standard((TvStandard)OPT_GET(prefix + "/TV Standard")->GetInt())
, text_encoding((TextEncoding)OPT_GET(prefix + "/Text Encoding")->GetInt())
, max_line_length(OPT_GET(prefix + "/Max Line Length")->GetInt())
, line_wrapping_mode((LineWrappingMode)OPT_GET(prefix + "/Line Wrapping Mode")->GetInt())
, translate_alignments(OPT_GET(prefix + "/Translate Alignments")->GetBool())
, inclusive_end_times(OPT_GET(prefix + "/Inclusive End Times")->GetBool())
, display_standard((DisplayStandard)OPT_GET(prefix + "/Display Standard")->GetInt())
{
	timecode_offset.h = OPT_GET(prefix + "/Timecode Offset/H")->GetInt();
	timecode_offset.m = OPT_GET(prefix + "/Timecode Offset/M")->GetInt();
	timecode_offset.s = OPT_GET(prefix + "/Timecode Offset/S")->GetInt();
	timecode_offset.f = OPT_GET(prefix + "/Timecode Offset/F")->GetInt();
}

void EbuExportSettings::Save() const {
	OPT_SET(prefix + "/TV Standard")->SetInt(tv_standard);
	OPT_SET(prefix + "/Text Encoding")->SetInt(text_encoding);
	OPT_SET(prefix + "/Max Line Length")->SetInt(max_line_length);
	OPT_SET(prefix + "/Line Wrapping Mode")->SetInt(line_wrapping_mode);
	OPT_SET(prefix + "/Translate Alignments")->SetBool(translate_alignments);
	OPT_SET(prefix + "/Inclusive End Times")->SetBool(inclusive_end_times);
	OPT_SET(prefix + "/Display Standard")->SetInt(display_standard);
	OPT_SET(prefix + "/Timecode Offset/H")->SetInt(timecode_offset.h);
	OPT_SET(prefix + "/Timecode Offset/M")->SetInt(timecode_offset.m);
	OPT_SET(prefix + "/Timecode Offset/S")->SetInt(timecode_offset.s);
	OPT_SET(prefix + "/Timecode Offset/F")->SetInt(timecode_offset.f);
}

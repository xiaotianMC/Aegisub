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

#include "ass_file.h"
#include "subtitle_project_access.h"
#include "subtitle_value_edit.h"
#include "async_video_provider.h"
#include "compat.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "help_button.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "project.h"
#include "resolution_resampler.h"
#include "theme.h"
#include "validators.h"
#include "video_controller.h"

#include <boost/algorithm/string/predicate.hpp>
#include <vector>
#include <wx/dialog.h>
#include <wx/gbsizer.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
class DialogProperties {
	wxDialog d;
	agi::Context *c; ///< Project this dialog is adjusting the properties of

	/// Pairs of a script property and a text control for that property
	std::vector<std::pair<std::string, wxTextCtrl*>> properties;

	// Things that effect rendering
	FlatChoice *WrapStyle;   ///< Wrapping style for long lines
	wxTextCtrl *ResX;        ///< Script x resolution
	wxTextCtrl *ResY;        ///< Script y resolution
	wxTextCtrl *LayoutResX;  ///< Layout x resolution
	wxTextCtrl *LayoutResY;  ///< Layout y resolution
	FlatCheck *ScaleBorder; ///< If script resolution != video resolution how should borders be handled
	FlatChoice *YCbCrMatrix;
	/// The matrix as the script has it, kept when it is none of the list's
	wxString original_matrix;

	/// OK button handler
	void OnOK(wxCommandEvent &event);
	/// Set script resolution to video resolution button
	void OnSetFromVideo(wxCommandEvent &event);
	/// Set layout resolution to video resolution button
	void OnSetLayoutResFromVideo(wxCommandEvent &event);
	/// Set YCbCr Matrix to recommended matrix for video button
	void OnSetYCbCrMatrixFromVideo(wxCommandEvent &event);
	/// Set a script info field
	/// @param key Name of field
	/// @param value New value
	/// @return Did the value actually need to be changed?
	int SetInfoIfDifferent(AssFile& target, std::string_view key, std::string_view value);

	/// Add a property with label and text box for updating the property
	/// @param parent Parent to construct the label and control with
	/// @param sizer Sizer to add the label and control to
	/// @param label Label text to use
	/// @param property Script info property name
	wxWindow *AddProperty(wxString const& label, std::string_view property);

public:
	/// Constructor
	/// @param c Project context
	DialogProperties(agi::Context *c);
	void ShowModal() { d.ShowModal(); }
};

DialogProperties::DialogProperties(agi::Context *c)
: d(c->parent, -1, _("Script Properties"))
, c(c)
{
	d.SetIcons(GETICONS(properties_toolbutton));
	theme::UseSurface(&d);
	auto const& file = c->subtitleAccess->Read();

	// Button sizer
	// Create buttons first. See:
	//  https://github.com/wangqr/Aegisub/issues/6
	//  https://trac.wxwidgets.org/ticket/18472#comment:9
	auto ButtonSizer = new flat::DialogButtons(&d, wxOK | wxCANCEL | wxHELP);
	d.Bind(wxEVT_BUTTON, &DialogProperties::OnOK, this, wxID_OK);
	d.Bind(wxEVT_BUTTON, std::bind(&HelpButton::OpenPage, "Properties"), wxID_HELP);

	// Drawn as the style editor is (docs/main-window.md): sections under a
	// bold title, fields with their names inside them
	const int row_gap = d.FromDIP(4), col_gap = d.FromDIP(8), section_gap = d.FromDIP(12);

	// Script: who made it. The title on a line of its own, the credits in
	// pairs, what was updated last on a line of its own.
	auto script = new wxBoxSizer(wxVERTICAL);
	script->Add(flat::SectionTitle(&d, _("Script")), 0, wxEXPAND);
	script->Add(AddProperty(wxGETTEXT_IN_CONTEXT("script info", "Title"), "Title"), 0, wxEXPAND);
	auto credits = new wxFlexGridSizer(2, row_gap, col_gap);
	credits->AddGrowableCol(0, 1);
	credits->AddGrowableCol(1, 1);
	credits->Add(AddProperty(wxGETTEXT_IN_CONTEXT("script info", "Original script"), "Original Script"), 0, wxEXPAND);
	credits->Add(AddProperty(wxGETTEXT_IN_CONTEXT("script info", "Translation"), "Original Translation"), 0, wxEXPAND);
	credits->Add(AddProperty(wxGETTEXT_IN_CONTEXT("script info", "Editing"), "Original Editing"), 0, wxEXPAND);
	credits->Add(AddProperty(wxGETTEXT_IN_CONTEXT("script info", "Timing"), "Original Timing"), 0, wxEXPAND);
	credits->Add(AddProperty(wxGETTEXT_IN_CONTEXT("script info", "Synch point"), "Synch Point"), 0, wxEXPAND);
	credits->Add(AddProperty(wxGETTEXT_IN_CONTEXT("script info", "Updated by"), "Script Updated By"), 0, wxEXPAND);
	script->Add(credits, 0, wxEXPAND | wxTOP, row_gap);
	script->Add(AddProperty(wxGETTEXT_IN_CONTEXT("script info", "Update details"), "Update Details"), 0, wxEXPAND | wxTOP, row_gap);

	// Resolution: a row each for the script's and the layout's, width ×
	// height and a button taking the video's; the colour matrix below
	bool video = !!c->project->VideoProvider();
	auto number = [&](wxTextCtrl *&text, wxString const& label, std::string_view key) {
		auto frame = new FlatFrame(&d, label);
		text = new wxTextCtrl(frame, -1, "", wxDefaultPosition, wxDefaultSize, wxBORDER_NONE, IntValidator(file.GetScriptInfoAsInt(key)));
		frame->SetContent(text, 60);
		return frame;
	};
	auto from_video = [&](wxString const& label, wxString const& tip, void (DialogProperties::*handler)(wxCommandEvent&)) {
		auto button = new FlatButton(&d, label);
		button->SetToolTip(video ? tip : _("Open a video to take this from it"));
		if (video) button->Bind(wxEVT_BUTTON, handler, this);
		else button->Enable(false);
		return button;
	};
	auto res_grid = new wxGridBagSizer(row_gap, d.FromDIP(6));
	// The name of a row: an old label without its colon
	auto row_label = [&](wxString text, int row) {
		text.Trim().Trim(false);
		if (text.EndsWith(":") || text.EndsWith(wxString::FromUTF8("\xEF\xBC\x9A"))) text.RemoveLast();
		res_grid->Add(new wxStaticText(&d, -1, text.Trim()), wxGBPosition(row, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxRIGHT, d.FromDIP(4));
	};
	auto times = [&](int row) {
		auto sign = new wxStaticText(&d, -1, wxString::FromUTF8("\xC3\x97"));
		sign->SetForegroundColour(flat::Colours(&d).dim);
		res_grid->Add(sign, wxGBPosition(row, 2), wxDefaultSpan, wxALIGN_CENTER);
	};
	row_label(_("Script: "), 0);
	res_grid->Add(number(ResX, wxGETTEXT_IN_CONTEXT("resolution", "Width"), "PlayResX"), wxGBPosition(0, 1), wxDefaultSpan, wxEXPAND);
	times(0);
	res_grid->Add(number(ResY, wxGETTEXT_IN_CONTEXT("resolution", "Height"), "PlayResY"), wxGBPosition(0, 3), wxDefaultSpan, wxEXPAND);
	res_grid->Add(from_video(_("From &video"), _("Use the video's resolution as the script's"), &DialogProperties::OnSetFromVideo), wxGBPosition(0, 4), wxDefaultSpan, wxEXPAND);
	row_label(_("Layout: "), 1);
	res_grid->Add(number(LayoutResX, wxGETTEXT_IN_CONTEXT("resolution", "Width"), "LayoutResX"), wxGBPosition(1, 1), wxDefaultSpan, wxEXPAND);
	times(1);
	res_grid->Add(number(LayoutResY, wxGETTEXT_IN_CONTEXT("resolution", "Height"), "LayoutResY"), wxGBPosition(1, 3), wxDefaultSpan, wxEXPAND);
	res_grid->Add(from_video(_("From video"), _("Use the video's resolution as the layout's"), &DialogProperties::OnSetLayoutResFromVideo), wxGBPosition(1, 4), wxDefaultSpan, wxEXPAND);

	row_label(_("YCbCr Matrix"), 2);
	YCbCrMatrix = new FlatChoice(&d, _("YCbCr Matrix"));
	{
		std::vector<wxString> matrices;
		for (auto const& name : agi::ycbcr::valid_header_strings) matrices.push_back(to_wx(name));
		YCbCrMatrix->Set(matrices);
	}
	original_matrix = to_wx(file.GetScriptInfo("YCbCr Matrix"));
	YCbCrMatrix->SetStringSelection(original_matrix);
	res_grid->Add(YCbCrMatrix, wxGBPosition(2, 1), wxGBSpan(1, 3), wxEXPAND);
	res_grid->Add(from_video(_("From video"), _("Use the matrix recommended for the video"), &DialogProperties::OnSetYCbCrMatrixFromVideo), wxGBPosition(2, 4), wxDefaultSpan, wxEXPAND);
	res_grid->AddGrowableCol(1, 1);
	res_grid->AddGrowableCol(3, 1);

	auto resolution = new wxBoxSizer(wxVERTICAL);
	resolution->Add(flat::SectionTitle(&d, _("Resolution")), 0, wxEXPAND);
	resolution->Add(res_grid, 0, wxEXPAND);

	// Options
	auto options = new wxBoxSizer(wxVERTICAL);
	options->Add(flat::SectionTitle(&d, _("Options")), 0, wxEXPAND);
	auto wrap_row = new wxBoxSizer(wxHORIZONTAL);
	wrap_row->Add(new wxStaticText(&d, -1, _("Wrap style")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, d.FromDIP(10));
	WrapStyle = new FlatChoice(&d, _("Wrap style"));
	WrapStyle->Set({
		_("0: Smart wrapping, top line is wider"),
		_("1: End-of-line word wrapping, only \\N breaks"),
		_("2: No word wrapping, both \\n and \\N break"),
		_("3: Smart wrapping, bottom line is wider")
	});
	int wrap = file.GetScriptInfoAsInt("WrapStyle");
	if (wrap >= 0 && wrap < 4) WrapStyle->SetStringSelection(WrapStyle->GetItems()[wrap]);
	wrap_row->Add(WrapStyle, 1, wxEXPAND);
	options->Add(wrap_row, 0, wxEXPAND);
	ScaleBorder = new FlatCheck(&d, _("Scale Border and Shadow"));
	ScaleBorder->SetToolTip(_("Scale border and shadow together with script/render resolution. If this is unchecked, relative border and shadow size will depend on renderer."));
	ScaleBorder->SetValue(boost::iequals(file.GetScriptInfo("ScaledBorderAndShadow"), "yes"));
	options->Add(ScaleBorder, 0, wxTOP, d.FromDIP(6));

	// MainSizer
	auto content = new wxBoxSizer(wxVERTICAL);
	content->Add(script, 0, wxEXPAND);
	content->Add(resolution, 0, wxEXPAND | wxTOP, section_gap);
	content->Add(options, 0, wxEXPAND | wxTOP, section_gap);
	wxSizer *MainSizer = new wxBoxSizer(wxVERTICAL);
	MainSizer->Add(content, 1, wxEXPAND | wxALL, d.FromDIP(12));
	MainSizer->Add(ButtonSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, d.FromDIP(8));

	d.SetSizerAndFit(MainSizer);
	d.SetSize(wxSize(std::max(d.GetSize().x, d.FromDIP(560)), -1));
	d.CenterOnParent();
}

wxWindow *DialogProperties::AddProperty(wxString const& label, std::string_view property) {
	auto frame = new FlatFrame(&d, label);
	wxTextCtrl *ctrl = new wxTextCtrl(frame, -1, to_wx(c->subtitleAccess->Read().GetScriptInfo(property)), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	frame->SetContent(ctrl, 100);
	properties.emplace_back(property, ctrl);
	return frame;
}

void DialogProperties::OnOK(wxCommandEvent &) {
    auto edit_scope = c->subtitleAccess->TryWrite();
    if (!edit_scope) return;
    SubtitleValueEdit staged(c);
	int count = 0;
	for (auto const& prop : properties)
		count += SetInfoIfDifferent(staged.file,prop.first, from_wx(prop.second->GetValue()));

	count += SetInfoIfDifferent(staged.file,"PlayResX", from_wx(ResX->GetValue()));
	count += SetInfoIfDifferent(staged.file,"PlayResY", from_wx(ResY->GetValue()));
	count += SetInfoIfDifferent(staged.file,"LayoutResX", from_wx(LayoutResX->GetValue()));
	count += SetInfoIfDifferent(staged.file,"LayoutResY", from_wx(LayoutResY->GetValue()));
	if (WrapStyle->GetSelection() >= 0)
		count += SetInfoIfDifferent(staged.file,"WrapStyle", std::to_string(WrapStyle->GetSelection()));
	count += SetInfoIfDifferent(staged.file,"ScaledBorderAndShadow", ScaleBorder->GetValue() ? "yes" : "no");
	// One not in the list is kept as the script has it
	wxString matrix = YCbCrMatrix->GetSelection() >= 0 ? YCbCrMatrix->GetStringSelection() : original_matrix;
	count += SetInfoIfDifferent(staged.file,"YCbCr Matrix", from_wx(matrix));

	if (count) staged.Commit(_("property changes"), AssFile::COMMIT_SCRIPTINFO);

	d.EndModal(!!count);
}

int DialogProperties::SetInfoIfDifferent(AssFile& target, std::string_view key, std::string_view value) {
	if (target.GetScriptInfo(key) != value) {
		target.SetScriptInfo(key, value);
		return 1;
	}
	return 0;
}

void DialogProperties::OnSetFromVideo(wxCommandEvent &) {
	auto [width, height] = c->project->VideoProvider()->GetDisplayResolution();
	ResX->SetValue(std::to_wstring(width));
	ResY->SetValue(std::to_wstring(height));
}

void DialogProperties::OnSetLayoutResFromVideo(wxCommandEvent &) {
	auto [width, height] = c->project->VideoProvider()->GetDisplayResolution();
	LayoutResX->SetValue(std::to_wstring(width));
	LayoutResY->SetValue(std::to_wstring(height));
}

void DialogProperties::OnSetYCbCrMatrixFromVideo(wxCommandEvent &) {
	YCbCrMatrix->SetStringSelection(to_wx(agi::ycbcr::Header(c->project->VideoProvider()->GetRealColorSpace()).to_best_practice_string()));
}
}

void ShowPropertiesDialog(agi::Context *c) {
	DialogProperties(c).ShowModal();
}

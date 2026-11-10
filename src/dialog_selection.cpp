// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
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

#include "flat_controls.h"
#include "flat_dialog.h"
#include "theme.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "dialog_manager.h"
#include "format.h"
#include "frame_main.h"
#include "help_button.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "search_replace_engine.h"
#include "selection_controller.h"
#include "subtitle_project_access.h"

#include <boost/range/algorithm/set_algorithm.hpp>

#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
class DialogSelection final : public wxDialog {
	agi::Context *con; ///< Project context

	wxTextCtrl *match_text; ///< Text to search for
	FlatCheck *case_sensitive; ///< Should the search be case-sensitive
	FlatCheck *apply_to_dialogue; ///< Select/deselect uncommented lines
	FlatCheck *apply_to_comments; ///< Select/deselect commented lines
	FlatSwitch *condition; ///< Lines which match, or which don't
	FlatSwitch *selection_change_type; ///< What sort of action to take on the selection
	FlatSwitch *dialogue_field; ///< Which dialogue field to look at
	FlatSwitch *match_mode;

	void Process(wxCommandEvent& event);

	/// Dialogue/Comment check handler to ensure at least one is always checked
	/// @param chk The checkbox to check if both are clear
	void OnDialogueCheckbox(FlatCheck *chk);

public:
	DialogSelection(agi::Context *c);
	~DialogSelection();
};

enum class Action {
	SET = 0,
	ADD,
	SUB,
	INTERSECT
};

enum Mode {
	EXACT = 0,
	CONTAINS,
	REGEXP
};

std::set<int> process(std::string const& match_text, bool match_case, Mode mode, bool invert, bool comments, bool dialogue, int field_n, AssFile const *ass) {
	SearchReplaceSettings settings = {
		match_text,
		std::string(),
		static_cast<SearchReplaceSettings::Field>(field_n),
		SearchReplaceSettings::Limit::ALL,
		match_case,
		mode == Mode::REGEXP,
		false,
		false,
		mode == Mode::EXACT
	};

	auto predicate = SearchReplaceEngine::GetMatcher(settings);

	std::set<int> matches;
	for (auto& diag : ass->Events) {
		if (diag.Comment && !comments) continue;
		if (!diag.Comment && !dialogue) continue;

		if (invert != predicate(&diag, 0))
			matches.insert(diag.Id);
	}

	return matches;
}

DialogSelection::DialogSelection(agi::Context *c) :
wxDialog (c->parent, -1, _("Select"), wxDefaultPosition, wxDefaultSize, wxCAPTION)
, con(c)
{
	SetIcons(GETICONS(select_lines_button));
	theme::UseSurface(this);
	auto dim = [this](wxString const& text) {
		auto label = new wxStaticText(this, -1, text);
		label->SetForegroundColour(flat::Colours(this).dim);
		return label;
	};

	// What to look for, and whether lines are chosen for having it or not
	auto text_frame = new FlatFrame(this, wxString());
	match_text = new wxTextCtrl(text_frame, -1, to_wx(OPT_GET("Tool/Select Lines/Text")->GetString()), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	text_frame->SetContent(match_text, 260);
	condition = new FlatSwitch(this, {_("&Matches"), _("&Doesn't Match")});
	case_sensitive = new FlatCheck(this, _("Match c&ase"));
	match_mode = new FlatSwitch(this, {wxGETTEXT_IN_CONTEXT("match mode", "E&xact"), _("&Contains"), wxGETTEXT_IN_CONTEXT("match mode", "&Regular expression")});
	dialogue_field = new FlatSwitch(this, {_("&Text"), _("&Style"), _("Act&or"), _("E&ffect")});
	apply_to_dialogue = new FlatCheck(this, _("D&ialogues"));
	apply_to_comments = new FlatCheck(this, _("Comme&nts"));
	selection_change_type = new FlatSwitch(this, {wxGETTEXT_IN_CONTEXT("selection action", "Re&place"), wxGETTEXT_IN_CONTEXT("selection action", "&Add"),
		wxGETTEXT_IN_CONTEXT("selection action", "S&ubtract"), wxGETTEXT_IN_CONTEXT("selection action", "Int&ersect")});
	selection_change_type->SetItemToolTip(0, _("Set selection"));
	selection_change_type->SetItemToolTip(1, _("Add to selection"));
	selection_change_type->SetItemToolTip(2, _("Subtract from selection"));
	selection_change_type->SetItemToolTip(3, _("Intersect with selection"));

	auto lines = new wxBoxSizer(wxHORIZONTAL);
	lines->Add(apply_to_dialogue, wxSizerFlags().CenterVertical());
	lines->Add(apply_to_comments, wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(16)));
	auto match_row = new wxBoxSizer(wxHORIZONTAL);
	match_row->Add(condition, wxSizerFlags(1).Expand());
	match_row->Add(case_sensitive, wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(12)));

	auto grid = new wxFlexGridSizer(2, FromDIP(6), FromDIP(10));
	grid->AddGrowableCol(1, 1);
	auto row = [&](wxString const& name, wxWindow *control, wxSizer *sizer = nullptr) {
		grid->Add(dim(name), wxSizerFlags().CenterVertical());
		if (control) grid->Add(control, wxSizerFlags().Expand());
		else grid->Add(sizer, wxSizerFlags().Expand());
	};
	row(_("Match"), nullptr, match_row);
	grid->AddSpacer(0);
	grid->Add(text_frame, wxSizerFlags().Expand());
	row(_("Mode"), match_mode);
	row(_("In Field"), dialogue_field);
	row(wxGETTEXT_IN_CONTEXT("select lines", "Lines"), nullptr, lines);
	row(_("Action"), selection_change_type);

	wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(grid, wxSizerFlags().Expand().Border(wxALL, FromDIP(10)));
	main_sizer->Add(new flat::DialogButtons(this, wxOK | wxCANCEL | wxAPPLY | wxHELP, "Select Lines"), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10)));
	SetSizerAndFit(main_sizer);
	CenterOnParent();

	dialogue_field->SetSelection(OPT_GET("Tool/Select Lines/Field")->GetInt());
	selection_change_type->SetSelection(OPT_GET("Tool/Select Lines/Action")->GetInt());
	case_sensitive->SetValue(OPT_GET("Tool/Select Lines/Match/Case")->GetBool());
	apply_to_dialogue->SetValue(OPT_GET("Tool/Select Lines/Match/Dialogue")->GetBool());
	apply_to_comments->SetValue(OPT_GET("Tool/Select Lines/Match/Comment")->GetBool());
	condition->SetSelection(OPT_GET("Tool/Select Lines/Condition")->GetInt() ? 1 : 0);
	match_mode->SetSelection(OPT_GET("Tool/Select Lines/Mode")->GetInt());
	match_text->SetFocus();
	match_text->SelectAll();

	Bind(wxEVT_BUTTON, &DialogSelection::Process, this, wxID_OK);
	Bind(wxEVT_BUTTON, &DialogSelection::Process, this, wxID_APPLY);
	apply_to_comments->Bind(wxEVT_CHECKBOX, std::bind(&DialogSelection::OnDialogueCheckbox, this, apply_to_dialogue));
	apply_to_dialogue->Bind(wxEVT_CHECKBOX, std::bind(&DialogSelection::OnDialogueCheckbox, this, apply_to_comments));
}

DialogSelection::~DialogSelection() {
	OPT_SET("Tool/Select Lines/Text")->SetString(from_wx(match_text->GetValue()));
	OPT_SET("Tool/Select Lines/Condition")->SetInt(condition->GetSelection());
	OPT_SET("Tool/Select Lines/Field")->SetInt(dialogue_field->GetSelection());
	OPT_SET("Tool/Select Lines/Action")->SetInt(selection_change_type->GetSelection());
	OPT_SET("Tool/Select Lines/Mode")->SetInt(match_mode->GetSelection());
	OPT_SET("Tool/Select Lines/Match/Case")->SetBool(case_sensitive->IsChecked());
	OPT_SET("Tool/Select Lines/Match/Dialogue")->SetBool(apply_to_dialogue->IsChecked());
	OPT_SET("Tool/Select Lines/Match/Comment")->SetBool(apply_to_comments->IsChecked());
}

void DialogSelection::Process(wxCommandEvent& event) {
	std::set<int> matches;

	try {
		matches = process(
			from_wx(match_text->GetValue()), case_sensitive->IsChecked(),
			static_cast<Mode>(match_mode->GetSelection()), condition->GetSelection() == 1,
			apply_to_comments->IsChecked(), apply_to_dialogue->IsChecked(),
			dialogue_field->GetSelection(), &con->subtitleAccess->Read());
	}
	catch (agi::Exception const&) {
		if (event.GetId() == wxID_OK) Close();
		return;
	}

	auto action = static_cast<Action>(selection_change_type->GetSelection());

	std::set<int> old_sel, new_sel;
	if (action != Action::SET)
		old_sel = con->selectionController->SelectedIDs();

	wxString message;
	size_t count = 0;
	switch (action) {
		case Action::SET:
			new_sel = std::move(matches);
			message = (count = new_sel.size())
				? fmt_plural(count, "Selection was set to one line", "Selection was set to %u lines", count)
				: _("Selection was set to no lines");
			break;

		case Action::ADD:
			boost::set_union(old_sel, matches, inserter(new_sel, new_sel.begin()));
			message = (count = new_sel.size() - old_sel.size())
				? fmt_plural(count, "One line was added to selection", "%u lines were added to selection", count)
				: _("No lines were added to selection");
			break;

		case Action::SUB:
			boost::set_difference(old_sel, matches, inserter(new_sel, new_sel.begin()));
			goto sub_message;

		case Action::INTERSECT:
			boost::set_intersection(old_sel, matches, inserter(new_sel, new_sel.begin()));
			sub_message:
			message = (count = old_sel.size() - new_sel.size())
				? fmt_plural(count, "One line was removed from selection", "%u lines were removed from selection", count)
				: _("No lines were removed from selection");
			break;
	}

	if (count == 0)
		wxMessageBox(message, _("Selection"), wxOK | wxCENTER, this);
	else if (con->frame)
		con->frame->StatusTimeout(message);

	int new_active = con->selectionController->ActiveID();
	if (new_sel.size() && !new_sel.count(new_active))
		new_active = *new_sel.begin();
	con->selectionController->SetSelectionAndActiveIDs(std::move(new_sel), new_active);

	if (event.GetId() == wxID_OK) Close();
}

void DialogSelection::OnDialogueCheckbox(FlatCheck *chk) {
	if(!apply_to_dialogue->IsChecked() && !apply_to_comments->GetValue())
		chk->SetValue(true);
}
}

void ShowSelectLinesDialog(agi::Context *c) {
	c->dialog->Show<DialogSelection>(c);
}

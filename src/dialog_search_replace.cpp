// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
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

/// @file dialog_search_replace.cpp
/// @brief Find and Search/replace dialogue box and logic
/// @ingroup secondary_ui
///

#include "dialog_search_replace.h"

#include "compat.h"
#include "dialog_manager.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "search_replace_engine.h"
#include "utils.h"
#include "validators.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "theme.h"


#include <functional>

#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/valgen.h>

template<bool has_replace>
DialogSearchReplace<has_replace>::DialogSearchReplace(agi::Context* c)
: wxDialog(c->parent, -1, has_replace ? _("Replace") : _("Find"))
, c(c)
, settings(std::make_unique<SearchReplaceSettings>())
{
	auto recent_find(lagi_MRU_wxAS("Find"));
	auto recent_replace(lagi_MRU_wxAS("Replace"));

	settings->field = static_cast<SearchReplaceSettings::Field>(OPT_GET("Tool/Search Replace/Field")->GetInt());
	settings->limit_to = static_cast<SearchReplaceSettings::Limit>(OPT_GET("Tool/Search Replace/Affect")->GetInt());
	settings->find = recent_find.empty() ? std::string() : from_wx(recent_find.front());
	settings->replace_with = recent_replace.empty() ? std::string() : from_wx(recent_replace.front());
	settings->match_case = OPT_GET("Tool/Search Replace/Match Case")->GetBool();
	settings->use_regex = OPT_GET("Tool/Search Replace/RegExp")->GetBool();
	settings->ignore_comments = OPT_GET("Tool/Search Replace/Skip Comments")->GetBool();
	settings->skip_tags = OPT_GET("Tool/Search Replace/Skip Tags")->GetBool();
	settings->exact_match = false;

	theme::UseSurface(this);

	// What to find and what to put in its place, each with its recent ones
	auto text_field = [this](wxString const& label, std::string *value, char const *mru_name) {
		auto frame = new FlatFrame(this, label);
		auto edit = new wxTextCtrl(frame, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER | wxBORDER_NONE, StringBinder(value));
		frame->SetContent(edit, 300);
		std::string mru = mru_name;
		frame->SetChevron([this, frame, edit, mru] {
			auto recent = lagi_MRU_wxAS(mru.c_str());
			if (recent.empty()) return;
			wxMenu menu;
			for (size_t i = 0; i < recent.size(); ++i)
				menu.Append(wxID_HIGHEST + 1 + i, recent[i]);
			int chosen = frame->GetPopupMenuSelectionFromUser(menu, 0, frame->GetSize().y);
			size_t index = chosen - wxID_HIGHEST - 1;
			if (chosen <= wxID_HIGHEST || index >= recent.size()) return;
			edit->ChangeValue(recent[index]);
			edit->SetFocus();
			edit->SelectAll();
		});
		return std::make_pair(frame, edit);
	};
	auto [find_frame, find] = text_field(_("Find what:").BeforeLast(':'), &settings->find, "Find");
	find_edit = find;
	auto fields = new wxBoxSizer(wxVERTICAL);
	fields->Add(find_frame, wxSizerFlags().Expand());
	if (has_replace) {
		auto [replace_frame, replace] = text_field(_("Replace with:").BeforeLast(':'), &settings->replace_with, "Replace");
		replace_edit = replace;
		fields->Add(replace_frame, wxSizerFlags().Expand().Border(wxTOP, FromDIP(6)));
	}

	// How to match
	match_case = new FlatCheck(this, _("&Match case"));
	use_regex = new FlatCheck(this, _("&Use regular expressions"));
	skip_comments = new FlatCheck(this, _("&Skip Comments"));
	skip_tags = new FlatCheck(this, _("S&kip Override Tags"));
	match_case->SetValue(settings->match_case);
	use_regex->SetValue(settings->use_regex);
	skip_comments->SetValue(settings->ignore_comments);
	skip_tags->SetValue(settings->skip_tags);
	auto checks = new wxGridSizer(2, FromDIP(6), FromDIP(16));
	for (auto check : {match_case, use_regex, skip_comments, skip_tags})
		checks->Add(check);

	// Where to look
	field = new FlatSwitch(this, {_("&Text"), _("St&yle"), _("A&ctor"), _("&Effect")});
	field->SetSelection(static_cast<int>(settings->field));
	limit_to = new FlatSwitch(this, {_("A&ll rows"), _("Selected &rows")});
	limit_to->SetSelection(static_cast<int>(settings->limit_to));
	auto dim = [this](wxString const& text) {
		auto label = new wxStaticText(this, -1, text);
		label->SetForegroundColour(flat::Colours(this).dim);
		return label;
	};
	auto where = new wxFlexGridSizer(2, FromDIP(6), FromDIP(10));
	where->AddGrowableCol(1, 1);
	where->Add(dim(_("In Field")), wxSizerFlags().CenterVertical());
	where->Add(field, wxSizerFlags().Expand());
	where->Add(dim(_("Limit to")), wxSizerFlags().CenterVertical());
	where->Add(limit_to, wxSizerFlags().Expand());

	auto left_sizer = new wxBoxSizer(wxVERTICAL);
	left_sizer->Add(fields, wxSizerFlags().Expand());
	left_sizer->Add(checks, wxSizerFlags().Expand().Border(wxTOP, FromDIP(12)));
	left_sizer->Add(where, wxSizerFlags().Expand().Border(wxTOP, FromDIP(12)));

	// What to do, the first filled as Enter in the find field does it
	auto find_next = new FlatButton(this, _("&Find next"), wxID_ANY, true);
	auto replace_next = new FlatButton(this, _("Replace &next"));
	auto replace_all = new FlatButton(this, _("Replace &all"));
	auto close = new FlatButton(this, _("Close"), wxID_CANCEL);
	auto button_sizer = new wxBoxSizer(wxVERTICAL);
	for (auto button : {find_next, replace_next, replace_all}) {
		button->SetMinSize(wxSize(std::max(FromDIP(110), button->GetBestSize().x), FromDIP(28)));
		button_sizer->Add(button, wxSizerFlags().Expand().Border(wxBOTTOM, FromDIP(6)));
	}
	button_sizer->AddStretchSpacer(1);
	close->SetMinSize(wxSize(std::max(FromDIP(110), close->GetBestSize().x), FromDIP(28)));
	button_sizer->Add(close, wxSizerFlags().Expand());
	if (!has_replace) {
		button_sizer->Hide(replace_next);
		button_sizer->Hide(replace_all);
	}

	auto top_sizer = new wxBoxSizer(wxHORIZONTAL);
	top_sizer->Add(left_sizer, wxSizerFlags(1).Expand());
	top_sizer->Add(button_sizer, wxSizerFlags().Expand().Border(wxLEFT, FromDIP(16)));

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(top_sizer, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(10)));
	SetSizerAndFit(main_sizer);
	CenterOnParent();
	flat::UseFlatButtonKeys(this);

	TransferDataToWindow();
	find_edit->SetFocus();
	find_edit->SelectAll();

	find_edit->Bind(wxEVT_TEXT_ENTER, std::bind(&DialogSearchReplace::FindReplace, this, &SearchReplaceEngine::FindNext));
	if (has_replace)
	  replace_edit->Bind(wxEVT_TEXT_ENTER, std::bind(&DialogSearchReplace::FindReplace, this, &SearchReplaceEngine::ReplaceNext));
	find_next->Bind(wxEVT_BUTTON, std::bind(&DialogSearchReplace::FindReplace, this, &SearchReplaceEngine::FindNext));
	replace_next->Bind(wxEVT_BUTTON, std::bind(&DialogSearchReplace::FindReplace, this, &SearchReplaceEngine::ReplaceNext));
	replace_all->Bind(wxEVT_BUTTON, std::bind(&DialogSearchReplace::FindReplace, this, &SearchReplaceEngine::ReplaceAll));
}

template<bool has_replace>
void DialogSearchReplace<has_replace>::ReadChoices() {
	settings->match_case = match_case->GetValue();
	settings->use_regex = use_regex->GetValue();
	settings->ignore_comments = skip_comments->GetValue();
	settings->skip_tags = skip_tags->GetValue();
	settings->field = static_cast<SearchReplaceSettings::Field>(field->GetSelection());
	settings->limit_to = static_cast<SearchReplaceSettings::Limit>(limit_to->GetSelection());
}

template<bool has_replace>
void DialogSearchReplace<has_replace>::FindReplace(bool (SearchReplaceEngine::*func)()) {
	TransferDataFromWindow();
	ReadChoices();

	if (settings->find.empty())
		return;

	c->search->Configure(*settings);
	try {
		((*c->search).*func)();
	}
	catch (std::exception const& e) {
		wxMessageBox(to_wx(e.what()), _("Error"), wxOK | wxICON_ERROR | wxCENTER, this);
		return;
	}

	config::mru->Add("Find", settings->find);
	if (has_replace)
		config::mru->Add("Replace", settings->replace_with);

	OPT_SET("Tool/Search Replace/Match Case")->SetBool(settings->match_case);
	OPT_SET("Tool/Search Replace/RegExp")->SetBool(settings->use_regex);
	OPT_SET("Tool/Search Replace/Skip Comments")->SetBool(settings->ignore_comments);
	OPT_SET("Tool/Search Replace/Skip Tags")->SetBool(settings->skip_tags);
	OPT_SET("Tool/Search Replace/Field")->SetInt(static_cast<int>(settings->field));
	OPT_SET("Tool/Search Replace/Affect")->SetInt(static_cast<int>(settings->limit_to));
}

template<bool replace>
void ShowSearchReplaceDialog(agi::Context *context) {
	auto other = context->dialog->Get<DialogSearchReplace<!replace>>();
	if (other != nullptr) {
		other->Close();
	}

	context->dialog->Show<DialogSearchReplace<replace>>(context);
	auto dialog = context->dialog->Get<DialogSearchReplace<replace>>();

	dialog->find_edit->SetFocus();
	dialog->find_edit->SelectAll();
	dialog->Raise();
}

void ShowSearchReplaceDialog(agi::Context *context, bool replace) {
	if (replace) {
		ShowSearchReplaceDialog<true>(context);
	} else {
		ShowSearchReplaceDialog<false>(context);
	}
}

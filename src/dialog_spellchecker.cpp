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

#include "ass_dialogue.h"
#include "utils.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "compat.h"
#include "dialog_manager.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "flat_list.h"
#include "include/aegisub/context.h"
#include "include/aegisub/spellchecker.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "selection_controller.h"
#include "text_selection_controller.h"
#include "theme.h"

#include <libaegisub/ass/dialogue_parser.h>
#include <libaegisub/exception.h>
#include <libaegisub/spellchecker.h>

#include <boost/locale/conversion.hpp>
#include <map>
#include <memory>
#include <set>
#include <wx/arrstr.h>
#include <wx/dialog.h>
#include <wx/intl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
class DialogSpellChecker final : public wxDialog {
	agi::Context *context; ///< The project context
	std::unique_ptr<agi::SpellChecker> spellchecker; ///< The spellchecking engine

	/// Words which the user has indicated should always be corrected
	std::map<std::string, std::string> auto_replace;

	/// Words which the user has temporarily added to the dictionary
	std::set<std::string> auto_ignore;

	/// Dictionaries available
	wxArrayString dictionary_lang_codes;

	int word_start; ///< Start index of the current misspelled word
	int word_len;   ///< Length of the current misspelled word

	wxTextCtrl *orig_word;    ///< The word being corrected
	wxTextCtrl *replace_word; ///< The replacement that will be used if "Replace" is clicked
	FlatList *suggest_list;   ///< The list of suggested replacements

	FlatChoice *language;      ///< The list of available languages
	FlatButton *add_button;    ///< Add word to currently active dictionary
	FlatButton *remove_button; ///< Remove word from currently active dictionary

	int start_line_id = 0;  ///< The first line checked
	int active_line_id = 0; ///< The most recently checked line
	bool has_looped = false;            ///< Has the search already looped from the end to beginning?

	/// Find the next misspelled word and close the dialog if there are none
	/// @return Are there any more misspelled words?
	bool FindNext();

	/// Check a single line for misspellings
	/// @param active_line Line to check
	/// @param start_pos Index in the line to start at
	/// @param[in,out] commit_id Commit id for coalescing autoreplace commits
	/// @return Was a misspelling found?
	bool CheckLine(AssDialogue const* active_line, int start_pos, int *commit_id, agi::SubtitleEditGate::WriteLease const&);

	/// Set the current word to be corrected
	void SetWord(std::string const& word);
	/// Correct the currently selected word
	void Replace();

	void OnChangeLanguage();
	void OnChangeSuggestion(wxCommandEvent&);

	void OnReplace(wxCommandEvent&);

public:
	DialogSpellChecker(agi::Context *context);
};

DialogSpellChecker::DialogSpellChecker(agi::Context *context)
: wxDialog(context->parent, -1, _("Spell Checker"))
, context(context)
, spellchecker(SpellCheckerFactory::GetSpellChecker())
{
	SetIcons(GETICONS(spellcheck_toolbutton));
	theme::UseSurface(this);
	int gap = FromDIP(10), row = FromDIP(6);

	if (!spellchecker) {
		wxMessageBox(_("No spellchecker available."), _("Error"), wxOK | wxICON_ERROR | wxCENTER);
		throw agi::UserCancelException("No spellchecker available");
	}
	dictionary_lang_codes = to_wx(spellchecker->GetLanguageList());
	if (dictionary_lang_codes.empty()) {
		wxMessageBox(_("No spellchecker dictionaries available."), _("Error"), wxOK | wxICON_ERROR | wxCENTER);
		throw agi::UserCancelException("No spellchecker dictionaries available");
	}

	// The misspelled word and what replaces it, each named in its field
	auto orig_frame = new FlatFrame(this, _("Misspelled word:").BeforeLast(':'));
	orig_word = new wxTextCtrl(orig_frame, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxBORDER_NONE);
	orig_frame->SetContent(orig_word, 200);
	orig_frame->SetDimContent(true);
	auto replace_frame = new FlatFrame(this, _("Replace with:").BeforeLast(':'));
	replace_word = new wxTextCtrl(replace_frame, -1, "", wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	replace_frame->SetContent(replace_word, 200);
	replace_word->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
		remove_button->Enable(spellchecker->CanRemoveWord(from_wx(replace_word->GetValue())));
	});

	// The suggestions: a click takes one, a double click replaces with it
	suggest_list = new FlatList(this, false, false, _("No suggestions"));
	suggest_list->SetMinSize(DipSize(300, 150));
	suggest_list->Bind(wxEVT_LISTBOX, &DialogSpellChecker::OnChangeSuggestion, this);
	suggest_list->Bind(wxEVT_LISTBOX_DCLICK, &DialogSpellChecker::OnReplace, this);

	// The dictionary's language
	std::vector<wxString> language_names(dictionary_lang_codes.begin(), dictionary_lang_codes.end());
	for (size_t i = 0; i < dictionary_lang_codes.size(); ++i) {
		if (const wxLanguageInfo *info = wxLocale::FindLanguageInfo(dictionary_lang_codes[i]))
			language_names[i] = info->Description;
	}
	language = new FlatChoice(this, _("Language"));
	language->Set(language_names);
	wxString cur_lang = to_wx(OPT_GET("Tool/Spell Checker/Language")->GetString());
	int cur_lang_index = dictionary_lang_codes.Index(cur_lang);
	if (cur_lang_index == wxNOT_FOUND) cur_lang_index = dictionary_lang_codes.Index("en");
	if (cur_lang_index == wxNOT_FOUND) cur_lang_index = dictionary_lang_codes.Index("en_US");
	if (cur_lang_index == wxNOT_FOUND) cur_lang_index = 0;
	language->SetStringSelection(language_names[cur_lang_index]);
	language->SetToolTip(_("Language"));
	language->on_choose = [this](int) { OnChangeLanguage(); };

	auto left = new wxBoxSizer(wxVERTICAL);
	left->Add(orig_frame, wxSizerFlags().Expand());
	left->Add(replace_frame, wxSizerFlags().Expand().Border(wxTOP, row));
	left->Add(suggest_list, wxSizerFlags(1).Expand().Border(wxTOP, row));
	left->Add(language, wxSizerFlags().Expand().Border(wxTOP, row));

	// What to skip, then what to do with the word
	auto actions = new wxBoxSizer(wxVERTICAL);
	auto make_checkbox = [&](wxString const& text, const char *opt) {
		auto checkbox = new FlatCheck(this, text);
		actions->Add(checkbox, wxSizerFlags().Border(wxBOTTOM, row));
		checkbox->SetValue(OPT_GET(opt)->GetBool());
		checkbox->Bind(wxEVT_CHECKBOX, [=](wxCommandEvent &) { OPT_SET(opt)->SetBool(checkbox->GetValue()); });
	};
	make_checkbox(_("&Skip Comments"), "Tool/Spell Checker/Skip Comments");
	make_checkbox(_("Ignore &UPPERCASE words"), "Tool/Spell Checker/Skip Uppercase");
	actions->AddSpacer(row);
	auto add_action = [&](FlatButton *button, std::function<void()> action) {
		button->SetMinSize(wxSize(std::max(FromDIP(150), button->GetBestSize().x), FromDIP(28)));
		button->Bind(wxEVT_BUTTON, [action](wxCommandEvent &) { action(); });
		actions->Add(button, wxSizerFlags().Expand().Border(wxBOTTOM, row));
		return button;
	};
	add_action(new FlatButton(this, _("&Replace"), wxID_ANY, true), [this] { Replace(); FindNext(); });
	add_action(new FlatButton(this, _("Replace &all")), [this] {
		auto_replace[from_wx(orig_word->GetValue())] = from_wx(replace_word->GetValue());
		Replace();
		FindNext();
	});
	add_action(new FlatButton(this, _("&Ignore")), [this] { FindNext(); });
	add_action(new FlatButton(this, _("Ignore a&ll")), [this] {
		auto_ignore.insert(from_wx(orig_word->GetValue()));
		FindNext();
	});
	add_button = add_action(new FlatButton(this, _("Add to &dictionary")), [this] {
		spellchecker->AddWord(from_wx(orig_word->GetValue()));
		FindNext();
	});
	remove_button = add_action(new FlatButton(this, _("Remove fro&m dictionary")), [this] {
		spellchecker->RemoveWord(from_wx(replace_word->GetValue()));
		SetWord(from_wx(orig_word->GetValue()));
	});

	auto top = new wxBoxSizer(wxHORIZONTAL);
	top->Add(left, wxSizerFlags(1).Expand());
	top->Add(actions, wxSizerFlags().Expand().Border(wxLEFT, gap));

	auto buttons = new flat::DialogButtons(this, wxCANCEL | wxHELP, "Spell Checker");
	buttons->Get(wxID_CANCEL)->SetLabel(_("Close"));

	wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(top, wxSizerFlags(1).Expand().Border(wxALL, gap));
	main_sizer->Add(buttons, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, gap));
	SetSizerAndFit(main_sizer);
	CenterOnParent();

	if (FindNext())
		Show();
}

void DialogSpellChecker::OnReplace(wxCommandEvent&) {
	Replace();
	FindNext();
}

void DialogSpellChecker::OnChangeLanguage() {
	wxString code = dictionary_lang_codes[language->GetSelection()];
	OPT_SET("Tool/Spell Checker/Language")->SetString(from_wx(code));

	FindNext();
}

void DialogSpellChecker::OnChangeSuggestion(wxCommandEvent&) {
	replace_word->SetValue(suggest_list->GetStringSelection());
}

bool DialogSpellChecker::FindNext() {
    auto edit_scope = context->subtitleAccess->TryWrite();
    if (!edit_scope) return false;
	AssDialogue const* real_active_line = context->selectionController->ReadActiveLine();
	// User has changed the active line; restart search from this position
	if (!real_active_line) return false;
    bool start_exists = std::any_of(context->subtitleAccess->Read().Events.begin(), context->subtitleAccess->Read().Events.end(),
        [&](auto const& row) { return row.Id == start_line_id; });
    if (real_active_line->Id != active_line_id || !start_exists) {
        active_line_id = real_active_line->Id;
		has_looped = false;
		start_line_id = active_line_id;
	}

	int start_pos = context->textSelectionController->GetInsertionPoint();
	int commit_id = -1;

	if (CheckLine(real_active_line, start_pos, &commit_id, edit_scope->Lease()))
		return true;

	auto it = context->subtitleAccess->Read().iterator_to(*real_active_line);

	// Note that it is deliberate that the start line is checked twice, as if
	// the cursor is past the first misspelled word in the current line, that
	// word should be hit last
	while(!has_looped || active_line_id != start_line_id) {
		// Wrap around to the beginning if we hit the end
		if (++it == context->subtitleAccess->Read().Events.end()) {
			it = context->subtitleAccess->Read().Events.begin();
			has_looped = true;
		}

		active_line_id = it->Id;
		if (CheckLine(&*it, 0, &commit_id, edit_scope->Lease()))
			return true;
	}

	edit_scope.reset(); // C1-C1c-13 (audit B6): no lease across the message boxes
	if (IsShown()) {
		wxMessageBox(_("Aegisub has finished checking spelling of this script."), _("Spell checking complete."));
		Close();
	}
	else {
		wxMessageBox(_("Aegisub has found no spelling mistakes in this script."), _("Spell checking complete."));
		throw agi::UserCancelException("No spelling mistakes");
	}

	return false;
}

bool DialogSpellChecker::CheckLine(AssDialogue const* active_line, int start_pos, int *commit_id, agi::SubtitleEditGate::WriteLease const& lease) {
	if (active_line->Comment && OPT_GET("Tool/Spell Checker/Skip Comments")->GetBool()) return false;

	std::string text = active_line->Text;
	auto tokens = agi::ass::TokenizeDialogueBody(text);
	agi::ass::SplitWords(text, tokens);

	bool ignore_uppercase = OPT_GET("Tool/Spell Checker/Skip Uppercase")->GetBool();

	word_start = 0;
	for (auto const& tok : tokens) {
		if (tok.type != agi::ass::DialogueTokenType::WORD || word_start < start_pos) {
			word_start += tok.length;
			continue;
		}

		word_len = tok.length;
		std::string word = text.substr(word_start, word_len);

		if (auto_ignore.count(word) || spellchecker->CheckWord(word) || (ignore_uppercase && word == boost::locale::to_upper(word))) {
			word_start += tok.length;
			continue;
		}

		auto auto_rep = auto_replace.find(word);
		if (auto_rep == auto_replace.end()) {
#ifdef __WXGTK__
			// http://trac.wxwidgets.org/ticket/14369
			orig_word->Remove(0, -1);
			replace_word->Remove(0, -1);
#endif

            auto current = std::find_if(context->subtitleAccess->Read().Events.begin(), context->subtitleAccess->Read().Events.end(),
                [&](AssDialogue const& row) { return row.Id == active_line->Id; });
            if (current == context->subtitleAccess->Read().Events.end()) return false;
            context->selectionController->SetSelectionAndActiveIDs({current->Id}, current->Id);
			SetWord(word);
			return true;
		}

		text.replace(word_start, word_len, auto_rep->second);
        context->subtitleAccess->Edit(lease).SetDialogueField(active_line->Id, &AssDialogueBase::Text, text);
		*commit_id = context->subtitleAccess->Edit(lease).Commit(_("spell check replace"), AssFile::COMMIT_DIAG_TEXT, *commit_id);
		word_start += auto_rep->second.size();
	}
	return false;
}

void DialogSpellChecker::Replace() {
    auto edit_scope = context->subtitleAccess->TryWrite();
    if (!edit_scope) return;
	AssDialogue const* active_line = context->selectionController->ReadActiveLine();
	if (!active_line) return; // C1-C1c-14 (audit C7)

	// Only replace if the user hasn't changed the selection to something else
	if (to_wx(active_line->Text.get().substr(word_start, word_len)) == orig_word->GetValue()) {
		std::string text = active_line->Text;
		text.replace(word_start, word_len, from_wx(replace_word->GetValue()));
		context->subtitleAccess->Edit(edit_scope->Lease()).SetDialogueField(active_line->Id,&AssDialogueBase::Text,text);
		context->subtitleAccess->Edit(edit_scope->Lease()).Commit(_("spell check replace"), AssFile::COMMIT_DIAG_TEXT);
		context->textSelectionController->SetInsertionPoint(word_start + replace_word->GetValue().size());
	}
}

void DialogSpellChecker::SetWord(std::string const& word) {
	orig_word->SetValue(to_wx(word));

	wxArrayString suggestions = to_wx(spellchecker->GetSuggestions(word));
	replace_word->SetValue(suggestions.size() ? suggestions[0] : to_wx(word));
	suggest_list->Set(suggestions);

	context->textSelectionController->SetSelection(word_start, word_start + word_len);
	context->textSelectionController->SetInsertionPoint(word_start + word_len);

	add_button->Enable(spellchecker->CanAddWord(word));
}
}

void ShowSpellcheckerDialog(agi::Context *c) {
	c->dialog->Show<DialogSpellChecker>(c);
}

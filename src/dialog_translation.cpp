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

/// @file dialog_translation.cpp
/// @brief Translation Assistant dialogue box and logic
/// @ingroup tools_ui
///

#include "dialog_translation.h"
#include "utils.h"
#include "subtitle_dialog_edit.h"

#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "libaegisub/unicode.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "command/command.h"
#include "compat.h"
#include "format.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "persist_location.h"
#include "project.h"
#include "subs_edit_ctrl.h"
#include "theme.h"
#include "selection_controller.h"
#include "video_controller.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>

static void add_hotkey(wxSizer *sizer, wxWindow *parent, const char *command, wxString const& text) {
	auto name = new wxStaticText(parent, -1, text);
	name->SetForegroundColour(flat::Colours(parent).dim);
	sizer->Add(name);
	sizer->Add(new wxStaticText(parent, -1, to_wx(hotkey::get_hotkey_str_first("Translation Assistant", command))));
}

// Skip over override blocks, comments, and whitespace between blocks
static bool bad_block(std::unique_ptr<AssDialogueBlock> &block) {
	bool is_whitespace = agi::unicode::is_whitespace(block->GetText());
	return block->GetType() != AssBlockType::PLAIN || (is_whitespace && OPT_GET("Tool/Translation Assistant/Skip Whitespace")->GetBool());
}

DialogTranslation::DialogTranslation(agi::Context *c)
: wxDialog(c->parent, -1, _("Translation Assistant"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMINIMIZE_BOX)
, c(c)
, file_change_connection(c->subtitleAccess->Read().AddCommitListener("dialog_translation.AddCommitListener", [this](int type, AssDialogue const*, SubtitleModelRebindTicket ticket) { OnExternalCommit(type); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)))
, active_line_connection(c->selectionController->AddActiveLineListener("dialog_translation.AddActiveLineListener", [this](SubtitleModelRebindTicket ticket) { OnActiveLineChanged(ticket.Current()); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)))
, edit(std::make_unique<SubtitleDialogEdit>(c))
, line_count(c->subtitleAccess->Read().Events.size())
{
	SetIcons(GETICONS(translation_toolbutton));
	Bind(wxEVT_SHOW,&DialogTranslation::OnShow,this);

	theme::UseSurface(this);
	wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
	int gap = FromDIP(10);

	// The line's text, its number beside the title
	auto original_title = new wxBoxSizer(wxHORIZONTAL);
	original_title->Add(flat::SectionTitle(this, _("Original")), wxSizerFlags(1).CenterVertical());
	line_number_display = new wxStaticText(this, -1, "");
	line_number_display->SetForegroundColour(flat::Colours(this).dim);
	original_title->Add(line_number_display, wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(8)));
	main_sizer->Add(original_title, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));

	original_text = new wxStyledTextCtrl(this, -1, wxDefaultPosition, DipSize(320, 80));
	original_text->SetWrapMode(wxSTC_WRAP_WORD);
	original_text->SetMarginWidth(1, 0);
	original_text->StyleSetForeground(1, wxColour(10, 60, 200));
	original_text->SetReadOnly(true);
	main_sizer->Add(original_text, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));

	main_sizer->Add(flat::SectionTitle(this, _("Translation")), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	translated_text = new SubsTextEditCtrl(this, DipSize(320, 80), 0, nullptr);
	translated_text->SetWrapMode(wxSTC_WRAP_WORD);
	translated_text->SetMarginWidth(1, 0);
	translated_text->SetFocus();
	translated_text->Bind(wxEVT_CHAR_HOOK, &DialogTranslation::OnKeyDown, this);
	translated_text->CmdKeyAssign(wxSTC_KEY_RETURN, wxSTC_KEYMOD_SHIFT, wxSTC_CMD_NEWLINE);
	main_sizer->Add(translated_text, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));

	// The keys, in two columns of names and keys
	main_sizer->Add(flat::SectionTitle(this, _("Keys")), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	auto hotkey_grid = new wxFlexGridSizer(4, FromDIP(4), FromDIP(10));
	hotkey_grid->AddGrowableCol(1, 1);
	hotkey_grid->AddGrowableCol(3, 1);
	add_hotkey(hotkey_grid, this, "tool/translation_assistant/commit", _("Accept changes"));
	add_hotkey(hotkey_grid, this, "tool/translation_assistant/preview", _("Preview changes"));
	add_hotkey(hotkey_grid, this, "tool/translation_assistant/prev", _("Previous line"));
	add_hotkey(hotkey_grid, this, "tool/translation_assistant/next", _("Next line"));
	add_hotkey(hotkey_grid, this, "tool/translation_assistant/insert_original", _("Insert original"));
	add_hotkey(hotkey_grid, this, "video/play/line", _("Play video"));
	add_hotkey(hotkey_grid, this, "audio/play/selection", _("Play audio"));
	add_hotkey(hotkey_grid, this, "edit/line/delete", _("Delete line"));
	main_sizer->Add(hotkey_grid, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));

	seek_video = new FlatCheck(this, _("Enable &preview"));
	seek_video->SetValue(true);
	main_sizer->Add(seek_video, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, gap));

	// Playing beside Cancel at the foot
	auto buttons = new flat::DialogButtons(this, wxCANCEL | wxHELP, "Translation Assistant");
	auto play_video = buttons->AddRight(_("Play &Video"));
	play_video->Enable(!!c->project->VideoProvider());
	play_video->Bind(wxEVT_BUTTON, &DialogTranslation::OnPlayVideoButton, this);
	auto play_audio = buttons->AddRight(_("Play &Audio"));
	play_audio->Enable(!!c->project->AudioProvider());
	play_audio->Bind(wxEVT_BUTTON, &DialogTranslation::OnPlayAudioButton, this);
	main_sizer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, gap));

	SetSizerAndFit(main_sizer);

	persist = std::make_unique<PersistLocation>(this, "Tool/Translation Assistant");

	Bind(wxEVT_KEY_DOWN, &DialogTranslation::OnKeyDown, this);

	edit->ResetActive();
	if(edit->Rows().empty())throw NothingToTranslate();
	active_line=std::make_unique<AssDialogue>(edit->Rows().front());
	translated_text->Bind(wxEVT_STC_MODIFIED,[this](wxStyledTextEvent& e){
		if(!updating_display && (e.GetModificationType() & (wxSTC_MOD_INSERTTEXT|wxSTC_MOD_DELETETEXT)))edit->Begin();
	});
	blocks = active_line->ParseTags();
	if (bad_block(blocks[0])) {
		if (!NextBlock())
			throw NothingToTranslate();
	}
	else
		UpdateDisplay();
}

DialogTranslation::~DialogTranslation() {
	Unbind(wxEVT_SHOW,&DialogTranslation::OnShow,this);
	edit->End();
}

void DialogTranslation::OnActiveLineChanged(bool installing) try {
	if (switching_lines || !edit->Alive()) return;
	edit->ResetActive();
	if(edit->Rows().empty()){edit->Invalidate();return;}
	active_line=std::make_unique<AssDialogue>(edit->Rows().front());
	blocks = active_line->ParseTags();
	cur_block = 0;

	if (!installing && bad_block(blocks[cur_block]) && !NextBlock()) {
		wxMessageBox(_("No more lines to translate."));
		EndModal(1);
	}
	else UpdateDisplay();
}
catch (...) {edit->Invalidate();throw;}

void DialogTranslation::OnExternalCommit(int) {
	// Keep the user's independent draft, but never rebase it onto changed data.
	edit->Invalidate();
}

bool DialogTranslation::NextBlock() try {
	if(!edit->Alive())return false;
	switching_lines = true;
	BOOST_SCOPE_EXIT_ALL(this){switching_lines=false;};
	edit->End();
	do {
		if (cur_block == blocks.size() - 1) {
			c->selectionController->NextLine();
			auto id=SubtitleDialogEdit::ActiveID(c);
			if(!id || id==active_line->Id)return false;
			edit->ResetActive();
			active_line=std::make_unique<AssDialogue>(edit->Rows().front());
			line_count=c->subtitleAccess->Read().Events.size();
			blocks = active_line->ParseTags();
			cur_block = 0;
		}
		else
			++cur_block;
	} while (bad_block(blocks[cur_block]));
	switching_lines = false;

	UpdateDisplay();
	return true;
}
catch (...) {edit->Invalidate();throw;}

bool DialogTranslation::PrevBlock() try {
	if(!edit->Alive())return false;
	switching_lines = true;
	BOOST_SCOPE_EXIT_ALL(this){switching_lines=false;};
	edit->End();
	do {
		if (cur_block == 0) {
			c->selectionController->PrevLine();
			auto id=SubtitleDialogEdit::ActiveID(c);
			if(!id || id==active_line->Id)return false;
			edit->ResetActive();
			active_line=std::make_unique<AssDialogue>(edit->Rows().front());
			line_count=c->subtitleAccess->Read().Events.size();
			blocks = active_line->ParseTags();
			cur_block = blocks.size() - 1;
		}
		else
			--cur_block;
	} while (bad_block(blocks[cur_block]));
	switching_lines = false;

	UpdateDisplay();
	return true;
}
catch (...) {edit->Invalidate();throw;}

void DialogTranslation::UpdateDisplay() {
	if(!edit->Alive())return;
	updating_display=true;
	BOOST_SCOPE_EXIT_ALL(this){updating_display=false;};
	line_number_display->SetLabel(fmt_tl("Current line: %d/%d", active_line->Row, line_count));

	original_text->SetReadOnly(false);
	original_text->ClearAll();

	size_t i = 0;
	for (auto& block : blocks) {
		if (block->GetType() == AssBlockType::PLAIN) {
			int initial_pos = original_text->GetLength();
			original_text->AppendTextRaw(block->GetText().c_str());
			if (i == cur_block) {
				original_text->StartStyling(initial_pos);
				original_text->SetStyling(block->GetText().size(), 1);
			}
		}
		else
			original_text->AppendTextRaw(block->GetText().c_str());
		++i;
	}

	original_text->SetReadOnly(true);

	if (seek_video->IsChecked()) c->videoController->JumpToTime(active_line->Start);

	translated_text->ClearAll();
	translated_text->SetFocus();
}

void DialogTranslation::Commit(bool next) try {
	if(!edit->Alive() || !active_line)return;
	std::string new_value = translated_text->GetTextRaw().data();
	boost::replace_all(new_value, "\r\n", "\\N");
	boost::replace_all(new_value, "\r", "\\N");
	boost::replace_all(new_value, "\n", "\\N");
	AssDialogue candidate(static_cast<AssDialogueBase const&>(*active_line));
	if(cur_block>=blocks.size())return;
	auto replacement=std::make_unique<AssDialogueBlockPlain>(new_value);
	std::string text;
	for(size_t i=0;i<blocks.size();++i)text+=i==cur_block?new_value:blocks[i]->GetText();
	candidate.Text=std::move(text);
	file_change_connection.Block();
	BOOST_SCOPE_EXIT_ALL(this){file_change_connection.Unblock();};
	if(!edit->Commit({AssDialogueBase(candidate)},_("translation assistant"),AssFile::COMMIT_DIAG_TEXT)) {
		wxMessageBox(_("The subtitle or its dependencies changed. Select the line again to restart."),_("Translation Assistant"),wxOK|wxICON_WARNING,this);return;
	}
	active_line=std::make_unique<AssDialogue>(edit->Rows().front());
	blocks[cur_block]=std::move(replacement);

	if (next) {
		if (!NextBlock()) {
			wxMessageBox(_("No more lines to translate."));
			EndModal(1);
		}
	}
	else {
		UpdateDisplay();
	}
}

catch (...) {edit->Invalidate();throw;}

void DialogTranslation::InsertOriginal() try {
	if(!edit->Alive() || !edit->Begin())return;
	auto const& text = blocks[cur_block]->GetText();
	translated_text->AddTextRaw(text.data(), text.size());
}
catch (...) {edit->Invalidate();throw;}

void DialogTranslation::OnKeyDown(wxKeyEvent &evt) {
	if(edit->Alive())hotkey::check("Translation Assistant", c, evt);
}

void DialogTranslation::OnPlayVideoButton(wxCommandEvent &) {
	if(edit->Alive())c->videoController->PlayLine();
	translated_text->SetFocus();
}

void DialogTranslation::OnPlayAudioButton(wxCommandEvent &) {
	if(edit->Alive())cmd::call("audio/play/selection", c);
	translated_text->SetFocus();
}

void DialogTranslation::OnShow(wxShowEvent& event) {
	if(!event.IsShown())edit->Invalidate();
	event.Skip();
}

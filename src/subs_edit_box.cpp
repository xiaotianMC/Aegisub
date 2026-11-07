// Copyright (c) 2005, Rodrigo Braz Monteiro
// Copyright (c) 2010, Thomas Goyne <plorkyeran@aegisub.org>
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

/// @file subs_edit_box.cpp
/// @brief Main subtitle editing area, including toolbars around the text control
/// @ingroup main_ui

#include "subs_edit_box.h"
#include "theme.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "base_grid.h"
#include "command/command.h"
#include "compat.h"
#include "dialogs.h"
#include "flat_controls.h"
#include "flyweight_hash.h"
#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "initial_line_state.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "subs_edit_ctrl.h"
#include "text_selection_controller.h"
#include "time_unit_toggle.h"
#include "timeedit_ctrl.h"
#include "utils.h"
#include "validators.h"

#include <libaegisub/character_count.h>
#include <libaegisub/util.h>

#include <algorithm>
#include <functional>
#include <unordered_set>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/menu.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/time.h>

#ifdef __WXMSW__
#include <wx/display.h>
#include <wx/msw/wrapwin.h>
#endif

namespace {

void time_edit_char_hook(wxKeyEvent &event) {
	// Enter: a modified event, and done with the field
	if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER) {
		TimeEdit *edit = static_cast<TimeEdit*>(event.GetEventObject());
		edit->SetValue(edit->GetValue());
		flat::LeaveField();
	}
	else
		event.Skip();
}

// Passing a pointer-to-member directly to a function sometimes does not work
// in VC++ 2015 Update 2, with it instead passing a null pointer
const auto AssDialogue_Actor = &AssDialogue::Actor;
const auto AssDialogue_Effect = &AssDialogue::Effect;
}

SubsEditBox::SubsEditBox(wxWindow *parent, agi::Context *context)
: wxPanel(parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_NONE, "SubsEditBox")
, c(context)
, undo_timer(GetEventHandler())
{
	using std::bind;
	// A block on the main window's backdrop, without a border (docs/theme.md)
	theme::UseSurface(this);

	// The fields, in two rows (docs/main-window.md): what the line is, then
	// when it is shown and on which layer. Colours, B/I/U/S and margins are
	// in the style panel (docs/style-panel.md); their commands are kept for
	// hotkeys and menus. Enter in the text goes to the next line, making
	// one after the last (grid/line/next/create).
	const int gap = FromDIP(6);
	top_sizer = new wxBoxSizer(wxHORIZONTAL);

	comment_box = new wxCheckBox(this,-1,_("&Comment"));
	comment_box->SetToolTip(_("Comment this line out. Commented lines don't show up on screen."));
#ifdef __WXGTK__
	// Only supported in wxgtk
	comment_box->SetCanFocus(false);
#endif
	top_sizer->Add(comment_box, wxSizerFlags().Center().Border(wxRIGHT, gap));

	// Its tooltip is the style's name, which may be cut short
	style_box = new FlatChoice(this, "");
	style_box->on_choose = [this](int) { OnStyleChange(); };
	top_sizer->Add(style_box, wxSizerFlags(3).Center().Border(wxRIGHT, gap));

	actor_box = MakeTextField(actor_frame, _("Actor"), _("Actor name for this speech. This is only for reference, and is mainly useless."), 70, actor_values);
	Bind(wxEVT_TEXT, &SubsEditBox::OnActorChange, this, actor_box->GetId());
	top_sizer->Add(actor_frame, wxSizerFlags(3).Center().Border(wxRIGHT, gap));

	effect_box = MakeTextField(effect_frame, _("Effect"), _("Effect for this line. This can be used to store extra information for karaoke scripts, or for the effects supported by the renderer."), 50, effect_values);
	Bind(wxEVT_TEXT, &SubsEditBox::OnEffectChange, this, effect_box->GetId());
	top_sizer->Add(effect_frame, wxSizerFlags(2).Center().Border(wxRIGHT, gap));

	split_box = new wxCheckBox(this,-1,_("Show Original"));
	split_box->SetToolTip(_("Show above the text what this line said before you changed it: its text when you moved to it, read-only, to compare or translate against. It follows when you move to another line.\n\nFour buttons come with it: back to the original, clear the line, clear the text but keep the tags, and put the original in at the cursor."));
	split_box->Bind(wxEVT_CHECKBOX, &SubsEditBox::OnSplit, this);
	top_sizer->Add(split_box, wxSizerFlags().Center());

	// On which layer, and when
	middle_left_sizer = new wxBoxSizer(wxHORIZONTAL);
	start_time = MakeTimeCtrl(wxGETTEXT_IN_CONTEXT("edit box", "Start"), _("Start time"), TIME_START);
	auto arrow = new wxStaticText(this, -1, wxS("→"));
	arrow->SetForegroundColour(flat::Colours(this).dim);
	connections.push_back(theme::AddChangeListener([this, arrow] { arrow->SetForegroundColour(flat::Colours(this).dim); arrow->Refresh(); }));
	middle_left_sizer->Add(arrow, wxSizerFlags().Center().Border(wxLEFT | wxRIGHT, gap / 2));
	end_time = MakeTimeCtrl(wxGETTEXT_IN_CONTEXT("edit box", "End"), _("End time"), TIME_END);
	middle_left_sizer->AddSpacer(gap);
	duration = MakeTimeCtrl(wxGETTEXT_IN_CONTEXT("edit box", "Duration"), _("Line duration: changing it moves the end time"), TIME_DURATION);
	static_cast<FlatFrame*>(duration->GetParent())->SetDimContent(true);
	middle_left_sizer->AddSpacer(gap);

	// Time or frames, next to the times it is about
	time_unit = new TimeUnitToggle(this);
	time_unit->on_change = [this](bool by_frame) { OnTimeUnitChange(by_frame); };
	middle_left_sizer->Add(time_unit, wxSizerFlags().Center());

	// The layer: typed, or dragged by its label, the wheel or the arrow keys
	// to move every selected line up or down from where it is
	layer_frame = new FlatFrame(this, wxGETTEXT_IN_CONTEXT("edit box", "Layer"));
	layer = new wxTextCtrl(layer_frame, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER | wxTE_CENTRE | wxBORDER_NONE);
	layer_frame->SetContent(layer, 34);
	auto layer_tip = wxString::Format(_("Layer: lines on a higher layer are drawn over lower ones. Only lines on the same layer shown at the same time move out of each other's way; on different layers they can overlap, as a box under text does.\n\nType a number, or drag the label, turn the wheel or press the arrow keys to move every selected line up or down from its own layer. %s: the selected lines are on different layers."), wxS("\u2014"));
	layer->SetToolTip(layer_tip);
	layer_frame->SetToolTip(layer_tip);
	layer->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { CommitLayerText(); });
	layer->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) { e.Skip(); CallAfter([this] { CommitLayerText(); }); });
	layer->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
		switch (e.GetKeyCode()) {
			case WXK_UP: case WXK_NUMPAD_UP: StepLayer(1); break;
			case WXK_DOWN: case WXK_NUMPAD_DOWN: StepLayer(-1); break;
			case WXK_ESCAPE: UpdateLayerField(); layer->SelectAll(); break;
			case WXK_RETURN: case WXK_NUMPAD_ENTER: CommitLayerText(); flat::LeaveField(); break;
			default: e.Skip();
		}
	});
	layer_frame->SetWheel([this](int steps) { StepLayer(steps); });
	layer_frame->SetScrub([this](int steps, bool final) { ScrubLayer(steps, final); });
	// First in the row, and first for Tab
	middle_left_sizer->Insert(0, layer_frame, wxSizerFlags().Center());
	middle_left_sizer->InsertSpacer(1, gap * 2);
	layer_frame->MoveBeforeInTabOrder(start_time->GetParent());

	fields_sizer = new wxBoxSizer(wxVERTICAL);
	fields_sizer->Add(top_sizer,0,wxEXPAND | wxALL,3);
	fields_sizer->Add(middle_left_sizer,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
	wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(fields_sizer,0,wxEXPAND);

	// Text editor
	edit_ctrl = new SubsTextEditCtrl(this, FromDIP(wxSize(300,50)), wxBORDER_NONE, c);
	edit_ctrl->Bind(wxEVT_CHAR_HOOK, &SubsEditBox::OnKeyDown, this);
	// Leaving a field (Enter, a click elsewhere) comes back to the text
	flat::SetHome(edit_ctrl);

	// The character count in its corner; the text wraps before it
	char_count = new FlatBadge(edit_ctrl);
	char_count->on_click = [this] { ShowPreferences(c->parent, _("Character Counter")); UpdateCharacterCount(CurrentLine() ? CurrentLine()->Text.get() : std::string()); };
	edit_ctrl->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) { PlaceCharacterCount(); e.Skip(); });

	// The original text, read-only above the text, under a caption saying so
	original_sizer = new wxBoxSizer(wxVERTICAL);
	original_caption = new wxStaticText(this, -1, _("Original: the line as it was when you moved to it"));
	secondary_editor = new wxTextCtrl(this, -1, "", wxDefaultPosition, FromDIP(wxSize(300,50)), wxBORDER_NONE | wxTE_MULTILINE | wxTE_READONLY);
	auto original_colours = [this] {
		auto p = flat::Colours(this);
		original_caption->SetForegroundColour(p.dim);
		secondary_editor->SetBackgroundColour(p.field);
		secondary_editor->SetForegroundColour(theme::Mix(p.field, p.text, 0.8));
		original_caption->Refresh();
		secondary_editor->Refresh();
	};
	original_colours();
	connections.push_back(theme::AddChangeListener(original_colours));
	original_sizer->Add(original_caption, 0, wxLEFT | wxTOP, 3);
	original_sizer->Add(secondary_editor, 1, wxEXPAND | wxALL, 3);

	main_sizer->Add(original_sizer,1,wxEXPAND);
	main_sizer->Add(edit_ctrl,1,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
	main_sizer->Hide(original_sizer);

	bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
	bottom_sizer->Add(MakeBottomButton("edit/revert"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/clear"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/clear/text"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/insert_original"));
	main_sizer->Add(bottom_sizer, 0, wxLEFT | wxBOTTOM, 3);
	main_sizer->Hide(bottom_sizer);

	SetSizer(main_sizer);
	ArrangeRows();
	Fit();
	rows_option = OPT_SUB("Subtitle/Edit Box/Fields Below Text", [this] { ArrangeRows(); });

	edit_ctrl->Bind(wxEVT_STC_MODIFIED, &SubsEditBox::OnChange, this);
	edit_ctrl->SetModEventMask(wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT | wxSTC_STARTACTION);

	Bind(wxEVT_CHECKBOX, &SubsEditBox::OnCommentChange, this, comment_box->GetId());

	Bind(wxEVT_CHAR_HOOK, &SubsEditBox::OnKeyDown, this);
	Bind(wxEVT_TIMER, [this](wxTimerEvent&) { commit_id = -1; });
	Bind(wxEVT_SIZE, [this](wxSizeEvent &e) { FitTimeLabels(); e.Skip(); });


	file_changed_slot = c->subtitleAccess->Read().AddCommitListener("subs_edit_box.AddCommitListener", [this](int type, AssDialogue const*, SubtitleModelRebindTicket ticket) { OnCommit(type); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime));
	for (auto& connection : agi::signal::make_vector({
		context->project->AddTimecodesListener(&SubsEditBox::UpdateFrameTiming, this),
		context->selectionController->AddActiveLineListener("subs_edit_box.AddActiveLineListener", [this](SubtitleModelRebindTicket ticket) { OnActiveLineChanged(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)),
		context->selectionController->AddSelectionListener("subs_edit_box.AddSelectionListener", [this](SubtitleModelRebindTicket ticket) { OnSelectedSetChanged(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)),
		context->initialLineState->AddChangeListener(&SubsEditBox::OnLineInitialTextChanged, this),
	 }))
		connections.push_back(std::move(connection)); // kept with the button icons' listeners

	context->textSelectionController->SetControl(edit_ctrl);
	edit_ctrl->SetFocus();

	bool show_original = OPT_GET("Subtitle/Show Original")->GetBool();
	if (show_original) {
		split_box->SetValue(true);
		DoOnSplit(true);
	}
}

SubsEditBox::~SubsEditBox() {
	c->textSelectionController->SetControl(nullptr);
}

TimeEdit *SubsEditBox::MakeTimeCtrl(wxString const& label, wxString const& tooltip, TimeField field) {
	auto frame = new FlatFrame(this, label);
	TimeEdit *ctrl = new TimeEdit(frame, -1, c, "", wxDefaultSize, field == TIME_END, wxBORDER_NONE);
	frame->SetContent(ctrl, ToDIP(ctrl->GetSizeFromText(wxS("0:00:00.000")).x));
	ctrl->SetToolTip(tooltip);
	frame->SetToolTip(tooltip);
	Bind(wxEVT_TEXT, [=, this](wxCommandEvent&) { CommitTimes(field); }, ctrl->GetId());
	ctrl->Bind(wxEVT_CHAR_HOOK, time_edit_char_hook);
	middle_left_sizer->Add(frame, wxSizerFlags().Center());
	return ctrl;
}

wxTextCtrl *SubsEditBox::MakeTextField(FlatFrame *&frame, wxString const& hint, wxString const& tooltip, int width, wxArrayString &values) {
	frame = new FlatFrame(this, "");
	auto ctrl = new wxTextCtrl(frame, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER | wxBORDER_NONE);
	ctrl->SetHint(hint);
	ctrl->SetToolTip(tooltip);
	// Enter is done with it, rather than going to the next line
	ctrl->Bind(wxEVT_CHAR_HOOK, [](wxKeyEvent &e) {
		if (e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER) flat::LeaveField();
		else e.Skip();
	});
	frame->SetToolTip(tooltip);
	frame->SetContent(ctrl, width);
	// The values the file already has, to choose from
	frame->SetChevron([this, ctrl, frame, &values] {
		if (values.empty()) return;
		wxMenu menu;
		for (size_t i = 0; i < values.size(); ++i)
			menu.AppendCheckItem(wxID_HIGHEST + 1 + i, values[i])->Check(values[i] == ctrl->GetValue());
		int chosen = frame->GetPopupMenuSelectionFromUser(menu, 0, frame->GetSize().y);
		size_t index = chosen - wxID_HIGHEST - 1;
		if (chosen <= wxID_HIGHEST || index >= values.size()) return;
		// One undo step of its own, as typing makes one
		last_commit_type.clear();
		ctrl->SetValue(values[index]);
		ctrl->SetInsertionPointEnd();
	});
	return ctrl;
}

wxButton *SubsEditBox::MakeBottomButton(const char *cmd_name) {
	cmd::Command *command = cmd::get(cmd_name);
	wxButton *btn = new wxButton(this, -1, command->StrDisplay(c));
	tool_tip_bindings.emplace_back(btn, command->StrHelp(), "Subtitle Edit Box", cmd_name);

	btn->Bind(wxEVT_BUTTON, std::bind(&SubsEditBox::CallCommand, this, cmd_name));
	return btn;
}

AssDialogue const* SubsEditBox::CurrentLine() const {
    for (auto const& row : c->subtitleAccess->Read().Events)
        if (row.Id == active_id) return &row;
    return nullptr;
}

void SubsEditBox::OnCommit(int type) {
    auto line = CurrentLine();
	wxEventBlocker blocker(this);

	initial_times.clear();

	if (type == AssFile::COMMIT_NEW || type & AssFile::COMMIT_STYLES) {
		std::vector<wxString> styles;
		for (auto const& name : c->subtitleAccess->Read().GetStyles()) styles.push_back(to_wx(name));
		style_box->Set(std::move(styles));
	}

	if (type == AssFile::COMMIT_NEW) {
		PopulateList(effect_box, effect_values, AssDialogue_Effect);
		PopulateList(actor_box, actor_values, AssDialogue_Actor);
		return;
	}
	else if (type & AssFile::COMMIT_STYLES)
		UpdateStyleBox();

	if (!(type ^ AssFile::COMMIT_ORDER)) return;

	SetControlsState(!!line);
	UpdateFields(type, true);
}

void SubsEditBox::UpdateFields(int type, bool repopulate_lists) {
    auto line = CurrentLine();
	if (!line) return;

	if (type & AssFile::COMMIT_DIAG_TIME) {
		start_time->SetTime(line->Start);
		end_time->SetTime(line->End);
		SetDurationField();
	}

	if (type & AssFile::COMMIT_DIAG_TEXT) {
		edit_ctrl->SetTextTo(line->Text);
		UpdateCharacterCount(line->Text);
	}

	if (type & AssFile::COMMIT_DIAG_META) {
		UpdateLayerField();
		comment_box->SetValue(line->Comment);
		UpdateStyleBox();

		if (repopulate_lists) PopulateList(effect_box, effect_values, AssDialogue_Effect);
		if (effect_box->GetValue() != to_wx(line->Effect)) effect_box->ChangeValue(to_wx(line->Effect));

		if (repopulate_lists) PopulateList(actor_box, actor_values, AssDialogue_Actor);
		if (actor_box->GetValue() != to_wx(line->Actor)) actor_box->ChangeValue(to_wx(line->Actor));
	}
}

void SubsEditBox::PopulateList(wxTextCtrl *ctrl, wxArrayString &values, boost::flyweight<std::string> AssDialogue::*field) {
	std::unordered_set<boost::flyweight<std::string>> found;
	for (auto const& line : c->subtitleAccess->Read().Events) {
		auto const& value = line.*field;
		if (!value.get().empty())
			found.insert(value);
	}

	wxArrayString list;
	list.reserve(found.size());
	for (auto const& value : found) list.push_back(to_wx(value));
	list.Sort();
	if (list == values) return;
	values = list;
	// Typing offers them too
	ctrl->AutoComplete(values);
}

void SubsEditBox::OnActiveLineChanged() {
    active_id = c->selectionController->ActiveID();
    wxEventBlocker blocker(this);
    SetControlsState(CurrentLine() != nullptr);
	commit_id = -1;

	UpdateFields(AssFile::COMMIT_DIAG_FULL, false);
}

void SubsEditBox::OnSelectedSetChanged() {
	initial_times.clear();
	wxEventBlocker blocker(this);
	UpdateLayerField();
}

void SubsEditBox::OnLineInitialTextChanged(std::string const& new_text) {
	if (split_box->IsChecked())
		secondary_editor->SetValue(to_wx(new_text));
}

void SubsEditBox::UpdateFrameTiming(agi::vfr::Framerate const& fps) {
	time_unit->AllowFrames(fps.IsLoaded());
	if (!fps.IsLoaded()) {
		start_time->SetByFrame(false);
		end_time->SetByFrame(false);
		duration->SetByFrame(false);
		c->subsGrid->SetByFrame(false);
	}
}

void SubsEditBox::OnKeyDown(wxKeyEvent &event) {
	if (osx::ime::process_key_event(edit_ctrl, event)) return;
	if (edit_ctrl->HasFocus() && edit_ctrl->HandleCompletionKey(event)) return;
	hotkey::check("Subtitle Edit Box", c, event);
}

void SubsEditBox::OnChange(wxStyledTextEvent &event) {
    auto line = CurrentLine();
	if (line && edit_ctrl->GetTextRaw().data() != line->Text.get()) {
		if (event.GetModificationType() & wxSTC_STARTACTION)
			commit_id = -1;
		CommitText(_("modify text"));
		UpdateCharacterCount(line->Text);
	}
}

void SubsEditBox::Commit(wxString const& desc, int type, bool amend, AssDialogue const* line) {
	file_changed_slot.Block();
	auto scope = c->subtitleAccess->TryWrite();
    if (!scope) { file_changed_slot.Unblock(); return; }
    commit_id = c->subtitleAccess->Edit(scope->Lease()).Commit(desc, type, (amend && desc == last_commit_type) ? commit_id : -1, line ? line->Id : 0);
	file_changed_slot.Unblock();
	last_commit_type = desc;
	last_time_commit_type = -1;
	initial_times.clear();
	undo_timer.Start(30000, wxTIMER_ONE_SHOT);
}

template<class setter>
void SubsEditBox::SetSelectedRows(setter set, wxString const& desc, int type, bool amend) {
    auto write = c->subtitleAccess->TryWrite();
    if (!write) { wxEventBlocker blocker(this); UpdateFields(AssFile::COMMIT_DIAG_FULL, false); return; }
	auto const sel = c->selectionController->ReadSelectedLines();
    std::vector<AssDialogueBase> updates;
    for (auto row : sel) { AssDialogue candidate(static_cast<AssDialogueBase const&>(*row)); set(&candidate); updates.push_back(candidate); }
    c->subtitleAccess->Edit(write->Lease()).UpdateRows(updates);
    Commit(desc, type, amend, sel.size() == 1 ? sel.front() : nullptr);
}

template<class T>
void SubsEditBox::SetSelectedRows(T AssDialogueBase::*field, T value, wxString const& desc, int type, bool amend) {
	SetSelectedRows([&](AssDialogue *d) { d->*field = value; }, desc, type, amend);
}

template<class T>
void SubsEditBox::SetSelectedRows(T AssDialogueBase::*field, wxString const& value, wxString const& desc, int type, bool amend) {
	boost::flyweight<std::string> conv_value(from_wx(value));
	SetSelectedRows([&](AssDialogue *d) { d->*field = conv_value; }, desc, type, amend);
}

void SubsEditBox::CommitText(wxString const& desc) {
	auto data = edit_ctrl->GetTextRaw();
	SetSelectedRows(&AssDialogue::Text, boost::flyweight<std::string>(data.data(), data.length()), desc, AssFile::COMMIT_DIAG_TEXT, true);
}

void SubsEditBox::CommitTimes(TimeField field) {
    auto line = CurrentLine();
    auto write = c->subtitleAccess->TryWrite();
    if (!write) { wxEventBlocker blocker(this); UpdateFields(AssFile::COMMIT_DIAG_FULL, false); return; }
	auto const& sel = c->selectionController->ReadSelectedSet();
	bool linked_neighbor = false;
    std::vector<AssDialogueBase> updates;
    int old_boundary = 0;
	for (AssDialogue const* original : sel) {
        AssDialogue candidate(static_cast<AssDialogueBase const&>(*original));
        auto d = &candidate;
		old_boundary = field == TIME_START ? d->Start : d->End;
		if (!initial_times.count(d->Id))
			initial_times[d->Id] = {d->Start, d->End};

		switch (field) {
			case TIME_START:
				initial_times[d->Id].first = d->Start = start_time->GetTime();
				d->End = std::max(d->Start, initial_times[d->Id].second);
				break;

			case TIME_END:
				initial_times[d->Id].second = d->End = end_time->GetTime();
				d->Start = std::min(d->End, initial_times[d->Id].first);
				break;

			case TIME_DURATION:
				if (time_unit->ByFrame()) {
					auto const& fps = c->project->Timecodes();
					d->End = fps.TimeAtFrame(fps.FrameAtTime(d->Start, agi::vfr::START) + duration->GetFrame() - 1, agi::vfr::END);
				}
				else
					d->End = d->Start + duration->GetTime();
				initial_times[d->Id].second = d->End;
				break;
		}
        updates.push_back(candidate);
    }
    auto edit = c->subtitleAccess->Edit(write->Lease());
    // C1-C1c-13 (audit C6): values and neighbour link are one validated write.
    std::vector<SubtitleProjectAccess::AdjacentLink> links;
    if (sel.size() == 1 && OPT_GET("Timing/Link Adjacent Lines")->GetBool()) {
        auto threshold_ms = static_cast<int>(OPT_GET("Timing/Adjacent Line Gap")->GetDouble() * 1000 + 0.5);
        links.push_back({(*sel.begin())->Id, field == TIME_START, old_boundary, threshold_ms});
    }
    linked_neighbor = edit.UpdateRowsLinkingAdjacent(updates, links);

	start_time->SetTime(line->Start);
	end_time->SetTime(line->End);

	if (field != TIME_DURATION)
		SetDurationField();

	if (field != last_time_commit_type)
		commit_id = -1;

	last_time_commit_type = field;
	file_changed_slot.Block();
	commit_id = c->subtitleAccess->Edit(write->Lease()).Commit(_("modify times"), AssFile::COMMIT_DIAG_TIME, commit_id, sel.size() == 1 && !linked_neighbor ? (*sel.begin())->Id : 0);
	file_changed_slot.Unblock();
	wxEventBlocker blocker(this);
	start_time->SetTime(line->Start);
	end_time->SetTime(line->End);
	SetDurationField();
}

void SubsEditBox::SetDurationField() {
	// With VFR, the frame count calculated from the duration in time can be
	// completely wrong (since the duration is calculated as if it were a start
	// time), so we need to explicitly set it with the correct units.
	if (time_unit->ByFrame())
		duration->SetFrame(end_time->GetFrame() - start_time->GetFrame() + 1);
	else
		duration->SetTime(end_time->GetTime() - start_time->GetTime());
}

void SubsEditBox::FitTimeLabels() {
	std::vector<FlatFrame*> frames;
	for (auto time : {start_time, end_time, duration})
		frames.push_back(static_cast<FlatFrame*>(time->GetParent()));
	bool was = frames.front()->IsLabelShown();
	for (auto frame : frames) frame->ShowLabel(true);
	// The row's margins, as fields_sizer gives it
	bool fits = middle_left_sizer->CalcMin().x + 6 <= GetClientSize().x;
	for (auto frame : frames) frame->ShowLabel(fits);
	if (fits != was) Layout();
}

void SubsEditBox::ArrangeRows() {
	auto sizer = GetSizer();
	bool below = OPT_GET("Subtitle/Edit Box/Fields Below Text")->GetBool();
	sizer->Detach(fields_sizer);
	if (below)
		sizer->Add(fields_sizer, 0, wxEXPAND);
	else
		sizer->Insert(0, fields_sizer, 0, wxEXPAND);
	// Tab goes through them as they are shown: the text, then the fields
	if (below)
		edit_ctrl->MoveBeforeInTabOrder(comment_box);
	else
		edit_ctrl->MoveAfterInTabOrder(duration->GetParent());
	Layout();
}

void SubsEditBox::OnTimeUnitChange(bool byFrame) {
	start_time->SetByFrame(byFrame);
	end_time->SetByFrame(byFrame);
	duration->SetByFrame(byFrame);
	c->subsGrid->SetByFrame(byFrame);

	SetDurationField();
}

void SubsEditBox::SetControlsState(bool state) {
	if (state == controls_enabled) return;
	controls_enabled = state;

	Enable(state);
	if (!state) {
		wxEventBlocker blocker(this);
		edit_ctrl->SetTextTo("");
	}
}

void SubsEditBox::OnSplit(wxCommandEvent&) {
	bool show_original = split_box->IsChecked();
	DoOnSplit(show_original);
	OPT_SET("Subtitle/Show Original")->SetBool(show_original);
}

void SubsEditBox::DoOnSplit(bool show_original) {
	Freeze();
	if (show_original)
		secondary_editor->SetValue(to_wx(c->initialLineState->GetInitialText()));

	GetSizer()->Show(original_sizer, show_original);
	GetSizer()->Show(bottom_sizer, show_original);
	Fit();
	SetMinSize(GetSize());
	wxSizer* parent_sizer = GetParent()->GetSizer();
	if (parent_sizer) parent_sizer->Layout();
	Thaw();
}

void SubsEditBox::UpdateStyleBox() {
    auto line = CurrentLine();
	if (!line) return;
	style_box->SetStringSelection(to_wx(line->Style));
	style_box->SetToolTip(to_wx(line->Style));
}

void SubsEditBox::OnStyleChange() {
	// Going through the styles with the wheel or the keys is one undo step
	bool amend = wxGetLocalTimeMillis() - last_step < 1000;
	last_step = wxGetLocalTimeMillis();
	SetSelectedRows(&AssDialogue::Style, style_box->GetStringSelection(), _("style change"), AssFile::COMMIT_DIAG_META, amend);
	UpdateStyleBox();
}

void SubsEditBox::OnActorChange(wxCommandEvent &) {
	SetSelectedRows(AssDialogue_Actor, actor_box->GetValue(), _("actor change"), AssFile::COMMIT_DIAG_META, true);
	PopulateList(actor_box, actor_values, AssDialogue_Actor);
}

void SubsEditBox::OnEffectChange(wxCommandEvent &) {
	SetSelectedRows(AssDialogue_Effect, effect_box->GetValue(), _("effect change"), AssFile::COMMIT_DIAG_META, true);
	PopulateList(effect_box, effect_values, AssDialogue_Effect);
}

void SubsEditBox::UpdateLayerField() {
	auto line = CurrentLine();
	if (!line) return;
	// "—" when the selected lines are on different layers
	bool same = true;
	for (auto row : c->selectionController->ReadSelectedLines())
		if (row->Layer != line->Layer) same = false;
	wxString text = same ? wxString::Format("%d", line->Layer) : wxString(wxS("—"));
	if (layer->GetValue() != text) layer->ChangeValue(text);
}

void SubsEditBox::CommitLayerText() {
	long value;
	wxString text = layer->GetValue();
	text.Trim(true).Trim(false);
	if (!text.ToLong(&value) || value < 0) {
		UpdateLayerField();
		return;
	}
	value = std::min(value, 999L);
	auto line = CurrentLine();
	bool changed = !line;
	for (auto row : c->selectionController->ReadSelectedLines())
		if (row->Layer != value) changed = true;
	if (changed)
		SetSelectedRows(&AssDialogue::Layer, static_cast<int>(value), _("layer change"), AssFile::COMMIT_DIAG_META);
	UpdateLayerField();
}

void SubsEditBox::ShiftLayers(int delta, bool amend) {
	// Each line from its own layer, so their order is kept
	SetSelectedRows([&](AssDialogue *d) {
		auto base = layer_base.find(d->Id);
		int from = base == layer_base.end() ? d->Layer : base->second;
		d->Layer = std::clamp(from + delta, 0, 999);
	}, _("layer change"), AssFile::COMMIT_DIAG_META, amend);
	UpdateLayerField();
}

void SubsEditBox::StepLayer(int delta) {
	bool amend = wxGetLocalTimeMillis() - last_step < 1000;
	last_step = wxGetLocalTimeMillis();
	layer_base.clear();
	ShiftLayers(delta, amend);
}

void SubsEditBox::ScrubLayer(int steps, bool final) {
	if (!scrubbing_layer) {
		// The layers when the drag began
		scrubbing_layer = true;
		layer_base.clear();
		for (auto row : c->selectionController->ReadSelectedLines())
			layer_base[row->Id] = row->Layer;
		ShiftLayers(steps, false);
	}
	else
		ShiftLayers(steps, true);
	if (final) {
		scrubbing_layer = false;
		layer_base.clear();
	}
}

void SubsEditBox::OnCommentChange(wxCommandEvent &evt) {
	SetSelectedRows(&AssDialogue::Comment, !!evt.GetInt(), _("comment change"), AssFile::COMMIT_DIAG_META);
}

void SubsEditBox::CallCommand(const char *cmd_name) {
	cmd::call(cmd_name, c);
	edit_ctrl->SetFocus();
}

void SubsEditBox::UpdateCharacterCount(std::string const& text) {
	int ignore = agi::IGNORE_BLOCKS;
	if (OPT_GET("Subtitle/Character Counter/Ignore Whitespace")->GetBool())
		ignore |= agi::IGNORE_WHITESPACE;
	if (OPT_GET("Subtitle/Character Counter/Ignore Punctuation")->GetBool())
		ignore |= agi::IGNORE_PUNCTUATION;
	size_t length = agi::MaxLineLength(text, ignore);
	size_t limit = (size_t)OPT_GET("Subtitle/Character Limit")->GetInt();
	// Used of the limit, or the count alone when there is none
	char_count->SetText(limit ? wxString::Format("%zu / %zu", length, limit) : wxString::Format("%zu", length), limit && length > limit);

	wxString counted = OPT_GET("Subtitle/Character Counter/Ignore Whitespace")->GetBool()
		? OPT_GET("Subtitle/Character Counter/Ignore Punctuation")->GetBool() ? _("not counting tags in {}, spaces or punctuation") : _("not counting tags in {} or spaces")
		: OPT_GET("Subtitle/Character Counter/Ignore Punctuation")->GetBool() ? _("not counting tags in {} or punctuation") : _("not counting tags in {}");
	wxString tip = wxString::Format(_("Characters in the longest line: the text split at \\N, %s."), counted);
	if (limit)
		tip += " " + wxString::Format(_("Red over %zu, the most a line should have so that it can be read and fits on the screen."), limit);
	tip += "\n" + _("Click to change the limit and what is counted (Preferences, Character Counter).");
	char_count->SetToolTip(tip);
	PlaceCharacterCount();
}

void SubsEditBox::PlaceCharacterCount() {
	wxSize size = char_count->GetBestSize();
	wxSize area = edit_ctrl->GetClientSize();
	int gap = FromDIP(4);
	char_count->SetSize(area.x - size.x - gap, area.y - size.y - gap, size.x, size.y);
	// The text wraps before it
	edit_ctrl->SetMarginRight(size.x + gap * 2);
}

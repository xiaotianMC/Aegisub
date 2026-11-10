// Copyright (c) 2006, Rodrigo Braz Monteiro
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

#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "subtitle_row_edit.h"
#include "async_video_provider.h"
#include "command/command.h"
#include "compat.h"
#include "format.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "flat_list.h"
#include "include/aegisub/context.h"
#include "keyframe_snap.h"
#include "timing_adjust.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "theme.h"
#include "utils.h"

#include <libaegisub/address_of_adaptor.h>
#include <libaegisub/ass/time.h>

#include <algorithm>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/range/algorithm_ext/push_back.hpp>
#include <functional>
#include <vector>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

using namespace boost::adaptors;

namespace {
/// @class DialogTimingProcessor
/// @brief Automatic postprocessor for correcting common timing issues
struct DialogTimingProcessor {
	wxDialog d;
	agi::Context *c; ///< Project context

	int leadIn;      ///< Lead-in to add in milliseconds
	int leadOut;     ///< Lead-out to add in milliseconds
	int beforeStart; ///< Maximum time in milliseconds to move start time of line backwards to land on a keyframe
	int afterStart;  ///< Maximum time in milliseconds to move start time of line forwards to land on a keyframe
	int beforeEnd;   ///< Maximum time in milliseconds to move end time of line backwards to land on a keyframe
	int afterEnd;    ///< Maximum time in milliseconds to move end time of line forwards to land on a keyframe
	int adjGap;      ///< Maximum gap in milliseconds to snap adjacent lines to each other
	int adjOverlap;  ///< Maximum overlap in milliseconds to snap adjacent lines to each other

	FlatCheck *onlySelection; ///< Only process selected lines of the selected styles
	FlatCheck *hasLeadIn;     ///< Enable adding lead-in
	FlatCheck *hasLeadOut;    ///< Enable adding lead-out
	FlatCheck *keysEnable;    ///< Enable snapping to keyframes
	FlatCheck *adjsEnable;    ///< Enable snapping adjacent lines to each other
	flat::NumberField *adjacentBias; ///< Bias between shifting start and end times when snapping adjacent lines, in percent
	FlatList *StyleList;      ///< List of styles to process
	wxWindow *ApplyButton;    ///< Button to apply the processing

	void OnApply(wxCommandEvent &event);

	/// Check or uncheck all styles
	void CheckAll(bool check);

	/// Enable and disable text boxes based on which checkboxes are checked
	void UpdateControls();

	/// Process the file
	void Process();

	/// Get a list of dialogue lines in the file sorted by start time
	std::vector<AssDialogue*> SortDialogues(SubtitleRowEdit const& staged);

	DialogTimingProcessor(agi::Context *c);
};

/// A number of milliseconds, used while @p cb is ticked, with "ms" after it
wxSizer *make_ctrl(wxWindow *parent, wxString const& desc, int *value, FlatCheck *cb, wxString const& tooltip) {
	auto field = flat::NumberField::Create(parent, desc, *value, 0, 1e6, 10, 0, 44);
	field->on_change = [value](double v) { *value = static_cast<int>(v); };
	field->SetToolTip(tooltip);
	field->Enable(cb->IsChecked());
	cb->Bind(wxEVT_CHECKBOX, [=](wxCommandEvent& evt) {
		field->Enable(cb->IsChecked());
		evt.Skip();
	});
	auto unit = new wxStaticText(parent, -1, "ms");
	unit->SetForegroundColour(flat::Colours(parent).dim);
	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(field->Frame(), wxSizerFlags(1).Expand());
	sizer->Add(unit, wxSizerFlags().CenterVertical().Border(wxLEFT, parent->FromDIP(4)));
	return sizer;
}

FlatCheck *make_check(wxWindow *parent, wxString const& desc, const char *opt, wxString const& tooltip) {
	auto cb = new FlatCheck(parent, desc);
	cb->SetToolTip(tooltip);
	cb->SetValue(OPT_GET(opt)->GetBool());
	return cb;
}

DialogTimingProcessor::DialogTimingProcessor(agi::Context *c)
: d(c->parent, -1, _("Timing Post-Processor"))
, c(c)
{
	using std::bind;

	d.SetIcons(GETICONS(timing_processor_toolbutton));

	// Read options
	leadIn = OPT_GET("Tool/Timing Post Processor/Lead/IN")->GetInt();
	leadOut = OPT_GET("Tool/Timing Post Processor/Lead/OUT")->GetInt();
	beforeStart = OPT_GET("Tool/Timing Post Processor/Threshold/Key Start Before")->GetInt();
	beforeEnd = OPT_GET("Tool/Timing Post Processor/Threshold/Key End Before")->GetInt();
	afterStart = OPT_GET("Tool/Timing Post Processor/Threshold/Key Start After")->GetInt();
	afterEnd = OPT_GET("Tool/Timing Post Processor/Threshold/Key End After")->GetInt();
	adjGap = OPT_GET("Tool/Timing Post Processor/Threshold/Adjacent Gap")->GetInt();
	adjOverlap = OPT_GET("Tool/Timing Post Processor/Threshold/Adjacent Overlap")->GetInt();

	theme::UseSurface(&d);
	auto parent = &d;
	int gap = d.FromDIP(10), row = d.FromDIP(6);

	// The styles to process
	StyleList = new FlatList(parent, true, true, _("No styles"));
	StyleList->Set(to_wx(c->subtitleAccess->Read().GetStyles()));
	StyleList->SetMinSize(DipSize(170, 200));
	StyleList->SetToolTip(_("Select styles to process. Unchecked ones will be ignored."));
	auto all = new FlatButton(parent, _("&All"));
	all->SetToolTip(_("Select all styles"));
	auto none = new FlatButton(parent, _("&None"));
	none->SetToolTip(_("Deselect all styles"));
	auto style_buttons = new wxBoxSizer(wxHORIZONTAL);
	style_buttons->Add(all, wxSizerFlags(1));
	style_buttons->Add(none, wxSizerFlags(1).Border(wxLEFT, row));
	auto left = new wxBoxSizer(wxVERTICAL);
	left->Add(flat::SectionTitle(parent, _("Apply to styles")), wxSizerFlags().Expand());
	left->Add(StyleList, wxSizerFlags(1).Expand().Border(wxTOP, row));
	left->Add(style_buttons, wxSizerFlags().Expand().Border(wxTOP, row));

	onlySelection = new FlatCheck(parent, _("Affect &selection only"));
	onlySelection->SetValue(OPT_GET("Tool/Timing Post Processor/Only Selection")->GetBool());

	// Lead-in and lead-out: each a tick and its length
	hasLeadIn = make_check(parent, flat::WithoutColon(_("Add lead &in:")), "Tool/Timing Post Processor/Enable/Lead/IN",
		_("Enable adding of lead-ins to lines"));
	hasLeadOut = make_check(parent, flat::WithoutColon(_("Add lead &out:")), "Tool/Timing Post Processor/Enable/Lead/OUT",
		_("Enable adding of lead-outs to lines"));
	auto lead = new wxFlexGridSizer(2, row, gap);
	lead->AddGrowableCol(1, 1);
	lead->Add(hasLeadIn, wxSizerFlags().CenterVertical());
	lead->Add(make_ctrl(parent, _("Lead"), &leadIn, hasLeadIn, _("Lead in to be added, in milliseconds")), wxSizerFlags().Expand());
	lead->Add(hasLeadOut, wxSizerFlags().CenterVertical());
	lead->Add(make_ctrl(parent, _("Lead"), &leadOut, hasLeadOut, _("Lead out to be added, in milliseconds")), wxSizerFlags().Expand());

	// Adjacent lines made continuous
	adjsEnable = make_check(parent, _("&Enable"), "Tool/Timing Post Processor/Enable/Adjacent",
		_("Enable snapping of subtitles together if they are within a certain distance of each other"));
	auto adjacent = new wxGridSizer(2, row, gap);
	adjacent->Add(make_ctrl(parent, flat::WithoutColon(_("Max gap:")), &adjGap, adjsEnable,
		_("Maximum difference between start and end time for two subtitles to be made continuous, in milliseconds")), wxSizerFlags().Expand());
	adjacent->Add(make_ctrl(parent, flat::WithoutColon(_("Max overlap:")), &adjOverlap, adjsEnable,
		_("Maximum overlap between the end and start time for two subtitles to be made continuous, in milliseconds")), wxSizerFlags().Expand());
	adjacentBias = flat::NumberField::Create(parent, _("Bias"), mid<int>(0, OPT_GET("Tool/Timing Post Processor/Adjacent Bias")->GetDouble() * 100, 100), 0, 100, 5, 0, 36);
	adjacentBias->SetToolTip(_("Sets how to set the adjoining of lines. If set totally to left, it will extend or shrink start time of the second line; if totally to right, it will extend or shrink the end time of the first line."));
	adjacentBias->Enable(adjsEnable->IsChecked());
	adjsEnable->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &evt) { adjacentBias->Enable(adjsEnable->IsChecked()); evt.Skip(); });
	auto bias_note = new wxStaticText(parent, -1, _("% toward the end of the first line"));
	bias_note->SetForegroundColour(flat::Colours(parent).dim);
	auto bias = new wxBoxSizer(wxHORIZONTAL);
	bias->Add(adjacentBias->Frame(), wxSizerFlags().Expand());
	bias->Add(bias_note, wxSizerFlags().CenterVertical().Border(wxLEFT, d.FromDIP(4)));

	// Keyframe snapping: how far before and after a keyframe starts and ends snap to it
	keysEnable = new FlatCheck(parent, _("E&nable"));
	keysEnable->SetToolTip(_("Enable snapping of subtitles to nearest keyframe, if distance is within threshold"));
	keysEnable->SetValue(OPT_GET("Tool/Timing Post Processor/Enable/Keyframe")->GetBool());
	// Keyframes are only available if timecodes are loaded
	bool keysAvailable = !c->project->Keyframes().empty() && c->project->Timecodes().IsLoaded();
	if (!keysAvailable) {
		keysEnable->SetValue(false);
		keysEnable->Enable(false);
		keysEnable->SetToolTip(_("Keyframe snapping needs keyframes and timecodes"));
	}
	// The video's own keyframes are where its encoder put them, not always
	// where the picture cuts: the cuts can be found and used instead
	auto detect = new FlatButton(parent, _("&Detect Scene Changes"));
	detect->SetToolTip(_("Find the frames where the picture cuts to another shot and use them as the keyframes, to snap the lines' times to"));
	detect->Enable(!!c->project->VideoProvider());
	auto keys_note = flat::DimLabel(parent, _("The video's own keyframes are not always scene changes"));
	keys_note->Show(c->project->VideoProvider() && !c->project->CanCloseKeyframes());
	detect->Bind(wxEVT_BUTTON, [this, c, keys_note](wxCommandEvent &) {
		cmd::call("keyframe/detect", c);
		bool available = !c->project->Keyframes().empty() && c->project->Timecodes().IsLoaded();
		if (available && !keysEnable->IsEnabled()) {
			keysEnable->Enable();
			keysEnable->SetToolTip(_("Enable snapping of subtitles to nearest keyframe, if distance is within threshold"));
		}
		if (available && !keysEnable->IsChecked()) {
			// As a click does, so the thresholds and OK follow
			keysEnable->SetValue(true);
			wxCommandEvent evt(wxEVT_CHECKBOX, keysEnable->GetId());
			evt.SetEventObject(keysEnable);
			evt.SetInt(1);
			keysEnable->GetEventHandler()->ProcessEvent(evt);
		}
		keys_note->Show(!c->project->CanCloseKeyframes());
		d.Layout();
		UpdateControls();
	});
	auto keys_top = new wxBoxSizer(wxHORIZONTAL);
	keys_top->Add(keysEnable, wxSizerFlags().CenterVertical());
	keys_top->AddStretchSpacer();
	keys_top->Add(detect, wxSizerFlags().CenterVertical());

	auto keys = new wxGridSizer(2, row, gap);
	keys->Add(make_ctrl(parent, _("Starts before"), &beforeStart, keysEnable,
		_("Threshold for 'before start' distance, that is, how many milliseconds a subtitle must start before a keyframe to snap to it")), wxSizerFlags().Expand());
	keys->Add(make_ctrl(parent, _("Starts after"), &afterStart, keysEnable,
		_("Threshold for 'after start' distance, that is, how many milliseconds a subtitle must start after a keyframe to snap to it")), wxSizerFlags().Expand());
	keys->Add(make_ctrl(parent, _("Ends before"), &beforeEnd, keysEnable,
		_("Threshold for 'before end' distance, that is, how many milliseconds a subtitle must end before a keyframe to snap to it")), wxSizerFlags().Expand());
	keys->Add(make_ctrl(parent, _("Ends after"), &afterEnd, keysEnable,
		_("Threshold for 'after end' distance, that is, how many milliseconds a subtitle must end after a keyframe to snap to it")), wxSizerFlags().Expand());

	auto right = new wxBoxSizer(wxVERTICAL);
	right->Add(onlySelection);
	right->Add(flat::SectionTitle(parent, _("Lead-in/Lead-out")), wxSizerFlags().Expand().Border(wxTOP, gap));
	right->Add(lead, wxSizerFlags().Expand().Border(wxTOP, row));
	right->Add(flat::SectionTitle(parent, _("Make adjacent subtitles continuous")), wxSizerFlags().Expand().Border(wxTOP, gap));
	right->Add(adjsEnable, wxSizerFlags().Border(wxTOP, row));
	right->Add(adjacent, wxSizerFlags().Expand().Border(wxTOP, row));
	right->Add(bias, wxSizerFlags().Border(wxTOP, row));
	right->Add(flat::SectionTitle(parent, _("Keyframe snapping")), wxSizerFlags().Expand().Border(wxTOP, gap));
	right->Add(keys_top, wxSizerFlags().Expand().Border(wxTOP, row));
	right->Add(keys_note, wxSizerFlags().Border(wxTOP, row / 2));
	right->Add(keys, wxSizerFlags().Expand().Border(wxTOP, row));

	auto top = new wxBoxSizer(wxHORIZONTAL);
	top->Add(left, wxSizerFlags().Expand());
	top->Add(right, wxSizerFlags(1).Expand().Border(wxLEFT, d.FromDIP(16)));

	auto buttons = new flat::DialogButtons(&d, wxOK | wxCANCEL | wxHELP, "Timing Processor");
	ApplyButton = buttons->Get(wxID_OK);

	auto MainSizer = new wxBoxSizer(wxVERTICAL);
	MainSizer->Add(top, wxSizerFlags(1).Expand().Border(wxALL, gap));
	MainSizer->Add(buttons, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, gap));
	d.SetSizerAndFit(MainSizer);
	d.CenterOnParent();

	d.Bind(wxEVT_CHECKBOX, bind(&DialogTimingProcessor::UpdateControls, this));
	d.Bind(wxEVT_CHECKLISTBOX, bind(&DialogTimingProcessor::UpdateControls, this));
	d.Bind(wxEVT_BUTTON, &DialogTimingProcessor::OnApply, this, wxID_OK);
	all->Bind(wxEVT_BUTTON, bind(&DialogTimingProcessor::CheckAll, this, true));
	none->Bind(wxEVT_BUTTON, bind(&DialogTimingProcessor::CheckAll, this, false));

	CheckAll(true);
}

void DialogTimingProcessor::CheckAll(bool value) {
	for (unsigned i = 0; i < StyleList->GetCount(); ++i)
		StyleList->Check(i, value);
	UpdateControls();
}

void DialogTimingProcessor::UpdateControls() {
	// Only enable the OK button if it'll actually do something
	bool any_checked = false;
	size_t len = StyleList->GetCount();
	for (size_t i = 0; !any_checked && i < len; ++i)
		any_checked = StyleList->IsChecked(i);
	ApplyButton->Enable(any_checked && (hasLeadIn->IsChecked() || hasLeadOut->IsChecked() || keysEnable->IsChecked() || adjsEnable->IsChecked()));
}

void DialogTimingProcessor::OnApply(wxCommandEvent &) {
    auto edit_scope = c->subtitleAccess->TryWrite();
    if (!edit_scope) return;
	d.TransferDataFromWindow();
	// Save settings
	OPT_SET("Tool/Timing Post Processor/Lead/IN")->SetInt(leadIn);
	OPT_SET("Tool/Timing Post Processor/Lead/OUT")->SetInt(leadOut);
	OPT_SET("Tool/Timing Post Processor/Threshold/Key Start Before")->SetInt(beforeStart);
	OPT_SET("Tool/Timing Post Processor/Threshold/Key Start After")->SetInt(afterStart);
	OPT_SET("Tool/Timing Post Processor/Threshold/Key End Before")->SetInt(beforeEnd);
	OPT_SET("Tool/Timing Post Processor/Threshold/Key End After")->SetInt(afterEnd);
	OPT_SET("Tool/Timing Post Processor/Threshold/Adjacent Gap")->SetInt(adjGap);
	OPT_SET("Tool/Timing Post Processor/Threshold/Adjacent Overlap")->SetInt(adjOverlap);
	OPT_SET("Tool/Timing Post Processor/Adjacent Bias")->SetDouble(adjacentBias->GetInt() / 100.0);
	OPT_SET("Tool/Timing Post Processor/Enable/Lead/IN")->SetBool(hasLeadIn->IsChecked());
	OPT_SET("Tool/Timing Post Processor/Enable/Lead/OUT")->SetBool(hasLeadOut->IsChecked());
	if (keysEnable->IsEnabled()) OPT_SET("Tool/Timing Post Processor/Enable/Keyframe")->SetBool(keysEnable->IsChecked());
	OPT_SET("Tool/Timing Post Processor/Enable/Adjacent")->SetBool(adjsEnable->IsChecked());
	OPT_SET("Tool/Timing Post Processor/Only Selection")->SetBool(onlySelection->IsChecked());

	Process();
	d.EndModal(0);
}

std::vector<AssDialogue*> DialogTimingProcessor::SortDialogues(SubtitleRowEdit const& staged) {
	std::set<boost::flyweight<std::string>> styles;
	for (size_t i = 0; i < StyleList->GetCount(); ++i) {
		if (StyleList->IsChecked(i))
			styles.insert(boost::flyweight<std::string>(from_wx(StyleList->GetString(i))));
	}

	std::vector<AssDialogue*> sorted;

	auto valid_line = [&](const AssDialogue *d) { return !d->Comment && styles.count(d->Style); };
    for(auto row:staged.rows)
        if(valid_line(row) && (!onlySelection->IsChecked() || staged.selected.contains(row->Id))) sorted.push_back(row);

	// Check if rows are valid
	for (auto diag : sorted) {
		if (diag->Start > diag->End) {
			wxMessageBox(
				fmt_tl("One of the lines in the file (%i) has negative duration. Aborting.", diag->Row),
				_("Invalid script"),
				wxOK | wxICON_ERROR | wxCENTER);
			sorted.clear();
			break;
		}
	}

	boost::sort(sorted, [](const AssDialogue *a, const AssDialogue *b) {
		return a->Start < b->Start;
	});
	return sorted;
}

void DialogTimingProcessor::Process() {
	auto scope=c->subtitleAccess->TryWrite();if(!scope)return;
    SubtitleRowEdit staged(c,true);
    std::vector<AssDialogue*> sorted = SortDialogues(staged);
	if (sorted.empty()) return;

	// Add lead-in/out, not over the lines around
	AddLead(sorted, sorted, hasLeadIn->IsChecked() ? leadIn : 0, hasLeadOut->IsChecked() ? leadOut : 0);

	// Make adjacent
	if (adjsEnable->IsChecked()) {
		double bias = adjacentBias->GetInt() / 100.0;

		for (size_t i = 1; i < sorted.size(); ++i) {
			AssDialogue *prev = sorted[i - 1];
			AssDialogue *cur = sorted[i];

			int dist = cur->Start - prev->End;
			if ((dist < 0 && -dist <= adjOverlap) || (dist > 0 && dist <= adjGap)) {
				int setPos = prev->End + int(dist * bias);
				cur->Start = setPos;
				prev->End = setPos;
			}
		}
	}

	// Keyframe snapping
	if (keysEnable->IsChecked()) {
		auto provider = c->project->VideoProvider();
		SnapToKeyframes({sorted.begin(), sorted.end()}, c->project->Keyframes(), provider ? provider->GetFrameCount() : 0,
			c->project->Timecodes(), {beforeStart, afterStart, beforeEnd, afterEnd});
	}

	std::vector<AssDialogueBase> updates;for(auto row:sorted)updates.push_back(*row);
    auto edit=c->subtitleAccess->Edit(scope->Lease());edit.UpdateRows(updates);edit.Commit(_("timing processor"), AssFile::COMMIT_DIAG_TIME);
}
}

void ShowTimingProcessorDialog(agi::Context *c) {
	DialogTimingProcessor(c).d.ShowModal();
}

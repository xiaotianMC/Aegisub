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

#include "video_box.h"
#include "utils.h"
#include "theme.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "include/aegisub/toolbar.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "video_controller.h"
#include "video_display.h"
#include "video_slider.h"
#include "video_time_label.h"
#include "command/command.h"

#include <boost/range/algorithm/binary_search.hpp>
#include <wx/combobox.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/toolbar.h>

VideoBox::VideoBox(wxWindow *parent, bool isDetached, agi::Context *context)
: wxPanel(parent, -1)
, context(context)
{
	// Two blocks on the backdrop (docs/theme.md): the video with its
	// controls, and the visual tools to its right.
	theme::UseBackdrop(this);
	auto videoArea = new wxPanel(this, -1);
	theme::UseSurface(videoArea);
	auto toolsArea = new wxPanel(this, -1);
	theme::UseSurface(toolsArea);

	auto videoSlider = slider = new VideoSlider(videoArea, context);

	auto mainToolbar = toolbar::GetToolbar(videoArea, "video", context, "Video", false);

	// Readouts drawn on the block: fixed width so the row holds still,
	// clicking the time jumps, right-click copies (docs/main-window.md)
	VideoPosition = new VideoTimeLabel(videoArea, "0:00:00.000 000000", true, false);
	VideoPosition->SetToolTip(_("Current frame time and number") + "\n" + _("Click to jump to a time or frame; right-click to copy"));
	VideoPosition->SetClickHandler([context] { cmd::call("video/jump", context); });
	VideoPosition->SetCopyItems({
		{_("Copy time"), [this, context] { return context->project->VideoProvider() ? VideoPosition->GetMain() : wxString(); }},
		{_("Copy frame number"), [this, context] { return context->project->VideoProvider() ? VideoPosition->GetSecondary() : wxString(); }},
	});

	VideoSubsPos = new VideoTimeLabel(videoArea, "+0000000ms +0000000ms", false, true);
	VideoSubsPos->SetToolTip(_("Time of this frame relative to start and end of current subs") + "\n" + _("Right-click to copy"));
	VideoSubsPos->SetCopyItems({{_("Copy"), [this, context]() -> wxString {
		AssDialogue const* line = context->selectionController->ReadActiveLine();
		if (!line || !context->project->VideoProvider()) return "";
		int time = context->videoController->TimeAtFrame(context->videoController->GetFrameN(), agi::vfr::EXACT);
		return fmt_wx("%+dms; %+dms", time - line->Start, time - line->End);
	}}});

	wxArrayString choices;
	for (int i = 1; i <= 24; ++i)
		choices.Add(fmt_wx("%g%%", i * 12.5));
	auto zoomBox = new wxComboBox(videoArea, -1, "75%", wxDefaultPosition, wxSize(FromDIP(90), -1), choices, wxCB_DROPDOWN | wxTE_PROCESS_ENTER);
	zoomBox->SetToolTip(_("Video zoom"));

	auto visualToolBar = toolbar::GetToolbar(toolsArea, "visual_tools", context, "Video", true);
	auto visualSubToolBar = new wxToolBar(toolsArea, -1, wxDefaultPosition, wxDefaultSize, wxTB_VERTICAL | wxTB_BOTTOM | wxTB_NODIVIDER | wxTB_FLAT);

	auto videoDisplay = new VideoDisplay(visualSubToolBar, isDetached, zoomBox, videoArea, context);
	videoDisplay->MoveBeforeInTabOrder(videoSlider);

	auto toolbarSizer = new wxBoxSizer(wxVERTICAL);
	toolbarSizer->Add(visualToolBar, wxSizerFlags(1));
	toolbarSizer->Add(visualSubToolBar, wxSizerFlags());
	toolsArea->SetSizer(toolbarSizer);
	// Toolbars have a colour of their own in some themes: the block's instead
	for (wxWindow *bar : {static_cast<wxWindow*>(mainToolbar), static_cast<wxWindow*>(visualToolBar), static_cast<wxWindow*>(visualSubToolBar)})
		theme::UseSurface(bar);

	// Buttons, readouts, then the zoom at the far right
	const int gap = theme::BlockGap(this);
	auto videoBottomSizer = new wxBoxSizer(wxHORIZONTAL);
	videoBottomSizer->Add(mainToolbar, wxSizerFlags(0).Center());
	videoBottomSizer->Add(VideoPosition, wxSizerFlags(0).Expand().Border(wxLEFT, gap));
	videoBottomSizer->Add(VideoSubsPos, wxSizerFlags(0).Expand());
	videoBottomSizer->AddStretchSpacer(1);
	videoBottomSizer->Add(zoomBox, wxSizerFlags(0).Center().Border(wxLEFT, gap));

	auto VideoSizer = new wxBoxSizer(wxVERTICAL);
	VideoSizer->Add(videoDisplay, 1, isDetached ? wxEXPAND : 0, 0);
	VideoSizer->Add(videoSlider, 0, wxEXPAND | wxTOP, gap);
	VideoSizer->Add(videoBottomSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, gap);
	videoArea->SetSizer(VideoSizer);

	// The visual tools sit to the right of the video, between it and the
	// audio display in the main window.
	auto blocks = new wxBoxSizer(wxHORIZONTAL);
	blocks->Add(videoArea, isDetached, wxEXPAND);
	toolbar_item = blocks->Add(toolsArea, 0, wxEXPAND | wxLEFT, theme::BlockGap(this));
	SetSizer(blocks);

	UpdateTimeBoxes();

	connections = agi::signal::make_vector({
		context->subtitleAccess->Read().AddCommitListener("video_box.AddCommitListener", [this](int, AssDialogue const*, SubtitleModelRebindTicket ticket) { UpdateTimeBoxes(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)),
		context->project->AddKeyframesListener(&VideoBox::UpdateTimeBoxes, this),
		context->project->AddTimecodesListener(&VideoBox::UpdateTimeBoxes, this),
		context->project->AddVideoProviderListener(&VideoBox::UpdateTimeBoxes, this),
		context->selectionController->AddSelectionListener("video_box.AddSelectionListener", [this](SubtitleModelRebindTicket ticket) { UpdateTimeBoxes(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)),
		context->videoController->AddSeekListener(&VideoBox::UpdateTimeBoxes, this),
	});
}

void VideoBox::UpdateTimeBoxes() {
	const wxString none(wxUniChar(0x2014)); // em dash
	if (!context->project->VideoProvider()) {
		VideoPosition->SetParts(none);
		VideoSubsPos->SetParts(none);
		slider->SetLineRange(-1, -1);
		return;
	}

	int frame = context->videoController->GetFrameN();
	int time = context->videoController->TimeAtFrame(frame, agi::vfr::EXACT);

	// Time, frame number dimmer, and a badge on keyframes
	VideoPosition->SetParts(agi::Time(time).GetAssFormatted(true), fmt_wx("%d", frame),
		boost::binary_search(context->project->Keyframes(), frame));

	AssDialogue const* active_line = context->selectionController->ReadActiveLine();
	if (!active_line) {
		VideoSubsPos->SetParts(none);
		slider->SetLineRange(-1, -1);
		return;
	}
	// A true minus sign for reading; copying gives plain ASCII
	auto offset = [](int ms) {
		wxString text = fmt_wx("%+dms", ms);
		if (ms < 0) text.replace(0, 1, wxString(wxUniChar(0x2212))); // minus sign
		return text;
	};
	VideoSubsPos->SetParts(offset(time - active_line->Start), offset(time - active_line->End));
	slider->SetLineRange(
		context->videoController->FrameAtTime(active_line->Start, agi::vfr::START),
		context->videoController->FrameAtTime(active_line->End, agi::vfr::END));
}

void VideoBox::SetToolbarGap(int gap) {
	// Same space on both sides of the visual tools: the caller passes the
	// space between them and whatever is to their right.
	toolbar_item->SetBorder(gap);
	Layout();
}

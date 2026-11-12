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
#include "async_video_provider.h"
#include "utils.h"
#include "theme.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "include/aegisub/toolbar.h"
#include "keyboard_regions.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "video_controller.h"
#include "video_display.h"
#include "video_slider.h"
#include "video_time_label.h"
#include "video_zoom_control.h"

#include <boost/range/algorithm/binary_search.hpp>
#include <climits>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/toolbar.h>

namespace {
/// Three items in a row: the first at the left, the last at the right and
/// the middle one centred on the row, moved aside only when it would touch
/// the others.
class CentredRowSizer final : public wxSizer {
	int gap;

public:
	explicit CentredRowSizer(int gap) : gap(gap) { }

	wxSize CalcMin() override {
		wxSize min;
		for (auto item : GetChildren()) {
			wxSize size = item->CalcMin();
			min.x += size.x;
			min.y = std::max(min.y, size.y);
		}
		min.x += gap * std::max<int>(0, GetChildren().size() - 1);
		return min;
	}

	void RepositionChildren(wxSize const&) override {
		auto const& items = GetChildren();
		if (items.size() != 3) return;
		auto place = [&](wxSizerItem *item, int x) {
			wxSize size = item->CalcMin();
			item->SetDimension(wxPoint(x, m_position.y + (m_size.y - size.y) / 2), size);
			return size.x;
		};
		int left_end = m_position.x + place(items[0], m_position.x);
		int right_width = items[2]->CalcMin().x;
		int right_start = m_position.x + m_size.x - right_width;
		place(items[2], right_start);
		int width = items[1]->CalcMin().x;
		int x = m_position.x + (m_size.x - width) / 2;
		// Too narrow to centre on the row: centre in the space between
		if (x < left_end + gap || x + width > right_start - gap)
			x = std::max(left_end + gap, left_end + (right_start - left_end - width) / 2);
		place(items[1], x);
	}
};
}

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
	auto optionsToolbar = toolbar::GetToolbar(videoArea, "video/options", context, "Video", false);

	// Readouts drawn on the block: fixed width so the row holds still;
	// click the time to type one, right-click to copy (docs/main-window.md)
	VideoPosition = new VideoTimeLabel(videoArea, "0:00:00.000 00000", true, false);
	VideoPosition->SetDivider(false);
	VideoPosition->SetToolTip(
		_("Current frame time and number. K marks a keyframe.") + "\n" +
		_("Click the time to type a time (0:01:23.456, or seconds such as 83.5), or the frame number to type a frame, then press Enter; Esc or a click elsewhere cancels.") + "\n" +
		_("Right-click to copy the time or the frame number."));
	// The time takes a time or a frame number; the frame number only a frame
	VideoPosition->EnableEditing(
		[this](wxString const& text) { return JumpTo(text); },
		[this](wxString text) {
			text.Trim(true).Trim(false);
			return !text.empty() && text.find_first_not_of("0123456789") == wxString::npos && JumpTo(text);
		});
	VideoPosition->SetFocusAfterEditing(slider);
	VideoPosition->SetCopyItems({
		{_("Copy time"), [this, context] { return context->project->VideoProvider() ? VideoPosition->GetMain() : wxString(); }},
		{_("Copy frame number"), [this, context] { return context->project->VideoProvider() ? VideoPosition->GetSecondary() : wxString(); }},
	});

	VideoSubsPos = new VideoTimeLabel(videoArea, "+00000ms +00000ms", false, true);
	VideoSubsPos->SetToolTip(
		_("Time of this frame relative to start and end of current subs") + "\n\n" +
		_("First: this frame minus the line's start. Negative means the line has not started yet.") + "\n" +
		_("Second: this frame minus the line's end. Negative means the line has not ended yet.") + "\n" +
		_("Both negative: before the line. First positive, second negative: during the line. Both positive: after it.") + "\n" +
		_("Useful for timing to the frame, e.g. +42ms on a scene change means the line starts 42ms too early.") + "\n\n" +
		_("Right-click to copy."));
	VideoSubsPos->SetCopyItems({{_("Copy"), [this, context]() -> wxString {
		AssDialogue const* line = context->selectionController->ReadActiveLine();
		if (!line || !context->project->VideoProvider()) return "";
		int time = context->videoController->TimeAtFrame(context->videoController->GetFrameN(), agi::vfr::EXACT);
		return fmt_wx("%+dms; %+dms", time - line->Start, time - line->End);
	}}});

	auto zoomBox = new VideoZoomControl(videoArea);

	auto visualToolBar = toolbar::GetToolbar(toolsArea, "visual_tools", context, "Video", true);
	auto visualSubToolBar = new wxToolBar(toolsArea, -1, wxDefaultPosition, wxDefaultSize, wxTB_VERTICAL | wxTB_BOTTOM | wxTB_NODIVIDER | wxTB_FLAT);

	auto videoDisplay = new VideoDisplay(visualSubToolBar, isDetached, zoomBox, videoArea, context);
	videoDisplay->MoveBeforeInTabOrder(videoSlider);
	// Picking a visual tool is for working on the video: the keyboard goes
	// there, for its keys (docs/keyboard-regions.md)
	regions::FocusAfterClicks(visualToolBar, videoDisplay);
	regions::FocusAfterClicks(visualSubToolBar, videoDisplay);

	auto toolbarSizer = new wxBoxSizer(wxVERTICAL);
	// The column is as wide as the wider of the two: the other sits in its
	// middle, not at its left with the spare width to its right
	toolbarSizer->Add(visualToolBar, wxSizerFlags(1).CenterHorizontal());
	toolbarSizer->Add(visualSubToolBar, wxSizerFlags().CenterHorizontal());
	toolsArea->SetSizer(toolbarSizer);
	// Toolbars have a colour of their own in some themes: the block's instead
	for (wxWindow *bar : {static_cast<wxWindow*>(mainToolbar), static_cast<wxWindow*>(optionsToolbar), static_cast<wxWindow*>(visualToolBar), static_cast<wxWindow*>(visualSubToolBar)})
		theme::UseSurface(bar);

	// Readouts on the left, play buttons in the middle, options and the
	// zoom on the right
	const int gap = theme::BlockGap(this);
	auto readouts = new wxBoxSizer(wxHORIZONTAL);
	readouts->Add(VideoPosition, wxSizerFlags(0).Expand());
	readouts->Add(VideoSubsPos, wxSizerFlags(0).Expand());
	auto options = new wxBoxSizer(wxHORIZONTAL);
	options->Add(optionsToolbar, wxSizerFlags(0).Center());
	options->Add(zoomBox, wxSizerFlags(0).Center().Border(wxLEFT, gap));
	auto videoBottomSizer = new CentredRowSizer(gap);
	videoBottomSizer->Add(readouts);
	videoBottomSizer->Add(mainToolbar);
	videoBottomSizer->Add(options);

	auto VideoSizer = new wxBoxSizer(wxVERTICAL);
	// A video narrower than the controls below sits in the middle of its
	// block, not at the left with a gap between it and the visual tools
	VideoSizer->Add(videoDisplay, 1, isDetached ? wxEXPAND : wxALIGN_CENTER_HORIZONTAL, 0);
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
	VideoPosition->SetEditable(context->project->VideoProvider() != nullptr);
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

bool VideoBox::JumpTo(wxString text) {
	auto provider = context->project->VideoProvider();
	text.Trim(true).Trim(false);
	if (!provider || text.empty()) return false;
	// Digits alone are a frame number
	if (text.find_first_not_of("0123456789") == wxString::npos) {
		unsigned long frame = 0;
		if (!text.ToULong(&frame)) return false;
		context->videoController->JumpToFrame(mid<int>(0, std::min<unsigned long>(frame, INT_MAX), provider->GetFrameCount() - 1));
		return true;
	}
	// Anything else must read as a time: h:mm:ss.ms, m:ss or seconds
	if (text.find_first_not_of("0123456789:.,") != wxString::npos) return false;
	if (text.find_first_of("0123456789") == wxString::npos) return false;
	context->videoController->JumpToTime(agi::Time(from_wx(text)));
	return true;
}

void VideoBox::SetToolbarGap(int gap) {
	// Same space on both sides of the visual tools: the caller passes the
	// space between them and whatever is to their right.
	toolbar_item->SetBorder(gap);
	Layout();
}

// Copyright (c) 2005, Rodrigo Braz Monteiro, Niels Martin Hansen
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

#include "audio_box.h"
#include "theme.h"

#include "include/aegisub/context.h"
#include "include/aegisub/toolbar.h"

#include "audio_controller.h"
#include "audio_display.h"
#include "audio_karaoke.h"
#include "audio_timing.h"
#include "options.h"
#include "project.h"
#include "audio_levels.h"
#include "command/command.h"
#include "utils.h"

#include <cmath>
#include <wx/panel.h>
#include <wx/scrolbar.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/toolbar.h>

AudioBox::AudioBox(wxWindow *parent, agi::Context *context)
: wxSashWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxCLIP_CHILDREN)
, controller(context->audioController.get())
, context(context)
, audio_open_connection(context->audioController->AddAudioPlayerOpenListener(&AudioBox::OnAudioOpen, this))
, panel(new wxPanel(this, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_NONE))
, audioDisplay(new AudioDisplay(panel, context->audioController.get(), context))
, levels(new AudioLevels(panel, {
	[this](int pos) { SetVerticalZoom(pos); },
	[this](int pos) { SetVolume(pos); },
	[this] { cmd::call("audio/opt/vertical_link", this->context); },
	[this] { cmd::call("audio/opt/reset_levels", this->context); },
}))
{
	SetSashVisible(wxSASH_BOTTOM, true);
	// A block on the backdrop (docs/theme.md): the resize strip below it is
	// the gap to the next block, drawn in the backdrop's colour.
	SetDefaultBorderSize(theme::BlockGap(this));
	theme::UseBackdrop(this);
	theme::UseSurface(panel);
	Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
		wxPaintDC dc(this);
		dc.SetBackground(wxBrush(GetBackgroundColour()));
		dc.Clear();
	});
	Bind(wxEVT_SASH_DRAGGED, &AudioBox::OnSashDrag, this);

	// Vertical zoom and volume: slim tracks drawn like the waveform
	// (docs/audio-waveform.md), with the link and reset buttons below them
	levels->SetZoom(mid<int>(1, OPT_GET("Audio/Zoom/Vertical")->GetInt(), 100));
	levels->SetVolume(mid<int>(1, OPT_GET("Audio/Volume")->GetInt(), 100));
	levels->SetLinked(OPT_GET("Audio/Link")->GetBool());
	if (levels->IsLinked()) levels->SetVolume(levels->GetZoom());
	BindConnection(OPT_SUB("Audio/Link", &AudioBox::OnVerticalLink, this));

	// Top sizer
	wxSizer *TopSizer = new wxBoxSizer(wxHORIZONTAL);
	TopSizer->Add(audioDisplay,1,wxEXPAND,0);
	TopSizer->Add(levels,0,wxEXPAND,0);

	context->karaoke = new AudioKaraoke(panel, context);

	// Main sizer
	auto MainSizer = new wxBoxSizer(wxVERTICAL);
	MainSizer->Add(TopSizer,1,wxEXPAND|wxALL,3);
	auto audio_toolbar = toolbar::GetToolbar(panel, "audio", context, "Audio");
	theme::UseSurface(audio_toolbar); // The block's colour, not the toolbar's own
	MainSizer->Add(audio_toolbar,0,wxEXPAND|wxLEFT|wxRIGHT,3);
	MainSizer->Add(context->karaoke,0,wxEXPAND|wxALL,3);
	MainSizer->Show(context->karaoke, false);
	panel->SetSizer(MainSizer);

	wxSizer *audioSashSizer = new wxBoxSizer(wxHORIZONTAL);
	audioSashSizer->Add(panel, 1, wxEXPAND);
	SetSizerAndFit(audioSashSizer);
	SetMinSize(wxSize(-1, OPT_GET("Audio/Display Height")->GetInt()));
	SetMinimumSizeY(panel->GetSize().GetHeight());

	audioDisplay->Bind(wxEVT_MOUSEWHEEL, &AudioBox::OnMouseWheel, this);

	// Horizontal zoom is on the scrollbar: drag the ends of its thumb
	audioDisplay->SetZoomLevel(OPT_GET("Audio/Zoom/Horizontal")->GetInt());
	audioDisplay->SetAmplitudeScale(pow(levels->GetZoom() / 50.0, 3));
}


void AudioBox::OnMouseWheel(wxMouseEvent &evt) {
	if (!ForwardMouseWheelEvent(audioDisplay, evt))
		return;
	// Alt + wheel always zooms horizontally
	const bool alt = WheelAltDown(evt);
	if (alt) NoteAltUsedForWheel(); else ResetAltUsedForWheel();
	bool zoom = alt || WheelCmdDown(evt) != OPT_GET("Audio/Wheel Default to Zoom")->GetBool();
	if (!zoom) {
		int amount = -evt.GetWheelRotation();
		// If the user did a horizontal scroll the amount should be inverted
		// for it to be natural.
		if (evt.GetWheelAxis() == 1) amount = -amount;

		// Reset any accumulated zoom
		mouse_zoom_accum = 0;

		audioDisplay->ScrollBy(amount);
	}
	else if (evt.GetWheelAxis() == 0) {
		mouse_zoom_accum += evt.GetWheelRotation();
		int zoom_delta = mouse_zoom_accum / evt.GetWheelDelta();
		mouse_zoom_accum %= evt.GetWheelDelta();
		SetHorizontalZoom(audioDisplay->GetZoomLevel() + zoom_delta);
	}
}

void AudioBox::OnSashDrag(wxSashEvent &event) {
	if (event.GetDragStatus() == wxSASH_STATUS_OUT_OF_RANGE)
		return;

	int new_height = std::min(event.GetDragRect().GetHeight(), GetParent()->GetSize().GetHeight() - 1);

	SetMinSize(wxSize(-1, new_height));
	GetParent()->Layout();

	// Karaoke mode is always disabled when the audio box is first opened, so
	// the initial height shouldn't include it
	if (context->karaoke->IsEnabled())
		new_height -= context->karaoke->GetSize().GetHeight() + 6;

	OPT_SET("Audio/Display Height")->SetInt(new_height);
}

void AudioBox::SetHorizontalZoom(int new_zoom) {
	new_zoom = mid(-50, new_zoom, 30);
	audioDisplay->SetZoomLevel(new_zoom);
	OPT_SET("Audio/Zoom/Horizontal")->SetInt(new_zoom);
}

void AudioBox::SetVerticalZoom(int pos) {
	pos = mid(1, pos, 100);
	levels->SetZoom(pos);
	OPT_SET("Audio/Zoom/Vertical")->SetInt(pos);
	double value = pow(pos / 50.0, 3);
	audioDisplay->SetAmplitudeScale(value);
	if (levels->IsLinked())
		SetVolume(pos);
}

void AudioBox::SetVolume(int pos) {
	pos = mid(1, pos, 100);
	levels->SetVolume(pos);
	OPT_SET("Audio/Volume")->SetInt(pos);
	controller->SetVolume(pow(pos / 50.0, 3));
}

void AudioBox::ResetLevels() {
	SetVerticalZoom(AudioLevels::neutral_position);
	SetVolume(AudioLevels::neutral_position);
}

void AudioBox::OnVerticalLink(agi::OptionValue const& opt) {
	levels->SetLinked(opt.GetBool());
	if (opt.GetBool())
		SetVolume(levels->GetZoom());
}

void AudioBox::OnAudioOpen() {
	controller->SetVolume(pow(levels->GetVolume() / 50.0, 3));
}

void AudioBox::ShowKaraokeBar(bool show) {
	wxSizer *panel_sizer = panel->GetSizer();
	if (panel_sizer->IsShown(context->karaoke) == show) return;

	int new_height = GetSize().GetHeight();
	int kara_height = context->karaoke->GetSize().GetHeight() + 6;

	if (show)
		new_height += kara_height;
	else
		new_height -= kara_height;

	panel_sizer->Show(context->karaoke, show);
	SetMinSize(wxSize(-1, new_height));
	GetParent()->Layout();
}

void AudioBox::ScrollAudioBy(int pixel_amount) {
	audioDisplay->ScrollBy(pixel_amount);
}

void AudioBox::ScrollToActiveLine(ScrollMode mode) {
	if (controller->GetTimingController()) {
		switch (mode) {
			case ScrollMode::Range:
				audioDisplay->ScrollTimeRangeInView(controller->GetTimingController()->GetIdealVisibleTimeRange());
				break;
			case ScrollMode::Start:
				audioDisplay->ScrollTimeToCenter(controller->GetTimingController()->GetActiveLineRange().begin());
				break;
			case ScrollMode::End:
				audioDisplay->ScrollTimeToCenter(controller->GetTimingController()->GetActiveLineRange().end());
				break;
		}
	}
}

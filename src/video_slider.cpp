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

/// @file video_slider.cpp
/// @brief Seek-bar control for video
/// @ingroup custom_control
///

#include "video_slider.h"

#include "async_video_provider.h"
#include "base_grid.h"
#include "command/command.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "options.h"
#include "project.h"
#include "theme.h"
#include "utils.h"
#include "video_controller.h"

#include <libaegisub/ass/time.h>

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/intl.h>
#include <wx/settings.h>

VideoSlider::VideoSlider (wxWindow* parent, agi::Context *c)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE | wxBORDER_NONE)
, c(c)
, connections(agi::signal::make_vector({
	OPT_SUB("Video/Slider/Show Keyframes", [this] { Refresh(false); }),
	c->videoController->AddSeekListener(&VideoSlider::SetValue, this),
	c->project->AddVideoProviderListener(&VideoSlider::VideoOpened, this),
	c->project->AddKeyframesListener(&VideoSlider::KeyframesChanged, this),
	theme::AddChangeListener([this] { Refresh(false); }),
}))
{
	SetMinSize(wxSize(FromDIP(20), FromDIP(22)));
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetToolTip(_("Seek video"));

	c->videoSlider = this;
	VideoOpened(c->project->VideoProvider());
}

void VideoSlider::SetValue(int value) {
	if (val == value) return;
	value = mid(0, value, max);
	if (GetXAtValue(val) != GetXAtValue(value))
		Refresh(false);
	val = value;
}

void VideoSlider::SetLineRange(int start, int end) {
	if (start < 0 || end < start) start = end = -1;
	if (start == line_start && end == line_end) return;
	line_start = start;
	line_end = end;
	Refresh(false);
}

void VideoSlider::VideoOpened(AsyncVideoProvider *provider) {
	if (provider) {
		max = provider->GetFrameCount() - 1;
		Refresh(false);
	}
}

void VideoSlider::KeyframesChanged(std::vector<int> const& newKeyframes) {
	keyframes = newKeyframes;
	Refresh(false);
}

int VideoSlider::Margin() const {
	// The largest handle and its focus ring fit at either end
	return FromDIP(10);
}

int VideoSlider::GetValueAtX(int x) const {
	int w = GetClientSize().GetWidth() - 2 * Margin();
	if (w <= 0 || max <= 0) return 0;
	return mid(0, (int)(((int64_t)(x - Margin()) * max + w / 2) / w), max);
}

int VideoSlider::GetXAtValue(int value) const {
	int w = GetClientSize().GetWidth() - 2 * Margin();
	if (max <= 0 || w <= 0) return Margin();
	return (int)((int64_t)value * w / max) + Margin();
}

void VideoSlider::UpdateToolTip(int x) {
	if (!c->project->VideoProvider()) {
		hover_frame = -1;
		SetToolTip(_("Seek video"));
		return;
	}
	int frame = GetValueAtX(x);
	if (frame == hover_frame) return;
	hover_frame = frame;
	auto time = agi::Time(c->videoController->TimeAtFrame(frame, agi::vfr::EXACT)).GetAssFormatted(true);
	SetToolTip(_("Seek video") + "\n" + fmt_tl("%s, frame %d", time, frame));
}

BEGIN_EVENT_TABLE(VideoSlider, wxWindow)
	EVT_MOUSE_EVENTS(VideoSlider::OnMouse)
	EVT_KEY_DOWN(VideoSlider::OnKeyDown)
	EVT_CHAR_HOOK(VideoSlider::OnCharHook)
	EVT_PAINT(VideoSlider::OnPaint)
	EVT_SET_FOCUS(VideoSlider::OnFocus)
	EVT_KILL_FOCUS(VideoSlider::OnFocus)
END_EVENT_TABLE()

void VideoSlider::OnMouse(wxMouseEvent &event) {
	if (event.Leaving()) {
		hover = false;
		Refresh(false);
		event.Skip();
		return;
	}
	if (!hover && (event.Entering() || event.Moving() || event.Dragging())) {
		hover = true;
		Refresh(false);
	}
	// Not while dragging: the handle has to keep up with the mouse
	if (event.Moving() || event.Entering())
		UpdateToolTip(event.GetX());

	bool had_focus = HasFocus();
	if (event.ButtonDown())
		SetFocus();

	if (event.LeftIsDown()) {
		int x = event.GetX();

		// If the slider didn't already have focus, don't seek if the user
		// clicked on the handle as they were probably just trying to focus
		// the slider
		if (!had_focus && abs(x - GetXAtValue(val)) < FromDIP(4))
			return;

		// Shift click to snap to keyframe
		if (event.ShiftDown() && keyframes.size()) {
			int clickedFrame = GetValueAtX(x);
			auto pos = lower_bound(keyframes.begin(), keyframes.end(), clickedFrame);
			if (pos == keyframes.end())
				--pos;
			else if (pos + 1 != keyframes.end() && clickedFrame - *pos > (*pos + 1) - clickedFrame)
				++pos;

			if (*pos == val) return;
			SetValue(*pos);
		}
		// Normal click
		else {
			int go = GetValueAtX(x);
			if (go == val) return;
			SetValue(go);
		}

		// Paint the handle now rather than when the queue is idle: while
		// dragging, mouse moves keep coming and the paint would trail the
		// video
		Update();
		c->videoController->JumpToFrame(val);
	}
	else if (event.GetWheelRotation() != 0 && ForwardMouseWheelEvent(this, event)) {
		// If mouse is over the slider, use wheel to step by frames or keyframes (when Shift is held)
		if (event.ShiftDown())
			if (event.GetWheelRotation() < 0)
				cmd::call("video/frame/next/keyframe", c);
			else
				cmd::call("video/frame/prev/keyframe", c);
		else {
			SetValue(val + (event.GetWheelRotation() > 0 ? -1 : 1));
			c->videoController->JumpToFrame(val);
		}
	}
}

void VideoSlider::OnCharHook(wxKeyEvent &event) {
	hotkey::check("Video", c, event);
}

void VideoSlider::OnKeyDown(wxKeyEvent &event) {
	// Forward up/down/pgup/pgdn/home/end to grid as those aren't yet handled by commands
	switch (event.GetKeyCode()) {
		case WXK_UP:
		case WXK_DOWN:
		case WXK_PAGEUP:
		case WXK_PAGEDOWN:
		case WXK_HOME:
		case WXK_END:
			c->subsGrid->GetEventHandler()->ProcessEvent(event);
			break;
		default:
			event.Skip();
	}
}

void VideoSlider::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered); // smooth circles
	int w, h;
	GetClientSize(&w, &h);

	// Shades of the block's colour, like the other drawn tracks
	wxColour surface = theme::Surface();
	wxColour text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
	wxColour accent = theme::UiColours().accent;
	wxColour groove = theme::Mix(surface, text, 0.22);
	wxColour tick = theme::Mix(surface, text, 0.5);
	wxColour line_tint = theme::Mix(surface, accent, theme::IsDark() ? 0.3 : 0.2);
	wxColour focus_ring = theme::Mix(surface, accent, 0.4);

	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(surface));
	dc.DrawRectangle(0, 0, w, h);

	bool has_video = c->project->VideoProvider() != nullptr;
	int left = Margin(), right = w - Margin();
	if (right <= left) return;
	int track = FromDIP(hover ? 6 : 4);
	int cy = h / 2 + FromDIP(2); // room for the keyframe ticks above
	int track_top = cy - track / 2;

	// The active line's time, behind the track
	if (has_video && line_start >= 0) {
		int x1 = GetXAtValue(line_start), x2 = GetXAtValue(std::min(line_end, max));
		if (x2 - x1 < FromDIP(2)) x2 = x1 + FromDIP(2);
		dc.SetBrush(wxBrush(line_tint));
		dc.DrawRectangle(x1, cy - FromDIP(7), x2 - x1, FromDIP(14));
	}

	// Track, played part in the accent colour
	dc.SetBrush(wxBrush(groove));
	dc.DrawRectangle(left, track_top, right - left, track);
	if (!has_video) return;
	int cur = GetXAtValue(val);
	dc.SetBrush(wxBrush(accent));
	dc.DrawRectangle(left, track_top, cur - left, track);

	// Keyframes as ticks above the track
	if (OPT_GET("Video/Slider/Show Keyframes")->GetBool()) {
		dc.SetBrush(wxBrush(tick));
		int tick_w = std::max(1, FromDIP(1));
		int tick_bottom = cy - FromDIP(5);
		int tick_top = tick_bottom - FromDIP(5);
		int last = -1;
		for (int frame : keyframes) {
			int x = GetXAtValue(frame);
			if (x == last) continue;
			last = x;
			dc.DrawRectangle(x, tick_top, tick_w, tick_bottom - tick_top);
		}
	}

	// Handle: a ring in the block's colour sets it off the played part;
	// focus adds an outer ring, a keyframe a hollow centre
	int radius = FromDIP(hover ? 7 : 6);
	if (HasFocus()) {
		dc.SetBrush(wxBrush(focus_ring));
		dc.DrawCircle(cur, cy, radius + FromDIP(3));
	}
	dc.SetBrush(wxBrush(surface));
	dc.DrawCircle(cur, cy, radius + FromDIP(1));
	dc.SetBrush(wxBrush(accent));
	dc.DrawCircle(cur, cy, radius);
	if (std::binary_search(keyframes.begin(), keyframes.end(), val)) {
		dc.SetBrush(wxBrush(surface));
		dc.DrawCircle(cur, cy, FromDIP(2));
	}
}

void VideoSlider::OnFocus(wxFocusEvent &event) {
	Refresh(false);
	event.Skip();
}

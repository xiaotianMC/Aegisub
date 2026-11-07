// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "time_unit_toggle.h"

#include "theme.h"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/intl.h>
#include <wx/settings.h>

namespace {
wxString PartLabel(int part) {
	return part == 0 ? _("Time") : _("Frame");
}
}

TimeUnitToggle::TimeUnitToggle(wxWindow *parent)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	int widest = std::max(GetTextExtent(PartLabel(0)).x, GetTextExtent(PartLabel(1)).x);
	SetMinSize(wxSize(2 * (widest + FromDIP(16)), GetCharHeight() + FromDIP(10)));
	SetToolTip(_("Show and type the times of lines as h:mm:ss.cs, or as frame numbers (needs a video or timecodes)"));
	Bind(wxEVT_PAINT, &TimeUnitToggle::OnPaint, this);
	for (auto type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_DCLICK, wxEVT_MOTION, wxEVT_ENTER_WINDOW, wxEVT_LEAVE_WINDOW})
		Bind(type, &TimeUnitToggle::OnMouse, this);
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
}

void TimeUnitToggle::SetByFrame(bool on) {
	on = on && frames_allowed;
	if (on == by_frame) return;
	by_frame = on;
	Refresh(false);
}

void TimeUnitToggle::AllowFrames(bool on) {
	if (on == frames_allowed) return;
	frames_allowed = on;
	if (!on) SetByFrame(false);
	Refresh(false);
}

void TimeUnitToggle::Choose(int part) {
	if (!IsEnabled() || (part == 1 && !frames_allowed)) return;
	bool on = part == 1;
	if (on == by_frame) return;
	by_frame = on;
	Refresh(false);
	if (on_change) on_change(by_frame);
}

wxRect TimeUnitToggle::PartRect(int part) const {
	wxSize size = GetClientSize();
	int half = size.x / 2;
	return part == 0 ? wxRect(0, 0, half, size.y) : wxRect(half, 0, size.x - half, size.y);
}

void TimeUnitToggle::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	wxSize size = GetClientSize();
	wxColour surface = theme::Surface();
	wxColour text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
	if (!IsEnabled()) text = theme::Mix(surface, text, 0.4);
	wxColour field = theme::Mix(surface, text, theme::IsDark() ? 0.08 : 0.05);
	wxColour line = theme::Mix(surface, text, 0.2);
	wxColour accent = theme::UiColours().accent;

	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(surface));
	dc.DrawRectangle(wxPoint(0, 0), size);

	// One rounded field; the chosen part a raised, tinted pill inside it
	int radius = FromDIP(5);
	dc.SetBrush(wxBrush(field));
	dc.SetPen(wxPen(line, std::max(1, FromDIP(1))));
	dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), radius);

	dc.SetFont(GetFont());
	for (int part : {0, 1}) {
		wxRect rect = PartRect(part);
		bool chosen = (part == 1) == by_frame;
		bool allowed = part == 0 || frames_allowed;
		wxRect inner = rect.Deflate(FromDIP(2));
		dc.SetPen(*wxTRANSPARENT_PEN);
		if (chosen && IsEnabled()) {
			dc.SetBrush(wxBrush(theme::Mix(surface, accent, theme::IsDark() ? 0.4 : 0.25)));
			dc.DrawRoundedRectangle(inner, radius - FromDIP(1));
		}
		else if (hover == part && allowed && IsEnabled()) {
			dc.SetBrush(wxBrush(theme::Mix(field, text, 0.08)));
			dc.DrawRoundedRectangle(inner, radius - FromDIP(1));
		}
		wxString label = PartLabel(part);
		wxSize extent = dc.GetTextExtent(label);
		dc.SetTextForeground(allowed ? text : theme::Mix(surface, text, 0.35));
		dc.DrawText(label, rect.x + (rect.width - extent.x) / 2, (size.y - extent.y) / 2);
	}
}

void TimeUnitToggle::OnMouse(wxMouseEvent &event) {
	int part = event.GetX() < GetClientSize().x / 2 ? 0 : 1;
	if (event.Leaving()) part = -1;
	if (part != hover) {
		hover = part;
		Refresh(false);
	}
	// Handled, not skipped: the click must not move the keyboard
	if (event.LeftDown() || event.LeftDClick())
		Choose(part);
}

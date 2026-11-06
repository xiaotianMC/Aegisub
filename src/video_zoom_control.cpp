// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "video_zoom_control.h"

#include "format.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/intl.h>
#include <wx/menu.h>
#include <wx/settings.h>

namespace {
constexpr double step = .125; // as the zoom has always stepped
constexpr double min_zoom = .125;
constexpr double max_zoom = 10;
}

VideoZoomControl::VideoZoomControl(wxWindow *parent)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	int value_width = GetTextExtent("187.5%").x + FromDIP(24);
	SetMinSize(wxSize(2 * ButtonWidth() + value_width, FromDIP(28)));
	Bind(wxEVT_PAINT, &VideoZoomControl::OnPaint, this);
	for (auto type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_MOTION, wxEVT_ENTER_WINDOW, wxEVT_LEAVE_WINDOW})
		Bind(type, &VideoZoomControl::OnMouse, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) { pressed = Part::None; Refresh(false); });
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
	UpdateToolTip();
}

std::vector<int> VideoZoomControl::Presets() {
	std::vector<int> presets;
	for (int percent = 25; percent <= 300; percent += 25)
		presets.push_back(percent);
	return presets;
}

double VideoZoomControl::StepIn(double zoom) {
	return std::min(max_zoom, zoom + step);
}

double VideoZoomControl::StepOut(double zoom) {
	return std::max(min_zoom, zoom - step);
}

void VideoZoomControl::SetZoom(double new_zoom) {
	if (new_zoom == zoom) return;
	zoom = new_zoom;
	Refresh(false);
}

wxString VideoZoomControl::ZoomText() const {
	// Steps of 12.5% show their half percent; other zooms (a resized
	// detached video) are rounded to one decimal
	return fmt_wx("%g%%", std::round(zoom * 1000) / 10);
}

int VideoZoomControl::ButtonWidth() const {
	return FromDIP(28);
}

wxRect VideoZoomControl::PartRect(Part part) const {
	wxSize size = GetClientSize();
	int button = std::min(ButtonWidth(), size.x / 3);
	switch (part) {
		case Part::Out:   return wxRect(0, 0, button, size.y);
		case Part::In:    return wxRect(size.x - button, 0, button, size.y);
		case Part::Value: return wxRect(button, 0, size.x - 2 * button, size.y);
		default:          return wxRect();
	}
}

VideoZoomControl::Part VideoZoomControl::HitTest(wxPoint point) const {
	for (Part part : {Part::Out, Part::Value, Part::In})
		if (PartRect(part).Contains(point)) return part;
	return Part::None;
}

void VideoZoomControl::Request(double new_zoom) {
	new_zoom = std::clamp(new_zoom, min_zoom, max_zoom);
	if (on_zoom) on_zoom(new_zoom);
}

void VideoZoomControl::Press(Part part) {
	if (part == Part::Out) Request(StepOut(zoom));
	else if (part == Part::In) Request(StepIn(zoom));
}

void VideoZoomControl::ShowPresets() {
	wxMenu menu;
	auto presets = Presets();
	int current = static_cast<int>(std::lround(zoom * 100));
	for (size_t i = 0; i < presets.size(); ++i) {
		auto item = menu.AppendCheckItem(wxID_HIGHEST + 1 + i, fmt_wx("%d%%", presets[i]));
		item->Check(presets[i] == current);
	}
	wxRect value = PartRect(Part::Value);
	int chosen = GetPopupMenuSelectionFromUser(menu, value.GetLeft(), value.GetBottom());
	if (chosen <= wxID_HIGHEST) return;
	size_t index = chosen - wxID_HIGHEST - 1;
	if (index < presets.size()) Request(presets[index] / 100.);
}

void VideoZoomControl::UpdateToolTip() {
	switch (hover) {
		case Part::Out: SetToolTip(_("Zoom video out")); break;
		case Part::In: SetToolTip(_("Zoom video in")); break;
		default: SetToolTip(_("Video zoom: click for presets")); break;
	}
}

void VideoZoomControl::OnMouse(wxMouseEvent &event) {
	Part part = HitTest(event.GetPosition());
	if (event.Leaving()) {
		hover = Part::None;
		Refresh(false);
		return;
	}
	if (part != hover) {
		hover = part;
		UpdateToolTip();
		Refresh(false);
	}
	if (event.LeftDown() || event.LeftDClick()) {
		// Double clicks step again rather than being swallowed
		pressed = part;
		Refresh(false);
		if (part == Part::Value) {
			pressed = Part::None;
			ShowPresets();
			Refresh(false);
		}
		else
			Press(part);
	}
	else if (event.LeftUp()) {
		pressed = Part::None;
		Refresh(false);
	}
}

void VideoZoomControl::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	wxSize size = GetClientSize();
	wxColour surface = theme::Surface();
	wxColour text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
	if (!IsEnabled()) text = theme::Mix(surface, text, 0.4);
	wxColour field = theme::Mix(surface, text, theme::IsDark() ? 0.08 : 0.05);
	wxColour line = theme::Mix(surface, text, 0.2);

	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(surface));
	dc.DrawRectangle(wxPoint(0, 0), size);

	// One rounded field holding the three parts, hovered part a shade darker
	int radius = FromDIP(5);
	wxRect outer(wxPoint(0, 0), size);
	dc.SetBrush(wxBrush(field));
	dc.SetPen(wxPen(line, std::max(1, FromDIP(1))));
	dc.DrawRoundedRectangle(outer, radius);
	dc.SetPen(*wxTRANSPARENT_PEN);
	if (hover != Part::None && IsEnabled()) {
		wxRect r = PartRect(hover).Deflate(FromDIP(2));
		dc.SetBrush(wxBrush(theme::Mix(surface, text, pressed == hover ? 0.22 : 0.12)));
		dc.DrawRoundedRectangle(r, radius - 2);
	}
	// Dividers between the parts
	dc.SetBrush(wxBrush(line));
	int divider = size.y / 2;
	for (Part part : {Part::Out, Part::Value}) {
		wxRect r = PartRect(part);
		dc.DrawRectangle(r.GetRight(), (size.y - divider) / 2, std::max(1, FromDIP(1)), divider);
	}

	// − and + as lines, crisp at any scale
	dc.SetBrush(wxBrush(text));
	int arm = FromDIP(5), thick = std::max(1, FromDIP(3) / 2);
	for (Part part : {Part::Out, Part::In}) {
		wxRect r = PartRect(part);
		wxPoint c(r.x + r.width / 2, r.y + r.height / 2);
		dc.DrawRectangle(c.x - arm, c.y - thick / 2, 2 * arm, thick);
		if (part == Part::In)
			dc.DrawRectangle(c.x - thick / 2, c.y - arm, thick, 2 * arm);
	}

	// The value with a small chevron for the presets
	wxRect value = PartRect(Part::Value);
	wxString label = ZoomText();
	wxSize extent = dc.GetTextExtent(label);
	int chevron = FromDIP(7);
	int x = value.x + (value.width - extent.x - FromDIP(5) - chevron) / 2;
	dc.SetFont(GetFont());
	dc.SetTextForeground(text);
	dc.DrawText(label, x, value.y + (value.height - extent.y) / 2);
	int cx = x + extent.x + FromDIP(5), cy = value.y + value.height / 2 - FromDIP(1);
	dc.SetPen(wxPen(theme::Mix(surface, text, 0.6), std::max(1, FromDIP(3) / 2)));
	dc.DrawLine(cx, cy - chevron / 4, cx + chevron / 2, cy + chevron / 4);
	dc.DrawLine(cx + chevron / 2, cy + chevron / 4, cx + chevron, cy - chevron / 4);
}

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "audio_levels.h"

#include "format.h"
#include "theme.h"

#include <libaegisub/format.h>

#include <algorithm>
#include <cmath>

#include <wx/bmpbndl.h>
#include <wx/dcbuffer.h>
#include <wx/intl.h>
#include <wx/settings.h>

namespace {
// On the audio block's colour, as the toolbar below the waveform is
// (docs/theme.md): the other colours follow from it and the text colour.
struct Colours {
	wxColour background, track, accent, text, handle, neutral, button_active;
};

Colours Current() {
	Colours c;
	bool dark = theme::IsDark();
	c.background = theme::Surface();
	wxColour ink = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
	c.track = theme::Mix(c.background, ink, dark ? 0.2 : 0.16);
	// The theme's accent, as the video seek bar's played part
	c.accent = theme::UiColours().accent;
	c.text = theme::Mix(c.background, ink, 0.7);
	c.handle = dark ? wxColour(225, 225, 225) : *wxWHITE;
	c.neutral = theme::Mix(c.background, ink, 0.45);
	c.button_active = theme::Mix(c.background, c.accent, dark ? 0.25 : 0.2);
	return c;
}

wxBitmapBundle Icon(const char *body, wxColour colour, int size) {
	auto svg = agi::format(
		"<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
		"<g fill='none' stroke='%s' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'>%s</g></svg>",
		colour.GetAsString(wxC2S_HTML_SYNTAX).utf8_string(), body);
	return wxBitmapBundle::FromSVG(svg.c_str(), wxSize(size, size));
}

// A chain of two links, and an arrow going round
const char *link_icon = "<path d='M6.5 9.5l3-3'/><path d='M7 4.5l1.2-1.2a2.6 2.6 0 0 1 3.7 3.7L10.7 8.2'/>"
	"<path d='M9 11.5l-1.2 1.2a2.6 2.6 0 0 1-3.7-3.7L5.3 7.8'/>";
const char *reset_icon = "<path d='M3.2 8a4.8 4.8 0 1 0 1.5-3.5'/><path d='M3 2.2v3h3'/>";
}

AudioLevels::AudioLevels(wxWindow *parent, Callbacks callbacks)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
, callbacks(std::move(callbacks))
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(FromDIP(64), FromDIP(80)));
	Bind(wxEVT_PAINT, &AudioLevels::OnPaint, this);
	for (auto type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_MOTION, wxEVT_LEAVE_WINDOW})
		Bind(type, &AudioLevels::OnMouse, this);
	Bind(wxEVT_MOUSEWHEEL, &AudioLevels::OnWheel, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) { dragging = Part::None; Refresh(false); });
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
}

void AudioLevels::SetZoom(int position) {
	position = std::clamp(position, min_position, max_position);
	if (position == zoom) return;
	zoom = position;
	UpdateToolTip();
	Refresh(false);
}

void AudioLevels::SetVolume(int position) {
	position = std::clamp(position, min_position, max_position);
	if (position == volume) return;
	volume = position;
	UpdateToolTip();
	Refresh(false);
}

void AudioLevels::SetLinked(bool new_linked) {
	if (linked == new_linked) return;
	linked = new_linked;
	UpdateToolTip();
	Refresh(false);
}

wxString AudioLevels::ZoomText(int position) {
	double factor = std::pow(position / 50.0, 3);
	if (factor >= 9.95) return fmt_wx("%.0f×", factor);
	if (factor >= 0.995) return fmt_wx("%.1f×", factor);
	if (factor >= 0.01) return fmt_wx("%.2f×", factor);
	return "<0.01×";
}

wxString AudioLevels::VolumeText(int position) {
	// The volume is (position / 50)³ of the signal: 60 log10(position / 50) dB
	int db = static_cast<int>(std::lround(60 * std::log10(position / 50.0)));
	if (db == 0) return "0dB";
	return fmt_wx("%+ddB", db);
}

int AudioLevels::LabelHeight() const {
	return GetCharHeight();
}

wxRect AudioLevels::ButtonRect(Part button) const {
	wxSize size = GetClientSize();
	int height = FromDIP(20);
	int half = size.x / 2;
	return button == Part::Link
		? wxRect(0, size.y - height, half, height)
		: wxRect(half, size.y - height, size.x - half, height);
}

wxRect AudioLevels::TrackColumn(Part track) const {
	wxSize size = GetClientSize();
	int bottom = size.y - FromDIP(20) - LabelHeight() - FromDIP(2);
	if (linked) return wxRect(0, 0, size.x, bottom);
	int half = size.x / 2;
	return track == Part::Zoom ? wxRect(0, 0, half, bottom) : wxRect(half, 0, size.x - half, bottom);
}

wxRect AudioLevels::TrackRect(Part track) const {
	wxRect column = TrackColumn(track);
	int radius = FromDIP(6);
	int width = FromDIP(4);
	return wxRect(column.x + column.width / 2 - width / 2, column.y + radius + FromDIP(2),
		width, std::max(1, column.height - 2 * radius - FromDIP(2)));
}

wxPoint AudioLevels::HandleAt(Part track, int position) const {
	wxRect rect = TrackRect(track);
	double share = double(position - min_position) / (max_position - min_position);
	return wxPoint(rect.x + rect.width / 2, rect.GetBottom() - int(std::lround(share * (rect.height - 1))));
}

int AudioLevels::PositionAt(Part track, int y) const {
	wxRect rect = TrackRect(track);
	double share = double(rect.GetBottom() - y) / std::max(1, rect.height - 1);
	return std::clamp(int(std::lround(min_position + share * (max_position - min_position))), min_position, max_position);
}

AudioLevels::Part AudioLevels::HitTest(wxPoint point) const {
	if (ButtonRect(Part::Link).Contains(point)) return Part::Link;
	if (ButtonRect(Part::Reset).Contains(point)) return Part::Reset;
	if (TrackColumn(Part::Zoom).Contains(point)) return Part::Zoom;
	if (!linked && TrackColumn(Part::Volume).Contains(point)) return Part::Volume;
	return Part::None;
}

void AudioLevels::Move(Part track, int position) {
	position = std::clamp(position, min_position, max_position);
	if (track == Part::Zoom) {
		if (position == zoom) return;
		SetZoom(position);
		if (callbacks.zoom) callbacks.zoom(position);
	}
	else if (track == Part::Volume) {
		if (position == volume) return;
		SetVolume(position);
		if (callbacks.volume) callbacks.volume(position);
	}
}

void AudioLevels::OnMouse(wxMouseEvent &event) {
	wxPoint point = event.GetPosition();
	Part part = HitTest(point);

	if (event.Leaving()) {
		if (dragging == Part::None) hover = Part::None;
		Refresh(false);
		return;
	}

	if (event.LeftDClick()) {
		// Back to neutral; the buttons act on each click
		if (part == Part::Zoom || part == Part::Volume) Move(part, neutral_position);
		else if (part == Part::Link && callbacks.link) callbacks.link();
		else if (part == Part::Reset && callbacks.reset) callbacks.reset();
		return;
	}

	if (event.LeftDown()) {
		if (part == Part::Link) { if (callbacks.link) callbacks.link(); return; }
		if (part == Part::Reset) { if (callbacks.reset) callbacks.reset(); return; }
		if (part != Part::Zoom && part != Part::Volume) return;
		int position = part == Part::Zoom ? zoom : volume;
		// Grab the handle where it was pressed; elsewhere on the track it
		// jumps to the mouse first
		wxPoint handle = HandleAt(part, position);
		if (std::abs(point.y - handle.y) > FromDIP(7)) {
			Move(part, PositionAt(part, point.y));
			position = part == Part::Zoom ? zoom : volume;
		}
		dragging = part;
		drag_last_y = point.y;
		drag_value = position;
		if (!HasCapture()) CaptureMouse();
		Refresh(false);
		return;
	}

	if (event.LeftUp()) {
		if (HasCapture()) ReleaseMouse();
		dragging = Part::None;
		hover = part;
		Refresh(false);
		return;
	}

	if (event.Dragging() && dragging != Part::None) {
		// Shift: a quarter of the speed, for fine steps
		wxRect rect = TrackRect(dragging);
		double per_pixel = double(max_position - min_position) / std::max(1, rect.height - 1);
		if (event.ShiftDown()) per_pixel /= 4;
		drag_value = std::clamp(drag_value + (drag_last_y - point.y) * per_pixel, double(min_position), double(max_position));
		drag_last_y = point.y;
		Move(dragging, int(std::lround(drag_value)));
		return;
	}

	if (part != hover) {
		hover = part;
		UpdateToolTip();
		Refresh(false);
	}
}

void AudioLevels::OnWheel(wxMouseEvent &event) {
	Part part = HitTest(event.GetPosition());
	if (part != Part::Zoom && part != Part::Volume) return;
	int steps = event.GetWheelRotation() / std::max(1, event.GetWheelDelta());
	if (steps == 0) steps = event.GetWheelRotation() > 0 ? 1 : -1;
	Move(part, (part == Part::Zoom ? zoom : volume) + steps);
}

void AudioLevels::UpdateToolTip() {
	wxString hint = _("Double-click to reset. Hold Shift while dragging for fine steps.");
	switch (hover) {
		case Part::Zoom:
			SetToolTip(linked
				? fmt_tl("Vertical zoom and volume: %s, %s", ZoomText(zoom), VolumeText(zoom)) + "\n" + hint
				: fmt_tl("Vertical zoom: %s", ZoomText(zoom)) + "\n" + hint);
			break;
		case Part::Volume:
			SetToolTip(fmt_tl("Audio volume: %s", VolumeText(volume)) + "\n" + hint);
			break;
		case Part::Link:
			SetToolTip(linked ? _("Vertical zoom and volume are linked: click to separate them")
				: _("Link vertical zoom and volume sliders"));
			break;
		case Part::Reset:
			SetToolTip(_("Reset vertical zoom and volume"));
			break;
		case Part::None:
			UnsetToolTip();
			break;
	}
}

void AudioLevels::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC dc(this);
	wxSize size = GetClientSize();
	const Colours colours = Current();
	wxColour const& background = colours.background, &track_colour = colours.track, &accent = colours.accent;
	wxColour const& text_colour = colours.text, &handle_colour = colours.handle, &neutral_mark = colours.neutral;
	wxColour const& button_active = colours.button_active;
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(background));
	dc.DrawRectangle(wxPoint(0, 0), size);

	wxFont font = GetFont();
	font.SetFractionalPointSize(font.GetFractionalPointSize() * 0.85);
	dc.SetFont(font);
	dc.SetTextForeground(text_colour);

	auto draw_track = [&](Part track, int position, wxString const& text) {
		wxRect rect = TrackRect(track);
		wxPoint handle = HandleAt(track, position);
		// Groove, then the part up to the handle in the accent colour
		dc.SetBrush(wxBrush(track_colour));
		dc.DrawRectangle(rect);
		dc.SetBrush(wxBrush(accent));
		dc.DrawRectangle(wxRect(rect.x, handle.y, rect.width, rect.GetBottom() - handle.y + 1));
		// Where neutral is
		wxPoint neutral = HandleAt(track, neutral_position);
		dc.SetBrush(wxBrush(neutral_mark));
		dc.DrawRectangle(rect.x - FromDIP(3), neutral.y, rect.width + FromDIP(6), std::max(1, FromDIP(1)));
		// The handle, highlighted under the mouse and while dragged
		bool active = hover == track || dragging == track;
		dc.SetBrush(wxBrush(active ? accent : handle_colour));
		if (!active && !theme::IsDark()) dc.SetPen(wxPen(neutral_mark, std::max(1, FromDIP(1))));
		dc.DrawCircle(handle, FromDIP(active ? 6 : 5));
		dc.SetPen(*wxTRANSPARENT_PEN);

		wxRect column = TrackColumn(track);
		wxSize extent = dc.GetTextExtent(text);
		wxDCClipper clip(dc, wxRect(column.x, column.GetBottom(), column.width, LabelHeight() + FromDIP(2)));
		dc.DrawText(text, column.x + (column.width - extent.x) / 2, column.GetBottom() + FromDIP(1));
	};
	draw_track(Part::Zoom, zoom, ZoomText(zoom));
	if (!linked) draw_track(Part::Volume, volume, VolumeText(volume));

	auto draw_button = [&](Part button, const char *icon, bool on) {
		wxRect rect = ButtonRect(button);
		if (on || hover == button) {
			dc.SetBrush(wxBrush(on ? button_active : track_colour));
			dc.DrawRectangle(rect.Deflate(FromDIP(2), FromDIP(1)));
		}
		wxBitmap bitmap = Icon(icon, on ? accent : text_colour, 16).GetBitmapFor(this);
		wxSize logical = bitmap.GetLogicalSize();
		dc.DrawBitmap(bitmap, rect.x + (rect.width - logical.x) / 2, rect.y + (rect.height - logical.y) / 2, true);
	};
	draw_button(Part::Link, link_icon, linked);
	draw_button(Part::Reset, reset_icon, false);
}

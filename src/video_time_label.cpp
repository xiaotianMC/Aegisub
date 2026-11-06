// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "video_time_label.h"

#include "theme.h"
#include "utils.h"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/menu.h>
#include <wx/settings.h>

VideoTimeLabel::VideoTimeLabel(wxWindow *parent, wxString sample, bool badge, bool dim)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
, badge(badge)
, dim(dim)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(WidthFor(sample, {}), GetCharHeight() + FromDIP(8)));
	Bind(wxEVT_PAINT, &VideoTimeLabel::OnPaint, this);
	for (auto type : {wxEVT_LEFT_DOWN, wxEVT_ENTER_WINDOW, wxEVT_LEAVE_WINDOW, wxEVT_MOTION})
		Bind(type, &VideoTimeLabel::OnMouse, this);
	Bind(wxEVT_CONTEXT_MENU, &VideoTimeLabel::OnContextMenu, this);
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
}

void VideoTimeLabel::SetParts(wxString const& m, wxString const& s, bool on) {
	on = on && badge;
	if (m == main && s == secondary && on == badge_on) return;
	main = m;
	secondary = s;
	badge_on = on;
	Fit(main, secondary);
	Refresh(false);
}

void VideoTimeLabel::SetBadge(wxString const& text) {
	badge_text = text;
	Refresh(false);
}

void VideoTimeLabel::SetClickHandler(std::function<void()> handler) {
	on_click = std::move(handler);
	SetCursor(on_click ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
}

void VideoTimeLabel::SetCopyItems(std::vector<CopyItem> items) {
	copy_items = std::move(items);
}

namespace {
bool IsDigit(wxUniChar ch) { return ch >= '0' && ch <= '9'; }

wxFont BadgeFont(wxFont font) {
	font.SetFractionalPointSize(font.GetFractionalPointSize() * 0.8);
	font.MakeBold();
	return font;
}
}

int VideoTimeLabel::DigitWidth(wxDC &dc) const {
	int widest = 0;
	for (char d = '0'; d <= '9'; ++d)
		widest = std::max(widest, dc.GetTextExtent(wxString(d)).x);
	return widest;
}

int VideoTimeLabel::TextWidth(wxDC &dc, wxString const& text, int digit) const {
	// Digits in cells, runs of anything else measured whole for kerning
	int width = 0;
	wxString run;
	for (wxUniChar ch : text) {
		if (IsDigit(ch)) {
			if (!run.empty()) { width += dc.GetTextExtent(run).x; run.clear(); }
			width += digit;
		}
		else
			run += ch;
	}
	if (!run.empty()) width += dc.GetTextExtent(run).x;
	return width;
}

int VideoTimeLabel::DrawTabular(wxDC &dc, wxString const& text, int x, int y, int digit) const {
	wxString run;
	auto flush = [&] {
		if (run.empty()) return;
		dc.DrawText(run, x, y);
		x += dc.GetTextExtent(run).x;
		run.clear();
	};
	for (wxUniChar ch : text) {
		if (IsDigit(ch)) {
			flush();
			wxString d(ch);
			dc.DrawText(d, x + (digit - dc.GetTextExtent(d).x) / 2, y);
			x += digit;
		}
		else
			run += ch;
	}
	flush();
	return x;
}

int VideoTimeLabel::BadgeWidth(wxDC &dc) const {
	wxFont old = dc.GetFont();
	dc.SetFont(BadgeFont(old));
	int w = dc.GetTextExtent(badge_text).x + FromDIP(8);
	dc.SetFont(old);
	return w;
}

int VideoTimeLabel::WidthFor(wxString const& m, wxString const& s) const {
	wxMemoryDC dc;
	dc.SetFont(GetFont());
	int digit = DigitWidth(dc);
	int width = 2 * Gap() + TextWidth(dc, m, digit);
	if (!s.empty()) width += Gap() + TextWidth(dc, s, digit);
	if (badge) width += FromDIP(6) + BadgeWidth(dc);
	return width;
}

void VideoTimeLabel::Fit(wxString const& m, wxString const& s) {
	// Never narrower than the sample, wider when the text needs it, so the
	// row does not shift while the numbers change
	int width = WidthFor(m, s);
	wxSize min = GetMinSize();
	if (width <= min.x) return;
	SetMinSize(wxSize(width, min.y));
	if (GetParent()) GetParent()->Layout();
}

void VideoTimeLabel::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	wxSize size = GetClientSize();
	wxColour surface = theme::Surface();
	wxColour text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
	wxColour faint = theme::Mix(surface, text, 0.55);
	wxColour accent = theme::UiColours().accent;

	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(surface));
	dc.DrawRectangle(wxPoint(0, 0), size);

	// Divider on the left, setting the label off what is before it
	dc.SetBrush(wxBrush(theme::Mix(surface, text, 0.2)));
	int divider = size.y * 3 / 5;
	dc.DrawRectangle(0, (size.y - divider) / 2, std::max(1, FromDIP(1)), divider);

	dc.SetFont(GetFont());
	int digit = DigitWidth(dc);
	int y = (size.y - dc.GetCharHeight()) / 2;
	int x = Gap();
	bool clickable = on_click && hover;
	dc.SetTextForeground(dim ? faint : clickable ? accent : text);
	int main_end = DrawTabular(dc, main, x, y, digit);
	if (clickable && !main.empty()) {
		// Underlined like a link while the mouse is on it
		dc.SetBrush(wxBrush(accent));
		dc.DrawRectangle(x, y + dc.GetCharHeight(), main_end - x, std::max(1, FromDIP(1)));
	}
	x = main_end;
	if (!secondary.empty()) {
		dc.SetTextForeground(faint);
		x = DrawTabular(dc, secondary, x + Gap(), y, digit);
	}
	if (badge_on) {
		wxFont font = BadgeFont(GetFont());
		dc.SetFont(font);
		wxSize extent = dc.GetTextExtent(badge_text);
		wxRect rect(x + FromDIP(6), (size.y - extent.y - FromDIP(2)) / 2, extent.x + FromDIP(8), extent.y + FromDIP(2));
		dc.SetBrush(wxBrush(accent));
		dc.DrawRoundedRectangle(rect, FromDIP(3));
		dc.SetTextForeground(surface);
		dc.DrawText(badge_text, rect.x + (rect.width - extent.x) / 2, rect.y + (rect.height - extent.y) / 2);
	}
}

void VideoTimeLabel::OnMouse(wxMouseEvent &event) {
	if (event.Entering() || event.Moving()) {
		if (!hover) { hover = true; if (on_click) Refresh(false); }
	}
	else if (event.Leaving()) {
		hover = false;
		if (on_click) Refresh(false);
	}
	else if (event.LeftDown() && on_click)
		on_click();
	event.Skip();
}

void VideoTimeLabel::OnContextMenu(wxContextMenuEvent &) {
	if (copy_items.empty()) return;
	wxMenu menu;
	for (size_t i = 0; i < copy_items.size(); ++i)
		menu.Append(wxID_HIGHEST + 1 + i, copy_items[i].first);
	int chosen = GetPopupMenuSelectionFromUser(menu);
	if (chosen <= wxID_HIGHEST) return;
	size_t index = chosen - wxID_HIGHEST - 1;
	if (index >= copy_items.size()) return;
	wxString text = copy_items[index].second();
	if (!text.empty()) SetClipboard(text.utf8_string());
}

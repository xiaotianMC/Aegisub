// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "video_time_label.h"

#include "theme.h"
#include "utils.h"

#include <algorithm>
#include <map>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <wx/settings.h>
#include <wx/textctrl.h>

namespace {
bool IsDigit(wxUniChar ch) { return ch >= '0' && ch <= '9'; }

wxFont BadgeFont(wxFont font) {
	font.SetFractionalPointSize(font.GetFractionalPointSize() * 0.8);
	font.MakeBold();
	return font;
}

/// Measures as the drawing does: through a graphics context, whose text
/// can be wider than a plain DC's
struct MeasureDC {
	wxBitmap bitmap{1, 1};
	wxMemoryDC memory{bitmap};
	wxGCDC dc{memory};
	explicit MeasureDC(wxFont const& font) { dc.SetFont(font); }
};

/// The rows of @p text drawn at y = 0 that hold ink: [top, bottom).
/// Glyph boxes include the room for descenders and accents, so a capital
/// centred by its box sits high; centring the ink does not.
std::pair<int, int> InkRows(wxFont const& font, wxString const& text) {
	static std::map<wxString, std::pair<int, int>> cache;
	wxString key = font.GetNativeFontInfoDesc() + "\n" + text;
	auto found = cache.find(key);
	if (found != cache.end()) return found->second;

	const int pad = 2;
	wxSize extent;
	{
		MeasureDC measure(font);
		extent = measure.dc.GetTextExtent(text);
	}
	std::pair<int, int> rows{0, std::max(1, extent.y)};
	wxBitmap bitmap(extent.x + 2 * pad, extent.y + 2 * pad, 24);
	{
		wxMemoryDC memory(bitmap);
		memory.SetBackground(*wxWHITE_BRUSH);
		memory.Clear();
		wxGCDC dc(memory);
		dc.SetFont(font);
		dc.SetTextForeground(*wxBLACK);
		dc.DrawText(text, pad, pad);
	}
	wxImage image = bitmap.ConvertToImage();
	int top = -1, bottom = -1;
	for (int y = 0; y < image.GetHeight(); ++y)
		for (int x = 0; x < image.GetWidth(); ++x)
			if (image.GetRed(x, y) < 128) {
				if (top < 0) top = y;
				bottom = y + 1;
				break;
			}
	if (top >= 0) rows = {top - pad, bottom - pad};
	cache[key] = rows;
	return rows;
}
}

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

VideoTimeLabel::~VideoTimeLabel() {
	StopFiltering();
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

void VideoTimeLabel::EnableEditing(std::function<bool(wxString const&)> commit,
	std::function<bool(wxString const&)> commit_secondary)
{
	on_commit = std::move(commit);
	on_commit_secondary = std::move(commit_secondary);
	SetCursor(wxCursor(wxCURSOR_IBEAM));
	// The text box exists from the start, hidden, so the label is already
	// as tall as it and nothing moves when it shows
	edit = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	edit->Hide();
	wxSize min = GetMinSize();
	SetMinSize(wxSize(min.x, std::max(min.y, edit->GetBestSize().y)));
	edit->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { CommitEditing(); });
	edit->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &event) {
		if (event.GetKeyCode() == WXK_ESCAPE) CancelEditing();
		else event.Skip();
	});
	edit->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &event) {
		event.Skip();
		// After the focus change completes, not inside it; and not when the
		// box has the keyboard again by then, editing another part: the
		// focus left it for a moment while switching
		CallAfter([this] { if (wxWindow::FindFocus() != edit) CancelEditing(); });
	});
}

void VideoTimeLabel::SetEditable(bool on) {
	if (on == editable) return;
	editable = on;
	if (!on) CancelEditing();
	if (on_commit) SetCursor(on ? wxCursor(wxCURSOR_IBEAM) : wxNullCursor);
	Refresh(false);
}

bool VideoTimeLabel::IsEditing() const {
	return edit && edit->IsShown();
}

int VideoTimeLabel::PartAt(int x) const {
	if (!on_commit_secondary || secondary.empty()) return 0;
	return x >= SecondaryX() - Gap() / 2 ? 1 : 0;
}

wxRect VideoTimeLabel::PartRect(int part) const {
	wxSize size = GetClientSize();
	int left = divider ? std::max(1, FromDIP(1)) + FromDIP(2) : 0;
	if (!on_commit_secondary || secondary.empty()) return {left, 0, size.x - left, size.y};
	int split = SecondaryX() - Gap() / 2;
	if (part == 0) return {left, 0, std::max(1, split - left), size.y};
	return {split, 0, std::max(1, size.x - split), size.y};
}

void VideoTimeLabel::StartEditing(int part) {
	if (!edit || !editable || IsEditing()) return;
	if (part == 1 && (!on_commit_secondary || secondary.empty())) return;
	editing_part = part;
	wxString const& value = part == 1 ? secondary : main;
	// Over the part clicked, wider when the box's own padding needs it
	wxRect rect = PartRect(part);
	wxSize client = GetClientSize();
	int needed = edit->GetSizeFromTextSize(MeasureDC(edit->GetFont()).dc.GetTextExtent(value).x + FromDIP(4)).x;
	if (rect.width < needed) {
		if (part == 0)
			rect.width = std::min(needed, client.x - rect.x);
		else {
			int right = rect.x + rect.width;
			rect.x = std::max(0, right - needed);
			rect.width = right - rect.x;
		}
	}
	edit->SetSize(rect);
	edit->ChangeValue(value);
	edit->Show();
	edit->SetFocus();
	edit->SelectAll();
	// A click on something that never takes the keyboard (the video, the
	// slider, drawn controls) would leave the box open otherwise
	if (!filtering) {
		wxEvtHandler::AddFilter(&click_outside);
		filtering = true;
	}
}

void VideoTimeLabel::StopFiltering() {
	if (!filtering) return;
	wxEvtHandler::RemoveFilter(&click_outside);
	filtering = false;
}

int VideoTimeLabel::ClickOutside::FilterEvent(wxEvent &event) {
	auto type = event.GetEventType();
	if (type == wxEVT_LEFT_DOWN || type == wxEVT_RIGHT_DOWN || type == wxEVT_MIDDLE_DOWN
		|| type == wxEVT_LEFT_DCLICK || type == wxEVT_RIGHT_DCLICK)
	{
		if (event.GetEventObject() != label->edit && label->IsEditing())
			label->CancelEditing();
	}
	return Event_Skip;
}

void VideoTimeLabel::CommitEditing() {
	if (!IsEditing()) return;
	auto const& commit = editing_part == 1 ? on_commit_secondary : on_commit;
	if (commit && !commit(edit->GetValue())) {
		wxBell();
		edit->SelectAll();
		return;
	}
	CancelEditing();
}

void VideoTimeLabel::CancelEditing() {
	if (!IsEditing()) return;
	bool had_focus = edit->HasFocus();
	StopFiltering();
	edit->Hide();
	// Leave the keyboard with the video, not nowhere
	if (had_focus && focus_after) focus_after->SetFocus();
	Refresh(false);
}

void VideoTimeLabel::SetCopyItems(std::vector<CopyItem> items) {
	copy_items = std::move(items);
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
	MeasureDC measure(GetFont());
	wxDC &dc = measure.dc;
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
	// Make room up the whole window: laying out the parent alone would
	// squeeze the row into the space it had
	InvalidateBestSize();
	if (auto top = wxGetTopLevelParent(this)) top->Layout();
	if (GetParent()) GetParent()->Layout();
}

int VideoTimeLabel::SecondaryX() const {
	MeasureDC measure(GetFont());
	return 2 * Gap() + TextWidth(measure.dc, main, DigitWidth(measure.dc));
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
	if (divider) {
		dc.SetBrush(wxBrush(theme::Mix(surface, text, 0.2)));
		int height = size.y * 3 / 5;
		dc.DrawRectangle(0, (size.y - height) / 2, std::max(1, FromDIP(1)), height);
	}

	dc.SetFont(GetFont());
	int digit = DigitWidth(dc);
	int y = (size.y - dc.GetCharHeight()) / 2;
	int x = Gap();
	if (on_commit && editable && hover && hover_part >= 0) {
		// A faint field under the mouse over the part a click types into
		dc.SetBrush(wxBrush(theme::Mix(surface, text, theme::IsDark() ? 0.1 : 0.07)));
		wxRect part = PartRect(hover_part);
		dc.DrawRoundedRectangle(part.x, FromDIP(2), part.width, size.y - 2 * FromDIP(2), FromDIP(4));
	}
	dc.SetTextForeground(dim ? faint : text);
	x = DrawTabular(dc, main, x, y, digit);
	if (!secondary.empty()) {
		dc.SetTextForeground(faint);
		x = DrawTabular(dc, secondary, x + Gap(), y, digit);
	}
	if (badge_on) {
		// Centred on the digits' ink, the letter on its own ink
		auto digits = InkRows(GetFont(), "0");
		int middle = y + (digits.first + digits.second) / 2;
		wxFont font = BadgeFont(GetFont());
		dc.SetFont(font);
		wxSize extent = dc.GetTextExtent(badge_text);
		auto ink = InkRows(font, badge_text);
		int ink_height = ink.second - ink.first;
		int height = ink_height + 2 * FromDIP(3);
		wxRect rect(x + FromDIP(6), middle - height / 2, extent.x + FromDIP(8), height);
		dc.SetBrush(wxBrush(accent));
		dc.DrawRoundedRectangle(rect, FromDIP(3));
		dc.SetTextForeground(surface);
		dc.DrawText(badge_text, rect.x + (rect.width - extent.x) / 2, rect.y + (rect.height - ink_height) / 2 - ink.first);
	}
}

void VideoTimeLabel::OnMouse(wxMouseEvent &event) {
	if (event.Entering() || event.Moving()) {
		int part = PartAt(event.GetX());
		if (!hover || part != hover_part) {
			hover = true;
			hover_part = part;
			if (on_commit) Refresh(false);
		}
	}
	else if (event.Leaving()) {
		hover = false;
		hover_part = -1;
		if (on_commit) Refresh(false);
	}
	else if (event.LeftDown() && on_commit && editable && !IsEditing()) {
		// Once the click is over, so nothing takes the focus back from the box
		int part = PartAt(event.GetX());
		CallAfter([this, part] { StartEditing(part); });
		return;
	}
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

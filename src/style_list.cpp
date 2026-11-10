// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "style_list.h"

#include "compat.h"
#include "flat_controls.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/intl.h>
#include <wx/settings.h>

namespace {
wxColour WarnColour() {
	return theme::IsDark() ? wxColour(236, 168, 82) : wxColour(196, 110, 0);
}

wxString Cut(wxDC &dc, wxString const& text, int width) {
	if (dc.GetTextExtent(text).x <= width) return text;
	wxString cut = text;
	while (!cut.empty() && dc.GetTextExtent(cut + wxS("…")).x > width)
		cut.RemoveLast();
	return cut + wxS("…");
}

wxColour Opaque(agi::Color colour) {
	wxColour wx = to_wx(colour);
	return wxColour(wx.Red(), wx.Green(), wx.Blue());
}

/// What the dim line under a style's name says: its font and size, and
/// bold or italic when it is
wxString Describe(AssStyle const& style) {
	double size = std::round(style.fontsize * 100) / 100;
	wxString text = to_wx(style.font) + wxS(" · ") + wxString::Format("%g", size);
	if (style.bold) text += wxS(" · ") + _("Bold");
	if (style.italic) text += wxS(" · ") + _("Italic");
	return text;
}
}

// StyleList

StyleList::StyleList(wxWindow *parent, wxString const& empty)
: wxVScrolledWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS)
, empty_text(empty)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	Bind(wxEVT_PAINT, &StyleList::OnPaint, this);
	Bind(wxEVT_LEFT_DOWN, &StyleList::OnLeftDown, this);
	Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent &event) {
		int row = HitRow(event.GetPosition());
		if (row < 0) return;
		SetSelection(row);
		Notify(wxEVT_LISTBOX);
		Notify(wxEVT_LISTBOX_DCLICK);
	});
	Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent &event) {
		SetFocus();
		int row = HitRow(event.GetPosition());
		if (row >= 0 && !IsSelected(row)) {
			SetSelection(row);
			Notify(wxEVT_LISTBOX);
		}
		if (on_menu) on_menu(event.GetPosition());
	});
	Bind(wxEVT_MOTION, [this](wxMouseEvent &event) {
		int row = HitRow(event.GetPosition());
		if (row != hover) { hover = row; Refresh(false); }
		event.Skip();
	});
	Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &event) { hover = -1; Refresh(false); event.Skip(); });
	Bind(wxEVT_KEY_DOWN, &StyleList::OnKey, this);
	Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &event) { Refresh(false); event.Skip(); });
	Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &event) { Refresh(false); event.Skip(); });
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
	SetMinSize(FromDIP(wxSize(300, 280)));
}

int StyleList::RowHeight() const {
	return GetCharHeight() * 2 + FromDIP(16);
}

int StyleList::HitRow(wxPoint pos) const {
	if (rows.empty()) return -1;
	int row = static_cast<int>(GetVisibleRowsBegin()) + pos.y / RowHeight();
	return row >= 0 && row < static_cast<int>(rows.size()) ? row : -1;
}

void StyleList::Notify(wxEventType type) {
	wxCommandEvent event(type, GetId());
	event.SetEventObject(this);
	wxArrayInt chosen;
	if (GetSelections(chosen)) event.SetInt(chosen[0]);
	ProcessWindowEvent(event);
}

void StyleList::SetRows(std::vector<Row> new_rows) {
	rows = std::move(new_rows);
	selected.assign(rows.size(), false);
	current = anchor = hover = -1;
	SetRowCount(rows.size());
	Refresh(false);
}

void StyleList::SetNote(size_t index, wxString const& note, Tone tone) {
	if (index >= rows.size()) return;
	if (rows[index].note == note && rows[index].tone == tone) return;
	rows[index].note = note;
	rows[index].tone = tone;
	RefreshRow(index);
}

wxString StyleList::GetString(unsigned index) const {
	return index < rows.size() ? to_wx(rows[index].style.name) : wxString();
}

int StyleList::GetSelections(wxArrayInt &selections) const {
	selections.clear();
	for (size_t i = 0; i < selected.size(); ++i)
		if (selected[i]) selections.push_back(static_cast<int>(i));
	return static_cast<int>(selections.size());
}

bool StyleList::IsSelected(int index) const {
	return index >= 0 && index < static_cast<int>(selected.size()) && selected[index];
}

void StyleList::DeselectAll() {
	std::fill(selected.begin(), selected.end(), false);
	Refresh(false);
}

void StyleList::Select(int index) {
	if (index < 0 || index >= static_cast<int>(rows.size())) return;
	selected[index] = true;
	current = anchor = index;
	if (!IsRowVisible(index)) ScrollToRow(index);
	Refresh(false);
}

void StyleList::Deselect(int index) {
	if (!IsSelected(index)) return;
	selected[index] = false;
	Refresh(false);
}

void StyleList::SetSelection(int index) {
	std::fill(selected.begin(), selected.end(), false);
	Select(index);
	Refresh(false);
}

bool StyleList::SetStringSelection(wxString const& name, bool select) {
	for (size_t i = 0; i < rows.size(); ++i) {
		if (to_wx(rows[i].style.name) != name) continue;
		if (select) Select(static_cast<int>(i));
		else Deselect(static_cast<int>(i));
		return true;
	}
	return false;
}

void StyleList::Click(int index, bool ctrl, bool shift) {
	if (index < 0 || index >= static_cast<int>(rows.size())) {
		if (!ctrl && !shift) {
			DeselectAll();
			Notify(wxEVT_LISTBOX);
		}
		return;
	}
	if (shift && anchor >= 0) {
		if (!ctrl) std::fill(selected.begin(), selected.end(), false);
		for (int i = std::min(anchor, index); i <= std::max(anchor, index); ++i)
			selected[i] = true;
		current = index;
	}
	else if (ctrl) {
		selected[index] = !selected[index];
		current = anchor = index;
	}
	else {
		std::fill(selected.begin(), selected.end(), false);
		selected[index] = true;
		current = anchor = index;
	}
	Refresh(false);
	Notify(wxEVT_LISTBOX);
}

void StyleList::OnLeftDown(wxMouseEvent &event) {
	SetFocus();
	Click(HitRow(event.GetPosition()), event.CmdDown(), event.ShiftDown());
}

void StyleList::MoveTo(int row, bool extend) {
	if (rows.empty()) return;
	row = std::clamp(row, 0, static_cast<int>(rows.size()) - 1);
	if (extend && anchor >= 0) {
		std::fill(selected.begin(), selected.end(), false);
		for (int i = std::min(anchor, row); i <= std::max(anchor, row); ++i)
			selected[i] = true;
		current = row;
	}
	else {
		std::fill(selected.begin(), selected.end(), false);
		selected[row] = true;
		current = anchor = row;
	}
	if (!IsRowVisible(row)) {
		if (row < static_cast<int>(GetVisibleRowsBegin())) ScrollToRow(row);
		else ScrollToRow(std::max(0, row - static_cast<int>(GetVisibleRowsEnd() - GetVisibleRowsBegin()) + 1));
	}
	Refresh(false);
	Notify(wxEVT_LISTBOX);
}

void StyleList::OnKey(wxKeyEvent &event) {
	int key = event.GetKeyCode();
	bool shift = event.ShiftDown();
	int page = std::max(1, GetClientSize().y / RowHeight() - 1);
	int from = current >= 0 ? current : -1;
	switch (key) {
		case WXK_UP: case WXK_NUMPAD_UP: MoveTo(from < 0 ? 0 : from - 1, shift); break;
		case WXK_DOWN: case WXK_NUMPAD_DOWN: MoveTo(from + 1, shift); break;
		case WXK_HOME: case WXK_NUMPAD_HOME: MoveTo(0, shift); break;
		case WXK_END: case WXK_NUMPAD_END: MoveTo(static_cast<int>(rows.size()) - 1, shift); break;
		case WXK_PAGEUP: case WXK_NUMPAD_PAGEUP: MoveTo(from - page, shift); break;
		case WXK_PAGEDOWN: case WXK_NUMPAD_PAGEDOWN: MoveTo(from + page, shift); break;
		case WXK_RETURN: case WXK_NUMPAD_ENTER:
			if (current >= 0) Notify(wxEVT_LISTBOX_DCLICK);
			break;
		case 'A':
			if (event.CmdDown() && !rows.empty()) {
				std::fill(selected.begin(), selected.end(), true);
				Refresh(false);
				Notify(wxEVT_LISTBOX);
				break;
			}
			[[fallthrough]];
		default:
			event.Skip();
	}
}

void StyleList::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	auto p = flat::Colours(this);
	wxSize size = GetClientSize();
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(p.surface));
	dc.DrawRectangle(wxPoint(0, 0), size);
	wxFont font = GetFont(), bold = GetFont().Bold();

	if (rows.empty()) {
		dc.SetFont(font);
		dc.SetTextForeground(p.dim);
		wxSize extent = dc.GetTextExtent(empty_text);
		dc.DrawText(empty_text, (size.x - extent.x) / 2, FromDIP(28));
		return;
	}

	int height = RowHeight();
	int gap = FromDIP(2);
	size_t first = GetVisibleRowsBegin(), last = std::min(GetVisibleRowsEnd() + 1, rows.size());
	for (size_t i = first; i < last; ++i) {
		Row const& row = rows[i];
		AssStyle const& style = row.style;
		wxRect box(0, static_cast<int>(i - first) * height + gap / 2, size.x, height - gap);
		bool chosen = selected[i];
		bool lit = static_cast<int>(i) == hover;
		if (chosen || lit) {
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(chosen ? theme::Mix(p.surface, p.accent, HasFocus() ? 0.16 : 0.1) : theme::Mix(p.surface, p.text, 0.05)));
			dc.DrawRoundedRectangle(box, FromDIP(6));
		}
		if (chosen) {
			dc.SetBrush(wxBrush(p.accent));
			dc.DrawRoundedRectangle(wxRect(box.x, box.y + FromDIP(8), FromDIP(3), box.height - FromDIP(16)), FromDIP(1));
		}
		if (HasFocus() && static_cast<int>(i) == current) {
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(theme::Mix(p.surface, p.accent, 0.6), std::max(1, FromDIP(1))));
			dc.DrawRoundedRectangle(box, FromDIP(6));
		}

		// A sample of the style, on a dark field as on the video
		int sample_size = box.height - FromDIP(14);
		wxRect sample(box.x + FromDIP(10), box.y + (box.height - sample_size) / 2, sample_size * 3 / 2, sample_size);
		dc.SetPen(wxPen(theme::Mix(p.surface, p.text, 0.15), std::max(1, FromDIP(1))));
		dc.SetBrush(wxBrush(wxColour(46, 48, 54)));
		dc.DrawRoundedRectangle(sample, FromDIP(5));
		{
			wxFont face(wxFontInfo(std::max(9, font.GetPointSize() + 3)).FaceName(to_wx(style.font)).Bold(style.bold).Italic(style.italic).Underlined(style.underline).Strikethrough(style.strikeout));
			dc.SetFont(face);
			wxString text = wxS("Aa");
			wxSize extent = dc.GetTextExtent(text);
			int x = sample.x + (sample.width - extent.x) / 2, y = sample.y + (sample.height - extent.y) / 2;
			if (style.outline_w > 0) {
				dc.SetTextForeground(Opaque(style.outline));
				int o = std::max(1, FromDIP(1));
				for (int dx = -o; dx <= o; dx += o)
					for (int dy = -o; dy <= o; dy += o)
						if (dx || dy) dc.DrawText(text, x + dx, y + dy);
			}
			dc.SetTextForeground(Opaque(style.primary));
			dc.DrawText(text, x, y);
		}

		int x = sample.GetRight() + FromDIP(12);
		int right = box.GetRight() - FromDIP(10);
		if (!row.note.empty()) {
			dc.SetFont(font);
			wxColour colour = row.tone == Tone::Accent ? p.accent : row.tone == Tone::Warn ? WarnColour() : p.dim;
			wxSize extent = dc.GetTextExtent(row.note);
			wxRect pill(right - extent.x - FromDIP(14), box.y + (box.height - extent.y - FromDIP(6)) / 2, extent.x + FromDIP(14), extent.y + FromDIP(6));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(theme::Mix(chosen ? theme::Mix(p.surface, p.accent, 0.16) : p.surface, colour, row.tone == Tone::Dim ? 0.1 : 0.16)));
			dc.DrawRoundedRectangle(pill, pill.height / 2);
			dc.SetTextForeground(colour);
			dc.DrawText(row.note, pill.x + FromDIP(7), pill.y + FromDIP(3));
			right = pill.x - FromDIP(8);
		}

		int line = GetCharHeight();
		int top = box.y + (box.height - line * 2 - FromDIP(2)) / 2;
		dc.SetFont(bold);
		dc.SetTextForeground(chosen ? p.accent : p.text);
		dc.DrawText(Cut(dc, to_wx(style.name), right - x), x, top);
		dc.SetFont(font);
		dc.SetTextForeground(p.dim);
		dc.DrawText(Cut(dc, Describe(style), right - x), x, top + line + FromDIP(2));
	}
}

// StyleTool

StyleTool::StyleTool(wxWindow *parent, Glyph glyph, wxString const& tip, bool accent)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
, glyph(glyph)
, accent(accent)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetToolTip(tip);
	SetCursor(wxCursor(wxCURSOR_HAND));
	SetMinSize(FromDIP(accent ? wxSize(34, 34) : wxSize(26, 26)));
	Bind(wxEVT_PAINT, &StyleTool::OnPaint, this);
	Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) { hover = true; Refresh(false); });
	Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { hover = pressed = false; Refresh(false); });
	auto down = [this](wxMouseEvent &) {
		if (!IsEnabled()) return;
		pressed = true;
		Refresh(false);
	};
	Bind(wxEVT_LEFT_DOWN, down);
	Bind(wxEVT_LEFT_DCLICK, down);
	Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
		if (!pressed) return;
		pressed = false;
		Refresh(false);
		if (IsEnabled()) Press();
	});
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
}

void StyleTool::Press() {
	wxCommandEvent event(wxEVT_BUTTON, GetId());
	event.SetEventObject(this);
	ProcessWindowEvent(event);
}

bool StyleTool::Enable(bool enable) {
	bool changed = wxWindow::Enable(enable);
	if (changed) Refresh(false);
	return changed;
}

void StyleTool::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	wxSize size = GetClientSize();
	auto p = flat::Colours(this);
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(GetParent()->GetBackgroundColour()));
	dc.DrawRectangle(wxPoint(0, 0), size);

	bool enabled = IsEnabled();
	bool lit = hover && enabled;
	wxColour ink = p.text;
	if (accent) {
		wxColour fill = enabled ? p.accent : theme::Mix(p.surface, p.text, 0.12);
		if (lit) fill = theme::Mix(fill, *wxWHITE, pressed ? 0.05 : 0.15);
		dc.SetBrush(wxBrush(fill));
		dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), FromDIP(8));
		ink = enabled ? *wxWHITE : theme::Mix(p.surface, p.text, 0.4);
	}
	else if (lit) {
		dc.SetBrush(wxBrush(theme::Mix(p.field, p.text, pressed ? 0.1 : 0.05)));
		dc.SetPen(wxPen(p.line, std::max(1, FromDIP(1))));
		dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), FromDIP(5));
	}

	double cx = size.x / 2.0, cy = size.y / 2.0;
	double arm = FromDIP(accent ? 6 : 5);
	int thick = accent ? FromDIP(2) : FromDIP(3) / 2;
	wxPen pen(ink, std::max(1, thick));
	pen.SetCap(wxCAP_ROUND);
	pen.SetJoin(wxJOIN_ROUND);
	dc.SetPen(pen);
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	auto line = [&](double x1, double y1, double x2, double y2) {
		dc.DrawLine(wxRound(x1), wxRound(y1), wxRound(x2), wxRound(y2));
	};
	// An arrow from (x1, y1) to (x2, y2), its head at the end
	auto arrow = [&](double x1, double y1, double x2, double y2) {
		line(x1, y1, x2, y2);
		double dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
		if (len == 0) return;
		dx /= len; dy /= len;
		double head = arm * 0.7;
		line(x2, y2, x2 - head * (dx + dy), y2 - head * (dy - dx));
		line(x2, y2, x2 - head * (dx - dy), y2 - head * (dy + dx));
	};
	switch (glyph) {
		case Glyph::Up: arrow(cx, cy + arm, cx, cy - arm); break;
		case Glyph::Down: arrow(cx, cy - arm, cx, cy + arm); break;
		case Glyph::Top:
			arrow(cx, cy + arm, cx, cy - arm + FromDIP(2));
			line(cx - arm, cy - arm - FromDIP(1), cx + arm, cy - arm - FromDIP(1));
			break;
		case Glyph::Bottom:
			arrow(cx, cy - arm, cx, cy + arm - FromDIP(2));
			line(cx - arm, cy + arm + FromDIP(1), cx + arm, cy + arm + FromDIP(1));
			break;
		case Glyph::Sort: {
			// Three lines getting shorter, and an arrow down beside them
			double left = cx - arm - FromDIP(1);
			for (int i = 0; i < 3; ++i) {
				double y = cy - arm + FromDIP(1) + i * (arm - FromDIP(1));
				line(left, y, left + arm * (1.4 - i * 0.4), y);
			}
			arrow(cx + arm * 0.8, cy - arm, cx + arm * 0.8, cy + arm);
			break;
		}
		case Glyph::Add:
			line(cx - arm, cy, cx + arm, cy);
			line(cx, cy - arm, cx, cy + arm);
			break;
		case Glyph::Remove: {
			// A bin: a lid, a handle and a body
			double w = arm * 0.8;
			line(cx - arm, cy - arm * 0.6, cx + arm, cy - arm * 0.6);
			line(cx - w * 0.4, cy - arm * 0.6, cx - w * 0.4, cy - arm);
			line(cx - w * 0.4, cy - arm, cx + w * 0.4, cy - arm);
			line(cx + w * 0.4, cy - arm, cx + w * 0.4, cy - arm * 0.6);
			line(cx - w, cy - arm * 0.6, cx - w * 0.8, cy + arm);
			line(cx - w * 0.8, cy + arm, cx + w * 0.8, cy + arm);
			line(cx + w * 0.8, cy + arm, cx + w, cy - arm * 0.6);
			break;
		}
		case Glyph::ToRight: arrow(cx - arm, cy, cx + arm, cy); break;
		case Glyph::ToLeft: arrow(cx + arm, cy, cx - arm, cy); break;
	}
}

// StyleCard

StyleCard::StyleCard(wxWindow *parent, wxString const& title)
: wxPanel(parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE)
, title(title)
{
	SetLabel(title);
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	theme::UseSurface(this);
	Bind(wxEVT_PAINT, &StyleCard::OnPaint, this);
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
}

void StyleCard::SetCount(wxString const& new_count) {
	if (count == new_count) return;
	count = new_count;
	Refresh(false);
}

int StyleCard::TitleWidth() const {
	wxClientDC dc(const_cast<StyleCard*>(this));
	dc.SetFont(GetFont().Bold().Scaled(1.15f));
	int width = dc.GetTextExtent(title).x;
	dc.SetFont(GetFont());
	// Room for the count of a few hundred styles
	return width + FromDIP(10) + dc.GetTextExtent(wxS("000 styles")).x;
}

void StyleCard::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	wxSize size = GetClientSize();
	auto p = flat::Colours(this);
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(GetParent()->GetBackgroundColour()));
	dc.DrawRectangle(wxPoint(0, 0), size);
	dc.SetPen(wxPen(theme::Mix(p.surface, p.text, 0.14), std::max(1, FromDIP(1))));
	dc.SetBrush(wxBrush(p.surface));
	dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), FromDIP(10));

	// The title in the first row of the controls, centred on it
	int pad = Padding();
	int row = FromDIP(28);
	wxFont big = GetFont().Bold().Scaled(1.15f);
	dc.SetFont(big);
	dc.SetTextForeground(p.text);
	wxSize extent = dc.GetTextExtent(title);
	int y = pad + (row - extent.y) / 2;
	dc.DrawText(title, pad + FromDIP(2), y);
	if (!count.empty()) {
		dc.SetFont(GetFont());
		dc.SetTextForeground(p.dim);
		wxSize small_extent = dc.GetTextExtent(count);
		dc.DrawText(count, pad + FromDIP(2) + extent.x + FromDIP(8), y + extent.y - small_extent.y - FromDIP(1));
	}
}

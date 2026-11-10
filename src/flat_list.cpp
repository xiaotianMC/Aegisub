// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "flat_list.h"

#include "flat_controls.h"
#include "theme.h"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/renderer.h>

namespace {
wxString Cut(wxDC &dc, wxString const& text, int width) {
	if (width <= 0) return wxString();
	if (dc.GetTextExtent(text).x <= width) return text;
	wxString cut = text;
	while (!cut.empty() && dc.GetTextExtent(cut + wxS("…")).x > width)
		cut.RemoveLast();
	return cut + wxS("…");
}
}

FlatList::FlatList(wxWindow *parent, bool multiple, bool checkable, wxString const& empty)
: wxVScrolledWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS)
, multiple(multiple)
, checkable(checkable)
, empty_text(empty)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	Bind(wxEVT_PAINT, &FlatList::OnPaint, this);
	Bind(wxEVT_LEFT_DOWN, &FlatList::OnLeftDown, this);
	Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent &event) {
		int row = HitRow(event.GetPosition());
		if (row < 0) return;
		// A double click on the box is two ticks, not opening the row
		if (this->checkable && event.GetPosition().x < Inset() + FromDIP(30)) {
			Click(row, false, false, true);
			return;
		}
		DoubleClick(row);
	});
	Bind(wxEVT_MOTION, [this](wxMouseEvent &event) {
		int row = HitRow(event.GetPosition());
		if (row != hover) { hover = row; ShowRowTip(); Refresh(false); }
		event.Skip();
	});
	Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &event) { hover = -1; ShowRowTip(); Refresh(false); event.Skip(); });
	Bind(wxEVT_KEY_DOWN, &FlatList::OnKey, this);
	Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &event) { Refresh(false); event.Skip(); });
	Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &event) { Refresh(false); event.Skip(); });
	theme_changed = theme::AddChangeListener([this] { Refresh(false); });
	SetMinSize(FromDIP(wxSize(160, 120)));
}

bool FlatList::TwoLines() const {
	return std::any_of(rows.begin(), rows.end(), [](Row const& row) { return !row.detail.empty(); });
}

int FlatList::RowHeight() const {
	return TwoLines() ? GetCharHeight() * 2 + FromDIP(14) : GetCharHeight() + FromDIP(10);
}

int FlatList::Inset() const {
	return FromDIP(4);
}

int FlatList::HitRow(wxPoint pos) const {
	if (rows.empty() || pos.y < Inset()) return -1;
	int row = static_cast<int>(GetVisibleRowsBegin()) + (pos.y - Inset()) / RowHeight();
	return row >= 0 && row < static_cast<int>(rows.size()) ? row : -1;
}

wxRect FlatList::CheckRect(wxRect const& row) const {
	int side = FromDIP(14);
	return wxRect(row.x + FromDIP(8), row.y + (row.height - side) / 2, side, side);
}

void FlatList::Notify(wxEventType type, int index) {
	wxCommandEvent event(type, GetId());
	event.SetEventObject(this);
	event.SetInt(index);
	if (index >= 0 && index < static_cast<int>(rows.size())) event.SetString(rows[index].text);
	ProcessWindowEvent(event);
}

void FlatList::Set(std::vector<Row> new_rows) {
	rows = std::move(new_rows);
	selected.assign(rows.size(), false);
	checked.assign(rows.size(), false);
	current = anchor = hover = -1;
	SetRowCount(rows.size());
	Refresh(false);
}

void FlatList::Set(wxArrayString const& texts) {
	std::vector<Row> made;
	for (auto const& text : texts) made.push_back(Row{text, wxString(), wxString()});
	Set(std::move(made));
}

void FlatList::SetRow(size_t index, Row row) {
	if (index >= rows.size()) return;
	rows[index] = std::move(row);
	RefreshAll();
}

wxString FlatList::GetString(unsigned index) const {
	return index < rows.size() ? rows[index].text : wxString();
}

int FlatList::GetSelection() const {
	if (IsSelected(current)) return current;
	auto it = std::find(selected.begin(), selected.end(), true);
	return it == selected.end() ? wxNOT_FOUND : static_cast<int>(it - selected.begin());
}

wxString FlatList::GetStringSelection() const {
	int index = GetSelection();
	return index == wxNOT_FOUND ? wxString() : rows[index].text;
}

int FlatList::GetSelections(wxArrayInt &selections) const {
	selections.clear();
	for (size_t i = 0; i < selected.size(); ++i)
		if (selected[i]) selections.push_back(static_cast<int>(i));
	return static_cast<int>(selections.size());
}

bool FlatList::IsSelected(int index) const {
	return index >= 0 && index < static_cast<int>(selected.size()) && selected[index];
}

void FlatList::ShowRow(int row) {
	if (row < 0 || IsRowVisible(row)) return;
	if (row < static_cast<int>(GetVisibleRowsBegin())) ScrollToRow(row);
	else ScrollToRow(std::max(0, row - static_cast<int>(GetVisibleRowsEnd() - GetVisibleRowsBegin()) + 1));
}

void FlatList::SetSelection(int index) {
	std::fill(selected.begin(), selected.end(), false);
	current = anchor = -1;
	Select(index);
	Refresh(false);
}

bool FlatList::SetStringSelection(wxString const& text) {
	for (size_t i = 0; i < rows.size(); ++i) {
		if (rows[i].text != text) continue;
		SetSelection(static_cast<int>(i));
		return true;
	}
	return false;
}

void FlatList::Select(int index) {
	if (index < 0 || index >= static_cast<int>(rows.size())) return;
	if (!multiple) std::fill(selected.begin(), selected.end(), false);
	selected[index] = true;
	current = anchor = index;
	ShowRow(index);
	Refresh(false);
}

void FlatList::Deselect(int index) {
	if (!IsSelected(index)) return;
	selected[index] = false;
	Refresh(false);
}

void FlatList::DeselectAll() {
	std::fill(selected.begin(), selected.end(), false);
	Refresh(false);
}

bool FlatList::IsChecked(unsigned index) const {
	return index < checked.size() && checked[index];
}

void FlatList::Check(unsigned index, bool check) {
	if (index >= checked.size() || checked[index] == check) return;
	checked[index] = check;
	RefreshRow(index);
}

void FlatList::Click(int index, bool ctrl, bool shift, bool on_box) {
	if (index < 0 || index >= static_cast<int>(rows.size())) return;
	if (on_box && checkable) {
		checked[index] = !checked[index];
		Refresh(false);
		Notify(wxEVT_CHECKLISTBOX, index);
		return;
	}
	if (!multiple) ctrl = shift = false;
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
	Notify(wxEVT_LISTBOX, index);
}

void FlatList::DoubleClick(int index) {
	if (index < 0 || index >= static_cast<int>(rows.size())) return;
	if (!IsSelected(index) || multiple) {
		std::fill(selected.begin(), selected.end(), false);
		selected[index] = true;
		current = anchor = index;
		Refresh(false);
		Notify(wxEVT_LISTBOX, index);
	}
	Notify(wxEVT_LISTBOX_DCLICK, index);
}

void FlatList::OnLeftDown(wxMouseEvent &event) {
	SetFocus();
	int row = HitRow(event.GetPosition());
	if (row < 0) return;
	wxRect line(Inset(), Inset() + (row - static_cast<int>(GetVisibleRowsBegin())) * RowHeight(), GetClientSize().x - 2 * Inset(), RowHeight());
	bool on_box = checkable && event.GetPosition().x < CheckRect(line).GetRight() + FromDIP(6);
	Click(row, event.CmdDown(), event.ShiftDown(), on_box);
}

void FlatList::MoveTo(int row, bool extend) {
	if (rows.empty()) return;
	row = std::clamp(row, 0, static_cast<int>(rows.size()) - 1);
	if (extend && multiple && anchor >= 0) {
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
	ShowRow(row);
	Refresh(false);
	Notify(wxEVT_LISTBOX, row);
}

void FlatList::OnKey(wxKeyEvent &event) {
	int key = event.GetKeyCode();
	bool shift = event.ShiftDown();
	int page = std::max(1, GetClientSize().y / RowHeight() - 1);
	int from = current;
	switch (key) {
		case WXK_UP: case WXK_NUMPAD_UP: MoveTo(from < 0 ? 0 : from - 1, shift); break;
		case WXK_DOWN: case WXK_NUMPAD_DOWN: MoveTo(from + 1, shift); break;
		case WXK_HOME: case WXK_NUMPAD_HOME: MoveTo(0, shift); break;
		case WXK_END: case WXK_NUMPAD_END: MoveTo(static_cast<int>(rows.size()) - 1, shift); break;
		case WXK_PAGEUP: case WXK_NUMPAD_PAGEUP: MoveTo(from - page, shift); break;
		case WXK_PAGEDOWN: case WXK_NUMPAD_PAGEDOWN: MoveTo(from + page, shift); break;
		case WXK_RETURN: case WXK_NUMPAD_ENTER:
			if (current >= 0) Notify(wxEVT_LISTBOX_DCLICK, current);
			else event.Skip();
			break;
		case WXK_SPACE:
			if (checkable && current >= 0) {
				// Every chosen row takes the ticking of the current one
				bool tick = !checked[current];
				for (size_t i = 0; i < rows.size(); ++i) {
					if (!selected[i] && static_cast<int>(i) != current) continue;
					if (checked[i] == tick) continue;
					checked[i] = tick;
					Notify(wxEVT_CHECKLISTBOX, static_cast<int>(i));
				}
				Refresh(false);
			}
			else event.Skip();
			break;
		case 'A':
			if (multiple && event.CmdDown() && !rows.empty()) {
				std::fill(selected.begin(), selected.end(), true);
				Refresh(false);
				Notify(wxEVT_LISTBOX, current);
				break;
			}
			[[fallthrough]];
		default:
			event.Skip();
	}
}

bool FlatList::Enable(bool enable) {
	bool changed = wxVScrolledWindow::Enable(enable);
	if (changed) Refresh(false);
	return changed;
}

void FlatList::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	auto p = flat::Colours(this);
	wxSize size = GetClientSize();
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(p.surface));
	dc.DrawRectangle(wxPoint(0, 0), size);
	// The field the rows are on
	dc.SetBrush(wxBrush(p.field));
	dc.SetPen(wxPen(HasFocus() ? theme::Mix(p.line, p.accent, 0.6) : p.line, std::max(1, FromDIP(1))));
	dc.DrawRoundedRectangle(wxRect(wxPoint(0, 0), size), FromDIP(6));

	wxFont font = GetFont();
	dc.SetFont(font);
	if (rows.empty()) {
		dc.SetTextForeground(p.dim);
		wxSize extent = dc.GetTextExtent(empty_text);
		dc.DrawText(empty_text, (size.x - extent.x) / 2, FromDIP(20));
		return;
	}

	dc.SetClippingRegion(wxRect(Inset() / 2, Inset() / 2, size.x - Inset(), size.y - Inset()));
	bool two = TwoLines();
	int height = RowHeight();
	int line = GetCharHeight();
	size_t first = GetVisibleRowsBegin(), last = std::min(GetVisibleRowsEnd() + 1, rows.size());
	for (size_t i = first; i < last; ++i) {
		Row const& row = rows[i];
		wxRect box(Inset(), Inset() + static_cast<int>(i - first) * height, size.x - 2 * Inset(), height);
		wxRect fill = box.Deflate(0, FromDIP(1));
		bool chosen = selected[i];
		bool lit = static_cast<int>(i) == hover && IsEnabled();
		if (chosen || lit) {
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(chosen ? theme::Mix(p.field, p.accent, HasFocus() ? 0.18 : 0.12) : theme::Mix(p.field, p.text, 0.05)));
			dc.DrawRoundedRectangle(fill, FromDIP(4));
		}
		if (HasFocus() && static_cast<int>(i) == current && multiple) {
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(theme::Mix(p.field, p.accent, 0.6), std::max(1, FromDIP(1))));
			dc.DrawRoundedRectangle(fill, FromDIP(4));
		}

		int x = box.x + FromDIP(8);
		if (checkable) {
			wxRect check = CheckRect(box);
			flat::DrawCheck(dc, this, check, checked[i] ? 1 : 0, p);
			x = check.GetRight() + FromDIP(8);
		}
		int right = box.GetRight() - FromDIP(8);
		if (!row.note.empty()) {
			dc.SetFont(font);
			wxSize extent = dc.GetTextExtent(row.note);
			if (row.warn) {
				// As FlatBadge has a warning: a pill in the error colour
				wxColour error = theme::Colour("Colour/Subtitle/Syntax/Background/Error");
				int pad = FromDIP(6);
				wxRect pill(right - extent.x - pad, box.y + (box.height - extent.y) / 2 - FromDIP(2), extent.x + 2 * pad, extent.y + FromDIP(4));
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(error));
				dc.DrawRoundedRectangle(pill, pill.height / 2.);
				dc.SetTextForeground(theme::Mix(error, p.text, 0.9));
				dc.DrawText(row.note, right - extent.x, box.y + (box.height - extent.y) / 2);
				right -= extent.x + pad + FromDIP(10);
			}
			else {
				dc.SetTextForeground(p.dim);
				dc.DrawText(row.note, right - extent.x, box.y + (box.height - extent.y) / 2);
				right -= extent.x + FromDIP(10);
			}
		}
		int top = two ? box.y + (box.height - line * 2 - FromDIP(2)) / 2 : box.y + (box.height - line) / 2;
		dc.SetFont(font);
		dc.SetTextForeground(!IsEnabled() ? p.dim : chosen ? p.accent : p.text);
		dc.DrawText(Cut(dc, row.text, right - x), x, top);
		if (two && !row.detail.empty()) {
			dc.SetTextForeground(p.dim);
			dc.DrawText(Cut(dc, row.detail, right - x), x, top + line + FromDIP(2));
		}
	}
}

void FlatList::ShowRowTip() {
	// A row with a dim line has its words whole in the tip, cut short as
	// they may be; elsewhere the list's own tip
	bool on_detail = hover >= 0 && hover < static_cast<int>(rows.size()) && !rows[hover].detail.empty();
	if (on_detail) {
		if (!own_tip) own_tip = GetToolTipText();
		SetToolTip(rows[hover].text + "\n" + rows[hover].detail);
	}
	else if (own_tip) {
		SetToolTip(*own_tip);
		own_tip.reset();
	}
}

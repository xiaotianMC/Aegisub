// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// A list drawn as the flat controls are, in place of wxListBox and
// wxCheckListBox in the dialogs (docs/dialogs.md).
#pragma once

#include <libaegisub/signal.h>

#include <optional>
#include <vector>

#include <wx/dynarray.h>
#include <wx/vscroll.h>

/// Rows of words on a rounded field: a line of text, a dim line under it if
/// any row has one, and a dim note at the right. Chosen as a wxListBox is
/// (click, Ctrl and Shift with a click, the arrow keys, Ctrl+A when several
/// may be chosen) and sending the same events: wxEVT_LISTBOX when the user
/// changes the selection, wxEVT_LISTBOX_DCLICK on a double click or Enter.
/// A checkable one has a box before each row, ticked by a click on it or
/// Space, sending wxEVT_CHECKLISTBOX as wxCheckListBox does.
class FlatList final : public wxVScrolledWindow {
public:
	struct Row {
		wxString text;
		wxString detail; ///< The dim line under the text
		wxString note;   ///< The dim words at the right
	};

private:
	std::vector<Row> rows;
	std::vector<bool> selected, checked;
	bool multiple, checkable;
	int current = -1, anchor = -1, hover = -1;
	wxString empty_text;
	/// The list's own tip while a row's is shown
	std::optional<wxString> own_tip;
	agi::signal::Connection theme_changed;

	bool TwoLines() const;
	int RowHeight() const;
	int Inset() const;
	int HitRow(wxPoint pos) const;
	wxRect CheckRect(wxRect const& row) const;
	void Notify(wxEventType type, int index);
	void MoveTo(int row, bool extend);
	void ShowRow(int row);
	void ShowRowTip();
	void OnPaint(wxPaintEvent &);
	void OnLeftDown(wxMouseEvent &event);
	void OnKey(wxKeyEvent &event);
	wxCoord OnGetRowHeight(size_t) const override { return RowHeight(); }

public:
	/// @p empty is shown when there are no rows
	FlatList(wxWindow *parent, bool multiple = false, bool checkable = false, wxString const& empty = wxString());

	void Set(std::vector<Row> new_rows);
	void Set(wxArrayString const& texts);
	Row const& GetRow(size_t index) const { return rows[index]; }
	void SetRow(size_t index, Row row);

	// As wxListBox and wxCheckListBox: none of these send an event
	unsigned GetCount() const { return static_cast<unsigned>(rows.size()); }
	bool IsEmpty() const { return rows.empty(); }
	wxString GetString(unsigned index) const;
	/// The row last chosen, or wxNOT_FOUND
	int GetSelection() const;
	wxString GetStringSelection() const;
	int GetSelections(wxArrayInt &selections) const;
	bool IsSelected(int index) const;
	void SetSelection(int index);
	bool SetStringSelection(wxString const& text);
	void Select(int index);
	void Deselect(int index);
	void DeselectAll();
	bool IsChecked(unsigned index) const;
	void Check(unsigned index, bool check = true);

	/// A click on row @p index as the user makes one, sending the events;
	/// @p on_box clicks the row's check box
	void Click(int index, bool ctrl = false, bool shift = false, bool on_box = false);
	void DoubleClick(int index);

	bool AcceptsFocus() const override { return true; }
	bool AcceptsFocusFromKeyboard() const override { return IsEnabled(); }
	bool Enable(bool enable = true) override;
};

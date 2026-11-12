// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The drawn parts of the styles manager (docs/main-window.md): a card for
// each side, a list of styles showing what each looks like, and small
// buttons with a glyph. Flat on the block colour, as the style panel is.
#pragma once

#include "ass_style.h"

#include <libaegisub/signal.h>

#include <functional>
#include <vector>

#include <wx/dynarray.h>
#include <wx/panel.h>
#include <wx/vscroll.h>

/// A list of styles, chosen from as a wxListBox with several selections
/// is: click, Ctrl and Shift with a click, the arrow keys with Shift.
/// Each row shows a sample of the style on a dark field (a light one for
/// dark letters), its name, its
/// font and size, and a note on the right. Sends wxEVT_LISTBOX when the
/// user changes the selection and wxEVT_LISTBOX_DCLICK on a double click
/// or Enter, as wxListBox does.
class StyleList final : public wxVScrolledWindow {
public:
	enum class Tone { Dim, Accent, Warn };
	/// The field a style's sample is drawn on: dark, as most videos are
	/// behind subtitles, unless the letters' edge (the border colour when
	/// there is a border, else the primary) is dark too, then light, so
	/// black text shows
	static wxColour SampleField(AssStyle const& style);
	struct Row {
		AssStyle style;
		wxString note;
		Tone tone = Tone::Dim;
	};

private:
	std::vector<Row> rows;
	std::vector<bool> selected;
	int current = -1, anchor = -1, hover = -1;
	wxString empty_text;
	agi::signal::Connection theme_changed;

	int RowHeight() const;
	int HitRow(wxPoint pos) const;
	void Notify(wxEventType type);
	void MoveTo(int row, bool extend);
	void OnPaint(wxPaintEvent &);
	void OnLeftDown(wxMouseEvent &event);
	void OnKey(wxKeyEvent &event);
	wxCoord OnGetRowHeight(size_t) const override { return RowHeight(); }

public:
	/// @p empty is shown when there are no styles
	StyleList(wxWindow *parent, wxString const& empty);

	/// Called on a right click, after the row under it is selected
	std::function<void(wxPoint)> on_menu;

	/// Show these styles, none selected
	void SetRows(std::vector<Row> new_rows);
	/// Change the note of row @p index, keeping the selection
	void SetNote(size_t index, wxString const& note, Tone tone);
	Row const& GetRow(size_t index) const { return rows[index]; }

	// As wxListBox: none of these send an event
	unsigned GetCount() const { return static_cast<unsigned>(rows.size()); }
	wxString GetString(unsigned index) const;
	int GetSelections(wxArrayInt &selections) const;
	bool IsSelected(int index) const;
	void DeselectAll();
	void Select(int index);
	void Deselect(int index);
	/// Select only @p index
	void SetSelection(int index);
	/// Add the row named @p name to the selection, or take it away
	bool SetStringSelection(wxString const& name, bool select = true);

	/// A click on row @p index as the user makes one, sending the events
	void Click(int index, bool ctrl = false, bool shift = false);

	bool AcceptsFocus() const override { return true; }
	bool AcceptsFocusFromKeyboard() const override { return true; }
};

/// A small square button with a glyph: an arrow, a plus, a bin. Sends
/// wxEVT_BUTTON as wxButton does. The accent kind is filled with the
/// accent colour, for the button doing what the dialog is for.
class StyleTool final : public wxWindow {
public:
	enum class Glyph { Up, Down, Top, Bottom, Sort, Add, Remove, ToRight, ToLeft };

private:
	Glyph glyph;
	bool accent;
	bool hover = false, pressed = false;
	agi::signal::Connection theme_changed;

	void OnPaint(wxPaintEvent &);

public:
	StyleTool(wxWindow *parent, Glyph glyph, wxString const& tip, bool accent = false);

	/// Send wxEVT_BUTTON, as a click does
	void Press();
	Glyph GetGlyph() const { return glyph; }

	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return false; }
	bool Enable(bool enable = true) override;
};

/// One side of the dialog: a rounded box with a bold title and a dim count
/// at its top left, the controls laid out inside it. Its label is the
/// title, so the controls in it can be told apart by their parent.
class StyleCard final : public wxPanel {
	wxString title, count;
	agi::signal::Connection theme_changed;

	void OnPaint(wxPaintEvent &);

public:
	StyleCard(wxWindow *parent, wxString const& title);

	/// The dim words after the title: how many styles it holds
	void SetCount(wxString const& new_count);
	wxString GetCount() const { return count; }
	/// The room the title takes at the top, and the padding inside
	int TitleWidth() const;
	int Padding() const { return FromDIP(12); }
};

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The time readouts below the video (docs/main-window.md): text drawn on
// the video block, digits in fixed-width cells so they hold still while
// playing, a secondary part in a dimmer colour and an optional badge.
#pragma once

#include <libaegisub/signal.h>

#include <functional>
#include <utility>
#include <vector>

#include <wx/window.h>

class wxTextCtrl;

class VideoTimeLabel final : public wxWindow {
public:
	/// A right-click menu item: its label and what it puts on the clipboard
	using CopyItem = std::pair<wxString, std::function<wxString()>>;

	/// @param sample Widest expected text (main + secondary), setting the width
	/// @param badge  Room for the badge is kept whether it shows or not
	/// @param dim    The main part in the dim colour too
	VideoTimeLabel(wxWindow *parent, wxString sample, bool badge, bool dim);

	/// Show @p main, then @p secondary dimmer, then the badge if @p badge_on
	void SetParts(wxString const& main, wxString const& secondary = {}, bool badge_on = false);
	wxString const& GetMain() const { return main; }
	wxString const& GetSecondary() const { return secondary; }
	bool BadgeShown() const { return badge_on; }

	/// Badge text, e.g. "K" for a keyframe
	void SetBadge(wxString const& text);
	/// Clicking turns the label into a text box holding the main part;
	/// Enter passes the text to @p commit, which returns false to keep
	/// editing (bad input). Esc or clicking elsewhere puts the label back.
	void EnableEditing(std::function<bool(wxString const&)> commit);
	void StartEditing();
	/// Whether clicking edits now (e.g. not without a video)
	void SetEditable(bool on);
	bool IsEditing() const;
	/// The text box while editing, for tests
	wxTextCtrl *EditBox() const { return edit; }
	/// Enter in the text box, for tests
	void CommitEditing();
	void CancelEditing();
	/// Where the keyboard goes when editing ends
	void SetFocusAfterEditing(wxWindow *window) { focus_after = window; }
	/// The thin line on the left, off for the first item in a row
	void SetDivider(bool on) { divider = on; Refresh(false); }
	/// Right-click menu entries copying parts of the text
	void SetCopyItems(std::vector<CopyItem> items);

	/// Never takes the keyboard itself: a click must leave it with the text box
	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return false; }

private:
	wxString main, secondary, badge_text = "K";
	bool badge, dim, badge_on = false;
	bool hover = false;
	std::function<bool(wxString const&)> on_commit;
	wxTextCtrl *edit = nullptr;
	wxWindow *focus_after = nullptr;
	bool divider = true;
	bool editable = true;
	std::vector<CopyItem> copy_items;
	agi::signal::Connection theme_changed;

	int DigitWidth(wxDC &dc) const;
	/// Width of @p text with each digit in a cell of @p digit pixels
	int TextWidth(wxDC &dc, wxString const& text, int digit) const;
	/// Draw @p text at @p x and return the x after it
	int DrawTabular(wxDC &dc, wxString const& text, int x, int y, int digit) const;
	int Gap() const { return FromDIP(8); }
	int BadgeWidth(wxDC &dc) const;
	/// Width for main + secondary (+ badge) with the paddings
	int WidthFor(wxString const& m, wxString const& s) const;
	void Fit(wxString const& m, wxString const& s);

	void OnPaint(wxPaintEvent &);
	void OnMouse(wxMouseEvent &event);
	void OnContextMenu(wxContextMenuEvent &);
};

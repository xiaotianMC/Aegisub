// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Flat controls for the edit box (docs/main-window.md), drawn as the style
// panel and the controls below the video are: rounded fields on the block
// colour, a dim label before the value, the accent colour for focus.
#pragma once

#include <libaegisub/signal.h>

#include <functional>
#include <vector>

#include <wx/bmpbndl.h>
#include <wx/popupwin.h>
#include <wx/window.h>

class wxMouseEvent;
class wxSizer;
class wxStaticText;
class wxTextCtrl;

namespace flat {
struct Palette {
	wxColour surface, text, dim, field, line, accent;
};
Palette Colours(wxWindow const *window);

/// Whole notches the wheel turned, positive away from the user (up);
/// a touchpad's small turns are added up in @p remainder
int WheelSteps(wxMouseEvent const& event, int &remainder);

/// Record that the wheel scrolled what is around the controls (a panel)
void NoteScroll();
/// Whether a wheel turn over a control should go on scrolling instead of
/// changing it: the wheel scrolled a moment ago, so the control has only
/// come under the pointer. Records the turn as scrolling if so.
bool Scrolling();

/// A check box as the style panel draws it: a rounded square, filled with
/// the accent colour and a tick when on (@p state 1), a dash when mixed (2)
void DrawCheck(wxDC &dc, wxWindow const *window, wxRect const& box, int state, Palette const& p);

/// A rounded outline @p thick wide, all of it inside @p rect: drawn as a
/// filled ring, so that no part of it falls half outside the window, as a
/// pen's line along the edge does at some scales
void DrawOutline(wxDC &dc, wxRect const& rect, double radius, int thick, wxColour const& colour);

/// Where the keyboard goes when a field is left: the subtitle text
void SetHome(wxWindow *home);
/// A field being typed in that Enter or a click anywhere else leaves
void AddField(wxWindow *field);
/// Give the keyboard back to the subtitle text, from wherever it is in its
/// window. @return Whether it went
bool GoHome(wxWindow *from);
/// Leave the field being typed in, if any: the keyboard goes home
void LeaveField();
/// A control the wheel changes: wheel turns over it go to it, even where
/// the system sends them to the window with the keyboard (Windows)
void AddWheelTarget(wxWindow *target);

/// A section's title in a dialog: bold words over a thin line, as the
/// preferences and the style editor have them
wxSizer *SectionTitle(wxWindow *parent, wxString const& title);

/// A label in the dim colour, kept so as the theme changes
wxStaticText *DimLabel(wxWindow *parent, wxString const& text);
/// @p label without the colon it ends with, as it is translated (":" or
/// the full-width "\uFF1A"), if it ends with one, and the space around it
wxString WithoutColon(wxString label);
/// @p text on one line: its line breaks spaces, as a list row shows it
wxString OneLine(wxString text);

/// The words of a stock button (wxID_OK, wxID_APPLY…) with the mnemonics
/// wxWidgets gives them, from our own translations: wxWidgets' are not
/// always installed. Empty for an id that isn't a stock one.
wxString StockLabel(int id);
}

/// A rounded field holding one borderless control (text, a time), with a
/// dim label before it and, optionally, a chevron opening a list after it.
/// The label can be dragged sideways to change a number, and the wheel
/// can change it too.
class FlatFrame final : public wxWindow {
	wxString label;
	wxWindow *content = nullptr;
	int content_width = 0;
	bool dim_content = false;
	bool label_shown = true;
	std::function<wxBitmapBundle()> make_icon;
	std::function<void()> on_chevron;
	std::function<void(int, bool)> on_scrub;
	std::function<void(int)> on_wheel;
	int wheel_remainder = 0;
	bool pressed = false, scrubbing = false;
	int press_x = 0, scrub_steps = 0;
	agi::signal::Connection theme_changed;

	int LabelWidth() const;
	wxRect ChevronRect() const;
	void Arrange();
	void ApplyColours();
	void OnPaint(wxPaintEvent &);
	void OnLeftDown(wxMouseEvent &event);
	void OnMotion(wxMouseEvent &event);
	void OnLeftUp(wxMouseEvent &event);
	void OnWheel(wxMouseEvent &event);

public:
	FlatFrame(wxWindow *parent, wxString label);

	/// The control inside, made with this frame as its parent, at least
	/// @p width DIP wide
	void SetContent(wxWindow *control, int width);
	wxWindow *GetContent() const { return content; }
	wxString GetLabel() const override { return label; }

	/// Show the control's text dim: a value worked out from others
	void SetDimContent(bool dim);
	/// Leave the label out where there is no room for it
	void ShowLabel(bool show);
	bool IsLabelShown() const { return label_shown; }
	/// An icon before the label, made again as it is drawn so that it
	/// follows the appearance
	void SetIcon(std::function<wxBitmapBundle()> make);
	/// A chevron after the control, opening a list
	void SetChevron(std::function<void()> open);
	/// Dragging the label sideways: @p change gets whole steps since the
	/// press, and true when the drag ends
	void SetScrub(std::function<void(int, bool)> change);
	/// Turning the wheel over the field: @p change gets the notches, up positive
	void SetWheel(std::function<void(int)> change);

	/// As a click on the chevron, a drag of the label or a wheel turn does; for tests
	void OpenList() { if (on_chevron) on_chevron(); }
	void Scrub(int steps, bool final) { if (on_scrub) on_scrub(steps, final); }
	void Wheel(int steps) { if (on_wheel) on_wheel(steps); }

	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return false; }
	bool Enable(bool enable = true) override;
};

namespace flat {
/// A search box as the other fields are drawn: a magnifier, @p hint while
/// it is empty, and Escape emptying it before it closes the dialog.
/// @p text is the box typed in.
FlatFrame *SearchField(wxWindow *parent, wxString const& hint, wxTextCtrl *&text, int width = 160);
}

/// A drawn list to choose from, as the style panel's style field: click,
/// Space or Enter opens it, the arrow keys and the wheel go through it.
class FlatChoice final : public wxWindow {
	std::vector<wxString> items;
	int selection = -1;
	int wheel_remainder = 0;
	bool hover = false;
	wxString typed;         ///< What was typed to find an item, kept for a second
	wxLongLong typed_at = 0;
	wxTextCtrl *editor = nullptr; ///< The text typed into an editable one
	bool completing = false;      ///< Choosing what was typed: leave the text be
	agi::signal::Connection theme_changed;

	void OnPaint(wxPaintEvent &);
	void OnKey(wxKeyEvent &event);
	void OnChar(wxKeyEvent &event);
	void OnWheel(wxMouseEvent &event);
	bool Typing() const;
	void OnEdited();
	void OnEditorKey(wxKeyEvent &event);
	/// Show the chosen item in the editor, the colours for no match cleared
	void SyncEditor();
	void ApplyEditorColours(bool matched);
	void ArrangeEditor();

public:
	/// An @p editable one is typed into as a text field: the first item
	/// beginning with what was typed is chosen and completed in the field
	FlatChoice(wxWindow *parent, wxString const& tip, bool editable = false);

	/// The field typed into, if it is editable
	wxTextCtrl *GetEditor() const { return editor; }

	/// Called with the index when the user chooses an item
	std::function<void(int)> on_choose;

	void Set(std::vector<wxString> new_items);
	std::vector<wxString> const& GetItems() const { return items; }
	/// Show @p name chosen, without calling on_choose; not in the list shows nothing chosen
	void SetStringSelection(wxString const& name);
	wxString GetStringSelection() const;
	int GetSelection() const { return selection; }

	/// Choose an item as the user does: on_choose is called if it changes
	void Choose(int index);
	/// Go @p delta items down the list (the wheel, the arrow keys)
	void Step(int delta);
	/// Open the list below the control
	void ShowList();
	/// Typed while it has the keyboard: choose the first item beginning with
	/// what was typed in the last second, ignoring case. The same letter
	/// again, when no item begins so, goes on to the next with the letter.
	void TypeAhead(wxString const& text);

	bool AcceptsFocus() const override { return IsEnabled(); }
	bool AcceptsFocusFromKeyboard() const override { return IsEnabled(); }
	wxSize DoGetBestClientSize() const override;
	bool Enable(bool enable = true) override;
};

/// Options side by side in one rounded field, the chosen one filled with the
/// accent colour, in place of radio buttons. An option can be unavailable;
/// Left and Right step through those that are, and Alt with an option's
/// underlined letter chooses it in a dialog using flat::DialogButtons.
class FlatSwitch final : public wxWindow {
	std::vector<wxString> labels;
	std::vector<bool> available;
	std::vector<wxString> tips;
	int selection = 0;
	int hover = -1;
	agi::signal::Connection theme_changed;

	wxRect Segment(int index) const;
	int HitSegment(wxPoint pos) const;
	void Step(int delta);
	void OnPaint(wxPaintEvent &);

public:
	FlatSwitch(wxWindow *parent, std::vector<wxString> labels);

	/// Called with the index when the user chooses an option
	std::function<void(int)> on_choose;

	/// Choose @p index as the user does: on_choose is called if it changes
	void Choose(int index);
	/// Show @p index chosen, without calling on_choose
	void SetSelection(int index);
	int GetSelection() const { return selection; }
	wxString GetLabel(int index) const { return labels[index]; }
	size_t GetCount() const { return labels.size(); }
	void EnableItem(int index, bool enable);
	bool IsItemEnabled(int index) const { return available[index]; }
	/// The tooltip over option @p index
	void SetItemToolTip(int index, wxString const& tip);
	/// The option whose underlined letter is @p key, or -1
	int MnemonicIndex(int key) const;

	wxSize DoGetBestClientSize() const override;
	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return IsEnabled(); }
	bool Enable(bool enable = true) override;
};

/// A small rounded label drawn on a control, such as the character count in
/// the corner of the text box: dim, or in the error colour when it warns
class FlatBadge final : public wxWindow {
	wxString text;
	bool warning = false;
	agi::signal::Connection theme_changed;

	void OnPaint(wxPaintEvent &);

public:
	FlatBadge(wxWindow *parent);

	/// Called when it is clicked
	std::function<void()> on_click;

	void SetText(wxString const& new_text, bool warn);
	wxString GetText() const { return text; }
	bool IsWarning() const { return warning; }
	wxSize DoGetBestClientSize() const override;

	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return false; }
};

/// A drawn check box with its label, as in the style panel. Sends
/// wxEVT_CHECKBOX as wxCheckBox does; Space toggles it when it has the
/// keyboard, and Alt with the label's mnemonic (&) anywhere in its window.
class FlatCheck final : public wxWindow {
	wxString label;
	bool value = false;
	agi::signal::Connection theme_changed;

	void OnPaint(wxPaintEvent &);

public:
	FlatCheck(wxWindow *parent, wxString const& label);

	bool GetValue() const { return value; }
	bool IsChecked() const { return value; }
	/// Without sending an event
	void SetValue(bool checked);
	/// Toggle as a click does, sending wxEVT_CHECKBOX
	void Toggle();
	/// The label as given, with its mnemonic marker
	wxString GetLabel() const override { return label; }
	/// The mnemonic's key, upper case, or 0
	int Mnemonic() const;

	wxSize DoGetBestClientSize() const override;
	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return IsEnabled(); }
	bool Enable(bool enable = true) override;
};

/// A drawn button with words on it, as the animation page's are: a rounded
/// field, a shade darker under the pointer. Sends wxEVT_BUTTON as wxButton
/// does; Space or Enter presses it when it has the keyboard. The accent kind
/// is filled with the accent colour, for what the dialog is for.
class FlatButton final : public wxWindow {
	wxString label;
	bool accent = false;
	bool hover = false, pressed = false;
	wxSize least;
	agi::signal::Connection theme_changed;

	void OnPaint(wxPaintEvent &);

public:
	/// An empty @p label with a stock @p id (wxID_OK...) is the stock label
	FlatButton(wxWindow *parent, wxString const& label, int id = wxID_ANY, bool accent = false);

	void SetAccent(bool new_accent);
	bool IsAccent() const { return accent; }
	/// The size the button is at least, however short its label: unlike a
	/// min size, a longer label set later still widens it
	void SetLeastSize(wxSize size);

	/// Send wxEVT_BUTTON, as a click does
	void Press();
	wxString GetLabel() const override { return label; }
	void SetLabel(wxString const& new_label) override;

	wxSize DoGetBestClientSize() const override;
	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return IsEnabled(); }
	bool Enable(bool enable = true) override;
};

/// Values to choose from under a text field as it is typed in, in place of
/// the system's autocompletion: those starting with what was typed, drawn
/// as the flat controls are. Up and Down go through them, Enter, Tab or a
/// click takes one, Escape closes them. The field keeps the keyboard.
class FlatSuggest final : public wxPopupWindow {
	wxTextCtrl *field;
	wxWindow *anchor;
	std::function<std::vector<wxString>()> values;
	std::vector<wxString> shown;
	int selected = -1, hover = -1;
	bool filling = false;
	agi::signal::Connection theme_changed;

	int RowHeight() const;
	void OnPaint(wxPaintEvent &);

public:
	/// Under @p anchor (the field's frame), as wide as it
	FlatSuggest(wxTextCtrl *field, wxWindow *anchor, std::function<std::vector<wxString>()> values);

	/// Show the values matching the field's text, or close if none does
	void Suggest();
	/// A key typed in the field: true if the list used it
	bool HandleKey(wxKeyEvent &event);
	/// Put value @p index of those shown in the field, and close
	void Take(int index);
	void Close();

	std::vector<wxString> const& Shown() const { return shown; }
	int Selected() const { return selected; }
};

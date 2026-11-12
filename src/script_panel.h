// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The script panel (docs/style-panel.md#文案面板): the video's whole script
// as plain text, typed or pasted in to check the subtitles against, kept with
// the subtitles in the .ass file. A panel of its own at the right, in the
// style panel's place: its top bar's button goes back to the style panel.
// A button breaks long text into one sentence a line.
#pragma once

#include <libaegisub/signal.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "style_panel.h"

#include <wx/panel.h>

namespace agi { struct Context; }
class wxSizer;
class wxStaticText;
class wxTextCtrl;

namespace script_text {
/// @p text with each sentence on a line of its own: a line breaks after
/// 。！？…, and after . ! ? followed by a space or the end of the line,
/// keeping a run of them and the quotes and brackets closing after them
/// (“好。”) with the sentence. Line breaks already there stay; spaces around
/// the sentences and empty lines go
wxString SplitSentences(wxString const& text);
/// The lines holding more than spaces
int CountLines(wxString const& text);

/// Where the thumb of the text's scroll bar is: @p pos of @p range
/// scrolled, @p page of it on the screen, on a track @p track long with
/// a thumb at least @p least long. Empty when it all fits
struct Thumb { int top = 0, height = 0; };
std::optional<Thumb> ThumbFor(int pos, int page, int range, int track, int least);
/// The scroll position putting the thumb's top at @p top on the track
int PosForThumb(int top, int page, int range, int track, int least);
}

class ScriptPanel final : public wxPanel, public PanelFocus {
protected:
	std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);

public:
	ScriptPanel(wxWindow *parent, agi::Context *c);
	~ScriptPanel();

	/// Read the script from the file again
	void UpdateNow();

	/// The box the script is typed into
	wxTextCtrl *TextBox() const { return text; }
	/// The text's scroll bar, drawn thin on the field (the box's own is
	/// hidden): where the thumb is, in the panel, empty when the text fits
	std::optional<wxRect> ScrollThumb() const;
	/// How far the text is scrolled, and how far it can be (pos, page,
	/// range, in the box's units); for tests
	std::tuple<int, int, int> ScrollState() const;
	/// Scroll the text to @p pos, as dragging the thumb does
	void ScrollTextTo(int pos);
	/// Act as typing: replace the text with @p value, as the user does
	void Type(wxString const& value);
	/// Act as clicking a button does: "style", "split" (also its undo),
	/// "collapse"
	void Press(std::string const& name);
	/// Whether the keyboard is in the panel
	bool HasKeyboard() const;
	/// Give the keyboard to the text, if the panel is on the screen.
	/// @return Whether it has it
	bool TakeKeyboard();
	/// Give the keyboard to the button or text after @p from, or before it;
	/// round from the last to the first
	void MoveFocus(wxWindow *from, bool forward) override;
	/// What the parts show: "split" (its label, " disabled" after it when it
	/// can't be used), "count", "note"; for tests
	std::map<std::string, wxString> Shown() const;

	/// Called by the button going back to the style panel
	std::function<void()> on_style;
	/// Called by the button folding the panel away
	std::function<void()> on_collapse;

private:
	agi::Context *c;
	std::vector<agi::signal::Connection> connections;
	agi::signal::Connection theme_changed;
	wxTextCtrl *text;
	/// Holds the box, cutting off its own scroll bar where it has to
	wxPanel *clip = nullptr;
	/// The room right of the text, on the field, for the thin scroll bar
	int bar_room = FromDIP(6);
	bool bar_hover = false;
	/// Dragging the thumb: from where in it, -1 when not
	int drag_from = -1;
	/// The scroll state the bar was last drawn for
	std::tuple<int, int, int> drawn_scroll;
	wxSizer *top_line = nullptr; ///< The name and the buttons, a line under them
	wxPanel *notice = nullptr;   ///< Why the text can't be changed, when it can't
	wxStaticText *notice_text = nullptr;
	int notice_width = 0;        ///< What its text is wrapped to
	/// Between the text and the edge of the field drawn around it
	int inset = FromDIP(8);
	std::map<std::string, wxWindow*> parts;
	/// The text before the last split, while nothing was typed since
	std::optional<wxString> before_split;
	/// Why the text can't be changed now, empty when it can
	wxString note;

	/// Write the box's text to the file; put the file's back if it can't be
	void Store();
	void Split();
	void UpdateParts();
	/// The colours for the theme: the field's, the notice's
	void Recolour();
	void OnPaint();
	/// The track the thumb runs on, in the panel
	wxRect BarTrack() const;
	void OnBarMouse(wxMouseEvent &event);
};

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The style panel's script page (docs/style-panel.md#文案页): the video's
// whole script as plain text, typed or pasted in to check the subtitles
// against, kept with the subtitles in the .ass file. A button breaks long
// text into one sentence a line.
#pragma once

#include <libaegisub/signal.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <wx/panel.h>

namespace agi { struct Context; }
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
}

class ScriptPage final : public wxPanel {
protected:
	std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);

public:
	ScriptPage(wxWindow *parent, agi::Context *c);
	~ScriptPage();

	/// Read the script from the file again
	void UpdateNow();

	/// The box the script is typed into
	wxTextCtrl *TextBox() const { return text; }
	/// Act as typing: replace the text with @p value, as the user does
	void Type(wxString const& value);
	/// Act as clicking a button does: "back", "split" (also its undo),
	/// "collapse"
	void Press(std::string const& name);
	/// What the parts show: "split" (its label, " disabled" after it when it
	/// can't be used), "count", "note"; for tests
	std::map<std::string, wxString> Shown() const;

	/// Called by the button going back to the panel's other pages
	std::function<void()> on_back;
	/// Called by the button folding the panel away
	std::function<void()> on_collapse;

private:
	agi::Context *c;
	std::vector<agi::signal::Connection> connections;
	wxTextCtrl *text;
	std::map<std::string, wxWindow*> parts;
	/// The text before the last split, while nothing was typed since
	std::optional<wxString> before_split;
	/// Why the text can't be changed now, empty when it can
	wxString note;

	/// Write the box's text to the file; put the file's back if it can't be
	void Store();
	void Split();
	void UpdateParts();
};

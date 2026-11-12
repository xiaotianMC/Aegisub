// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "script_page.h"

#include "ass_file.h"
#include "compat.h"
#include "flat_controls.h"
#include "format.h"
#include "style_panel.h"
#include "style_panel_parts.h"
#include "subtitle_project_access.h"
#include "theme.h"
#include "include/aegisub/context.h"

#include <wx/intl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace script_text {
namespace {
bool Ends(wxUniChar ch) {
	switch (ch.GetValue()) {
		case 0x3002: // 。
		case 0xFF01: // ！
		case 0xFF1F: // ？
		case 0x2026: // …
		case 0x2025: // ‥
		case 0xFF0E: // ．
			return true;
	}
	return false;
}
/// . ! ? end a sentence only before a space or the end of the line:
/// "3.14", "e.g" and "Hm?Ok" stay whole
bool EndsBeforeSpace(wxUniChar ch) {
	return ch == '.' || ch == '!' || ch == '?';
}
/// Quotes and brackets closing after the end of a sentence go with it
bool Closes(wxUniChar ch) {
	switch (ch.GetValue()) {
		case 0x201D: // ”
		case 0x2019: // ’
		case 0x300D: // 」
		case 0x300F: // 』
		case 0xFF09: // ）
		case 0x3011: // 】
		case 0x300B: // 》
		case 0x3009: // 〉
		case 0xFF3D: // ］
		case 0xFF5D: // ｝
		case ')': case ']': case '}': case '"': case '\'':
			return true;
	}
	return false;
}
bool Space(wxUniChar ch) {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == 0x3000 || ch == 0xA0;
}
wxString Trimmed(wxString s) {
	size_t start = 0, end = s.size();
	while (start < end && Space(s[start])) ++start;
	while (end > start && Space(s[end - 1])) --end;
	return s.Mid(start, end - start);
}
}

wxString SplitSentences(wxString const& text) {
	std::vector<wxString> out;
	auto push = [&](wxString const& sentence) {
		wxString trimmed = Trimmed(sentence);
		if (!trimmed.empty()) out.push_back(trimmed);
	};
	for (wxString const& line : wxSplit(text, '\n', 0)) {
		size_t n = line.size();
		wxString current;
		for (size_t i = 0; i < n; ) {
			wxUniChar ch = line[i++];
			current += ch;
			if (!Ends(ch) && !EndsBeforeSpace(ch)) continue;
			// The rest of a run, "？！" or "……", and what closes after it
			size_t j = i;
			while (j < n && (Ends(line[j]) || EndsBeforeSpace(line[j]))) ++j;
			while (j < n && Closes(line[j])) ++j;
			bool full = false;
			for (size_t k = i - 1; k < j; ++k) full |= Ends(line[k]);
			current += line.Mid(i, j - i);
			i = j;
			if (full || i == n || Space(line[i])) {
				push(current);
				current.clear();
			}
		}
		push(current);
	}
	wxString result;
	for (size_t i = 0; i < out.size(); ++i) {
		if (i) result += '\n';
		result += out[i];
	}
	return result;
}

int CountLines(wxString const& text) {
	int count = 0;
	for (wxString const& line : wxSplit(text, '\n', 0))
		if (!Trimmed(line).empty()) ++count;
	return count;
}
}

namespace {
/// Whether the box takes @p e itself rather than the window's hotkeys
bool EditsText(wxKeyEvent const& e) {
	int key = e.GetKeyCode();
	if (key >= WXK_F1 && key <= WXK_F24) return false;
	// Typing, and moving and selecting with or without Shift
	if (!e.ControlDown() && !e.AltDown() && !e.MetaDown()) return true;
	if (e.AltDown()) return false;
	switch (key) {
		case 'A': case 'C': case 'V': case 'X': case 'Z': case 'Y':
		case WXK_DELETE: case WXK_BACK: case WXK_INSERT:
		case WXK_LEFT: case WXK_RIGHT: case WXK_UP: case WXK_DOWN:
		case WXK_HOME: case WXK_END: case WXK_PAGEUP: case WXK_PAGEDOWN:
			return true;
	}
	return false;
}
}

ScriptPage::ScriptPage(wxWindow *parent, agi::Context *c)
: wxPanel(parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_NONE)
, c(c)
{
	theme::UseSurface(this);
	const int pad = FromDIP(12);
	auto outer = new wxBoxSizer(wxVERTICAL);

	// ‹ and the page's name on the first line, where the switch is on the
	// other pages, with the button folding the panel away
	{
		auto line = new wxBoxSizer(wxHORIZONTAL);
		auto back = new StylePanel::IconButton(this, StylePanel::IconButton::Glyph::Earlier, _("Back to the style, animation and timing pages"));
		back->on_click = [this] { if (on_back) on_back(); };
		parts["back"] = back;
		line->AddSpacer(pad - FromDIP(6));
		line->Add(back, 0, wxALIGN_CENTER_VERTICAL);
		auto title = new wxStaticText(this, -1, wxGETTEXT_IN_CONTEXT("page", "Script"));
		title->SetFont(title->GetFont().Bold());
		line->Add(title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(2));
		auto collapse = new StylePanel::IconButton(this, StylePanel::IconButton::Glyph::Collapse, _("Hide the style panel"));
		collapse->on_click = [this] { if (on_collapse) on_collapse(); };
		parts["collapse"] = collapse;
		line->Add(collapse, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, pad - FromDIP(4));
		outer->Add(line, 0, wxEXPAND | wxTOP | wxBOTTOM, pad - FromDIP(4));
	}

	// The split button, and how many lines there are
	{
		auto line = new wxBoxSizer(wxHORIZONTAL);
		auto split = new StylePanel::TextButton(this, _("One sentence a line"),
			_("Break the text after each sentence's end (。！？… and . ! ? before a space), so each sentence is on a line of its own"));
		split->on_click = [this] { Press("split"); };
		parts["split"] = split;
		line->Add(split, 0, wxALIGN_CENTER_VERTICAL);
		auto count = new StylePanel::Caption(this);
		parts["count"] = count;
		line->Add(count, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
		outer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
	}

	// Why it can't be changed now
	auto note_caption = new StylePanel::Caption(this);
	parts["note"] = note_caption;
	outer->Add(note_caption, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
	note_caption->Hide();

	text = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_RICH2);
	text->SetHint(_("Type or paste the video's whole script here, to check the subtitles against it. It is saved with the subtitles"));
	text->SetToolTip(_("The video's script, saved in the subtitle file"));
	outer->Add(text, 1, wxEXPAND | wxALL, pad);

	text->Bind(wxEVT_TEXT, [this](wxCommandEvent &) {
		// Typed: a split can no longer be taken back as such
		before_split.reset();
		Store();
	});
	text->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
		int key = e.GetKeyCode();
		if (key == WXK_TAB && !e.ControlDown() && !e.AltDown()) {
			// On to the panel's buttons, as Tab goes between them
			for (wxWindow *w = GetParent(); w; w = w->GetParent())
				if (auto panel = dynamic_cast<StylePanel*>(w)) {
					panel->MoveFocus(text, !e.ShiftDown());
					return;
				}
		}
		if (key == WXK_ESCAPE && !e.HasAnyModifiers() && flat::GoHome(this)) return;
		if (EditsText(e)) {
			// The box's, not the window's: Space plays the video, Ctrl+Z
			// takes back a change to the subtitles, Ctrl+Delete deletes lines
			e.DoAllowNextEvent();
			return;
		}
		// Ctrl+S, F6 and the window's other keys
		e.Skip();
	});

	SetSizer(outer);

	connections.push_back(c->subtitleAccess->Read().AddCommitListener("script_page.AddCommitListener",
		[this](int type, AssDialogue const*, SubtitleModelRebindTicket ticket) {
			// A file opened or made new
			if (type == AssFile::COMMIT_NEW) UpdateNow();
			if (ticket.Current()) ticket.Acknowledge(ticket.Revision());
		},
		std::weak_ptr<void const>(model_observer_lifetime)));
	connections.push_back(c->subtitleAccess->AddMetadataListener([this] { UpdateNow(); }));

	UpdateNow();
}

ScriptPage::~ScriptPage() = default;

void ScriptPage::UpdateNow() {
	wxString stored = to_wx(c->subtitleAccess->Read().Properties.script_text);
	if (text->GetValue() != stored) {
		// Another file's script: what the split took back belongs to the old one
		before_split.reset();
		text->ChangeValue(stored);
	}
	UpdateParts();
}

void ScriptPage::Store() {
	std::string value = from_wx(text->GetValue());
	try {
		c->subtitleAccess->SetMetadata(SubtitleProjectAccess::MetadataField::ScriptText, std::move(value));
		note.clear();
	}
	catch (SubtitleProjectBusy const&) {
		// Compared: the file's script stays as it is, and so does the box
		before_split.reset();
		note = _("The subtitles are being compared: the script can be changed once the comparison ends");
		text->ChangeValue(to_wx(c->subtitleAccess->Read().Properties.script_text));
	}
	UpdateParts();
}

void ScriptPage::Split() {
	if (before_split) {
		// Pressed again: back to the text as it was
		wxString old = *before_split;
		text->ChangeValue(old);
		before_split.reset();
		Store();
		return;
	}
	wxString old = text->GetValue();
	wxString split = script_text::SplitSentences(old);
	if (split == old) return;
	text->ChangeValue(split);
	Store();
	// Kept only once the file has it
	if (note.empty()) before_split = old;
	UpdateParts();
}

void ScriptPage::UpdateParts() {
	auto split = static_cast<StylePanel::TextButton*>(parts["split"]);
	split->SetLabel(before_split ? _("Undo the split") : _("One sentence a line"));
	split->Enable(before_split || !text->GetValue().empty());
	int lines = script_text::CountLines(text->GetValue());
	static_cast<StylePanel::Caption*>(parts["count"])->SetText(lines ? fmt_plural(lines, "%d line", "%d lines", lines) : wxString());
	auto note_caption = static_cast<StylePanel::Caption*>(parts["note"]);
	note_caption->SetText(note);
	note_caption->SetToolTip(note);
	if (note_caption->IsShown() != !note.empty()) {
		note_caption->Show(!note.empty());
		Layout();
	}
}

void ScriptPage::Type(wxString const& value) {
	// As typed: wxEVT_TEXT follows
	text->SetValue(value);
}

void ScriptPage::Press(std::string const& name) {
	if (name == "back") { if (on_back) on_back(); }
	else if (name == "collapse") { if (on_collapse) on_collapse(); }
	else if (name == "split") Split();
}

std::map<std::string, wxString> ScriptPage::Shown() const {
	std::map<std::string, wxString> shown;
	auto split = static_cast<StylePanel::TextButton*>(parts.at("split"));
	shown["split"] = split->Shown() + (split->IsEnabled() ? "" : " disabled");
	shown["count"] = static_cast<StylePanel::Caption*>(parts.at("count"))->Shown();
	shown["note"] = note;
	return shown;
}

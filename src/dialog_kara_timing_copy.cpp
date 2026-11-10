// Copyright (c) 2005, Dan Donovan (Dansolo)
// Copyright (c) 2009, Niels Martin Hansen
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#include "ass_dialogue.h"
#include "utils.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "subtitle_dialog_edit.h"
#include "ass_karaoke.h"
#include "compat.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "theme.h"

#include <libaegisub/ass/karaoke.h>
#include <libaegisub/karaoke_matcher.h>
#include <libaegisub/unicode.h>

#include <deque>
#include <memory>
#include <vector>
#include <wx/dcclient.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>

namespace {
#define TEXT_LABEL_SOURCE _("Source: ")
#define TEXT_LABEL_DEST _("Dest: ")

class KaraokeLineMatchDisplay final : public wxControl {
	agi::KaraokeMatcher matcher;
	std::vector<int> matchgroup_widths;
	int last_total_matchgroup_render_width = 0;
	using MatchSyllable = agi::ass::KaraokeSyllable;

	struct MatchGroup {
		std::vector<MatchSyllable> src;
		std::string dst;
		int last_render_width = 0;
	};

	void OnPaint(wxPaintEvent &event);

	wxString label_source = TEXT_LABEL_SOURCE;
	wxString label_destination = TEXT_LABEL_DEST;

public:
	/// Start processing a new line pair
	void SetInputData(AssDialogue const *src, AssDialogue const *dst);
	/// Build and return the output line from the matched syllables
	std::string GetOutputLine() const { return matcher.GetOutputLine(); }

	/// Number of syllables not yet matched from source
	size_t GetRemainingSource() const { return matcher.UnmatchedSource().size() + matcher.CurrentSourceSelection().size(); }
	/// Number of characters not yet matched from destination
	size_t GetRemainingDestination() const { return matcher.UnmatchedDestination().size() + matcher.CurrentDestinationSelection().size(); }

	// Adjust source and destination match lengths
	void IncreaseSourceMatch();
	void DecreaseSourceMatch();
	void IncreaseDestinationMatch();
	void DecreaseDestinationMatch();
	/// Attempt to treat source as Japanese romaji, destination as Japanese kana+kanji, and make an automatic match
	void AutoMatchJapanese();
	/// Accept current selection and save match
	bool AcceptMatch();
	/// Undo last match, adding it back to the unmatched input
	bool UndoMatch();

	KaraokeLineMatchDisplay(wxWindow *parent);

	wxSize GetBestSize() const;
};

KaraokeLineMatchDisplay::KaraokeLineMatchDisplay(wxWindow *parent)
: wxControl(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE|wxWANTS_CHARS)
{
	InheritAttributes();
	SetInputData(nullptr, nullptr);

	wxSize best_size = GetBestSize();
	SetMaxSize(wxSize(-1, best_size.GetHeight()));
	SetMinSize(best_size);

	Bind(wxEVT_SET_FOCUS, std::bind(&wxControl::Refresh, this, true, nullptr));
	Bind(wxEVT_KILL_FOCUS, std::bind(&wxControl::Refresh, this, true, nullptr));
	Bind(wxEVT_PAINT, &KaraokeLineMatchDisplay::OnPaint, this);
}

wxSize KaraokeLineMatchDisplay::GetBestSize() const
{
	int w_src, h_src, w_dst, h_dst;
	GetTextExtent(label_source, &w_src, &h_src);
	GetTextExtent(label_destination, &w_dst, &h_dst);

	int min_width = std::max(w_dst, w_src);

	// Magic number 7:
	// 1 pixel for top black border, 1 pixel white top border in first row, 1 pixel white bottom padding in first row
	// 1 pixel middle border, 1 pixel top and 1 pixel bottom padding in second row, 1 pixel bottom border
	return wxSize(min_width * 2, h_src + h_dst + 7);
}

int DrawBoxedText(wxDC &dc, wxString const& txt, int x, int y)
{
	int tw, th;
	// Assume the pen, brush and font properties have already been set in the DC.
	// Return the advance width, including box margins, borders etc

	if (txt.empty())
	{
		// Empty string gets special handling:
		// The box is drawn in shorter width, to emphasize it's empty
		// GetTextExtent has to be called with a non-empty string, otherwise it returns the wrong height
		dc.GetTextExtent(" ", &tw, &th);
		dc.DrawRectangle(x, y-2, 4, th+4);
		return 3;
	}
	else
	{
		dc.GetTextExtent(txt, &tw, &th);
		dc.DrawRectangle(x, y-2, tw+4, th+4);
		dc.DrawText(txt, x+2, y);
		return tw+3;
	}
}

void KaraokeLineMatchDisplay::OnPaint(wxPaintEvent &)
{
	wxPaintDC dc(this);

	wxColour outer_text(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
	wxColour outer_back(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
	wxColour outer_frame(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
	wxColour inner_back(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	wxColour inner_text(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
	wxColour sel_back(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
	wxColour sel_text(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));

	// Y coordinates of the top and bottom lines
	int y_line1, y_line2, y_line3;
	// Next X coordinate to draw a matched syllable at
	int next_x;

	wxSize client_size = GetClientSize();

	// Calculate the text line positions
	{
		int x, y, x2;
		GetTextExtent(label_source, &x, &y);
		y_line1 = 2;
		y_line2 = y_line1 + y + 3;
		y_line3 = y_line2 + y + 3;
		GetTextExtent(label_destination, &x2, &y);
		next_x = (x2 > x) ? x2 : x;
		next_x += 3;
	}

	// Paint the labels
	dc.SetTextBackground(outer_back);
	if (FindFocus() == this)
		dc.SetTextForeground(outer_text);
	else
		dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	dc.SetFont(GetFont());
	dc.SetBackgroundMode(wxTRANSPARENT);
	dc.DrawText(label_source, wxPoint(0, y_line1));
	dc.DrawText(label_destination, wxPoint(0, y_line2));

	// Horizontal lines through the width of the control
	dc.SetPen(wxPen(outer_frame));
	dc.DrawLine(next_x-1, y_line1-2, client_size.GetWidth(), y_line1-2);
	dc.DrawLine(next_x-1, y_line2-2, client_size.GetWidth(), y_line2-2);
	dc.DrawLine(next_x-1, y_line3-2, client_size.GetWidth(), y_line3-2);

	// Draw matched groups
	int this_total_matchgroup_render_width = 0;
	bool scroll_arrows_drawn = false;

	auto groups = matcher.MatchedGroups();
	matchgroup_widths.resize(groups.size());
	for (size_t i = 0; i < groups.size(); ++i) {
		auto& grp = groups[i];
		int prev_x = next_x;

		// Skip groups that would cause the input part to be too far right
		this_total_matchgroup_render_width += matchgroup_widths[i];
		if (last_total_matchgroup_render_width - this_total_matchgroup_render_width > client_size.x / 2)
		{
			// If we're skipping some syllables, show an arrow as feedback that something is scrolled off
			if (!scroll_arrows_drawn)
			{
				dc.SetBrush(wxBrush(outer_frame));
				int height = y_line2 - y_line1;
				wxPoint triangle[] = {
					wxPoint(next_x-3, height/2),
					wxPoint(next_x-3+height/2, 0),
					wxPoint(next_x-3+height/2, height)
				};
				dc.DrawPolygon(3, triangle, 0, 0);
				dc.DrawPolygon(3, triangle, 0, height);
				next_x += height/2 - 4;
			}
			scroll_arrows_drawn = true;
			continue;
		}

		dc.SetPen(wxPen(outer_frame));
		dc.SetBrush(wxBrush(inner_back));
		dc.SetTextBackground(inner_back);
		dc.SetTextForeground(inner_text);

		// Matched source syllables
		int syl_x = next_x;
		for (auto const& syl : grp.src)
			syl_x += DrawBoxedText(dc, to_wx(syl.text), syl_x, y_line1);

		// Matched destination text
		{
			const int adv = DrawBoxedText(dc, to_wx(grp.dst), next_x, y_line2);

			// Adjust next_x here while we have the text_w
			next_x = syl_x > next_x + adv ? syl_x : next_x + adv;
		}

		// Spacing between groups
		next_x += 3;
		matchgroup_widths[i] = next_x - prev_x;
	}

	last_total_matchgroup_render_width = this_total_matchgroup_render_width;

	// Spacing between grouped and ungrouped parts
	next_x += 4;

	// Remaining source syllables
	int syl_x = next_x;
	// Start out with highlight colours
	dc.SetTextBackground(sel_back);
	dc.SetTextForeground(sel_text);
	dc.SetBrush(wxBrush(sel_back));
	for (auto syl : matcher.CurrentSourceSelection()) {
		syl_x += DrawBoxedText(dc, to_wx(syl.text), syl_x, y_line1);
	}

	// Switch to regular colours after all selected syllables
	dc.SetTextBackground(inner_back);
	dc.SetTextForeground(inner_text);
	dc.SetBrush(wxBrush(inner_back));
	for (auto syl : matcher.UnmatchedSource()) {
		syl_x += DrawBoxedText(dc, to_wx(syl.text), syl_x, y_line1);
	}

	// Remaining destination
	if (auto dest = matcher.CurrentDestinationSelection(); !dest.empty()) {
		dc.SetTextBackground(sel_back);
		dc.SetTextForeground(sel_text);
		dc.SetBrush(wxBrush(sel_back));
		next_x += DrawBoxedText(dc, to_wx(dest), next_x, y_line2);
	}

	if (auto dest = matcher.UnmatchedDestination(); !dest.empty()) {
		dc.SetTextBackground(inner_back);
		dc.SetTextForeground(inner_text);
		dc.SetBrush(wxBrush(inner_back));
		DrawBoxedText(dc, to_wx(dest), next_x, y_line2);
	}
}

void KaraokeLineMatchDisplay::SetInputData(AssDialogue const *src, AssDialogue const *dst)
{
	last_total_matchgroup_render_width = 0;
	matcher.SetInputData(ParseKaraokeSyllables(src), dst ? dst->GetStrippedText() : "");
	Refresh(true);
}

void KaraokeLineMatchDisplay::IncreaseSourceMatch()
{
	if (matcher.IncreaseSourceMatch()) Refresh(true);
}

void KaraokeLineMatchDisplay::DecreaseSourceMatch()
{
	if (matcher.DecreaseSourceMatch()) Refresh(true);
}

void KaraokeLineMatchDisplay::IncreaseDestinationMatch()
{
	if (matcher.IncreaseDestinationMatch()) Refresh(true);
}

void KaraokeLineMatchDisplay::DecreaseDestinationMatch()
{
	if (matcher.DecreaseDestinationMatch()) Refresh(true);
}

void KaraokeLineMatchDisplay::AutoMatchJapanese()
{
	matcher.AutoMatchJapanese();
}

bool KaraokeLineMatchDisplay::AcceptMatch()
{
	if (matcher.AcceptMatch()) {
		Refresh(true);
		return true;
	}
	return false;
}

bool KaraokeLineMatchDisplay::UndoMatch()
{
	if (matcher.UndoMatch()) {
		Refresh(true);
		return true;
	}
	return false;
}

class DialogKanjiTimer final : public wxDialog {
protected:
    // Declared first: expires only after all owned bindings/cache members are destroyed.
    std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);
private:
	SubtitleDialogEdit edit;
	agi::signal::Connection file_changed;

	KaraokeLineMatchDisplay *display;

	FlatChoice *SourceStyle, *DestStyle;
	FlatCheck *Interpolate;

	std::vector<std::pair<int, std::string>> LinesToChange;

	int currentSourceLine = 0;
	int currentDestinationLine = 0;

	void OnClose(wxCommandEvent &event);
	void OnStart(wxCommandEvent &event);
	void OnLink(wxCommandEvent &event);
	void OnUnlink(wxCommandEvent &event);
	void OnSkipSource(wxCommandEvent &event);
	void OnSkipDest(wxCommandEvent &event);
	void OnGoBack(wxCommandEvent &event);
	void OnAccept(wxCommandEvent &event);
	void OnKeyDown(wxKeyEvent &event);

	void ResetForNewLine();
	void TryAutoMatch();

	int FindNextStyleMatch(int search_from, const std::string &search_style);
	int FindPrevStyleMatch(int search_from, const std::string &search_style);

public:
	DialogKanjiTimer(agi::Context *context);
};

DialogKanjiTimer::DialogKanjiTimer(agi::Context *c)
: wxDialog(c->parent, -1, _("Kanji timer"))
, edit(c)
, file_changed(c->subtitleAccess->Read().AddCommitListener("dialog_kara_timing_copy.AddCommitListener", [this](SubtitleModelRebindTicket ticket){edit.Invalidate();currentSourceLine=currentDestinationLine=0;display->SetInputData(nullptr,nullptr);if (ticket.Current()) ticket.Acknowledge(ticket.Revision());}, std::weak_ptr<void const>(model_observer_lifetime)))
{
	SetIcons(GETICONS(kara_timing_copier));
	theme::UseSurface(this);
	int gap = FromDIP(10), row = FromDIP(6);

	// The lines being matched, as before; the rest drawn as the flat dialogs are
	display = new KaraokeLineMatchDisplay(this);
	Interpolate = new FlatCheck(this, _("Attempt to &interpolate kanji"));
	Interpolate->SetValue(OPT_GET("Tool/Kanji Timer/Interpolation")->GetBool());

	// The styles of the lines timed and of those given the timing
	auto styles = to_wx(c->subtitleAccess->Read().GetStyles());
	auto style_choice = [&](wxString const& name) {
		auto choice = new FlatChoice(this, name);
		choice->Set(std::vector<wxString>(styles.begin(), styles.end()));
		choice->SetMinSize(FromDIP(wxSize(160, -1)));
		return choice;
	};
	SourceStyle = style_choice(flat::WithoutColon(TEXT_LABEL_SOURCE));
	DestStyle = style_choice(flat::WithoutColon(TEXT_LABEL_DEST));
	auto StylesGridSizer = new wxFlexGridSizer(2, row, gap);
	StylesGridSizer->AddGrowableCol(1, 1);
	StylesGridSizer->Add(flat::DimLabel(this, flat::WithoutColon(TEXT_LABEL_SOURCE)), wxSizerFlags().CenterVertical());
	StylesGridSizer->Add(SourceStyle, wxSizerFlags().Expand());
	StylesGridSizer->Add(flat::DimLabel(this, flat::WithoutColon(TEXT_LABEL_DEST)), wxSizerFlags().CenterVertical());
	StylesGridSizer->Add(DestStyle, wxSizerFlags().Expand());

	auto ShortcutKeys = flat::DimLabel(this, _("When the destination textbox has focus, use the following keys:\n\nRight Arrow: Increase dest. selection length\nLeft Arrow: Decrease dest. selection length\nUp Arrow: Increase source selection length\nDown Arrow: Decrease source selection length\nEnter: Link, accept line when done\nBackspace: Unlink last"));

	// The commands down the right, accepting the line the one it ends with
	auto Start = new FlatButton(this, _("S&tart!"));
	auto Link = new FlatButton(this, _("&Link"));
	auto Unlink = new FlatButton(this, _("&Unlink"));
	auto SkipSourceLine = new FlatButton(this, _("Skip &Source Line"));
	auto SkipDestLine = new FlatButton(this, _("Skip &Dest Line"));
	auto GoBackLine = new FlatButton(this, _("&Go Back a Line"));
	auto AcceptLine = new FlatButton(this, _("&Accept Line"), wxID_ANY, true);
	auto ButtonsSizer = new wxBoxSizer(wxVERTICAL);
	for (auto button : {Start, Link, Unlink, SkipSourceLine, SkipDestLine, GoBackLine, AcceptLine})
		ButtonsSizer->Add(button, wxSizerFlags().Expand().Border(wxTOP, button == Start ? 0 : row));

	auto buttons = new flat::DialogButtons(this, wxCLOSE | wxHELP, "Kanji Timer");
	auto CloseKT = buttons->Get(wxID_CLOSE);
	CloseKT->SetLabel(_("&Close"));
	// Accept Line is the one in the accent colour
	CloseKT->SetAccent(false);

	auto section = [&](wxString const& title, wxSizer *content, int proportion = 0) {
		auto sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(flat::SectionTitle(this, title), wxSizerFlags().Expand());
		sizer->Add(content, wxSizerFlags(proportion).Expand().Border(wxTOP, row));
		return sizer;
	};
	auto DisplaySizer = new wxBoxSizer(wxVERTICAL);
	DisplaySizer->Add(display, wxSizerFlags().Expand());
	DisplaySizer->Add(Interpolate, wxSizerFlags().Border(wxTOP, row));
	auto HelpSizer = new wxBoxSizer(wxVERTICAL);
	HelpSizer->Add(ShortcutKeys);

	auto BottomLeftStackSizer = new wxBoxSizer(wxVERTICAL);
	BottomLeftStackSizer->Add(section(_("Styles"), StylesGridSizer), wxSizerFlags().Expand());
	BottomLeftStackSizer->Add(section(_("Shortcut Keys"), HelpSizer, 1), wxSizerFlags(1).Expand().Border(wxTOP, gap));
	auto BottomShelfSizer = new wxBoxSizer(wxHORIZONTAL);
	BottomShelfSizer->Add(BottomLeftStackSizer, wxSizerFlags(1).Expand());
	BottomShelfSizer->Add(section(_("Commands"), ButtonsSizer), wxSizerFlags().Expand().Border(wxLEFT, gap * 2));

	auto MainStackSizer = new wxBoxSizer(wxVERTICAL);
	MainStackSizer->Add(section(_("Text"), DisplaySizer), wxSizerFlags().Expand().Border(wxALL, gap));
	MainStackSizer->Add(BottomShelfSizer, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, gap));
	MainStackSizer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, gap));

	SetSizerAndFit(MainStackSizer);
	CenterOnParent();

	// Escape leaves without applying, as closing the window does, rather
	// than pressing Close, which applies
	Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &event) {
		if (event.GetKeyCode() == WXK_ESCAPE && !event.HasAnyModifiers()) Close();
		else event.Skip();
	});

	Bind(wxEVT_KEY_DOWN, &DialogKanjiTimer::OnKeyDown, this);
	display->Bind(wxEVT_KEY_DOWN, &DialogKanjiTimer::OnKeyDown, this);

	Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent&){edit.End();LinesToChange.clear();EndModal(wxID_CANCEL);});
	CloseKT->Bind(wxEVT_BUTTON, &DialogKanjiTimer::OnClose, this);
	Start->Bind(wxEVT_BUTTON, &DialogKanjiTimer::OnStart, this);
	Link->Bind(wxEVT_BUTTON, &DialogKanjiTimer::OnLink, this);
	Unlink->Bind(wxEVT_BUTTON, &DialogKanjiTimer::OnUnlink, this);
	SkipSourceLine->Bind(wxEVT_BUTTON, &DialogKanjiTimer::OnSkipSource, this);
	SkipDestLine->Bind(wxEVT_BUTTON, &DialogKanjiTimer::OnSkipDest, this);
	GoBackLine->Bind(wxEVT_BUTTON, &DialogKanjiTimer::OnGoBack, this);
	AcceptLine->Bind(wxEVT_BUTTON, &DialogKanjiTimer::OnAccept, this);
}

void DialogKanjiTimer::OnClose(wxCommandEvent &) try {
	OPT_SET("Tool/Kanji Timer/Interpolation")->SetBool(Interpolate->GetValue());

	BOOST_SCOPE_EXIT_ALL(this){edit.End();};
	if(!LinesToChange.empty()) {
		std::vector<AssDialogueBase> updates;updates.reserve(LinesToChange.size());
		for(auto const& [id,text]:LinesToChange) {
			auto original=edit.Find(id);if(!original){edit.Invalidate();return;}
			auto row=*original;row.Text=text;updates.push_back(std::move(row));
		}
		file_changed.Block();
		BOOST_SCOPE_EXIT_ALL(this){file_changed.Unblock();};
		if(!edit.Commit(updates,_("kanji timing"),AssFile::COMMIT_DIAG_TEXT)) {
			wxMessageBox(_("The subtitles or their dependencies changed. Start again before applying."),_("Kanji timer"),wxOK|wxICON_WARNING,this);return;
		}
	}
	LinesToChange.clear();EndModal(wxID_OK);
}

catch (...) {edit.Invalidate();throw;}

void DialogKanjiTimer::OnStart(wxCommandEvent &) try {
	LinesToChange.clear();currentSourceLine=currentDestinationLine=0;edit.End();
	if (SourceStyle->GetStringSelection().empty() || DestStyle->GetStringSelection().empty())
		wxMessageBox(_("Select source and destination styles first."),_("Error"),wxICON_EXCLAMATION | wxOK);
	else if (SourceStyle->GetStringSelection() == DestStyle->GetStringSelection())
		wxMessageBox(_("The source and destination styles must be different."),_("Error"),wxICON_EXCLAMATION | wxOK);
	else {
		edit.ResetAll();
		if(edit.Rows().empty() || !edit.Begin())return;
		currentDestinationLine=currentSourceLine=edit.Rows().front().Id;
		auto sourceStyle = from_wx(SourceStyle->GetStringSelection());
		auto destStyle = from_wx(DestStyle->GetStringSelection());
		if (edit.Find(currentSourceLine)->Style != sourceStyle)
			currentSourceLine = FindNextStyleMatch(currentSourceLine, sourceStyle);
		if (edit.Find(currentDestinationLine)->Style != destStyle)
			currentDestinationLine = FindNextStyleMatch(currentDestinationLine, destStyle);
		ResetForNewLine();
	}
	LinesToChange.clear();
}
catch (...) {edit.Invalidate();throw;}

void DialogKanjiTimer::OnLink(wxCommandEvent &) try {
	if (display->AcceptMatch())
		TryAutoMatch();
	else
		wxBell();
}
catch (...) {edit.Invalidate();throw;}

void DialogKanjiTimer::OnUnlink(wxCommandEvent &) try {
	if (!display->UndoMatch())
		wxBell();
	// Don't auto-match here, undoing sets the selection to the undone match
}
catch (...) {edit.Invalidate();throw;}

void DialogKanjiTimer::OnSkipSource(wxCommandEvent &) try {
	currentSourceLine = FindNextStyleMatch(currentSourceLine, from_wx(SourceStyle->GetStringSelection()));
	ResetForNewLine();
}
catch (...) {edit.Invalidate();throw;}

void DialogKanjiTimer::OnSkipDest(wxCommandEvent &) try {
	currentDestinationLine = FindNextStyleMatch(currentDestinationLine, from_wx(DestStyle->GetStringSelection()));
	ResetForNewLine();
}
catch (...) {edit.Invalidate();throw;}

void DialogKanjiTimer::OnGoBack(wxCommandEvent &) try {
	if (LinesToChange.size())
		LinesToChange.pop_back(); //If we go back, then take out the modified line we saved.

	currentSourceLine = FindPrevStyleMatch(currentSourceLine, from_wx(SourceStyle->GetStringSelection()));
	currentDestinationLine = FindPrevStyleMatch(currentDestinationLine, from_wx(DestStyle->GetStringSelection()));
	ResetForNewLine();
}
catch (...) {edit.Invalidate();throw;}

void DialogKanjiTimer::OnAccept(wxCommandEvent &) try {
	if (!currentDestinationLine) return;

	if (display->GetRemainingSource() > 0)
		wxMessageBox(_("Group all of the source text."),_("Error"),wxICON_EXCLAMATION | wxOK);
	else {
		LinesToChange.emplace_back(currentDestinationLine, display->GetOutputLine());

		currentSourceLine = FindNextStyleMatch(currentSourceLine, from_wx(SourceStyle->GetStringSelection()));
		currentDestinationLine = FindNextStyleMatch(currentDestinationLine, from_wx(DestStyle->GetStringSelection()));
		ResetForNewLine();
	}
}
catch (...) {edit.Invalidate();throw;}

void DialogKanjiTimer::OnKeyDown(wxKeyEvent &event) try {
	wxCommandEvent evt;
	switch(event.GetKeyCode()) {
		case WXK_ESCAPE:
			edit.End();LinesToChange.clear();EndModal(wxID_CANCEL);
			break;
		case WXK_BACK:
			OnUnlink(evt);
			break;
		case WXK_RIGHT: //inc dest selection len
			display->IncreaseDestinationMatch();
			break;
		case WXK_LEFT: //dec dest selection len
			display->DecreaseDestinationMatch();
			break;
		case WXK_UP: //inc source selection len
			display->IncreaseSourceMatch();
			break;
		case WXK_DOWN: //dec source selection len
			display->DecreaseSourceMatch();
			break;
		case WXK_RETURN:
			if (display->GetRemainingSource() == 0 && display->GetRemainingDestination() == 0)
				OnAccept(evt);
			else
				OnLink(evt);
			break;
		default :
			event.Skip();
	}
}
catch (...) {edit.Invalidate();throw;}

void DialogKanjiTimer::ResetForNewLine()
{
	if (!currentSourceLine || !currentDestinationLine)
	{
		currentSourceLine = currentDestinationLine = 0;
		wxBell();
	}

	auto source=edit.Find(currentSourceLine),destination=edit.Find(currentDestinationLine);
	if(source && destination) {
		AssDialogue src(*source),dst(*destination);display->SetInputData(&src,&dst);
	} else display->SetInputData(nullptr,nullptr);

	TryAutoMatch();

	display->SetFocus();
}

void DialogKanjiTimer::TryAutoMatch()
{
	if (Interpolate->GetValue())
		display->AutoMatchJapanese();
}

int DialogKanjiTimer::FindNextStyleMatch(int id, const std::string& style) {
	bool found=false;
	for(auto const& row:edit.Rows()) {
		if(found && row.Style==style && !row.Text.get().empty())return row.Id;
		if(row.Id==id)found=true;
	}
	return 0;
}
int DialogKanjiTimer::FindPrevStyleMatch(int id, const std::string& style) {
	int previous=0;
	for(auto const& row:edit.Rows()) {
		if(row.Id==id)return previous;
		if(row.Style==style && !row.Text.get().empty())previous=row.Id;
	}
	return 0;
}

}

void ShowKanjiTimerDialog(agi::Context *c) {
	DialogKanjiTimer(c).ShowModal();
}

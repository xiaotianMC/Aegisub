// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "script_panel.h"

#include "ass_file.h"
#include "compat.h"
#include "flat_controls.h"
#include "format.h"
#include "style_panel.h"
#include "style_panel_parts.h"
#include "subtitle_project_access.h"
#include "theme.h"
#include "include/aegisub/context.h"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/intl.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <richedit.h>
#endif

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

std::optional<Thumb> ThumbFor(int pos, int page, int range, int track, int least) {
	if (page <= 0 || range <= page || track <= 0) return std::nullopt;
	Thumb thumb;
	thumb.height = std::clamp(static_cast<int>(static_cast<long long>(track) * page / range), std::min(least, track), track);
	int room = track - thumb.height;
	pos = std::clamp(pos, 0, range - page);
	thumb.top = static_cast<int>(static_cast<long long>(room) * pos / (range - page));
	return thumb;
}

int PosForThumb(int top, int page, int range, int track, int least) {
	auto thumb = ThumbFor(0, page, range, track, least);
	if (!thumb) return 0;
	int room = track - thumb->height;
	if (room <= 0) return 0;
	top = std::clamp(top, 0, room);
	return static_cast<int>((static_cast<long long>(top) * (range - page) + room / 2) / room);
}

int CountLines(wxString const& text) {
	int count = 0;
	for (wxString const& line : wxSplit(text, '\n', 0))
		if (!Trimmed(line).empty()) ++count;
	return count;
}
}

namespace {
/// The box's own scroll bar is hidden, and drawn thin on the field instead
/// (docs/style-panel.md#文案面板): on Windows it is cut off by the window
/// holding the box, made narrower than it; on GTK it is never shown
int HiddenBarWidth(wxWindow *box) {
#ifdef __WXMSW__
	return wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, box);
#else
	(void)box;
	return 0;
#endif
}

void HideOwnBar(wxTextCtrl *box) {
#ifdef __WXGTK__
	// Scrolled still, by the wheel and the keys, with no bar of its own
	auto window = GTK_SCROLLED_WINDOW(box->GetHandle());
	GtkPolicyType horizontal, vertical;
	gtk_scrolled_window_get_policy(window, &horizontal, &vertical);
	gtk_scrolled_window_set_policy(window, horizontal, GTK_POLICY_EXTERNAL);
#else
	(void)box;
#endif
}

/// pos, page, range of the box's text, in its own units (pixels)
std::tuple<int, int, int> ReadScroll(wxTextCtrl *box) {
#if defined(__WXGTK__)
	auto adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(box->GetHandle()));
	double lower = gtk_adjustment_get_lower(adjustment);
	return {static_cast<int>(gtk_adjustment_get_value(adjustment) - lower),
		static_cast<int>(gtk_adjustment_get_page_size(adjustment)),
		static_cast<int>(gtk_adjustment_get_upper(adjustment) - lower)};
#elif defined(__WXMSW__)
	// Disabled while the text fits: no bar then, whatever it counts
	SCROLLBARINFO bar{};
	bar.cbSize = sizeof(bar);
	if (::GetScrollBarInfo(static_cast<HWND>(box->GetHWND()), OBJID_VSCROLL, &bar)
		&& (bar.rgstate[0] & (STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_INVISIBLE)))
		return {0, 0, 0};
	SCROLLINFO info{};
	info.cbSize = sizeof(info);
	info.fMask = SIF_ALL;
	if (!::GetScrollInfo(static_cast<HWND>(box->GetHWND()), SB_VERT, &info)) return {0, 0, 0};
	return {info.nPos - info.nMin, static_cast<int>(info.nPage), info.nMax - info.nMin + 1};
#else
	(void)box;
	return {0, 0, 0};
#endif
}

void WriteScroll(wxTextCtrl *box, int pos) {
#if defined(__WXGTK__)
	auto adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(box->GetHandle()));
	gtk_adjustment_set_value(adjustment, gtk_adjustment_get_lower(adjustment) + pos);
#elif defined(__WXMSW__)
	// The rich edit box scrolls by pixels, as its scroll bar counts them
	POINT point{};
	::SendMessage(static_cast<HWND>(box->GetHWND()), EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&point));
	point.y = pos;
	::SendMessage(static_cast<HWND>(box->GetHWND()), EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&point));
#else
	(void)box; (void)pos;
#endif
}

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

ScriptPanel::ScriptPanel(wxWindow *parent, agi::Context *c)
: wxPanel(parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_NONE)
, c(c)
{
	theme::UseSurface(this);
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	const int pad = FromDIP(12);
	auto outer = new wxBoxSizer(wxVERTICAL);

	// The panel's name on the top bar, with the button going back to the
	// style panel and the one folding it away, as tall as the style panel's
	// with its switch
	{
		auto line = new wxBoxSizer(wxHORIZONTAL);
		line->SetMinSize(wxSize(-1, FromDIP(26)));
		auto title = new wxStaticText(this, -1, wxGETTEXT_IN_CONTEXT("page", "Script"));
		title->SetFont(title->GetFont().Bold().Scaled(1.15f));
		line->AddSpacer(pad);
		line->Add(title, 1, wxALIGN_CENTER_VERTICAL);
		auto style = new StylePanel::IconButton(this, StylePanel::IconButton::Glyph::Style, _("Back to the style panel: style, animation and timing"));
		style->on_click = [this] { if (on_style) on_style(); };
		parts["style"] = style;
		line->Add(style, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, pad - FromDIP(6));
		auto collapse = new StylePanel::IconButton(this, StylePanel::IconButton::Glyph::Collapse, _("Hide the script panel"));
		collapse->on_click = [this] {
			// The keyboard on it would be left in a hidden window
			bool keyboard = HasKeyboard();
			if (on_collapse) on_collapse();
			if (keyboard) flat::GoHome(this);
		};
		parts["collapse"] = collapse;
		line->Add(collapse, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, pad - FromDIP(4));
		outer->Add(line, 0, wxEXPAND | wxTOP | wxBOTTOM, pad - FromDIP(4));
		top_line = line;
	}

	// Below the line under the top bar: the split button, and how many
	// lines there are against the right edge
	{
		auto line = new wxBoxSizer(wxHORIZONTAL);
		auto split = new StylePanel::TextButton(this, _("One sentence a line"),
			_("Break the text after each sentence's end (。！？… and . ! ? before a space), so each sentence is on a line of its own"));
		split->on_click = [this] { Press("split"); };
		parts["split"] = split;
		line->Add(split, 0, wxALIGN_CENTER_VERTICAL);
		auto count = new StylePanel::Caption(this);
		count->AlignRight();
		parts["count"] = count;
		line->Add(count, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
		outer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
	}

	// Why it can't be changed now: a notice, as a locked editor has, its
	// words wrapped rather than cut
	notice = new wxPanel(this, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	notice_text = new wxStaticText(notice, -1, "");
	{
		auto sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(notice_text, 0, wxEXPAND | wxALL, FromDIP(8));
		notice->SetSizer(sizer);
	}
	notice->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
		e.Skip();
		int width = e.GetSize().x - FromDIP(16);
		if (width > 0 && width != notice_width) {
			notice_width = width;
			notice_text->SetLabel(note);
			notice_text->Wrap(width);
			notice->Layout();
			CallAfter([this] { Layout(); });
		}
	});
	parts["note"] = notice;
	outer->Add(notice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
	notice->Hide();

	// The text on a rounded field, inset from its edge; the edge in the
	// accent colour while it has the keyboard, and a thin scroll bar at
	// its right in place of the box's own (drawn by OnPaint)
	clip = new wxPanel(this, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	text = new wxTextCtrl(clip, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_RICH2 | wxBORDER_NONE);
	text->SetHint(_("Type or paste the video's whole script here, to check the subtitles against it. It is saved with the subtitles"));
	text->SetToolTip(_("The video's script, saved in the subtitle file"));
	HideOwnBar(text);
	clip->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
		// Wider than the window holding it by its own bar, cut off
		wxSize size = e.GetSize();
		text->SetSize(0, 0, size.x + HiddenBarWidth(text), size.y);
	});
	{
		auto field = new wxBoxSizer(wxHORIZONTAL);
		field->Add(clip, 1, wxEXPAND);
		field->AddSpacer(bar_room);
		outer->Add(field, 1, wxEXPAND | wxALL, pad + inset);
	}
	// The bar follows the text however it was scrolled: the wheel, the
	// keys, typing, selecting
	text->Bind(wxEVT_IDLE, [this](wxIdleEvent &e) {
		e.Skip();
		if (ReadScroll(text) != drawn_scroll) RefreshRect(BarTrack().Inflate(FromDIP(4)), false);
	});
	for (auto type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_MOTION, wxEVT_LEAVE_WINDOW, wxEVT_MOUSEWHEEL})
		Bind(type, &ScriptPanel::OnBarMouse, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) {
		drag_from = -1;
		Refresh(false);
	});
	text->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) { e.Skip(); Refresh(false); });
	text->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) { e.Skip(); Refresh(false); });

	Bind(wxEVT_PAINT, [this](wxPaintEvent &) { OnPaint(); });
	Bind(wxEVT_SIZE, [this](wxSizeEvent &e) { e.Skip(); Refresh(false); });
	theme_changed = theme::AddChangeListener([this] { Recolour(); });
	Recolour();

	text->Bind(wxEVT_TEXT, [this](wxCommandEvent &) {
		// Typed: a split can no longer be taken back as such
		before_split.reset();
		Store();
	});
	text->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
		int key = e.GetKeyCode();
		if (key == WXK_TAB && !e.ControlDown() && !e.AltDown()) {
			// On to the panel's buttons, as Tab goes between them
			MoveFocus(text, !e.ShiftDown());
			return;
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

	connections.push_back(c->subtitleAccess->Read().AddCommitListener("script_panel.AddCommitListener",
		[this](int type, AssDialogue const*, SubtitleModelRebindTicket ticket) {
			// A file opened or made new
			if (type == AssFile::COMMIT_NEW) UpdateNow();
			if (ticket.Current()) ticket.Acknowledge(ticket.Revision());
		},
		std::weak_ptr<void const>(model_observer_lifetime)));
	connections.push_back(c->subtitleAccess->AddMetadataListener([this] { UpdateNow(); }));

	UpdateNow();
}

ScriptPanel::~ScriptPanel() = default;

void ScriptPanel::Recolour() {
	auto ui = theme::UiColours();
	wxColour surface = theme::Surface();
	wxColour ink = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
	text->SetBackgroundColour(theme::Mix(surface, ink, theme::IsDark() ? 0.08 : 0.05));
	clip->SetBackgroundColour(text->GetBackgroundColour());
	text->SetForegroundColour(ink);
	notice->SetBackgroundColour(ui.notice_back);
	notice_text->SetForegroundColour(ui.notice_text);
	notice->Refresh();
	text->Refresh();
	Refresh();
}

void ScriptPanel::OnPaint() {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	wxColour surface = theme::Surface();
	wxColour ink = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
	dc.SetBackground(wxBrush(surface));
	dc.Clear();
	int thin = std::max(1, FromDIP(1));
	// A line under the top bar, as under the style panel's section titles
	if (top_line) {
		int y = top_line->GetPosition().y + top_line->GetSize().y + FromDIP(8);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(theme::Mix(surface, ink, 0.12)));
		dc.DrawRectangle(0, y, GetClientSize().x, thin);
	}
	// The field the text is on, its edge the accent colour while typing
	wxRect field = clip->GetRect();
	field.width += bar_room;
	field.Inflate(inset);
	bool typing = FindFocus() == text;
	dc.SetBrush(wxBrush(text->GetBackgroundColour()));
	dc.SetPen(wxPen(typing ? theme::UiColours().accent : theme::Mix(surface, ink, 0.2), typing ? std::max(1, FromDIP(3) / 2) : thin));
	dc.DrawRoundedRectangle(field, FromDIP(6));
	// The thin scroll bar: a rounded thumb only, wider and darker under
	// the pointer, none when the text fits
	drawn_scroll = ReadScroll(text);
	if (auto thumb = ScrollThumb()) {
		bool strong = bar_hover || drag_from >= 0;
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(theme::Mix(text->GetBackgroundColour(), ink, strong ? 0.5 : 0.28)));
		wxRect r = *thumb;
		if (!strong) r.Deflate(FromDIP(1), 0);
		dc.DrawRoundedRectangle(r, r.width / 2.0);
	}
	// The notice's corners rounded, on the surface around it
	if (notice->IsShown()) {
		wxRect r = notice->GetRect();
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(notice->GetBackgroundColour()));
		dc.DrawRoundedRectangle(r.Inflate(FromDIP(2)), FromDIP(5));
	}
}

wxRect ScriptPanel::BarTrack() const {
	// Down the room right of the text, as tall as the text, its right
	// edge a little in from the field's
	wxRect box = clip->GetRect();
	int width = FromDIP(6);
	return wxRect(box.GetRight() + 1 + bar_room + inset - FromDIP(3) - width, box.y, width, box.height);
}

std::tuple<int, int, int> ScriptPanel::ScrollState() const {
	return ReadScroll(text);
}

std::optional<wxRect> ScriptPanel::ScrollThumb() const {
	auto [pos, page, range] = ReadScroll(text);
	wxRect track = BarTrack();
	auto thumb = script_text::ThumbFor(pos, page, range, track.height, FromDIP(24));
	if (!thumb) return std::nullopt;
	return wxRect(track.x, track.y + thumb->top, track.width, thumb->height);
}

void ScriptPanel::ScrollTextTo(int pos) {
	auto [now, page, range] = ReadScroll(text);
	WriteScroll(text, std::clamp(pos, 0, std::max(0, range - page)));
	Refresh(false);
}

void ScriptPanel::OnBarMouse(wxMouseEvent &event) {
	event.Skip();
	wxPoint p = event.GetPosition();
	wxRect track = BarTrack();
	// Easier to hit than to see: the whole room right of the text
	wxRect hit(clip->GetRect().GetRight() + 1, track.y, track.GetRight() + 1 - clip->GetRect().GetRight() + FromDIP(3), track.height);
	auto thumb = ScrollThumb();
	auto [pos, page, range] = ReadScroll(text);
	auto set_hover = [&](bool on) {
		if (bar_hover == on) return;
		bar_hover = on;
		RefreshRect(track.Inflate(FromDIP(4)), false);
	};
	auto type = event.GetEventType();
	if (type == wxEVT_LEAVE_WINDOW) {
		if (drag_from < 0) set_hover(false);
		return;
	}
	if (type == wxEVT_MOUSEWHEEL) {
		if (!thumb || !hit.Contains(p)) return;
		int lines = event.GetLinesPerAction() > 0 ? event.GetLinesPerAction() : 3;
		ScrollTextTo(pos - event.GetWheelRotation() * lines * text->GetCharHeight() / std::max(1, event.GetWheelDelta()));
		return;
	}
	if (type == wxEVT_MOTION) {
		if (drag_from >= 0 && thumb) {
			ScrollTextTo(script_text::PosForThumb(p.y - drag_from - track.y, page, range, track.height, FromDIP(24)));
			return;
		}
		set_hover(thumb && hit.Contains(p));
		return;
	}
	if (type == wxEVT_LEFT_UP) {
		if (drag_from < 0) return;
		drag_from = -1;
		if (HasCapture()) ReleaseMouse();
		set_hover(hit.Contains(p));
		Refresh(false);
		return;
	}
	if (type == wxEVT_LEFT_DOWN && thumb && hit.Contains(p)) {
		if (p.y >= thumb->y && p.y < thumb->GetBottom() + 1) {
			drag_from = p.y - thumb->y;
			CaptureMouse();
			Refresh(false);
		}
		// Above or below the thumb: a page up or down
		else ScrollTextTo(pos + (p.y < thumb->y ? -page : page));
	}
}

void ScriptPanel::UpdateNow() {
	wxString stored = to_wx(c->subtitleAccess->Read().Properties.script_text);
	if (text->GetValue() != stored) {
		// Another file's script: what the split took back belongs to the old one
		before_split.reset();
		text->ChangeValue(stored);
	}
	UpdateParts();
}

void ScriptPanel::Store() {
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

void ScriptPanel::Split() {
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

void ScriptPanel::UpdateParts() {
	auto split = static_cast<StylePanel::TextButton*>(parts["split"]);
	split->SetLabel(before_split ? _("Undo the split") : _("One sentence a line"));
	split->Enable(before_split || !text->GetValue().empty());
	int lines = script_text::CountLines(text->GetValue());
	static_cast<StylePanel::Caption*>(parts["count"])->SetText(lines ? fmt_plural(lines, "%d line", "%d lines", lines) : wxString());
	if (notice_text->GetLabel() != note) {
		notice_text->SetLabel(note);
		if (notice_width > 0) notice_text->Wrap(notice_width);
	}
	if (notice->IsShown() != !note.empty()) {
		notice->Show(!note.empty());
		Layout();
		Refresh(false);
	}
}

void ScriptPanel::Type(wxString const& value) {
	// As typed: wxEVT_TEXT follows
	text->SetValue(value);
}

void ScriptPanel::Press(std::string const& name) {
	if (name == "style") { if (on_style) on_style(); }
	else if (name == "collapse") static_cast<StylePanel::IconButton*>(parts["collapse"])->on_click();
	else if (name == "split") Split();
}

std::map<std::string, wxString> ScriptPanel::Shown() const {
	std::map<std::string, wxString> shown;
	auto split = static_cast<StylePanel::TextButton*>(parts.at("split"));
	shown["split"] = split->Shown() + (split->IsEnabled() ? "" : " disabled");
	shown["count"] = static_cast<StylePanel::Caption*>(parts.at("count"))->Shown();
	shown["note"] = note;
	return shown;
}

bool ScriptPanel::HasKeyboard() const {
	for (wxWindow *w = FindFocus(); w; w = w->GetParent())
		if (w == this) return true;
	return false;
}

bool ScriptPanel::TakeKeyboard() {
	if (!text->IsShownOnScreen()) return false;
	text->SetFocus();
	return true;
}

void ScriptPanel::MoveFocus(wxWindow *from, bool forward) {
	// Down the panel: the top bar's buttons, the split button, the text
	std::vector<wxWindow*> list;
	for (auto name : {"style", "collapse", "split"}) {
		auto part = parts[name];
		if (part->IsEnabled() && part->IsShownOnScreen()) list.push_back(part);
	}
	if (text->IsShownOnScreen()) list.push_back(text);
	if (list.empty()) {
		flat::GoHome(this);
		return;
	}
	auto it = std::find(list.begin(), list.end(), from);
	size_t n = list.size(), i;
	if (it == list.end()) i = forward ? 0 : n - 1;
	else i = (static_cast<size_t>(it - list.begin()) + (forward ? 1 : n - 1)) % n;
	list[i]->SetFocus();
}

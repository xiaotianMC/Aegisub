// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "font_popup.h"

#include <algorithm>

#include <wx/app.h>
#include <wx/fontenum.h>
#include <wx/intl.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

/// Installed fonts, without the vertical "@" variants of Windows
wxArrayString const& InstalledFonts() {
	static wxArrayString fonts;
	if (fonts.empty()) {
		for (auto const& face : wxFontEnumerator::GetFacenames())
			if (!face.StartsWith("@")) fonts.Add(face);
		fonts.Sort([](wxString const& a, wxString const& b) { return a.CmpNoCase(b); });
	}
	return fonts;
}

void FontPopup::Filter() {
	wxString query = search->GetValue().Lower();
	query.Trim(true).Trim(false);
	wxArrayString shown;
	for (auto const& font : fonts)
		if (query.empty() || font.Lower().Contains(query)) shown.Add(font);
	list->Freeze();
	list->Set(shown);
	int selected = query.empty() ? list->FindString(current) : 0;
	if (selected == wxNOT_FOUND && !shown.empty()) selected = 0;
	if (selected != wxNOT_FOUND && !shown.empty()) {
		list->SetSelection(selected);
		list->EnsureVisible(selected);
	}
	list->Thaw();
}

void FontPopup::Pick(wxString font) {
	font.Trim(true).Trim(false);
	if (font.empty()) return;
	auto pick = on_pick;
	Dismiss();
	pick(font);
}

void FontPopup::PickSelected() {
	int typed_match = list->FindString(search->GetValue().Trim(true).Trim(false));
	if (list->GetSelection() != wxNOT_FOUND && (typed_match != wxNOT_FOUND || list->GetCount() > 0))
		Pick(typed_match != wxNOT_FOUND ? list->GetString(typed_match) : list->GetStringSelection());
	else
		Pick(search->GetValue());
}

void FontPopup::OnDismiss() {
	wxTheApp->ScheduleForDestruction(this);
}

FontPopup::FontPopup(wxWindow *parent, wxArrayString fonts, wxString current, std::function<void(wxString const&)> pick)
: wxPopupTransientWindow(parent, wxBORDER_SIMPLE)
, fonts(std::move(fonts))
, current(std::move(current))
, on_pick(std::move(pick))
{
	auto panel = new wxPanel(this);
	search = new wxTextCtrl(panel, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	search->SetHint(_("Search fonts"));
	list = new wxListBox(panel, -1, wxDefaultPosition, wxSize(-1, FromDIP(280)));
	auto sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(search, 0, wxEXPAND | wxALL, FromDIP(4));
	sizer->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
	panel->SetSizer(sizer);
	Filter();

	search->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { Filter(); });
	search->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { PickSelected(); });
	search->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
		int n = list->GetSelection(), count = list->GetCount();
		switch (e.GetKeyCode()) {
			case WXK_DOWN: if (count) { n = std::min(count - 1, n + 1); list->SetSelection(n); list->EnsureVisible(n); } break;
			case WXK_UP: if (count) { n = std::max(0, n - 1); list->SetSelection(n); list->EnsureVisible(n); } break;
			case WXK_ESCAPE: Dismiss(); break;
			default: e.Skip();
		}
	});
	list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &e) { Pick(e.GetString()); });
}

void FontPopup::Open(wxWindow *field) {
	wxSize size(std::max(field->GetSize().x, FromDIP(220)), -1);
	GetChildren().front()->SetInitialSize(wxSize(size.x, -1));
	GetChildren().front()->Fit();
	SetClientSize(GetChildren().front()->GetSize());
	Position(field->ClientToScreen(wxPoint(0, 0)), wxSize(0, field->GetSize().y));
	Popup(search);
}

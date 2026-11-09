// Copyright (c) 2012, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#include "flat_controls.h"
#include "flat_dialog.h"
#include "flat_list.h"
#include "theme.h"

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

int GetSelectedChoices(wxWindow *parent, wxArrayInt& selections, wxString const& message, wxString const& caption, wxArrayString const& choices) {
	wxDialog dialog(parent, -1, caption, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	theme::UseSurface(&dialog);
	int gap = dialog.FromDIP(10), row = dialog.FromDIP(6);

	// The choices ticked, as wxMultiChoiceDialog has them
	auto list = new FlatList(&dialog, true, true);
	list->Set(choices);
	list->SetMinSize(dialog.FromDIP(wxSize(300, 240)));
	for (int i : selections)
		if (i >= 0 && i < static_cast<int>(choices.size())) list->Check(i);

	auto tick_all = [&](bool tick) {
		for (unsigned i = 0; i < list->GetCount(); ++i) list->Check(i, tick);
	};
	auto buttons = new flat::DialogButtons(&dialog, wxOK | wxCANCEL);
	buttons->AddLeft(_("Select &All"))->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { tick_all(true); });
	buttons->AddLeft(_("Select &None"))->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { tick_all(false); });

	auto sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(new wxStaticText(&dialog, -1, message), wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	sizer->Add(list, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, row + gap / 2));
	sizer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, gap));
	dialog.SetSizerAndFit(sizer);
	dialog.CenterOnParent();
	list->SetFocus();

	if (dialog.ShowModal() != wxID_OK) return -1;

	selections.clear();
	for (unsigned i = 0; i < list->GetCount(); ++i)
		if (list->IsChecked(i)) selections.push_back(i);
	return selections.GetCount();
}

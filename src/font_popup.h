// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The list of fonts opened from a font field: the style panel's and the
// style editor's (docs/style-panel.md).
#pragma once

#include <functional>

#include <wx/arrstr.h>
#include <wx/popupwin.h>

class wxListBox;
class wxTextCtrl;

/// Installed fonts, without the vertical "@" variants of Windows, sorted
wxArrayString const& InstalledFonts();

/// The font list below a font field: type to narrow it down, Enter or a
/// click to choose; Enter on a name that is not in the list uses it as typed
class FontPopup final : public wxPopupTransientWindow {
	wxTextCtrl *search;
	wxListBox *list;
	wxArrayString fonts;
	wxString current;
	std::function<void(wxString const&)> on_pick;

	void Filter();
	void Pick(wxString font);
	void PickSelected();
	void OnDismiss() override;

public:
	FontPopup(wxWindow *parent, wxArrayString fonts, wxString current, std::function<void(wxString const&)> pick);

	/// Show it below @p field, at least as wide
	void Open(wxWindow *field);
};

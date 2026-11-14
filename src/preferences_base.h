// Copyright (c) 2010, Amar Takhar <verm@aegisub.org>
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

/// @file preferences_base.h
/// @brief Base preferences dialogue classes
/// @see preferences_base.cpp
/// @ingroup configuration_ui

#pragma once

#include <libaegisub/signal.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <wx/panel.h>
#include <wx/scrolwin.h>

class Preferences;
class wxControl;
class wxFlexGridSizer;
class wxSimplebook;
class wxSizer;
class wxString;
namespace agi { class OptionValue; }

/// The pages of the preferences and the list choosing one on their left,
/// drawn as the main window's panels are: a page's name, indented under
/// the page it belongs to, the one shown marked in the accent colour. While
/// searching, pages with a match show how many and the others are dim.
class PreferencesBook final : public wxPanel {
public:
	class Nav;

private:
	wxSimplebook *pages;
	Nav *nav;
	wxSizer *nav_column;

public:
	PreferencesBook(wxWindow *parent);

	/// The window a page is made in
	wxWindow *PageParent() const;
	void AddPage(wxWindow *page, wxString const& name, bool sub);
	size_t GetPageCount() const;
	wxWindow *GetPage(size_t page) const;
	wxString GetPageText(size_t page) const;
	int GetSelection() const;
	/// Show @p page, calling on_change
	void SetSelection(size_t page);
	/// Show @p page, without calling on_change
	void ChangeSelection(size_t page);
	/// Put @p window above the list (the search box)
	void AddAboveList(wxWindow *window);
	/// While searching, the number of matches on each page; empty when not
	void SetMatches(std::vector<size_t> const& matches);
	/// The number shown beside @p page, as text; for tests
	wxString MatchText(size_t page) const;

	/// Called with the page shown when the user chooses another
	std::function<void(size_t)> on_change;
};

struct PageSection {
	// Create controls in a section with the box as a parent and add them to
	// the sizer: three to a row, the row's mark, the name and its control.
	wxFlexGridSizer *sizer;
	wxWindow *box;
};

struct OptionAddArgs {
	double min = 0;
	double max = INT_MAX;
	double inc = 1;
	bool alpha = false;
	/// A line explaining the setting, under its name
	wxString help = {};
};

class OptionPage : public wxScrolled<wxPanel> {
public:
	class Mark;

private:
	template<class T>
	Mark *Add(PageSection section, wxString const& label, T *control, std::string const& option, wxString const& help);
	/// The name of a row, with its explanation under it if there is one
	wxSizer *Label(PageSection section, wxWindow *name, wxString const& help);

	/// A setting's row: its option, and showing a value in its control
	struct Row {
		std::string option;
		Mark *mark;
		wxWindow *control;
		/// Show @p value in the control, and take it as the dialog's
		std::function<void(agi::OptionValue const&)> show;
	};
	std::vector<Row> rows;
	/// The row's mark, before its name
	Mark *AddMark(PageSection section, std::string const& option);
	void AddRow(std::string const& option, Mark *mark, wxWindow *control, std::function<void(agi::OptionValue const&)> show);

public:
	enum Style {
		PAGE_DEFAULT    =   0x00000000,
		PAGE_SCROLL     =   0x00000001,
		PAGE_SUB        =   0x00000002
	};

	wxSizer *sizer;
	Preferences *parent;
	PageSection PageSizer(wxString name);

	/// Nothing now: each setting has a row of its own
	void CellSkip(PageSection section);
	/// A dim line of explanation in @p section
	void Note(PageSection section, wxString const& text);
	/// A row named @p name holding @p control, for what is not a setting
	void Row(PageSection section, wxString const& name, wxSizer *control, wxString const& help = {});

	/// A row for the option @p opt_name: a check box for a yes or no, a
	/// field for a number or text, a button for a colour. @return Its control
	wxWindow *OptionAdd(PageSection section, const wxString &name, const char *opt_name, OptionAddArgs kwargs = {});
	void OptionChoice(PageSection section, const wxString &name, const wxArrayString &choices, const char *opt_name, bool translate = false, wxString const& help = {});
	void OptionBrowse(PageSection section, const wxString &name, const char *opt_name, wxWindow *enabler = nullptr, bool do_enable = false);
	void OptionFont(PageSection section, std::string opt_prefix);

	/// Enable ctrl only when cbx is checked
	void EnableIfChecked(wxWindow *cbx, wxWindow *ctrl);
	/// Enable ctrl only when cbx is not checked
	void DisableIfChecked(wxWindow *cbx, wxWindow *ctrl);

	/// Whether @p option will have a value other than its default: changed
	/// before, or in the dialog
	bool IsModified(std::string const& option) const;
	/// Give @p option its default, as its row's ↺ does, until OK or Cancel
	void Reset(std::string const& option);
	/// Give every setting of the page its default, as Restore Defaults does
	void ResetAll();
	/// The options with a row on the page
	std::vector<std::string> Options() const;
	/// The control of @p option's row; for tests
	wxWindow *ControlOf(std::string const& option) const;
	/// Draw the rows' marks again, after a change
	void RefreshMarks();
	/// Scroll so @p window shows
	void ScrollTo(wxWindow *window);

	OptionPage(PreferencesBook *book, Preferences *parent, wxString name, int style = PAGE_DEFAULT);
};

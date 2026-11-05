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

/// @file preferences.h
/// @brief Preferences dialogue
/// @see preferences.cpp
/// @ingroup configuration_ui

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <wx/dialog.h>
#include <wx/font.h>

class wxButton;
class wxSearchCtrl;
class wxStaticText;
class wxTreebook;
namespace agi { class OptionValue; }

class Preferences final : public wxDialog {
public:
	typedef std::function<void ()> Thunk;
private:
	wxTreebook *book;
	wxButton *applyButton;

	std::map<std::string, std::unique_ptr<agi::OptionValue>> pending_changes;
	std::vector<Thunk> pending_callbacks;
	std::vector<std::string> option_names;

	/// A label of a page that the search box looks in.
	struct SearchEntry {
		wxWindow *window;          ///< nullptr for the page's own name
		size_t page;
		std::vector<std::wstring> texts; ///< Label, its pinyin and initials, normalised
		wxFont font;
		wxColour colour;           ///< Explicitly set colour, invalid if none
		bool matched = false;
	};
	wxSearchCtrl *search;
	wxStaticText *search_status;
	std::vector<SearchEntry> search_entries;
	std::vector<size_t> matching_pages;
	std::wstring search_query; ///< Normalised

	void IndexPages();
	void ShowMatches();
	void NextMatchingPage();

	void OnOK(wxCommandEvent &);
	void OnCancel(wxCommandEvent &);
	void OnApply(wxCommandEvent &);
	void OnResetDefault(wxCommandEvent&);

public:
	Preferences(wxWindow *parent);

	/// Add an option to be set when the OK or Apply button is clicked
	/// @param new_value Clone of the option with the new value to copy over
	void SetOption(std::unique_ptr<agi::OptionValue> new_value);

	/// All a function to call when the OK or Apply button is clicked
	/// @param callback Function to call
	void AddPendingChange(Thunk const& callback);

	/// Add an option which can be changed via the dialog
	/// @param name Name of the option
	///
	/// This is used for resetting options to the defaults. We don't want to
	/// simply revert to the default config file as a bunch of things other than
	/// user options are stored in it. Perhaps that should change in the future.
	void AddChangeableOption(std::string const& name);

	/// Search the labels of every page for @p query (by text, pinyin or pinyin
	/// initials); matching labels are highlighted and the first matching page
	/// is shown if the current one has no match. Empty clears the search.
	void Search(wxString const& query);
	/// Pages with a match, in order.
	std::vector<size_t> const& MatchingPages() const { return matching_pages; }
	/// Highlighted controls on @p page.
	std::vector<wxWindow*> MatchingControls(size_t page) const;
	/// Page shown now.
	size_t CurrentPage() const;
	/// What the search found, as shown beside the search box.
	wxString SearchStatus() const;
};

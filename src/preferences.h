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
class PreferencesBook;
namespace agi { class OptionValue; }

class Preferences final : public wxDialog {
public:
	typedef std::function<void ()> Thunk;
private:
	PreferencesBook *book = nullptr;
	wxWindow *applyButton;

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
	wxTextCtrl *search;
	wxWindow *search_frame;
	wxStaticText *search_status;
	std::vector<SearchEntry> search_entries;
	std::vector<size_t> matching_pages;
	std::wstring search_query; ///< Normalised

	void IndexPages();
	void ShowMatches();
	void NextMatchingPage();
	/// Scroll @p page to its first match, while searching
	void ShowFirstMatch(size_t page);

	void OnOK(wxCommandEvent &);
	void OnCancel(wxCommandEvent &);
	void OnApply(wxCommandEvent &);

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
	/// Put @p query in the search box and search for it
	void ShowSearch(wxString const& query);
	/// The value @p name will be given by OK or Apply, if it was changed; for tests
	agi::OptionValue const* Pending(std::string const& name) const {
		auto it = pending_changes.find(name);
		return it == pending_changes.end() ? nullptr : it->second.get();
	}
	/// Pages with a match, in order.
	std::vector<size_t> const& MatchingPages() const { return matching_pages; }
	/// Highlighted controls on @p page.
	std::vector<wxWindow*> MatchingControls(size_t page) const;
	/// Page shown now.
	size_t CurrentPage() const;
	PreferencesBook *Book() const { return book; }
	/// Give the page shown its defaults, as Restore Page Defaults does: its
	/// settings until OK or Cancel; the hotkeys at once, after asking
	void RestorePageDefaults();
	/// What the search found, as shown beside the search box.
	wxString SearchStatus() const;

	/// A label the search looks at, and the page it is on.
	struct Setting {
		wxString label, page;
		bool is_page = false; ///< The page's own name
	};
	/// Every label the search looks at, in page order.
	std::vector<Setting> Settings() const;
};

/// Preferences::Settings, read once a run from a dialog made unseen (the
/// command palette's ',' search): about a third of a second the first time.
std::vector<Preferences::Setting> const& SettingLabels(wxWindow *parent);

// Copyright (c) 2005, Rodrigo Braz Monteiro
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
#include "subtitle_value_edit.h"
#include "ass_style.h"
#include "ass_style_storage.h"
#include "charset_detect.h"
#include "compat.h"
#include "dialog_manager.h"
#include "dialog_style_editor.h"
#include "dialogs.h"
#include "flat_controls.h"
#include "format.h"
#include "help_button.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "persist_location.h"
#include "selection_controller.h"
#include "style_list.h"
#include "subtitle_format.h"
#include "theme.h"

#include <libaegisub/fs.h>
#include <libaegisub/path.h>
#include <libaegisub/signal.h>
#include <libaegisub/split.h>
#include <libaegisub/vfr.h>

#include <algorithm>
#include <set>
#include <functional>
#include <future>
#include <memory>
#include <vector>
#include <unordered_map>
#include <wx/filename.h>
#include <wx/fontenum.h>
#include <wx/intl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/choicdlg.h> // Keep this last so wxUSE_CHOICEDLG is set.

namespace {
class DialogStyleManager final : public wxDialog {
protected:
    // Declared first: expires only after all owned bindings/cache members are destroyed.
    std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);
private:
	agi::Context *c; ///< Project context
	std::unique_ptr<PersistLocation> persist;

	agi::signal::Connection commit_connection;
	agi::signal::Connection active_line_connection;

	std::shared_future<wxArrayString> font_list;

	/// Styles in the current subtitle file
	std::vector<AssStyle const*> styleMap;

	/// Style storage manager
	AssStyleStorage Store;

	/// The catalogs to choose from, as shown in CatalogList
	std::vector<wxString> catalogs;

	StyleCard *StorageCard;
	StyleCard *CurrentCard;

	FlatChoice *CatalogList;
	StyleList *StorageList;
	StyleList *CurrentList;

	wxWindow *CatalogDelete;

	wxWindow *MoveToLocal;
	wxWindow *MoveToStorage;

	wxWindow *StorageNew;
	wxWindow *StorageEdit;
	wxWindow *StorageCopy;
	wxWindow *StorageDelete;
	wxWindow *StorageMoveUp;
	wxWindow *StorageMoveDown;
	wxWindow *StorageMoveTop;
	wxWindow *StorageMoveBottom;
	wxWindow *StorageSort;

	wxWindow *CurrentNew;
	wxWindow *CurrentEdit;
	wxWindow *CurrentCopy;
	wxWindow *CurrentDelete;
	wxWindow *CurrentMoveUp;
	wxWindow *CurrentMoveDown;
	wxWindow *CurrentMoveTop;
	wxWindow *CurrentMoveBottom;
	wxWindow *CurrentSort;

	/// Load the list of available storages
	void LoadCatalog();
	/// Show the catalogs in the list, @p name chosen
	void ShowCatalogs(wxString const& name);
	/// Load the style list from the subtitles file
	void LoadCurrentStyles(int commit_type);
	/// Enable/disable all of the buttons as appropriate
	void UpdateButtons();
	/// The notes on the right of each style: how many lines use a style of
	/// the script, and whether a stored style is in the script as it is
	void UpdateNotes();
	/// Move styles up or down
	/// @param storage Storage or current file styles
	/// @param type 0: up; 1: top; 2: down; 3: bottom; 4: sort
	void MoveStyles(bool storage, int type);

	/// Open the style editor for the given style on the script
	/// @param style Style to edit, or nullptr for new
	/// @param new_name Default new name for copies
	void ShowCurrentEditor(AssStyle const* style, std::string const& new_name = "");

	/// Open the style editor for the given style in the storage
	/// @param style Style to edit, or nullptr for new
	/// @param new_name Default new name for copies
	void ShowStorageEditor(AssStyle *style, std::string const& new_name = "");

	/// Save the storage and update the view after a change
	void UpdateStorage();

	void OnChangeCatalog();
	void OnCatalogNew();
	void OnCatalogDelete();

	void OnCopyToCurrent();
	void OnCopyToStorage();

	void OnCurrentCopy();
	void OnCurrentDelete();
	void OnCurrentEdit();
	void OnCurrentImport();
	void OnCurrentNew();

	void OnStorageCopy();
	void OnStorageDelete();
	void OnStorageEdit();
	void OnStorageNew();

	void OnKeyDown(wxKeyEvent &event);
	void PasteToCurrent();
	void PasteToStorage();

	/// The menu of a right click on one of the lists
	void ShowMenu(bool storage, wxPoint pos);

	template<class T>
	void CopyToClipboard(StyleList *list, T const& v);

	void OnActiveLineChanged();

public:
	DialogStyleManager(agi::Context *context);
};

template<class Func>
std::string unique_name(Func name_checker, std::string const& source_name) {
	if (name_checker(source_name)) {
		std::string name = agi::format(_("%s - Copy"), source_name);
		for (int i = 2; name_checker(name); ++i)
			name = agi::format(_("%s - Copy (%d)"), source_name, i);
		return name;
	}
	return source_name;
}

template<class Func1, class Func2>
void add_styles(Func1 name_checker, Func2 style_adder) {
	auto cb = GetClipboard();
	int failed_to_parse = 0;
	for (auto tok : agi::Split(cb, '\n')) {
		tok = agi::Trim(tok);
		if (tok.empty()) continue;
		try {
			AssStyle *s = new AssStyle(tok);
			s->name = unique_name(name_checker, s->name);
			style_adder(s);
		}
		catch (...) {
			++failed_to_parse;
		}
	}
	if (failed_to_parse)
		wxMessageBox(_("Could not parse style"), _("Could not parse style"), wxOK | wxICON_EXCLAMATION);
}

int confirm_delete(int n, wxWindow *parent, wxString const& title) {
	return wxMessageBox(
		fmt_plural(n, "Are you sure you want to delete this style?", "Are you sure you want to delete these %d styles?", n),
		title, wxYES_NO | wxICON_EXCLAMATION, parent);
}

int get_single_sel(StyleList *lb) {
	wxArrayInt selections;
	int n = lb->GetSelections(selections);
	return n == 1 ? selections[0] : -1;
}

DialogStyleManager::DialogStyleManager(agi::Context *context)
: wxDialog(context->parent, -1, _("Styles Manager"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
, c(context)
, commit_connection(c->subtitleAccess->Read().AddCommitListener("dialog_style_manager.AddCommitListener", [this](int type, AssDialogue const*, SubtitleModelRebindTicket ticket) { LoadCurrentStyles(type); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)))
, active_line_connection(c->selectionController->AddActiveLineListener("dialog_style_manager.AddActiveLineListener", [this](SubtitleModelRebindTicket ticket) { OnActiveLineChanged(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime)))
, font_list(std::async(std::launch::async, []() -> wxArrayString {
	wxArrayString fontList = wxFontEnumerator::GetFacenames();
	fontList.Sort();
	return fontList;
}))
{
	SetIcons(GETICONS(style_toolbutton));
	theme::UseSurface(this);

	// A card for each side (docs/main-window.md): the title and what is
	// chosen at its top, the styles, and what can be done to them below
	auto make_card = [this](wxString const& title, wxString const& empty, StyleCard **card, StyleList **list,
		wxWindow **nw, wxWindow **edit, wxWindow **copy, wxWindow **del,
		wxWindow **up, wxWindow **down, wxWindow **top, wxWindow **bottom, wxWindow **sort) {
		*card = new StyleCard(this, title);
		*list = new StyleList(*card, empty);
		*nw = new FlatButton(*card, _("&New"));
		*edit = new FlatButton(*card, _("&Edit"));
		*copy = new FlatButton(*card, _("&Copy"));
		*del = new FlatButton(*card, _("&Delete"));
		(*nw)->SetToolTip(_("Make a new style"));
		(*edit)->SetToolTip(_("Change the style chosen (or double-click it)"));
		(*copy)->SetToolTip(_("Make a new style starting from the one chosen"));
		(*del)->SetToolTip(_("Delete the styles chosen (Delete)"));
		*up = new StyleTool(*card, StyleTool::Glyph::Up, _("Move style up"));
		*down = new StyleTool(*card, StyleTool::Glyph::Down, _("Move style down"));
		*top = new StyleTool(*card, StyleTool::Glyph::Top, _("Move style to top"));
		*bottom = new StyleTool(*card, StyleTool::Glyph::Bottom, _("Move style to bottom"));
		*sort = new StyleTool(*card, StyleTool::Glyph::Sort, _("Sort styles alphabetically"));

		auto footer = new wxBoxSizer(wxHORIZONTAL);
		for (auto button : {*nw, *edit, *copy, *del})
			footer->Add(button, wxSizerFlags().Border(wxRIGHT, FromDIP(6)));
		footer->AddStretchSpacer(1);
		for (auto tool : {*up, *down, *top, *bottom, *sort})
			footer->Add(tool, wxSizerFlags().Center().Border(wxLEFT, FromDIP(2)));

		auto column = new wxBoxSizer(wxVERTICAL);
		auto header = new wxBoxSizer(wxHORIZONTAL);
		header->SetMinSize(-1, FromDIP(28));
		header->AddSpacer((*card)->TitleWidth());
		header->AddStretchSpacer(1);
		column->Add(header, wxSizerFlags().Expand());
		column->Add(*list, wxSizerFlags(1).Expand().Border(wxTOP | wxBOTTOM, FromDIP(8)));
		column->Add(footer, wxSizerFlags().Expand());
		auto padded = new wxBoxSizer(wxVERTICAL);
		padded->Add(column, wxSizerFlags(1).Expand().Border(wxALL, (*card)->Padding()));
		(*card)->SetSizer(padded);
		return header;
	};

	// The storage: a catalog of styles kept between scripts
	auto storage_header = make_card(_("Storage"), _("No styles stored in this catalog"), &StorageCard, &StorageList,
		&StorageNew, &StorageEdit, &StorageCopy, &StorageDelete,
		&StorageMoveUp, &StorageMoveDown, &StorageMoveTop, &StorageMoveBottom, &StorageSort);
	CatalogList = new FlatChoice(StorageCard, _("Catalog of available storages"));
	CatalogList->SetMinSize(wxSize(FromDIP(150), FromDIP(28)));
	auto CatalogNew = new StyleTool(StorageCard, StyleTool::Glyph::Add, _("New catalog: a storage of its own"));
	CatalogDelete = new StyleTool(StorageCard, StyleTool::Glyph::Remove, _("Delete this catalog and its styles"));
	storage_header->Add(CatalogList, wxSizerFlags().Center());
	storage_header->Add(CatalogNew, wxSizerFlags().Center().Border(wxLEFT, FromDIP(4)));
	storage_header->Add(CatalogDelete, wxSizerFlags().Center().Border(wxLEFT, FromDIP(2)));

	// The styles of the script
	auto current_header = make_card(_("Current script"), _("The script has no styles"), &CurrentCard, &CurrentList,
		&CurrentNew, &CurrentEdit, &CurrentCopy, &CurrentDelete,
		&CurrentMoveUp, &CurrentMoveDown, &CurrentMoveTop, &CurrentMoveBottom, &CurrentSort);
	auto CurrentImport = new FlatButton(CurrentCard, _("&Import from script..."));
	CurrentImport->SetToolTip(_("Copy styles from another subtitle file into this script"));
	current_header->Add(CurrentImport, wxSizerFlags().Center());

	// Between the cards, copying the styles chosen from one to the other
	MoveToLocal = new StyleTool(this, StyleTool::Glyph::ToRight, _("Copy the styles chosen to the current script"), true);
	MoveToStorage = new StyleTool(this, StyleTool::Glyph::ToLeft, _("Copy the styles chosen to the storage"), true);
	auto transfer = new wxBoxSizer(wxVERTICAL);
	transfer->AddStretchSpacer(1);
	transfer->Add(MoveToLocal, wxSizerFlags().Center());
	transfer->AddSpacer(FromDIP(10));
	transfer->Add(MoveToStorage, wxSizerFlags().Center());
	transfer->AddStretchSpacer(1);

	auto StylesSizer = new wxBoxSizer(wxHORIZONTAL);
	StylesSizer->Add(StorageCard, wxSizerFlags(1).Expand());
	StylesSizer->Add(transfer, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, FromDIP(10)));
	StylesSizer->Add(CurrentCard, wxSizerFlags(1).Expand());

	// What the keyboard and the mouse do, and the dialog's buttons
	auto hint = new wxStaticText(this, -1, _("Double-click a style to edit it, right-click for more. Ctrl+C and Ctrl+V copy styles as text, between scripts too."));
	hint->SetForegroundColour(flat::Colours(this).dim);
	wxStdDialogButtonSizer *stdButtons = CreateStdDialogButtonSizer(wxCANCEL | wxHELP);
	stdButtons->GetCancelButton()->SetLabel(_("Close"));
	Bind(wxEVT_BUTTON, std::bind(&HelpButton::OpenPage, "Styles Manager"), wxID_HELP);
	auto buttonSizer = new wxBoxSizer(wxHORIZONTAL);
	buttonSizer->Add(hint, wxSizerFlags(1).Center().Border(wxLEFT | wxRIGHT, FromDIP(4)));
	buttonSizer->Add(stdButtons, wxSizerFlags().Expand());

	wxSizer *MainSizer = new wxBoxSizer(wxVERTICAL);
	MainSizer->Add(StylesSizer, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(12)));
	MainSizer->Add(buttonSizer, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));

	SetSizerAndFit(MainSizer);
	SetMinSize(GetSize());
	SetSize(wxSize(std::max(GetSize().x, FromDIP(860)), std::max(GetSize().y, FromDIP(560))));

	// Position window
	persist = std::make_unique<PersistLocation>(this, "Tool/Style Manager");

	// Populate lists
	LoadCatalog();
	LoadCurrentStyles(AssFile::COMMIT_STYLES | AssFile::COMMIT_DIAG_META);

	//Set key handlers for lists
	StorageList->Bind(wxEVT_KEY_DOWN, &DialogStyleManager::OnKeyDown, this);
	CurrentList->Bind(wxEVT_KEY_DOWN, &DialogStyleManager::OnKeyDown, this);
	StorageList->on_menu = [this](wxPoint pos) { ShowMenu(true, pos); };
	CurrentList->on_menu = [this](wxPoint pos) { ShowMenu(false, pos); };

	auto on = [](wxWindow *button, std::function<void()> action) {
		button->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
	};
	on(StorageMoveUp, [this] { MoveStyles(true, 0); });
	on(StorageMoveTop, [this] { MoveStyles(true, 1); });
	on(StorageMoveDown, [this] { MoveStyles(true, 2); });
	on(StorageMoveBottom, [this] { MoveStyles(true, 3); });
	on(StorageSort, [this] { MoveStyles(true, 4); });

	on(CurrentMoveUp, [this] { MoveStyles(false, 0); });
	on(CurrentMoveTop, [this] { MoveStyles(false, 1); });
	on(CurrentMoveDown, [this] { MoveStyles(false, 2); });
	on(CurrentMoveBottom, [this] { MoveStyles(false, 3); });
	on(CurrentSort, [this] { MoveStyles(false, 4); });

	on(CatalogNew, [this] { OnCatalogNew(); });
	on(CatalogDelete, [this] { OnCatalogDelete(); });

	on(StorageNew, [this] { OnStorageNew(); });
	on(StorageEdit, [this] { OnStorageEdit(); });
	on(StorageCopy, [this] { OnStorageCopy(); });
	on(StorageDelete, [this] { OnStorageDelete(); });

	on(CurrentNew, [this] { OnCurrentNew(); });
	on(CurrentEdit, [this] { OnCurrentEdit(); });
	on(CurrentCopy, [this] { OnCurrentCopy(); });
	on(CurrentDelete, [this] { OnCurrentDelete(); });

	on(CurrentImport, [this] { OnCurrentImport(); });

	on(MoveToLocal, [this] { OnCopyToCurrent(); });
	on(MoveToStorage, [this] { OnCopyToStorage(); });

	CatalogList->on_choose = [this](int) { OnChangeCatalog(); };

	StorageList->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { UpdateButtons(); });
	StorageList->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { OnStorageEdit(); });

	CurrentList->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { UpdateButtons(); });
	CurrentList->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { OnCurrentEdit(); });

	CurrentList->SetFocus();
}

void DialogStyleManager::LoadCurrentStyles(int commit_type) {
	if (commit_type & AssFile::COMMIT_STYLES || commit_type == AssFile::COMMIT_NEW) {
		styleMap.clear();
		std::vector<StyleList::Row> rows;
		for (auto const& style : c->subtitleAccess->Read().Styles) {
			rows.push_back({style});
			styleMap.push_back(&style);
		}
		CurrentList->SetRows(std::move(rows));
	}

	if (commit_type & AssFile::COMMIT_DIAG_META) {
		AssDialogue const* dia = c->selectionController->ReadActiveLine();
		CurrentList->DeselectAll();
		if (dia && commit_type != AssFile::COMMIT_NEW)
			CurrentList->SetStringSelection(to_wx(dia->Style));
		else
			CurrentList->SetSelection(0);
	}

	UpdateNotes();
	UpdateButtons();
}

void DialogStyleManager::OnActiveLineChanged() {
    auto new_line = c->selectionController->ReadActiveLine();
	if (new_line) {
		CurrentList->DeselectAll();
		CurrentList->SetStringSelection(to_wx(new_line->Style));
		UpdateButtons();
	}
}

void DialogStyleManager::UpdateStorage() {
	Store.Save();

	std::vector<StyleList::Row> rows;
	for (auto const& style : Store)
		rows.push_back({*style});
	StorageList->SetRows(std::move(rows));

	UpdateNotes();
	UpdateButtons();
}

void DialogStyleManager::OnChangeCatalog() {
	std::string catalog(from_wx(CatalogList->GetStringSelection()));
	if (catalog.empty()) return;
	try {
		c->subtitleAccess->SetMetadata(SubtitleProjectAccess::MetadataField::StyleStorage, catalog);
	}
	catch (SubtitleProjectBusy const&) {
		// C1-C1c-12: the catalog is still shown; only remembering it in the
		// subtitles is skipped during a comparison.
	}
	Store.LoadCatalog(catalog);
	UpdateStorage();
}

void DialogStyleManager::LoadCatalog() {
	catalogs.clear();

	// Get saved style catalogs
	for (auto const& name : AssStyleStorage::GetCatalogs())
		catalogs.push_back(to_wx(name));

	// Create a default storage if there are none
	if (catalogs.empty()) {
		Store.LoadCatalog("Default");
		Store.push_back(std::make_unique<AssStyle>());
		Store.Save();
		catalogs.push_back("Default");
	}

	// Set to default if available
	std::string pickStyle = c->subtitleAccess->Read().Properties.style_storage;
	if (pickStyle.empty())
		pickStyle = "Default";

	wxString pick = catalogs.front();
	for (auto const& name : catalogs)
		if (name.CmpNoCase(to_wx(pickStyle)) == 0) pick = name;
	ShowCatalogs(pick);

	OnChangeCatalog();
}

void DialogStyleManager::ShowCatalogs(wxString const& name) {
	CatalogList->Set(catalogs);
	CatalogList->SetStringSelection(name);
}

void DialogStyleManager::OnCatalogNew() {
	wxString name = wxGetTextFromUser(_("New storage name:"), _("New catalog entry"), "", this);
	if (!name) return;

	// Remove bad characters from the name
	wxString badchars = wxFileName::GetForbiddenChars(wxPATH_DOS);
	int badchars_removed = 0;
	for (wxUniCharRef chr : name) {
		if (badchars.find(chr) != badchars.npos) {
			chr = '_';
			++badchars_removed;
		}
	}

	// Make sure that there is no storage with the same name (case insensitive search since Windows filenames are case insensitive)
	if (std::any_of(catalogs.begin(), catalogs.end(), [&](wxString const& other) { return other.CmpNoCase(name) == 0; })) {
		wxMessageBox(_("A catalog with that name already exists."), _("Catalog name conflict"), wxOK | wxICON_ERROR | wxCENTER);
		return;
	}

	// Warn about bad characters
	if (badchars_removed) {
		wxMessageBox(
			fmt_tl("The specified catalog name contains one or more illegal characters. They have been replaced with underscores instead.\nThe catalog has been renamed to \"%s\".", name),
			_("Invalid characters"));
	}

	// Add to list of storages
	catalogs.push_back(name);
	ShowCatalogs(name);
	OnChangeCatalog();
}

void DialogStyleManager::OnCatalogDelete() {
	if (catalogs.size() <= 1) return;

	wxString name = CatalogList->GetStringSelection();
	wxString message = fmt_tl("Are you sure you want to delete the storage \"%s\" from the catalog?", name);
	int option = wxMessageBox(message, _("Confirm delete"), wxYES_NO | wxICON_EXCLAMATION , this);
	if (option == wxYES) {
		agi::fs::Remove(config::path->Decode("?user/catalog/" + from_wx(name) + ".sty"));
		catalogs.erase(catalogs.begin() + CatalogList->GetSelection());
		ShowCatalogs(catalogs.front());
		OnChangeCatalog();
	}
}

void DialogStyleManager::OnCopyToStorage() {
	wxArrayInt selections;
	int n = CurrentList->GetSelections(selections);
	wxArrayString copied;
	copied.reserve(n);
	for (int i = 0; i < n; i++) {
		wxString styleName = CurrentList->GetString(selections[i]);

		if (AssStyle *style = Store.GetStyle(from_wx(styleName))) {
			if (wxYES == wxMessageBox(fmt_tl("There is already a style with the name \"%s\" in the current storage. Overwrite?", styleName), _("Style name collision"), wxYES_NO)) {
				*style = *styleMap.at(selections[i]);
				copied.push_back(styleName);
			}
		}
		else {
			Store.push_back(std::make_unique<AssStyle>(*styleMap.at(selections[i])));
			copied.push_back(styleName);
		}
	}

	UpdateStorage();
	for (auto const& style_name : copied)
		StorageList->SetStringSelection(style_name, true);

	UpdateButtons();
}

void DialogStyleManager::OnCopyToCurrent() {
	// C1-C1c-13 (audit B6): refuse before any question, then ask about name
	// collisions without holding the write lease. The live styles are staged
	// only afterwards; a collision that appears after the questions is never
	// overwritten without confirmation.
	if (!c->subtitleAccess->TryWrite()) return;
	wxArrayInt selections;
	int n = StorageList->GetSelections(selections);
	std::set<std::string> overwrite;
	for (int i = 0; i < n; i++) {
		wxString styleName = StorageList->GetString(selections[i]);
		if (c->subtitleAccess->Read().GetStyle(from_wx(styleName)) &&
			wxYES == wxMessageBox(fmt_tl("There is already a style with the name \"%s\" in the current script. Overwrite?", styleName), _("Style name collision"), wxYES_NO))
			overwrite.insert(from_wx(styleName));
	}

    auto edit_scope = c->subtitleAccess->TryWrite();
    if (!edit_scope) return;
    SubtitleValueEdit staged(c);
	wxArrayString copied;
	copied.reserve(n);
	for (int i = 0; i < n; i++) {
		wxString styleName = StorageList->GetString(selections[i]);

		if (AssStyle *style = staged.file.GetStyle(from_wx(styleName))) {
			if (overwrite.count(from_wx(styleName))) {
				*style = *Store[selections[i]];
				copied.push_back(styleName);
			}
		}
		else {
			staged.file.Styles.push_back(*new AssStyle(*Store[selections[i]]));
			copied.push_back(styleName);
		}
	}

	staged.Commit(_("style copy"), AssFile::COMMIT_STYLES);

	CurrentList->DeselectAll();
	for (auto const& style_name : copied)
		CurrentList->SetStringSelection(style_name, true);
	UpdateButtons();
}

template<class T>
void DialogStyleManager::CopyToClipboard(StyleList *list, T const& v) {
	wxArrayInt selections;
	list->GetSelections(selections);

	std::string data;
	for(size_t i = 0; i < selections.size(); ++i) {
		if (i) data += "\r\n";
        // Serialization must not mutate a live style during a read-only copy.
        AssStyle serialized(*v[selections[i]]);
        serialized.UpdateData();
        data += serialized.GetEntryData();
	}

	SetClipboard(data);
}

void DialogStyleManager::PasteToCurrent() {
    auto edit_scope = c->subtitleAccess->TryWrite();
    if (!edit_scope) return;
    SubtitleValueEdit staged(c);
	add_styles(
		[&](std::string const& str) { return staged.file.GetStyle(str); },
		[&](AssStyle *s) { staged.file.Styles.push_back(*s); });

	staged.Commit(_("style paste"), AssFile::COMMIT_STYLES);
}

void DialogStyleManager::PasteToStorage() {
	add_styles(
		[this](std::string const& str) { return Store.GetStyle(str); },
		[this](AssStyle *s) { Store.push_back(std::unique_ptr<AssStyle>(s)); });

	UpdateStorage();
	StorageList->SetStringSelection(to_wx(Store.back()->name));
	UpdateButtons();
}

void DialogStyleManager::ShowStorageEditor(AssStyle *style, std::string const& new_name) {
	DialogStyleEditor editor(this, style, c, &Store, new_name, font_list.get());
	if (editor.ShowModal()) {
		UpdateStorage();
		StorageList->SetStringSelection(to_wx(editor.GetStyleName()));
		UpdateButtons();
	}
}

void DialogStyleManager::OnStorageNew() {
	ShowStorageEditor(nullptr);
}

void DialogStyleManager::OnStorageEdit() {
	int sel = get_single_sel(StorageList);
	if (sel == -1) return;
	ShowStorageEditor(Store[sel]);
}

void DialogStyleManager::OnStorageCopy() {
	int sel = get_single_sel(StorageList);
	if (sel == -1) return;

	ShowStorageEditor(Store[sel], unique_name(
		[this](std::string const& str) { return Store.GetStyle(str); }, Store[sel]->name));
}

void DialogStyleManager::OnStorageDelete() {
	wxArrayInt selections;
	int n = StorageList->GetSelections(selections);

	if (confirm_delete(n, this, _("Confirm delete from storage")) == wxYES) {
		for (int i = 0; i < n; i++)
			Store.Delete(selections[i] - i);
		UpdateStorage();
	}
}

void DialogStyleManager::ShowCurrentEditor(AssStyle const* style, std::string const& new_name) {
	DialogStyleEditor editor(this, style, c, nullptr, new_name, font_list.get());
	if (editor.ShowModal()) {
		CurrentList->DeselectAll();
		CurrentList->SetStringSelection(to_wx(editor.GetStyleName()));
		UpdateButtons();
	}
}

void DialogStyleManager::OnCurrentNew() {
	ShowCurrentEditor(nullptr);
}

void DialogStyleManager::OnCurrentEdit() {
	int sel = get_single_sel(CurrentList);
	if (sel == -1) return;
	ShowCurrentEditor(styleMap[sel]);
}

void DialogStyleManager::OnCurrentCopy() {
	int sel = get_single_sel(CurrentList);
	if (sel == -1) return;

	ShowCurrentEditor(styleMap[sel], unique_name(
		[this](std::string const& str) { return c->subtitleAccess->Read().GetStyle(str); },
		styleMap[sel]->name));
}

void DialogStyleManager::OnCurrentDelete() {
	// C1-C1c-13 (audit B6): confirm without holding the write lease. The list
	// selection and its style cache are read again afterwards, because any
	// commit during the question refreshes them.
	if (!c->subtitleAccess->TryWrite()) return;
	wxArrayInt selections;
	int n = CurrentList->GetSelections(selections);
	if (confirm_delete(n, this, _("Confirm delete from current")) != wxYES) return;

    auto edit_scope = c->subtitleAccess->TryWrite();
    if (!edit_scope) return;
	if (CurrentList->GetSelections(selections) != n || n == 0) return;
    SubtitleValueEdit staged(c);
	for (int i = 0; i < n; i++) {
		delete staged.Source(styleMap.at(selections[i]));
	}
	staged.Commit(_("style delete"), AssFile::COMMIT_STYLES);
}

void DialogStyleManager::OnCurrentImport() {
	// C1-C1c-13 (audit B6): the file, choice and collision dialogs all run
	// without the write lease; the live styles are staged after the last one.
	if (!c->subtitleAccess->TryWrite()) return;
	auto filename = OpenFileSelector(c, _("Open Subtitles File"), "Path/Last/Subtitles", "", "", SubtitleFormat::GetWildcards(0), this);
	if (filename.empty()) return;

	std::string charset;
	try {
		charset = CharSetDetect::GetEncoding(filename);
	}
	catch (agi::UserCancelException const&) {
		return;
	}

	AssFile temp;
	try {
		auto reader = SubtitleFormat::GetReader(filename, charset.c_str());
		if (!reader) {
			wxMessageBox(_("Unsupported subtitle format"), _("Error"), wxOK | wxICON_ERROR | wxCENTER, this);
			return;
		}
		reader->ReadFile(&temp, filename, 0, charset.c_str());
	}
	catch (agi::Exception const& err) {
		wxMessageBox(to_wx(err.GetMessage()), _("Error"), wxOK | wxICON_ERROR | wxCENTER, this);
	}
	catch (...) {
		wxMessageBox(_("Unknown error"), _("Error"), wxOK | wxICON_ERROR | wxCENTER, this);
		return;
	}

	// Get styles
	auto styles = temp.GetStyles();
	if (styles.empty()) {
		wxMessageBox(_("The selected file has no available styles."), _("Error Importing Styles"));
		return;
	}

	// Get selection
	wxArrayInt selections;
	int res = GetSelectedChoices(this, selections, _("Choose styles to import:"), _("Import Styles"), to_wx(styles));
	if (res == -1 || selections.empty()) return;

	std::set<std::string> overwrite;
	for (auto const& sel : selections) {
		if (c->subtitleAccess->Read().GetStyle(styles[sel]) && wxYES == wxMessageBox(
				fmt_tl("There is already a style with the name \"%s\" in the current script. Overwrite?", styles[sel]),
				_("Style name collision"),
				wxYES_NO))
			overwrite.insert(styles[sel]);
	}

    auto edit_scope = c->subtitleAccess->TryWrite();
    if (!edit_scope) return;
    SubtitleValueEdit staged(c);
	bool modified = false;

	// Loop through selection
	for (auto const& sel : selections) {
		// Check if there is already a style with that name
		if (AssStyle *existing = staged.file.GetStyle(styles[sel])) {
			if (overwrite.count(styles[sel])) {
				modified = true;
				*existing = *temp.GetStyle(styles[sel]);
			}
			continue;
		}

		// Copy
		modified = true;
		staged.file.Styles.push_back(*new AssStyle(*temp.GetStyle(styles[sel])));
	}

	// Update
	if (modified)
		staged.Commit(_("style import"), AssFile::COMMIT_STYLES);
}

void DialogStyleManager::UpdateNotes() {
	auto const& file = c->subtitleAccess->Read();
	// Style names are matched without regard to case, as GetStyle does
	auto key = [](std::string name) {
		for (auto &ch : name) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
		return name;
	};
	std::unordered_map<std::string, int> used;
	for (auto const& line : file.Events)
		++used[key(line.Style.get())];
	for (size_t i = 0; i < styleMap.size(); ++i) {
		auto found = used.find(key(styleMap[i]->name));
		int lines = found == used.end() ? 0 : found->second;
		CurrentList->SetNote(i, lines ? fmt_plural(lines, "%d line", "%d lines", lines) : _("unused"), StyleList::Tone::Dim);
	}

	auto entry = [](AssStyle const& style) {
		AssStyle copy(style);
		copy.UpdateData();
		return copy.GetEntryData();
	};
	for (size_t i = 0; i < Store.size(); ++i) {
		AssStyle const* script = file.GetStyle(Store[i]->name);
		if (!script)
			StorageList->SetNote(i, wxString(), StyleList::Tone::Dim);
		else if (entry(*script) == entry(*Store[i]))
			StorageList->SetNote(i, _("in script"), StyleList::Tone::Accent);
		else
			StorageList->SetNote(i, _("differs from script"), StyleList::Tone::Warn);
	}
}

void DialogStyleManager::ShowMenu(bool storage, wxPoint pos) {
	StyleList *list = storage ? StorageList : CurrentList;
	wxArrayInt sels;
	int n = list->GetSelections(sels);
	enum { New = 1, Edit, Duplicate, Delete, Transfer, CopyText, PasteText };
	wxMenu menu;
	menu.Append(Edit, _("&Edit"))->Enable(n == 1);
	menu.Append(Duplicate, _("D&uplicate..."))->Enable(n == 1);
	menu.Append(Delete, _("&Delete"))->Enable(n > 0);
	menu.AppendSeparator();
	menu.Append(Transfer, storage ? _("Copy to &current script ->") : _("<- Copy to &storage"))->Enable(n > 0);
	menu.AppendSeparator();
	menu.Append(CopyText, _("Copy as te&xt\tCtrl+C"))->Enable(n > 0);
	menu.Append(PasteText, _("&Paste styles\tCtrl+V"));
	menu.AppendSeparator();
	menu.Append(New, _("&New"));
	switch (list->GetPopupMenuSelectionFromUser(menu, pos)) {
		case New: storage ? OnStorageNew() : OnCurrentNew(); break;
		case Edit: storage ? OnStorageEdit() : OnCurrentEdit(); break;
		case Duplicate: storage ? OnStorageCopy() : OnCurrentCopy(); break;
		case Delete: storage ? OnStorageDelete() : OnCurrentDelete(); break;
		case Transfer: storage ? OnCopyToCurrent() : OnCopyToStorage(); break;
		case CopyText:
			if (storage) CopyToClipboard(StorageList, Store);
			else CopyToClipboard(CurrentList, styleMap);
			break;
		case PasteText: storage ? PasteToStorage() : PasteToCurrent(); break;
		default: break;
	}
}

void DialogStyleManager::UpdateButtons() {
	CatalogDelete->Enable(catalogs.size() > 1);
	StorageCard->SetCount(fmt_plural(StorageList->GetCount(), "%d style", "%d styles", StorageList->GetCount()));
	CurrentCard->SetCount(fmt_plural(CurrentList->GetCount(), "%d style", "%d styles", CurrentList->GetCount()));

	// Get storage selection
	wxArrayInt sels;
	int n = StorageList->GetSelections(sels);

	StorageEdit->Enable(n == 1);
	StorageCopy->Enable(n == 1);
	StorageDelete->Enable(n > 0);
	MoveToLocal->Enable(n > 0);

	int firstStor = -1;
	int lastStor = -1;
	if (n) {
		firstStor = sels[0];
		lastStor = sels[n-1];
	}

	// Check if selection is continuous
	bool contStor = true;
	for (int i = 1; i < n; ++i) {
		if (sels[i] != sels[i-1]+1) {
			contStor = false;
			break;
		}
	}

	int itemsStor = StorageList->GetCount();
	StorageMoveUp->Enable(contStor && firstStor > 0);
	StorageMoveTop->Enable(contStor && firstStor > 0);
	StorageMoveDown->Enable(contStor && lastStor != -1 && lastStor < itemsStor-1);
	StorageMoveBottom->Enable(contStor && lastStor != -1 && lastStor < itemsStor-1);
	StorageSort->Enable(itemsStor > 1);

	// Get current selection
	n = CurrentList->GetSelections(sels);

	CurrentEdit->Enable(n == 1);
	CurrentCopy->Enable(n == 1);
	CurrentDelete->Enable(n > 0);
	MoveToStorage->Enable(n > 0);

	int firstCurr = -1;
	int lastCurr = -1;
	if (n) {
		firstCurr = sels[0];
		lastCurr = sels[n-1];
	}

	// Check if selection is continuous
	bool contCurr = true;
	for (int i = 1; i < n; ++i) {
		if (sels[i] != sels[i-1]+1) {
			contCurr = false;
			break;
		}
	}

	int itemsCurr = CurrentList->GetCount();
	CurrentMoveUp->Enable(contCurr && firstCurr > 0);
	CurrentMoveTop->Enable(contCurr && firstCurr > 0);
	CurrentMoveDown->Enable(contCurr && lastCurr != -1 && lastCurr < itemsCurr-1);
	CurrentMoveBottom->Enable(contCurr && lastCurr != -1 && lastCurr < itemsCurr-1);
	CurrentSort->Enable(itemsCurr > 1);
}

struct cmp_name {
	template<typename T>
	bool operator()(T const& lft, T const& rgt) const { return lft->name < rgt->name; }
};

template<class Cont>
static void do_move(Cont& styls, int type, int& first, int& last, bool storage) {
	auto begin = styls.begin();

	// Move up
	if (type == 0) {
		if (first == 0) return;
		rotate(begin + first - 1, begin + first, begin + last + 1);
		first--;
		last--;
	}
	// Move to top
	else if (type == 1) {
		rotate(begin, begin + first, begin + last + 1);
		last = last - first;
		first = 0;
	}
	// Move down
	else if (type == 2) {
		if (last + 1 == (int)styls.size()) return;
		rotate(begin + first, begin + last + 1, begin + last + 2);
		first++;
		last++;
	}
	// Move to bottom
	else if (type == 3) {
		rotate(begin + first, begin + last + 1, styls.end());
		first = styls.size() - (last - first + 1);
		last = styls.size() - 1;
	}
	// Sort
	else if (type == 4) {
		// Get confirmation
		if (storage) {
			int res = wxMessageBox(_("Are you sure? This cannot be undone!"), _("Sort styles"), wxYES_NO | wxCENTER);
			if (res == wxNO) return;
		}

		sort(styls.begin(), styls.end(), cmp_name());

		first = 0;
		last = 0;
	}
}

void DialogStyleManager::MoveStyles(bool storage, int type) {
    auto edit_scope = storage ? std::optional<SubtitleProjectAccess::WriteScope>{} : c->subtitleAccess->TryWrite();
    if (!storage && !edit_scope) return;
	StyleList *list = storage ? StorageList : CurrentList;

	// Get selection
	wxArrayInt sels;
	int n = list->GetSelections(sels);
	if (n == 0 && type != 4) return;

	int first = 0, last = 0;
	if (n) {
		first = sels.front();
		last = sels.back();
	}

	if (storage) {
		do_move(Store, type, first, last, true);
		UpdateStorage();
	}
	else {
        // Mutable node identities exist only inside this write scope. Build
        // once so a read-only UI cache does not turn reorder into O(n^2).
        SubtitleValueEdit staged(c);
        std::unordered_map<AssStyle const*, AssStyle*> live;
        live.reserve(styleMap.size());
        for (auto style:styleMap)live.emplace(style,staged.Source(style));
		do_move(styleMap, type, first, last, false);

		// Replace styles
		size_t curn = 0;
		for (auto it = staged.file.Styles.begin(); it != staged.file.Styles.end(); ++it) {
            auto found = live.find(styleMap[curn]);
            if (found == live.end()) throw std::logic_error("Style list changed during reorder");
            auto new_style_at_pos = staged.file.Styles.iterator_to(*found->second);
			EntryList<AssStyle>::node_algorithms::swap_nodes(it.pointed_node(), new_style_at_pos.pointed_node());
			if (++curn == styleMap.size()) break;
			it = new_style_at_pos;
		}

		staged.Commit(_("style move"), AssFile::COMMIT_STYLES);
	}

	for (int i = 0 ; i < (int)list->GetCount(); ++i) {
		if (i < first || i > last)
			list->Deselect(i);
		else
			list->Select(i);
	}

	UpdateButtons();
}

void DialogStyleManager::OnKeyDown(wxKeyEvent &event) {
	wxWindow *focus = wxWindow::FindFocus();

	switch(event.GetKeyCode()) {
		case WXK_DELETE :
			if (focus == StorageList)
				OnStorageDelete();
			else if (focus == CurrentList)
				OnCurrentDelete();
			break;

		case 'C' :
		case 'c' :
			if (event.CmdDown()) {
				if (focus == StorageList)
					CopyToClipboard(StorageList, Store);
				else if (focus == CurrentList)
					CopyToClipboard(CurrentList, styleMap);
			}
			break;

		case 'V' :
		case 'v' :
			if (event.CmdDown()) {
				if (focus == StorageList)
					PasteToStorage();
				else if (focus == CurrentList)
					PasteToCurrent();
			}
			break;
		default:
			event.Skip();
			break;
	}
}
}

void ShowStyleManagerDialog(agi::Context *c) {
	c->dialog->Show<DialogStyleManager>(c);
}

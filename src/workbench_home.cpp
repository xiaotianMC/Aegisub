// SPDX-License-Identifier: ISC
#include "workbench_home.h"
#include "workbench_activity.h"
#include "workbench_metadata.h"
#include "workbench_series.h"
#include "workbench_store.h"
#include "workbench_trash.h"
#include "command/command.h"
#include "compat.h"
#include "frame_main.h"
#include "include/aegisub/context.h"
#include "main.h"
#include "options.h"

#include <libaegisub/exception.h>
#include <libaegisub/path.h>
#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/choicdlg.h>
#include <wx/dirdlg.h>
#include <wx/datetime.h>
#include <wx/dcclient.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/listbox.h>
#include <wx/log.h>
#include <wx/mstream.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/scrolwin.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/textctrl.h>
#include <wx/wrapsizer.h>
#include <wx/weakref.h>
#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>

namespace {
using namespace workbench;
constexpr int page_size = 50;

class WorkbenchHome final : public wxPanel {
	agi::Context *c;
	wxPanel *landing;
	std::unique_ptr<Index> index;
	std::optional<Manifest> current;
	std::optional<Location> parent_series;
	std::string child_id;
	std::vector<Location> locations;
	wxListBox *list;
	wxTextCtrl *search;
	wxStaticText *title, *unfinished;
	wxButton *resume, *dashboard, *remove_registration, *trash_folder, *delete_folder, *confirm_removal;
	std::optional<Location> selected_location;
	std::array<wxButton*, 3> cards{};
	std::vector<Location> recent;
	LibraryView view = LibraryView::All;
	bool home_view = true;
	wxStaticText *details, *status;
	wxStaticBitmap *cover;
	wxButton *previous, *next, *rename, *choose_cover, *edit_metadata, *project_files, *registration_tasks;
	wxButton *episodes, *back;
	int offset = 0;

	Path TemplateDirectory() const {
		auto setting = OPT_GET("Path/Workbench Templates")->GetString();
		auto directory = setting.empty() ? Roots::Resolve(*c->path).templates : c->path->Decode(setting);
		if (!directory.is_absolute()) throw std::runtime_error("Invalid template library path");
		return directory;
	}
	Index& Database() {
		if (!index) {
			auto opened = std::make_unique<Index>(Roots::Resolve(*c->path).database);
			opened->RecoverInterrupted();
			index = std::move(opened);
		}
		return *index;
	}
	void TaskCount() {
		auto count = Database().UnfinishedRegistrations();
		registration_tasks->SetLabel(count ? wxString::Format(_("Registration Tasks (%lld unfinished)"), static_cast<long long>(count))
		                                  : _("Registration Tasks"));
		wxString pending;
		if (home_view) {
			for (auto const& task : Database().Tasks(TaskView::Unfinished, 0, 3)) {
				if (pending.empty()) pending = _("Unfinished Tasks");
				pending += "\n" + to_wx(task.name);
				if (!task.project_name.empty()) pending += "  —  " + to_wx(task.project_name);
			}
		}
		unfinished->SetLabelText(pending);
		unfinished->Show(!pending.empty());
		landing->Layout();
		Layout();
	}
	void ShowActivity(WorkbenchActivityPage page, bool project_tasks = false) {
		Database();
		bool scoped = project_tasks || page == WorkbenchActivityPage::Dashboard || page == WorkbenchActivityPage::Files;
		auto project = scoped && current && current->kind == Kind::Project && project_files->IsEnabled() ? current : std::nullopt;
		if (scoped && !project) return;
		ShowWorkbenchActivity(this, Roots::Resolve(*c->path).database, c, project, page, [home=wxWeakRef<WorkbenchHome>(this)] {
			if (home && !home->IsBeingDeleted()) home->Run([&] {
				home->TaskCount();
				if (!home->current || !home->project_files->IsEnabled()) return;
				auto latest = workbench::Open(home->current->root);
				if (latest.id != home->current->id) throw std::runtime_error("The folder now contains a different project.");
				if (latest.bytes != home->current->bytes) home->Select(std::move(latest), "");
			});
		});
	}
	void SetStatus(wxString const& message) {
		status->SetLabelText(message);
		status->Wrap(std::max(240, GetClientSize().x - FromDIP(64)));
		landing->Layout();
		Layout();
	}
	template<class Action> void Run(Action action) {
		status->SetLabelText({});
		try { action(); }
		catch (agi::Exception const& e) { SetStatus(to_wx(e.GetMessage())); }
		catch (std::exception const& e) { SetStatus(to_wx(std::string(e.what()))); }
	}
	void RefreshFolderRemovals() {
		if (confirm_removal->Show(!Database().UncertainFolderRemovals().empty())) landing->Layout();
	}
	void RefreshList() {
		RefreshFolderRemovals();
		auto page = Database().List(offset, page_size + 1, view, from_wx(search->GetValue()));
		bool more = page.size() > page_size;
		if (more) page.resize(page_size);
		locations = std::move(page);
		list->Clear();
		for (auto const& row : locations) {
			auto item = list->Append(to_wx(row.name) + "  —  " + to_wx(row.root));
			if (current && current->id == row.id && current->root == row.root) list->SetSelection(item);
		}
		previous->Enable(offset > 0);
		next->Enable(more);
		recent = Database().List(0, cards.size(), LibraryView::Recent);
		resume->Enable(!recent.empty());
		bool show_cards = home_view && search->IsEmpty();
		for (size_t i = 0; i < cards.size(); ++i) {
			auto card = cards[i];
			card->Show(show_cards && i < recent.size());
			if (!card->IsShown()) continue;
			auto const& item = recent[i];
			auto path = to_wx(item.root);
			auto stamp = wxDateTime(static_cast<time_t>(item.updated_at)).Format("%Y-%m-%d %H:%M");
			wxClientDC dc(card);
			auto short_text = [&](wxString const& text) { return wxControl::Ellipsize(text, dc, wxELLIPSIZE_MIDDLE, FromDIP(175)); };
			card->SetLabelText(short_text(to_wx(item.name)) + "\n" + WorkbenchStatusLabel(item.status) + "\n" + stamp + "\n" + short_text(path));
			card->SetToolTip(to_wx(item.name) + "\n" + path);
			Manifest cached{}; cached.root = item.root; cached.cover = item.cover;
			card->SetBitmap(CoverBitmap(cached, FromDIP(wxSize(40, 32))));
		}
		TaskCount();
	}
	void OpenLocation(Location location, bool restore) {
		current.reset(); details->SetLabelText(to_wx(location.root)); EnableSelectionActions(false); ShowCover();
		selected_location = location; remove_registration->Enable();
		auto m = workbench::Open(location.root);
		if (m.id != location.id || m.root != location.root) throw std::runtime_error("The folder now contains a different project.");
		Select(std::move(m));
		if (restore && location.page == "series") ShowEpisodes();
		if (restore && location.page == "dashboard") ShowActivity(WorkbenchActivityPage::Dashboard);
		if (restore && location.page == "files") ShowActivity(WorkbenchActivityPage::Files);
		if (restore && location.page == "tasks") ShowActivity(WorkbenchActivityPage::Tasks, true);
		if (restore && location.page == "downloads") ShowActivity(WorkbenchActivityPage::Downloads, true);
	}
	void Browse(LibraryView filter, bool home, wxString const& heading) {
		view = filter; home_view = home; offset = 0; search->ChangeValue({});
		title->SetLabelText(heading); RefreshList();
	}
	wxBitmap CoverBitmap(Manifest const& manifest, wxSize size) {
		wxImage image;
		try {
			auto file = CoverFile(manifest);
			if (!file.empty()) {
				wxLogNull quiet;
				image.SetOption(wxIMAGE_OPTION_MAX_WIDTH, size.x);
				image.SetOption(wxIMAGE_OPTION_MAX_HEIGHT, size.y);
				image.LoadFile(to_wx(file));
			}
		}
		catch (std::filesystem::filesystem_error const&) { /* An offline cover uses the folder icon. */ }
		return image.IsOk() ? wxBitmap(image) : wxArtProvider::GetBitmap(wxART_FOLDER, wxART_OTHER, size);
	}
	void ShowCover() {
		cover->Show(current.has_value());
		if (!current) return;
		cover->SetBitmap(CoverBitmap(*current, FromDIP(wxSize(80, 56))));
	}
	void ChooseCover() {
		if (!current) return;
		auto expected = *current;
		auto filter = wxImage::FindHandler(wxBITMAP_TYPE_JPEG)
		            ? _("PNG and JPEG images") + "|*.png;*.jpg;*.jpeg;*.PNG;*.JPG;*.JPEG" : _("PNG images") + "|*.png;*.PNG";
		wxFileDialog picker(this, _("Choose a cover image"), {}, {}, filter, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (picker.ShowModal() != wxID_OK) return;
		auto updated = SetCover(expected, from_wx(picker.GetPath()), [](std::string const& bytes) {
			wxLogNull quiet;
			wxMemoryInputStream stream(bytes.data(), bytes.size());
			wxImage image;
			image.SetOption(wxIMAGE_OPTION_MAX_WIDTH, 128);
			image.SetOption(wxIMAGE_OPTION_MAX_HEIGHT, 128);
			return image.LoadFile(stream) && image.IsOk();
		});
		Select(std::move(updated));
	}
	void EnableSelectionActions(bool enabled) {
		rename->Enable(enabled);
		choose_cover->Enable(enabled);
		edit_metadata->Enable(enabled);
		project_files->Enable(enabled && current && current->kind == Kind::Project);
		dashboard->Enable(project_files->IsEnabled());
		trash_folder->Enable(project_files->IsEnabled());
		delete_folder->Enable(project_files->IsEnabled());
		episodes->Show(current && current->kind == Kind::Series);
		episodes->Enable(current && current->kind == Kind::Series);
		back->Show(parent_series && current && child_id == current->id);
	}
	void ShowEpisodes() {
		if (!current || current->kind != Kind::Series) return;
		if (edit_metadata->IsEnabled()) Database().Visit(*current, "series");
		auto page = CreateWorkbenchSeries(this, *current, Roots::Resolve(*c->path).database, edit_metadata->IsEnabled(),
			[this] { return LoadTemplate(TemplateDirectory(), Kind::Project).folders; },
			[this](Manifest const& s, Manifest child) {
				parent_series = Location{s.id, s.name, s.root};
				child_id = child.id;
				Run([&] { Select(std::move(child)); });
			},
			[this](Manifest const& series) {
				if (parent_series && parent_series->id == series.id && parent_series->root == series.root &&
				    std::none_of(series.episodes.begin(), series.episodes.end(), [this](Episode const& ep) { return ep.project_id == child_id; }))
					parent_series.reset();
				if (current && current->id == series.id && current->root == series.root && current->bytes != series.bytes)
					Select(series, "series");
			}, [this](wxWindow* page) {
				GetSizer()->Detach(page);
				page->Hide(); landing->Show(); Layout();
				(back->IsShown() ? back : episodes)->SetFocus();
			});
		landing->Hide();
		GetSizer()->Add(page, wxSizerFlags(1).Expand());
		Layout(); page->SetFocus();
	}
	void Select(Manifest manifest, std::string const& page = "home") {
		selected_location.reset(); remove_registration->Disable();
		current = std::move(manifest);
		details->SetLabelText(to_wx(current->name) + "  —  " + WorkbenchStatusLabel(current->metadata.status) + "\n" + to_wx(current->root)
		    + "\n" + _("Last updated") + "  " + wxDateTime(static_cast<time_t>(current->updated_at)).FormatISOCombined(' '));
		EnableSelectionActions(false);
		ShowCover();
		landing->Layout();
		Layout();
		// A registration error must leave the real manifest visible.
		Database().Visit(*current, page);
		selected_location = Database().RegisteredLocation(current->id); remove_registration->Enable();
		EnableSelectionActions(true);
		RefreshList();
	}
	void CreateNew(Kind kind) {
		auto folders = LoadTemplate(TemplateDirectory(), kind).folders;
		wxDirDialog parent(this, _("Choose the parent directory"), {}, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
		if (parent.ShowModal() != wxID_OK) return;
		wxTextEntryDialog name(this, _("Name of the new folder"), kind == Kind::Project ? _("New Project") : _("New Series"));
		if (name.ShowModal() != wxID_OK) return;
		Database();
		Select(workbench::Create(from_wx(parent.GetPath()), from_wx(name.GetValue()), kind, folders));
	}
	void RegisterExisting() {
		wxDirDialog picker(this, _("Register Existing Folder"), {}, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
		if (picker.ShowModal() != wxID_OK) return;
		Path root(from_wx(picker.GetPath()));
		if (std::filesystem::exists(root / ".aeg")) {
			auto m = Open(root);
			Database().RegisterExisting(m);
			Select(std::move(m)); return;
		}
		wxTextEntryDialog confirm(this, _("Add project metadata to this folder. Existing files stay in place."),
		                          _("Project display name"), to_wx(root.filename()));
		if (confirm.ShowModal() != wxID_OK) return;
		Database();
		Select(RegisterFolder(root, from_wx(confirm.GetValue()), Kind::Project, LoadTemplate(TemplateDirectory(), Kind::Project).folders));
	}
	void RemoveRegistration() {
		if (!selected_location) return;
		auto target = *selected_location;
		auto pending = Database().PendingTasks(target.id);
		wxMessageDialog confirm(this, wxString::Format(_("Remove %s from this workbench?"), to_wx(target.name)),
		                        _("Remove from Workbench"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
		confirm.SetExtendedMessage(to_wx(target.root) + "\n" + _("Files and series membership are kept.")
		    + "\n" + wxString::Format(_("%lld unfinished tasks will pause until this project is registered again."), static_cast<long long>(pending)));
		confirm.SetYesNoLabels(wxID_REMOVE, wxID_CANCEL);
		if (confirm.ShowModal() != wxID_YES) return;
		Database().Unmanage(target);
		RefreshAfterFolderChange(target);
	}
	void RefreshAfterFolderChange(Location const& target) {
		CloseWorkbenchProjectActivity(Roots::Resolve(*c->path).database, target);
		if ((selected_location && selected_location->id == target.id && selected_location->root == target.root) ||
		    (current && current->id == target.id && current->root == target.root)) {
			current.reset(); selected_location.reset(); parent_series.reset(); child_id.clear();
			remove_registration->Disable(); EnableSelectionActions(false); details->SetLabelText({}); ShowCover();
		}
		offset = 0; RefreshList();
	}
	bool CheckEditor(Path const& folder) {
		if (!wxGetApp().HasOpenFilesIn(folder)) return true;
		SetStatus(_("Close the open subtitles, audio or video from this folder before removing it."));
		return false;
	}
	void RemoveFolder(FolderDisposition mode) {
		if (!selected_location || !current || current->kind != Kind::Project || !project_files->IsEnabled()) return;
		if (!CheckEditor(selected_location->root)) return;
		auto preview = Database().PreviewFolderRemoval(*selected_location);
		bool trash = mode == FolderDisposition::Trash;
		auto action = trash ? _("Move to Trash") : _("Permanently Delete");
		wxMessageDialog confirm(this,
			trash ? wxString::Format(_("Move the folder for %s to the system Trash?"), to_wx(preview.project.name))
			      : wxString::Format(_("Permanently delete the folder for %s?"), to_wx(preview.project.name)),
			action, wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
		auto details = to_wx(preview.project.root) + "\n\n"
			+ _("File count") + wxString::Format("  %lld\n", static_cast<long long>(preview.files))
			+ wxString::Format(_("%lld unfinished tasks will pause until this project is registered again."),
				static_cast<long long>(preview.tasks)) + "\n";
		if (preview.series) {
			auto const& episodes = preview.series->episodes;
			auto episode = std::find_if(episodes.begin(), episodes.end(), [&](Episode const& item) {
				return item.project_id == preview.project.id;
			});
			if (episode == episodes.end()) throw std::runtime_error("The series episode changed.");
			details += wxString::Format(_("Episode %s will be removed from the series %s."),
				to_wx(episode->label), to_wx(preview.series->name));
		}
		else details += _("This is an independent project.");
		if (!trash) details += "\n\n" + _("Permanent deletion is irreversible.");
		confirm.SetExtendedMessage(details);
		confirm.SetYesNoLabels(action, wxID_CANCEL);
		if (confirm.ShowModal() != wxID_YES || !CheckEditor(preview.project.root)) return;
		try {
			Database().RemoveFolder(preview, mode, [trash](Path const& path) {
				if (trash) TrashFolder(path);
				else std::filesystem::remove_all(path);
			});
		}
		catch (...) {
			RefreshFolderRemovals();
			throw;
		}
		RefreshAfterFolderChange(preview.location);
	}
	void ConfirmRemovedFolder() {
		auto pending = Database().UncertainFolderRemovals();
		if (pending.empty()) { RefreshFolderRemovals(); return; }
		size_t choice = 0;
		if (pending.size() > 1) {
			wxArrayString labels;
			for (auto const& location : pending) labels.Add(to_wx(location.name) + "  —  " + to_wx(location.root));
			wxSingleChoiceDialog select(this, _("Choose a folder to verify."), _("Confirm Folder Removal"), labels);
			if (select.ShowModal() != wxID_OK) return;
			choice = select.GetSelection();
		}
		auto const& target = pending.at(choice);
		auto recovery = Database().FolderRecovery(target);
		if (!recovery) { RefreshFolderRemovals(); return; }
		auto paths = _("Original project folder") + "\n" + to_wx(target.root);
		if (!recovery->staged.empty())
			paths += "\n\n" + _("Staging folder") + "\n" + to_wx(recovery->staged);
		if (recovery->can_restore) {
			auto action = _("Restore Remaining Files");
			wxMessageDialog restore(this, _("Return the remaining files to the original project folder?"),
				action, wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
			restore.SetExtendedMessage(paths + "\n\n" + _("Restoring recovers only the files still in the staging folder."));
			restore.SetYesNoLabels(action, wxID_CANCEL);
			if (restore.ShowModal() != wxID_YES) return;
			Database().RestoreFolderRemainder(target);
			RefreshAfterFolderChange(target);
			SetStatus(_("Remaining files restored to the original project folder."));
			return;
		}
		if (!recovery->can_confirm) {
			wxMessageDialog repair(this, _("Check these paths to resolve the interrupted folder removal."),
				_("Interrupted Folder Removal"), wxOK | wxICON_WARNING);
			repair.SetExtendedMessage(paths);
			repair.ShowModal();
			return;
		}
		bool trash = recovery->staged.empty();
		wxMessageDialog confirm(this,
			trash ? _("Confirm that this folder was moved to the system Trash.")
			      : _("Confirm that the permanent deletion is complete."),
			_("Confirm Folder Removal"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
		if (trash) paths += "\n\n" + _("Check the system Trash for this folder before confirming.");
		confirm.SetExtendedMessage(paths + "\n\n"
			+ _("Confirmation updates the workbench, pauses unfinished tasks, and updates the series episode list if applicable."));
		confirm.SetYesNoLabels(_("Confirm Removal"), wxID_CANCEL);
		if (confirm.ShowModal() != wxID_YES) return;
		Database().ConfirmFolderRemoval(target);
		RefreshAfterFolderChange(target);
	}
	void OpenFolder() {
		wxDirDialog picker(this, _("Open Project or Series"), {}, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
		if (picker.ShowModal() == wxID_OK) Select(workbench::Open(from_wx(picker.GetPath())));
	}
	void ImportEditorPreferences() {
		wxFileDialog file(this, _("Choose an older Aegisub configuration"), {}, "config.json",
		                  "JSON (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (file.ShowModal() != wxID_OK) return;
		auto preview = PreviewPreferences(from_wx(file.GetPath()), c->path->Decode("?user/config.json"), *config::opt);
		if (preview.choices.empty()) {
			SetStatus(_("This file has no different supported editor preferences."));
			return;
		}
		wxArrayString choices;
		for (auto const& change : preview.choices)
			choices.Add(to_wx(change.key) + "  —  " + (change.incoming ? _("Enabled") : _("Disabled")));
		auto message = _("Select the editor preferences to import.") + "\n" + to_wx(preview.source) + "\n→ " + to_wx(preview.destination);
		wxMultiChoiceDialog select(this, message, _("Import Editor Preferences"), choices);
		if (select.ShowModal() != wxID_OK || select.GetSelections().empty()) return;
		auto selection = select.GetSelections();
		auto backup = ImportPreferences(preview, {selection.begin(), selection.end()}, *config::opt);
		SetStatus(backup.empty() ? _("Selected preferences imported.") : _("Previous configuration saved to") + "\n" + to_wx(backup));
	}
	void EditTemplate() {
		wxArrayString kinds;
		kinds.Add(_("Project"));
		kinds.Add(_("Series"));
		wxSingleChoiceDialog select(this, _("Choose a directory template"), _("Directory Templates"), kinds);
		if (select.ShowModal() != wxID_OK) return;
		auto kind = select.GetSelection() == 0 ? Kind::Project : Kind::Series;
		auto directory = TemplateDirectory();
		auto original = LoadTemplate(directory, kind);
		wxDialog dialog(this, wxID_ANY, _("Directory Templates"));
		auto box = new wxBoxSizer(wxVERTICAL);
		auto grid = new wxFlexGridSizer(2, FromDIP(6), FromDIP(12));
		grid->AddGrowableCol(1);
		std::map<std::string, wxTextCtrl*> fields;
		std::map<std::string, wxString> const labels{{"audio", _("Audio")}, {"video", _("Video")}, {"subtitles", _("Subtitles")},
			{"translations", _("Translations")}, {"exports", _("Exports")}, {"episodes", _("Episodes")}};
		for (auto const& [role, path] : original.folders) {
			grid->Add(new wxStaticText(&dialog, wxID_ANY, labels.at(role)), wxSizerFlags().CenterVertical());
			auto field = new wxTextCtrl(&dialog, wxID_ANY, to_wx(path));
			fields.emplace(role, field);
			grid->Add(field, wxSizerFlags().Expand());
		}
		box->Add(grid, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(12)));
		auto files = new wxBoxSizer(wxHORIZONTAL);
		auto import = new wxButton(&dialog, wxID_ANY, _("Import Template…"));
		auto share = new wxButton(&dialog, wxID_ANY, _("Export Template…"));
		files->Add(import);
		files->Add(share, wxSizerFlags().Border(wxLEFT, FromDIP(6)));
		box->Add(files, wxSizerFlags().Border(wxLEFT | wxRIGHT, FromDIP(12)));
		auto values = [&] {
			Folders edited;
			for (auto const& [role, field] : fields) edited.emplace(role, from_wx(field->GetValue()));
			return edited;
		};
		auto attempt = [&](auto action) {
			try { action(); return true; }
			catch (agi::Exception const& e) { wxMessageBox(to_wx(e.GetMessage()), _("Directory Templates"), wxOK | wxICON_ERROR, &dialog); }
			catch (std::exception const& e) { wxMessageBox(to_wx(std::string(e.what())), _("Directory Templates"), wxOK | wxICON_ERROR, &dialog); }
			return false;
		};
		import->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
			wxFileDialog file(&dialog, _("Import Template…"), {}, {}, "JSON (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (file.ShowModal() == wxID_OK) attempt([&] {
				auto incoming = ImportTemplate(from_wx(file.GetPath()), kind);
				for (auto const& [role, path] : incoming) fields.at(role)->ChangeValue(to_wx(path));
			});
		});
		share->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
			wxFileDialog file(&dialog, _("Export Template…"), {}, kind == Kind::Project ? "project-template.json" : "series-template.json",
			                  "JSON (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (file.ShowModal() == wxID_OK) attempt([&] { ExportTemplate(from_wx(file.GetPath()), kind, values()); });
		});
		box->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL, FromDIP(8)));
		dialog.SetSizerAndFit(box);
		while (dialog.ShowModal() == wxID_OK)
			if (attempt([&] { SaveTemplate(directory, kind, values(), original); })) return;
	}
public:
	WorkbenchHome(wxWindow *parent, agi::Context *context, wxWindow *&editor_button)
	: wxPanel(parent), c(context) {
		SetName("workbench-home");
		auto scroller = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxTAB_TRAVERSAL);
		scroller->SetScrollRate(0, FromDIP(12));
		landing = scroller;
		auto layout = new wxBoxSizer(wxVERTICAL);
		title = new wxStaticText(landing, wxID_ANY, _("Workbench"));
		title->SetFont(title->GetFont().Larger().Bold());
		layout->Add(title, wxSizerFlags().Border(wxBOTTOM, FromDIP(12)));
		auto add = [&](wxSizer *row, wxString const& label, auto action) {
			auto button = new wxButton(landing, wxID_ANY, label);
			button->Bind(wxEVT_BUTTON, [this, action](wxCommandEvent&) { Run(action); });
			row->Add(button, wxSizerFlags().Border(wxRIGHT, FromDIP(6)));
			return button;
		};
		auto projects = new wxWrapSizer(wxHORIZONTAL);
		auto sidebar = new wxBoxSizer(wxVERTICAL);
		add(sidebar, _("Home"), [this] { Browse(LibraryView::All, true, _("Workbench")); })->SetName("workbench-nav-home");
		add(sidebar, _("All Projects"), [this] { Browse(LibraryView::All, false, _("All Projects")); })->SetName("workbench-nav-all");
		add(sidebar, _("Series Projects"), [this] { Browse(LibraryView::Series, false, _("Series Projects")); })->SetName("workbench-nav-series");
		add(sidebar, _("Recently Opened"), [this] { Browse(LibraryView::Recent, false, _("Recently Opened")); })->SetName("workbench-nav-recent");
		add(sidebar, _("Download Center"), [this] { ShowActivity(WorkbenchActivityPage::Downloads); })->SetName("workbench-nav-downloads");
		add(sidebar, _("Settings"), [this] { cmd::call("app/options", c); });
		add(projects, _("New Project"), [this] { CreateNew(Kind::Project); })->SetName("workbench-create-project");
		add(projects, _("New Series"), [this] { CreateNew(Kind::Series); })->SetName("workbench-create-series");
		add(projects, _("Open Project or Series"), [this] { OpenFolder(); })->SetName("workbench-open-project");
		add(projects, _("Register Existing Folder"), [this] { RegisterExisting(); })->SetName("workbench-register-folder");
		add(sidebar, _("Directory Templates"), [this] { EditTemplate(); })->SetName("workbench-templates");
		add(projects, _("Choose Template Library Folder"), [this] {
			wxDirDialog picker(this, _("Choose Template Library Folder"), to_wx(TemplateDirectory()), wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
			if (picker.ShowModal() == wxID_OK) {
				auto path = from_wx(picker.GetPath());
				// Validate both templates before making this the active library.
				LoadTemplate(path, Kind::Project);
				LoadTemplate(path, Kind::Series);
				auto option = OPT_SET("Path/Workbench Templates");
				auto old_path = option->GetString();
				auto relative = Path(path).lexically_relative(c->path->Decode("?user"));
				option->SetString(!relative.empty() && *relative.begin() != ".."
				                  ? (Path("?user") / Path(relative)).generic_string() : path);
				try { config::opt->Flush(); }
				catch (...) { option->SetString(old_path); throw; }
			}
		})->SetName("workbench-template-library");
		layout->Add(projects, wxSizerFlags().Expand().Border(wxBOTTOM, FromDIP(8)));
		auto visits = new wxWrapSizer;
		resume = add(visits, _("Continue Last Project"), [this] {
			auto last = Database().List(0, 1, LibraryView::Recent);
			if (!last.empty()) OpenLocation(last.front(), true);
		});
		resume->SetName("workbench-resume-project"); resume->Disable();
		layout->Add(visits, wxSizerFlags().Expand());
		auto strip = new wxBoxSizer(wxHORIZONTAL);
		for (size_t i = 0; i < cards.size(); ++i) {
			auto card = cards[i] = new wxButton(landing, wxID_ANY, {});
			card->SetMinSize(FromDIP(wxSize(1, 86)));
			card->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { Run([&] { if (i < recent.size()) OpenLocation(recent[i], true); }); });
			strip->Add(card, wxSizerFlags(1).Expand().Border(wxRIGHT, FromDIP(4)));
			card->Hide();
		}
		layout->Add(strip, wxSizerFlags().Expand());
		unfinished = new wxStaticText(landing, wxID_ANY, {}); unfinished->SetName("workbench-unfinished");
		layout->Add(unfinished, wxSizerFlags().Expand().Border(wxTOP, FromDIP(4)));
		search = new wxTextCtrl(landing, wxID_ANY);
		search->SetName("workbench-project-search"); search->SetHint(_("Search by project name or folder"));
		search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Run([&] { offset = 0; RefreshList(); }); });
		layout->Add(search, wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, FromDIP(4)));
		list = new wxListBox(landing, wxID_ANY);
		list->SetMinSize(FromDIP(wxSize(-1, 100)));
		list->SetName("workbench-projects");
		list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) {
			auto item = event.GetSelection();
			if (item >= 0 && static_cast<size_t>(item) < locations.size())
				Run([&] { OpenLocation(locations[item], false); });
		});
		layout->Add(list, wxSizerFlags(1).Expand());
		auto pages = new wxWrapSizer(wxHORIZONTAL);
		previous = add(pages, _("Previous"), [this] { offset = std::max(0, offset - page_size); RefreshList(); });
		next = add(pages, _("Next"), [this] { offset += page_size; RefreshList(); });
		add(pages, _("Refresh"), [this] { index.reset(); offset = 0; RefreshList(); });
		rename = add(pages, _("Rename Project or Series"), [this] {
			if (!current) return;
			auto expected = *current;
			wxTextEntryDialog dialog(this, _("Display name"), _("Rename Project or Series"), to_wx(expected.name));
			if (dialog.ShowModal() == wxID_OK) Select(workbench::Rename(expected, from_wx(dialog.GetValue())));
		});
		rename->SetName("workbench-rename");
		edit_metadata = add(pages, _("Project Details"), [this] {
			if (!current) return;
			auto expected = *current;
			if (auto updated = EditWorkbenchMetadata(this, expected)) Select(std::move(*updated));
		});
		edit_metadata->SetName("workbench-edit-metadata");
		choose_cover = add(pages, _("Choose Cover…"), [this] { ChooseCover(); });
		choose_cover->SetName("workbench-choose-cover");
		dashboard = add(pages, _("Project Dashboard"), [this] { ShowActivity(WorkbenchActivityPage::Dashboard); });
		dashboard->SetName("workbench-dashboard");
		project_files = add(pages, _("Project Files"), [this] { ShowActivity(WorkbenchActivityPage::Files); });
		project_files->SetName("workbench-files");
		episodes = add(pages, _("Series Episodes"), [this] { ShowEpisodes(); });
		episodes->SetName("workbench-episodes");
		back = add(pages, _("Back to Series"), [this] {
			if (!parent_series) return;
			auto series = workbench::Open(parent_series->root);
			if (series.id != parent_series->id || series.kind != Kind::Series) throw std::runtime_error("The series at this location changed.");
			Run([&] { Select(std::move(series)); });
			ShowEpisodes();
		});
		back->SetName("workbench-back-series");
		registration_tasks = add(pages, _("Registration Tasks"), [this] { ShowActivity(WorkbenchActivityPage::Tasks); });
		registration_tasks->SetName("workbench-tasks");
		layout->Add(pages, wxSizerFlags().Expand().Border(wxTOP, FromDIP(6)));
		auto removal = new wxWrapSizer(wxHORIZONTAL);
		remove_registration = add(removal, _("Remove from Workbench"), [this] { RemoveRegistration(); });
		remove_registration->SetName("workbench-remove-registration"); remove_registration->Disable();
		trash_folder = add(removal, _("Move Project Folder to Trash…"), [this] { RemoveFolder(FolderDisposition::Trash); });
		trash_folder->SetName("workbench-trash-folder");
		delete_folder = add(removal, _("Permanently Delete Project Folder…"), [this] { RemoveFolder(FolderDisposition::Permanent); });
		delete_folder->SetName("workbench-delete-folder");
		confirm_removal = add(removal, _("Review Interrupted Folder Removal…"), [this] { ConfirmRemovedFolder(); });
		confirm_removal->SetName("workbench-confirm-folder-removal"); confirm_removal->Hide();
		previous->Disable(); next->Disable(); EnableSelectionActions(false);
		layout->Add(removal, wxSizerFlags().Expand().Border(wxTOP, FromDIP(6)));
		details = new wxStaticText(landing, wxID_ANY, {});
		status = new wxStaticText(landing, wxID_ANY, {});
		details->SetName("workbench-project-details");
		status->SetName("workbench-status");
		auto metadata = new wxBoxSizer(wxHORIZONTAL);
		cover = new wxStaticBitmap(landing, wxID_ANY, wxNullBitmap);
		cover->SetName("workbench-cover");
		ShowCover();
		metadata->Add(cover, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(8)));
		metadata->Add(details, wxSizerFlags(1).CenterVertical());
		layout->Add(metadata, wxSizerFlags().Expand().Border(wxTOP, FromDIP(8)));
		layout->Add(status, wxSizerFlags().Expand().Border(wxBOTTOM, FromDIP(8)));
		auto editors = new wxWrapSizer(wxHORIZONTAL);
		editor_button = add(editors, _("Open Editor"), [this] { c->frame->ShowEditor(); });
		add(editors, _("Open Subtitles..."), [this] { cmd::call("subtitle/open", c); });
		add(editors, _("New Window"), [this] { cmd::call("app/new_window", c); });
		add(editors, _("Import Editor Preferences"), [this] { ImportEditorPreferences(); })->SetName("workbench-import-preferences");
		layout->Add(editors, wxSizerFlags().Expand());
		auto outer = new wxBoxSizer(wxHORIZONTAL);
		outer->Add(sidebar, wxSizerFlags().Border(wxTOP | wxLEFT, FromDIP(12)));
		outer->Add(layout, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(20)));
		landing->SetSizer(outer);
		auto pages_layout = new wxBoxSizer(wxVERTICAL);
		pages_layout->Add(landing, wxSizerFlags(1).Expand());
		SetSizer(pages_layout);
		Run([this] { RefreshList(); });
	}
};
}

wxWindow *CreateWorkbenchHome(wxWindow *parent, agi::Context *context, wxWindow *&editor_button) {
	return new WorkbenchHome(parent, context, editor_button);
}

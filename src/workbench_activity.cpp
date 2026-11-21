// SPDX-License-Identifier: ISC
#include "workbench_activity.h"
#include "compat.h"
#include "workbench_metadata.h"
#include "include/aegisub/context.h"
#include "frame_main.h"
#include "main.h"
#include "project.h"
#include "subs_controller.h"

#include <libaegisub/exception.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/wrapsizer.h>
#include <wx/utils.h>
#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

namespace {
using namespace workbench;
constexpr int page_size = 50;

wxString StateLabel(std::string const& value) {
	if (value == "queued") return _("Queued");
	if (value == "paused_project") return _("Paused until the project is registered again");
	if (value == "registering") return _("Registering");
	if (value == "interrupted") return _("Interrupted");
	if (value == "needs_attention") return _("Needs attention");
	if (value == "cancelled") return _("Cancelled");
	if (value == "succeeded") return _("Completed");
	if (value == "none") return _("Awaiting verification");
	if (value == "archived") return _("Verified in project");
	if (value == "registered") return _("Registered");
	return to_wx(value);
}

class Activity final : public wxDialog {
	Path database;
	agi::Context* context;
	std::optional<Manifest> project;
	std::function<void()> notify_home;
	Index reader;
	wxNotebook* pages;
	wxPanel *file_page, *task_page;
	wxChoice *file_view, *task_view;
	wxStaticText *overview = nullptr, *suggestion = nullptr;
	wxButton *accept = nullptr, *ignore = nullptr, *open_file, *new_window, *reveal;
	std::optional<StatusHint> hint;
	wxListBox *files, *tasks, *outputs;
	wxStaticText *scan_status, *task_status, *message;
	wxButton *file_prev, *file_next, *rebuild, *queue, *task_prev, *task_next, *run, *cancel, *out_prev, *out_next, *stop;
	int file_offset = 0, task_offset = 0, output_offset = 0;
	std::vector<FileEntry> file_rows;
	std::vector<RegistrationTask> task_rows;
	std::string selected;
	bool closing = false;
	wxTimer timer;
	std::future<std::string> result;
	std::jthread worker;

	bool Busy() const { return worker.joinable(); }
	template<class Action> void Report(Action action) {
		try { action(); }
		catch (agi::UserCancelException const&) { message->SetLabelText(_("Operation stopped.")); }
		catch (agi::Exception const& e) { message->SetLabelText(to_wx(e.GetMessage())); }
		catch (std::exception const& e) { message->SetLabelText(to_wx(std::string(e.what()))); }
		Layout();
	}
	template<class Action> wxButton* Button(wxWindow* parent, wxSizer* row, wxString label, char const* name, Action action) {
		auto b = new wxButton(parent, wxID_ANY, label);
		b->SetName(name);
		b->Bind(wxEVT_BUTTON, [this, action](wxCommandEvent&) { Report(action); });
		row->Add(b, wxSizerFlags().Border(wxRIGHT, FromDIP(5)));
		return b;
	}
	void RefreshFiles() {
		files->Clear();
		file_rows.clear();
		rebuild->Enable(project.has_value() && !Busy());
		queue->Disable();
		open_file->Disable(); new_window->Disable(); reveal->Disable();
		file_prev->Enable(project.has_value() && file_offset > 0);
		file_next->Disable();
		if (!project) { scan_status->SetLabelText(_("Choose a project on the home page to view its files.")); return; }
		auto state = reader.Scan(project->id);
		scan_status->SetLabelText(!state.generation ? _("This project has not been scanned yet.")
		        : wxString::Format(_("%lld indexed files"), static_cast<long long>(state.files)));
		file_rows = reader.Files(project->id, file_offset, page_size + 1, static_cast<FileView>(file_view->GetSelection()));
		file_next->Enable(file_rows.size() > page_size);
		if (file_rows.size() > page_size) file_rows.resize(page_size);
		for (auto const& row : file_rows) files->Append(to_wx(row.relative.generic_string()));
	}
	void RefreshOutputs() {
		outputs->Clear(); task_status->SetLabelText({});
		run->Disable(); cancel->Disable(); out_prev->Disable(); out_next->Disable();
		auto it = std::find_if(task_rows.begin(), task_rows.end(), [&](auto const& task) { return task.id == selected; });
		if (it == task_rows.end()) { selected.clear(); return; }
		bool active = task_view->GetSelection() == 0 && it->state != "paused_project" && it->state != "succeeded" && it->state != "cancelled";
		run->Enable(active && !Busy());
		cancel->Enable(active);
		task_status->SetLabelText(to_wx(it->reason));
		auto page = reader.Outputs(selected, output_offset, page_size + 1);
		out_prev->Enable(output_offset > 0);
		out_next->Enable(page.size() > page_size);
		if (page.size() > page_size) page.resize(page_size);
		for (auto const& out : page) outputs->Append(to_wx(out.relative.generic_string()) + "  —  " + StateLabel(out.stage));
	}
	void RefreshTasks() {
		auto scope = task_view->GetSelection() == 1 ? TaskView::Downloads : TaskView::Registrations;
		pages->SetPageText(1, task_view->GetStringSelection());
		task_rows = reader.Tasks(scope, task_offset, page_size + 1, project ? project->id : "");
		task_prev->Enable(task_offset > 0);
		task_next->Enable(task_rows.size() > page_size);
		if (task_rows.size() > page_size) task_rows.resize(page_size);
		tasks->Clear();
		for (auto const& task : task_rows) {
			auto row = tasks->Append(to_wx(task.name) + "  —  " + to_wx(task.project_name) + "  —  " + StateLabel(task.state)
			                         + wxString::Format("  %lld/%lld", static_cast<long long>(task.registered), static_cast<long long>(task.outputs)));
			if (task.id == selected) tasks->SetSelection(row);
		}
		RefreshOutputs();
		if (task_rows.empty() && task_view->GetSelection() == 1) task_status->SetLabelText(_("No download tasks."));
	}
	void Start(wxString label, std::function<std::string(Index&, std::stop_token)> action) {
		if (Busy()) return;
		std::packaged_task<std::string(std::stop_token)> work([path=database, action=std::move(action)](std::stop_token token) {
			Index writer(path);
			return action(writer, token);
		});
		result = work.get_future();
		worker = std::jthread(std::move(work));
		message->SetLabelText(label);
		rebuild->Disable(); queue->Disable(); run->Disable(); stop->Enable();
		timer.Start(75);
	}
	void Poll() {
		if (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
		timer.Stop();
		worker.join();
		stop->Disable();
		message->SetLabelText({});
		Report([&] {
			auto new_task = result.get();
			if (!new_task.empty()) {
				selected = new_task; task_offset = output_offset = 0;
				pages->SetSelection(1);
			}
		});
		if (!closing) Report([&] { RefreshFiles(); RefreshTasks(); RefreshOverview(); });
		notify_home();
		if (closing) Destroy();
	}
	void BuildFiles() {
		auto layout = new wxBoxSizer(wxVERTICAL);
		file_view = new wxChoice(file_page, wxID_ANY);
		for (auto label : {_("All files"), _("Recent files"), _("Video files"), _("Subtitle files")}) file_view->Append(label);
		file_view->SetName("workbench-file-view"); file_view->SetSelection(0);
		layout->Add(file_view, wxSizerFlags().Border(wxALL, FromDIP(4)));
		file_view->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { Report([&] { file_offset = 0; RefreshFiles(); }); });
		scan_status = new wxStaticText(file_page, wxID_ANY, {});
		files = new wxListBox(file_page, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_EXTENDED | wxLB_HSCROLL);
		files->SetName("workbench-index-files");
		layout->Add(scan_status, wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));
		layout->Add(files, wxSizerFlags(1).Expand());
		auto actions = new wxWrapSizer(wxHORIZONTAL);
		file_prev = Button(file_page, actions, _("Previous"), "workbench-files-prev", [this] { file_offset -= page_size; RefreshFiles(); });
		file_next = Button(file_page, actions, _("Next"), "workbench-files-next", [this] { file_offset += page_size; RefreshFiles(); });
		rebuild = Button(file_page, actions, _("Rebuild File Index"), "workbench-rebuild-files", [this] {
			if (!project) return;
			file_offset = 0;
			Start(_("Scanning project files…"), [p=*project](Index& index, std::stop_token token) {
				index.RebuildFiles(p, token); return std::string{};
			});
		});
		queue = Button(file_page, actions, _("Queue Selected Files for Registration"), "workbench-queue-files", [this] {
			wxArrayInt selection;
			if (!project || !files->GetSelections(selection)) return;
			std::vector<Path> paths;
			for (int row : selection) paths.push_back(file_rows.at(row).relative);
			Start(_("Verifying selected files…"), [p=*project, paths=std::move(paths)](Index& index, std::stop_token token) {
				return index.QueueRegistration(p, paths, token);
			});
		});
		auto open_actions = new wxWrapSizer;
		open_file = Button(file_page, open_actions, _("Open in Editor"), "workbench-open-file", [this] { OpenSelected(false); });
		new_window = Button(file_page, open_actions, _("Open in Independent Editor"), "workbench-open-separate", [this] { OpenSelected(true); });
		reveal = Button(file_page, open_actions, _("Open Containing Folder"), "workbench-reveal-file", [this] {
			wxArrayInt rows;
			if (!project || files->GetSelections(rows) != 1) return;
			auto path = ResolveProjectFile(*project, file_rows.at(rows[0]).relative);
			if (!wxLaunchDefaultApplication(to_wx(path.parent_path()))) throw std::runtime_error("Could not open folder: " + path.parent_path().string());
		});
		files->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
			wxArrayInt selection;
			auto count = files->GetSelections(selection);
			auto kind = count == 1 ? MediaKind(file_rows.at(selection[0]).relative.string()) : FileView::All;
			queue->Enable(!Busy() && count > 0);
			open_file->Enable(kind == FileView::Subtitles || kind == FileView::Video);
			new_window->Enable(kind == FileView::Subtitles); reveal->Enable(count == 1);
		});
		layout->Add(actions, wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, FromDIP(6)));
		layout->Add(open_actions, wxSizerFlags().Expand());
		file_page->SetSizer(layout);
	}
	void BuildTasks() {
		auto layout = new wxBoxSizer(wxVERTICAL);
		task_view = new wxChoice(task_page, wxID_ANY);
		task_view->Append(_("Registration Tasks")); task_view->Append(_("Download Tasks")); task_view->SetSelection(0);
		task_view->SetName("workbench-task-view");
		task_view->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { Report([&] { task_offset = output_offset = 0; selected.clear(); RefreshTasks(); RememberPage(); }); });
		layout->Add(task_view, wxSizerFlags().Border(wxALL, FromDIP(4)));
		tasks = new wxListBox(task_page, wxID_ANY);
		tasks->SetName("workbench-registration-list");
		layout->Add(tasks, wxSizerFlags(1).Expand());
		auto actions = new wxWrapSizer(wxHORIZONTAL);
		task_prev = Button(task_page, actions, _("Previous"), "workbench-tasks-prev", [this] { task_offset -= page_size; RefreshTasks(); });
		task_next = Button(task_page, actions, _("Next"), "workbench-tasks-next", [this] { task_offset += page_size; RefreshTasks(); });
		run = Button(task_page, actions, _("Run or Check Registration"), "workbench-run-task", [this] {
			Start(_("Checking registration…"), [id=selected](Index& index, std::stop_token token) {
				index.RunRegistration(id, token); return std::string{};
			});
		});
		cancel = Button(task_page, actions, _("Cancel Task"), "workbench-cancel-task", [this] {
			reader.CancelRegistration(selected); RefreshTasks(); notify_home();
		});
		layout->Add(actions, wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, FromDIP(6)));
		task_status = new wxStaticText(task_page, wxID_ANY, {});
		layout->Add(task_status, wxSizerFlags().Expand());
		outputs = new wxListBox(task_page, wxID_ANY);
		outputs->SetName("workbench-registration-outputs");
		layout->Add(outputs, wxSizerFlags(1).Expand());
		auto nav = new wxWrapSizer(wxHORIZONTAL);
		out_prev = Button(task_page, nav, _("Previous Files"), "workbench-outputs-prev", [this] { output_offset -= page_size; RefreshOutputs(); });
		out_next = Button(task_page, nav, _("Next Files"), "workbench-outputs-next", [this] { output_offset += page_size; RefreshOutputs(); });
		layout->Add(nav, wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, FromDIP(6)));
		tasks->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& e) {
			Report([&] { selected = task_rows.at(e.GetSelection()).id; output_offset = 0; RefreshOutputs(); });
		});
		task_page->SetSizer(layout);
	}
	void OpenSelected(bool separate) {
		wxArrayInt selection;
		if (!project || files->GetSelections(selection) != 1) return;
		auto path = ResolveProjectFile(*project, file_rows.at(selection[0]).relative);
		auto kind = MediaKind(path.string());
		if (kind == FileView::Subtitles) {
			auto destination = wxGetApp().ActivateOpenSubtitle(path);
			if (!destination) {
				destination = separate ? &wxGetApp().NewProjectContext() : context;
				if (separate) destination->project->LoadSubtitles(path);
				else wxGetApp().OpenSubtitle(destination, path);
			}
			std::error_code error;
			if (destination->subsController->HasFile() && std::filesystem::equivalent(path, destination->subsController->Filename(), error)) {
				Hide(); destination->frame->Raise();
			}
		}
		else if (!separate && kind == FileView::Video) {
			context->project->LoadVideo(path);
			if (context->project->VideoName() == path) { Hide(); context->frame->Raise(); }
		}
	}
	void RefreshOverview() {
		if (!overview || !project) return;
		auto latest = Open(project->root);
		if (latest.id != project->id || latest.root != project->root) throw std::runtime_error("The project at this location changed.");
		project = std::move(latest);
		auto const& m = project->metadata;
		wxString info = to_wx(project->name) + "  —  " + WorkbenchStatusLabel(m.status) + "\n" + to_wx(project->root);
		if (!m.description.empty()) info += "\n" + to_wx(m.description);
		if (!m.source.empty()) info += "\n" + to_wx(m.source);
		if (!m.source_language.empty() || !m.target_language.empty()) info += "\n" + to_wx(m.source_language) + " → " + to_wx(m.target_language);
		if (!m.owner.empty()) info += "\n" + _("Owner") + "  " + to_wx(m.owner);
		for (auto const& tag : m.tags) info += "  #" + to_wx(tag);
		overview->SetLabelText(info);
		hint = reader.SuggestedStatus(*project);
		wxString reason = _("No status suggestion.");
		if (hint) {
			if (hint->download) reason = _("Downloaded files were registered. Mark this project in progress?");
			else if (hint->status == "review") reason = _("Subtitles were exported. Mark this project ready for review?");
			else reason = _("Subtitles were saved. Mark this project in progress?");
		}
		suggestion->SetLabelText(reason);
		accept->Enable(hint.has_value()); ignore->Enable(hint.has_value());
		overview->GetParent()->Layout();
	}
	void ResolveHint(bool apply) {
		if (hint && project) project = reader.ResolveSuggestion(*project, *hint, apply);
		RefreshOverview(); notify_home();
	}
	void RememberPage() {
		auto tab = pages->GetSelection();
		auto caption = pages->GetPageText(tab);
		SetTitle(project ? caption + " — " + to_wx(project->name) : caption);
		if (!project) return;
		auto page = tab == 2 ? "dashboard" : tab == 0 ? "files" : task_view->GetSelection() == 1 ? "downloads" : "tasks";
		reader.Visit(*project, page);
	}
	void BuildOverview() {
		auto panel = new wxPanel(pages);
		auto layout = new wxBoxSizer(wxVERTICAL);
		overview = new wxStaticText(panel, wxID_ANY, {}); overview->SetName("workbench-dashboard-details");
		layout->Add(overview, wxSizerFlags().Expand().Border(wxALL, FromDIP(8)));
		suggestion = new wxStaticText(panel, wxID_ANY, {}); suggestion->SetName("workbench-status-suggestion");
		layout->Add(suggestion, wxSizerFlags().Expand().Border(wxALL, FromDIP(8)));
		auto actions = new wxWrapSizer;
		accept = Button(panel, actions, _("Apply Suggestion"), "workbench-accept-suggestion", [this] { ResolveHint(true); });
		ignore = Button(panel, actions, _("Dismiss Suggestion"), "workbench-dismiss-suggestion", [this] { ResolveHint(false); });
		Button(panel, actions, _("Project Details"), "dashboard-metadata", [this] {
			if (auto changed = EditWorkbenchMetadata(this, *project)) { project = std::move(changed); RefreshOverview(); notify_home(); }
		});
		Button(panel, actions, _("Open Project Folder"), "dashboard-folder", [this] {
			RefreshOverview();
			if (!wxLaunchDefaultApplication(to_wx(project->root))) throw std::runtime_error("Could not open folder: " + project->root.string());
		});
		layout->Add(actions, wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));
		auto links = new wxWrapSizer;
		for (auto [label, view] : {std::pair{_("Recent files"), FileView::Recent}, {_("Video files"), FileView::Video}, {_("Subtitle files"), FileView::Subtitles}})
			Button(panel, links, label, "", [this, view] { file_view->SetSelection(static_cast<int>(view)); file_offset = 0; pages->SetSelection(0); RefreshFiles(); });
		Button(panel, links, _("Download Tasks"), "dashboard-downloads", [this] { Display(WorkbenchActivityPage::Downloads); });
		Button(panel, links, _("Open Editor"), "dashboard-editor", [this] { Hide(); context->frame->ShowEditor(); context->frame->Raise(); });
		layout->Add(links, wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));
		panel->SetSizer(layout); pages->AddPage(panel, _("Project Dashboard"));
	}
public:
	Activity(wxWindow* parent, Path const& path, agi::Context* c, std::optional<Manifest> p, std::function<void()> notify)
	: wxDialog(parent, wxID_ANY, p ? _("Project Files and Registration") + " — " + to_wx(p->name) : _("Registration Tasks"),
	           wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	  database(path), context(c), project(std::move(p)), notify_home(std::move(notify)), reader(path), timer(this) {
		SetName("workbench-activity");
		auto outer = new wxBoxSizer(wxVERTICAL);
		pages = new wxNotebook(this, wxID_ANY);
		file_page = new wxPanel(pages); task_page = new wxPanel(pages);
		BuildFiles(); BuildTasks();
		pages->AddPage(file_page, _("Project Files")); pages->AddPage(task_page, _("Registration Tasks"));
		if (project) BuildOverview();
		pages->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
			Report([&] { if (e.GetSelection() == 2) RefreshOverview(); RememberPage(); });
			e.Skip();
		});
		outer->Add(pages, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(8)));
		message = new wxStaticText(this, wxID_ANY, {});
		message->SetName("workbench-activity-status");
		outer->Add(message, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, FromDIP(8)));
		auto bottom = new wxWrapSizer;
		Button(this, bottom, _("Refresh"), "workbench-activity-refresh", [this] { RefreshFiles(); RefreshTasks(); RefreshOverview(); });
		stop = Button(this, bottom, _("Stop Current Operation"), "workbench-stop-operation", [this] {
			worker.request_stop(); stop->Disable();
		});
		stop->Disable();
		outer->Add(bottom, wxSizerFlags().Expand().Border(wxALL, FromDIP(8)));
		SetSizer(outer); SetSize(FromDIP(wxSize(760, 520)));
		Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Poll(); }, timer.GetId());
		Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
			if (Busy() && e.CanVeto()) { e.Veto(); closing = true; worker.request_stop(); Hide(); }
			else Destroy();
		});
	}
	bool Matches(Path const& path, std::optional<Manifest> const& p) const {
		return !closing && !IsBeingDeleted() && database == path &&
		       ((!project && !p) || (project && p && project->id == p->id && project->root == p->root));
	}
	bool ShowsProject(Path const& path, Location const& location) const {
		return !closing && !IsBeingDeleted() && database == path &&
		       project && project->id == location.id && project->root == location.root;
	}
	void Display(WorkbenchActivityPage page) {
		bool downloads = page == WorkbenchActivityPage::Downloads;
		if (task_view->GetSelection() != int(downloads)) { selected.clear(); task_offset = output_offset = 0; }
		task_view->SetSelection(downloads ? 1 : 0);
		pages->ChangeSelection(page == WorkbenchActivityPage::Dashboard && project ? 2 : page == WorkbenchActivityPage::Files ? 0 : 1);
		Report([this] { RefreshFiles(); RefreshTasks(); RefreshOverview(); RememberPage(); });
		Show(); Raise();
	}
};
}

void ShowWorkbenchActivity(wxWindow* parent, workbench::Path const& database, agi::Context* context, std::optional<workbench::Manifest> const& project,
                           WorkbenchActivityPage page, std::function<void()> notify_home) {
	for (auto window : parent->GetChildren()) {
		if (auto existing = dynamic_cast<Activity*>(window); existing && existing->Matches(database, project)) {
			existing->Display(page); return;
		}
	}
	(new Activity(parent, database, context, project, std::move(notify_home)))->Display(page);
}

void CloseWorkbenchProjectActivity(workbench::Path const& database, workbench::Location const& project) {
	for (auto node = wxTopLevelWindows.GetFirst(); node;) {
		auto activity = dynamic_cast<Activity*>(node->GetData());
		node = node->GetNext();
		if (activity && activity->ShowsProject(database, project)) activity->Close();
	}
}

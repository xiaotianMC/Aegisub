// SPDX-License-Identifier: ISC
#include "workbench_activity.h"
#include "compat.h"

#include <libaegisub/exception.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/wrapsizer.h>
#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

namespace {
using namespace workbench;
constexpr int page_size = 50;

wxString StateLabel(std::string const& value) {
	if (value == "queued") return _("Queued");
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
	std::optional<Manifest> project;
	std::function<void()> notify_home;
	Index reader;
	wxNotebook* pages;
	wxPanel *file_page, *task_page;
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
		file_prev->Enable(project.has_value() && file_offset > 0);
		file_next->Disable();
		if (!project) { scan_status->SetLabelText(_("Choose a project on the home page to view its files.")); return; }
		auto state = reader.Scan(project->id);
		scan_status->SetLabelText(!state.generation ? _("This project has not been scanned yet.")
		        : wxString::Format(_("%lld indexed files"), static_cast<long long>(state.files)));
		file_rows = reader.Files(project->id, file_offset, page_size + 1);
		file_next->Enable(file_rows.size() > page_size);
		if (file_rows.size() > page_size) file_rows.resize(page_size);
		for (auto const& row : file_rows) files->Append(to_wx(row.relative.generic_string()));
	}
	void RefreshOutputs() {
		outputs->Clear(); task_status->SetLabelText({});
		run->Disable(); cancel->Disable(); out_prev->Disable(); out_next->Disable();
		auto it = std::find_if(task_rows.begin(), task_rows.end(), [&](auto const& task) { return task.id == selected; });
		if (it == task_rows.end()) { selected.clear(); return; }
		bool active = it->state != "succeeded" && it->state != "cancelled";
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
		task_rows = reader.Registrations(task_offset, page_size + 1);
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
		if (!closing) Report([&] { RefreshFiles(); RefreshTasks(); });
		notify_home();
		if (closing) Destroy();
	}
	void BuildFiles() {
		auto layout = new wxBoxSizer(wxVERTICAL);
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
		files->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
			wxArrayInt selection;
			queue->Enable(!Busy() && files->GetSelections(selection) > 0);
		});
		layout->Add(actions, wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, FromDIP(6)));
		file_page->SetSizer(layout);
	}
	void BuildTasks() {
		auto layout = new wxBoxSizer(wxVERTICAL);
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
public:
	Activity(wxWindow* parent, Path const& path, std::optional<Manifest> p, std::function<void()> notify)
	: wxDialog(parent, wxID_ANY, p ? _("Project Files and Registration") + " — " + to_wx(p->name) : _("Registration Tasks"),
	           wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	  database(path), project(std::move(p)), notify_home(std::move(notify)), reader(path), timer(this) {
		SetName("workbench-activity");
		auto outer = new wxBoxSizer(wxVERTICAL);
		pages = new wxNotebook(this, wxID_ANY);
		file_page = new wxPanel(pages); task_page = new wxPanel(pages);
		BuildFiles(); BuildTasks();
		pages->AddPage(file_page, _("Project Files")); pages->AddPage(task_page, _("Registration Tasks"));
		outer->Add(pages, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(8)));
		message = new wxStaticText(this, wxID_ANY, {});
		message->SetName("workbench-activity-status");
		outer->Add(message, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, FromDIP(8)));
		auto bottom = new wxWrapSizer;
		Button(this, bottom, _("Refresh"), "workbench-activity-refresh", [this] { RefreshFiles(); RefreshTasks(); });
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
	void Display(bool show_tasks) {
		pages->SetSelection(show_tasks ? 1 : 0);
		Report([this] { RefreshFiles(); RefreshTasks(); });
		Show(); Raise();
	}
};
}

void ShowWorkbenchActivity(wxWindow* parent, workbench::Path const& database, std::optional<workbench::Manifest> const& project,
                           bool show_tasks, std::function<void()> notify_home) {
	for (auto window : parent->GetChildren()) {
		if (auto existing = dynamic_cast<Activity*>(window); existing && existing->Matches(database, project)) {
			existing->Display(show_tasks); return;
		}
	}
	(new Activity(parent, database, project, std::move(notify_home)))->Display(show_tasks);
}

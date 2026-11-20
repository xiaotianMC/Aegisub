// SPDX-License-Identifier: ISC
#include "workbench_series.h"
#include "workbench_metadata.h"
#include "compat.h"

#include <libaegisub/exception.h>
#include <wx/button.h>
#include <wx/panel.h>
#include <wx/dirdlg.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/timer.h>
#include <wx/wrapsizer.h>
#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

namespace {
using namespace workbench;
struct Result {
	SeriesOverview overview;
	std::string selected;
	std::string notice;
};
class SeriesPage final : public wxPanel {
	Manifest series;
	Path database;
	std::function<Folders()> project_template;
	std::function<void(Manifest const&, Manifest)> open_child;
	std::function<void(Manifest const&)> updated;
	std::function<void(wxWindow*)> leave;
	wxStaticText *title, *summary, *message;
	wxListBox* list;
	wxButton *create, *attach, *open, *refresh, *back;
	std::vector<EpisodeState> states;
	wxTimer timer;
	std::future<Result> result;
	std::jthread worker;
	bool writable;
	bool closing = false;

	template<class Action> bool Report(Action action) {
		bool success = false;
		try { action(); success = true; }
		catch (agi::Exception const& e) { message->SetLabelText(to_wx(e.GetMessage())); }
		catch (std::exception const& e) { message->SetLabelText(to_wx(std::string(e.what()))); }
		message->Wrap(std::max(200, GetClientSize().x - FromDIP(24)));
		Layout();
		return success;
	}
	void Selection() {
		auto i = list->GetSelection();
		bool selected = i != wxNOT_FOUND && size_t(i) < states.size();
		open->Enable(selected && !worker.joinable() && states[i].error.empty());
		message->SetLabelText(selected ? to_wx(states[i].error) : wxString{});
	}
	void ShowRows(Result loaded) {
		series = std::move(loaded.overview.series);
		states = std::move(loaded.overview.states);
		title->SetLabelText(to_wx(series.name));
		list->Clear();
		unsigned done = 0, unknown = 0;
		for (size_t i = 0; i < states.size(); ++i) {
			auto const& ep = series.episodes[i];
			auto const& state = states[i];
			done += state.status == "done";
			unknown += !state.error.empty();
			auto label = state.error.empty() ? WorkbenchStatusLabel(state.status) : _("Status unknown");
			auto row = list->Append(to_wx(ep.label) + "  —  " + to_wx(state.name) + "  —  " + label + "  —  " + to_wx(ep.relative.generic_string()));
			if (ep.project_id == loaded.selected) list->SetSelection(row);
		}
		summary->SetLabelText(wxString::Format(_("%u of %u episodes completed, %u unknown"), done, unsigned(states.size()), unknown));
		Selection();
		updated(series);
		if (!loaded.notice.empty()) message->SetLabelText(to_wx(loaded.notice));
	}
	template<class Action> void Start(Action action) {
		if (worker.joinable()) return;
		std::packaged_task<Result(std::stop_token)> work(std::move(action));
		result = work.get_future();
		worker = std::jthread(std::move(work));
		create->Disable(); attach->Disable(); open->Disable(); refresh->Disable(); list->Disable();
		message->SetLabelText(_("Reading series episodes…"));
		timer.Start(60);
	}
	void ReloadSeries() {
		auto i = list->GetSelection();
		auto id = i == wxNOT_FOUND ? std::string{} : series.episodes.at(i).project_id;
		Start([s=series, id](std::stop_token stop) { return Result{InspectSeries(s, stop), id, {}}; });
	}
	void Add(bool make) {
		Path source;
		std::string name;
		Folders folders;
		if (make) {
			wxTextEntryDialog prompt(this, _("Name of the new project folder"), _("New Episode"));
			if (prompt.ShowModal() != wxID_OK) return;
			name = from_wx(prompt.GetValue());
			folders = project_template();
		}
		else {
			wxDirDialog picker(this, _("Choose a project inside this series"), to_wx(series.root), wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
			if (picker.ShowModal() != wxID_OK) return;
			source = from_wx(picker.GetPath());
		}
		wxTextEntryDialog number(this, _("Episode number or label"), _("Series Episodes"), wxString::Format("%zu", series.episodes.size() + 1));
		if (number.ShowModal() != wxID_OK) return;
		auto label = from_wx(number.GetValue().Strip(wxString::both));
		if (label.empty()) { message->SetLabelText(_("Enter an episode number or label.")); return; }
		back->Disable();
		Start([s=series, db=database, make, name, source, folders, label](std::stop_token stop) {
			Index index(db);
			auto child = make ? CreateEpisodeProject(s, name, folders) : Open(source);
			std::string notice;
			try { index.AddEpisode(s, child, label); }
			catch (agi::Exception const& e) { notice = e.GetMessage(); }
			catch (std::exception const& e) { notice = e.what(); }
			if (make && !notice.empty())
				notice += "\nProject saved at " + child.root.string() + ". Use Add Existing Episode to retry.";
			return Result{InspectSeries(s, stop), child.id, std::move(notice)};
		});
	}
	void Leave() {
		if (closing) return;
		closing = true;
		leave(this);
		if (worker.joinable()) worker.request_stop();
		else CallAfter([this] { Destroy(); });
	}
	void OpenSelected() {
		if (closing || !open->IsEnabled()) return;
		auto project = OpenEpisode(series, series.episodes.at(list->GetSelection()));
		open_child(series, std::move(project));
		Leave();
	}
public:
	SeriesPage(wxWindow* parent, Manifest const& s, Path const& db, bool writable, std::function<Folders()> tpl, std::function<void(Manifest const&, Manifest)> select,
	             std::function<void(Manifest const&)> notify, std::function<void(wxWindow*)> on_leave)
	: wxPanel(parent), series(s), database(db), project_template(std::move(tpl)), open_child(std::move(select)), updated(std::move(notify)), leave(std::move(on_leave)), timer(this), writable(writable) {
		SetName("workbench-series");
		auto box = new wxBoxSizer(wxVERTICAL);
		title = new wxStaticText(this, wxID_ANY, to_wx(s.name));
		title->SetFont(title->GetFont().Larger().Bold());
		box->Add(title, wxSizerFlags().Border(wxALL, FromDIP(8)));
		summary = new wxStaticText(this, wxID_ANY, {}); summary->SetName("series-progress");
		box->Add(summary, wxSizerFlags().Expand().Border(wxALL, FromDIP(8)));
		list = new wxListBox(this, wxID_ANY); list->SetName("series-list");
		box->Add(list, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, FromDIP(8)));
		auto buttons = new wxWrapSizer;
		auto button = [&](wxString const& label, char const* name, auto action) {
			auto b = new wxButton(this, wxID_ANY, label); b->SetName(name);
			b->Bind(wxEVT_BUTTON, [this, action](wxCommandEvent&) { Report(action); });
			buttons->Add(b, wxSizerFlags().Border(wxRIGHT, FromDIP(5)));
			return b;
		};
		back = button(_("Back to Workbench"), "series-back", [this] { Leave(); });
		create = button(_("New Episode"), "series-create", [this] { Add(true); });
		attach = button(_("Add Existing Episode"), "series-add", [this] { Add(false); });
		open = button(_("Open Episode"), "series-open", [this] { OpenSelected(); });
		refresh = button(_("Refresh"), "series-refresh", [this] { ReloadSeries(); });
		box->Add(buttons, wxSizerFlags().Expand().Border(wxALL, FromDIP(8)));
		message = new wxStaticText(this, wxID_ANY, {}); message->SetName("series-status");
		box->Add(message, wxSizerFlags().Expand().Border(wxALL, FromDIP(8)));
		SetSizer(box);
		list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { Report([&] { Selection(); }); });
		list->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { Report([&] { OpenSelected(); }); });
		Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
			if (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
			timer.Stop(); worker.join();
			if (closing) { CallAfter([this] { Destroy(); }); return; }
			back->Enable(); create->Enable(this->writable); attach->Enable(this->writable); refresh->Enable(); list->Enable();
			Selection();
			if (!Report([&] { ShowRows(result.get()); })) {
				list->Clear(); states.clear(); open->Disable();
				summary->SetLabelText(_("Refresh to check series progress."));
			}
		}, timer.GetId());
		ReloadSeries();
	}

};
}
wxWindow* CreateWorkbenchSeries(wxWindow* parent, workbench::Manifest const& series, workbench::Path const& database, bool writable,
                         std::function<workbench::Folders()> project_template, std::function<void(workbench::Manifest const&, workbench::Manifest)> open_child,
                         std::function<void(workbench::Manifest const&)> updated, std::function<void(wxWindow*)> leave) {
	return new SeriesPage(parent, series, database, writable, std::move(project_template), std::move(open_child), std::move(updated), std::move(leave));
}

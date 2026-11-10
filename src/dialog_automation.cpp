// Copyright (c) 2006, Niels Martin Hansen
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

#include "auto4_base.h"
#include "utils.h"
#include "compat.h"
#include "command/command.h"
#include "dialog_manager.h"
#include "format.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "flat_list.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "theme.h"

#include <libaegisub/signal.h>

#include <algorithm>
#include <boost/range/algorithm/transform.hpp>
#include <vector>

#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>

namespace {
/// Struct to attach a flag for global/local to scripts
struct ExtraScriptInfo {
	Automation4::Script *script;
	bool is_global;
};

class DialogAutomation final : public wxDialog {
	agi::Context *context;

	/// Currently loaded scripts
	std::vector<ExtraScriptInfo> script_info;

	/// File-local script manager
	Automation4::ScriptManager *local_manager;

	/// Listener for external changes to the local scripts
	agi::signal::Connection local_scripts_changed;

	/// Global script manager
	Automation4::ScriptManager *global_manager;

	/// Listener for external changes to the global scripts
	agi::signal::Connection global_scripts_changed;


	/// List of loaded scripts
	FlatList *list;

	/// Unload a local script
	FlatButton *remove_button;

	/// Reload a script
	FlatButton *reload_button;

	void RebuildList();
	void AddScript(Automation4::Script *script, bool is_global);
	/// The script chosen, if there is one
	ExtraScriptInfo *Chosen();
	void UpdateDisplay();

	void OnAdd(wxCommandEvent &);
	void OnRemove(wxCommandEvent &);
	void OnReload(wxCommandEvent &);

	void OnInfo(wxCommandEvent &);
	void OnReloadAutoload(wxCommandEvent &);

public:
	DialogAutomation(agi::Context *context);
};

DialogAutomation::DialogAutomation(agi::Context *c)
: wxDialog(c->parent, -1, _("Automation Manager"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
, context(c)
, local_manager(c->local_scripts.get())
, local_scripts_changed(local_manager->AddScriptChangeListener(&DialogAutomation::RebuildList, this))
, global_manager(config::global_scripts)
, global_scripts_changed(global_manager->AddScriptChangeListener(&DialogAutomation::RebuildList, this))
{
	SetIcons(GETICONS(automation_toolbutton));
	theme::UseSurface(this);
	int gap = FromDIP(10), row = FromDIP(6);

	// Each script: its name, its file and description under it, and at the
	// right whether it is global or local, or how its loading went wrong
	list = new FlatList(this, false, false, _("No scripts loaded"));
	list->SetMinSize(FromDIP(wxSize(600, 240)));
	auto add_button = new FlatButton(this, _("&Add"));
	remove_button = new FlatButton(this, _("&Remove"));
	reload_button = new FlatButton(this, _("Re&load"));
	auto info_button = new FlatButton(this, _("Show &Info"));
	auto reload_autoload_button = new FlatButton(this, _("Re&scan Autoload Dir"));

	list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { UpdateDisplay(); });
	list->Bind(wxEVT_LISTBOX_DCLICK, &DialogAutomation::OnInfo, this);
	add_button->Bind(wxEVT_BUTTON, &DialogAutomation::OnAdd, this);
	remove_button->Bind(wxEVT_BUTTON, &DialogAutomation::OnRemove, this);
	reload_button->Bind(wxEVT_BUTTON, &DialogAutomation::OnReload, this);
	info_button->Bind(wxEVT_BUTTON, &DialogAutomation::OnInfo, this);
	reload_autoload_button->Bind(wxEVT_BUTTON, &DialogAutomation::OnReloadAutoload, this);

	// What is done with the scripts at the left, the autoload folder at the right
	wxSizer *button_box = new wxBoxSizer(wxHORIZONTAL);
	button_box->Add(add_button);
	button_box->Add(remove_button, wxSizerFlags().Border(wxLEFT, row));
	button_box->Add(reload_button, wxSizerFlags().Border(wxLEFT, gap));
	button_box->Add(info_button, wxSizerFlags().Border(wxLEFT, row));
	button_box->AddStretchSpacer(1);
	button_box->Add(reload_autoload_button, wxSizerFlags().Border(wxLEFT, gap));

	auto buttons = new flat::DialogButtons(this, wxCANCEL | wxHELP, "Automation Manager");
	buttons->Get(wxID_CANCEL)->SetLabel(_("&Close"));
	// Enter closes, as Escape does
	SetAffirmativeId(wxID_CANCEL);

	wxSizer *main_box = new wxBoxSizer(wxVERTICAL);
	main_box->Add(list, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	auto button_row = new wxBoxSizer(wxVERTICAL);
	button_row->Add(button_box, wxSizerFlags().Expand().Border(wxTOP, row));
	main_box->Add(button_row, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, gap));
	main_box->Add(buttons, wxSizerFlags().Expand().Border(wxALL, gap));
	SetSizerAndFit(main_box);
	Center();

	RebuildList();
}

void DialogAutomation::RebuildList()
{
	// The script chosen stays chosen as the list is made again
	auto chosen = Chosen();
	auto chosen_script = chosen ? chosen->script : nullptr;
	script_info.clear();

	for (auto& script : local_manager->GetScripts()) AddScript(script.get(), false);
	for (auto& script : global_manager->GetScripts()) AddScript(script.get(), true);

	std::vector<FlatList::Row> rows;
	for (auto const& ei : script_info) {
		auto script = ei.script;
		FlatList::Row row{to_wx(script->GetName()), wxString(), wxString()};
		row.detail = script->GetPrettyFilename().wstring();
		auto description = flat::OneLine(to_wx(script->GetDescription()));
		if (!description.empty()) row.detail += L" \u2014 " + description;
		if (!script->GetLoadedState()) {
			row.note = _("Failed to load");
			row.warn = true;
		}
		else if (!script->GetWarnings().empty()) {
			row.note = _("Loaded with warnings");
			row.warn = true;
		}
		else
			row.note = ei.is_global ? wxGETTEXT_IN_CONTEXT("automation script", "Global") : wxGETTEXT_IN_CONTEXT("automation script", "Local");
		rows.push_back(std::move(row));
	}
	list->Set(std::move(rows));
	for (size_t i = 0; i < script_info.size(); ++i)
		if (script_info[i].script == chosen_script) list->SetSelection(static_cast<int>(i));

	UpdateDisplay();
}

void DialogAutomation::AddScript(Automation4::Script *script, bool is_global)
{
	script_info.push_back({script, is_global});
}

ExtraScriptInfo *DialogAutomation::Chosen()
{
	int i = list->GetSelection();
	return i >= 0 && i < static_cast<int>(script_info.size()) ? &script_info[i] : nullptr;
}

void DialogAutomation::UpdateDisplay()
{
	auto ei = Chosen();
	remove_button->Enable(ei && !ei->is_global);
	reload_button->Enable(ei != nullptr);
}

template<class Container>
static bool has_file(Container const& c, agi::fs::path const& fn)
{
	return any_of(c.begin(), c.end(),
		[&](std::unique_ptr<Automation4::Script> const& s) { return fn == s->GetFilename(); });
}

void DialogAutomation::OnAdd(wxCommandEvent &)
{
	wxFileDialog diag(this,
		_("Add Automation script"),
		to_wx(OPT_GET("Path/Last/Automation")->GetString()),
		"",
		to_wx(Automation4::ScriptFactory::GetWildcardStr()),
		wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);

	if (diag.ShowModal() == wxID_CANCEL) return;

	wxArrayString fnames;
	diag.GetPaths(fnames);

	for (auto const& fname : fnames) {
		agi::fs::path fnpath(fname.wx_str());
		OPT_SET("Path/Last/Automation")->SetString(fnpath.parent_path().string());

		if (has_file(local_manager->GetScripts(), fnpath) || has_file(global_manager->GetScripts(), fnpath)) {
			wxLogError(fmt_tl("Script '%s' is already loaded", fname));
			continue;
		}

		local_manager->Add(Automation4::ScriptFactory::CreateFromFile(fnpath, true));
	}
}

void DialogAutomation::OnRemove(wxCommandEvent &)
{
	auto ei = Chosen();
	if (!ei || ei->is_global) return;

	local_manager->Remove(ei->script);
}

void DialogAutomation::OnReload(wxCommandEvent &)
{
	auto ei = Chosen();
	if (!ei) return;

	if (ei->is_global)
		global_manager->Reload(ei->script);
	else
		local_manager->Reload(ei->script);
}

void DialogAutomation::OnInfo(wxCommandEvent &)
{
	ExtraScriptInfo *ei = Chosen();

	wxArrayString info;
	std::back_insert_iterator<wxArrayString> append_info(info);

	info.push_back(fmt_tl(
		"Total scripts loaded: %d\nGlobal scripts loaded: %d\nLocal scripts loaded: %d\n",
		local_manager->GetScripts().size() + global_manager->GetScripts().size(),
		global_manager->GetScripts().size(),
		local_manager->GetScripts().size()));

	info.push_back(_("Scripting engines installed:"));
	boost::transform(Automation4::ScriptFactory::GetFactories(), append_info,
		[](std::unique_ptr<Automation4::ScriptFactory> const& f) {
			return fmt_wx("- %s (%s)", f->GetEngineName(), f->GetFilenamePattern());
		});

	if (ei) {
		info.push_back(fmt_tl("\nScript info:\nName: %s\nDescription: %s\nAuthor: %s\nVersion: %s\nFull path: %s\nState: %s\n",
			ei->script->GetName(),
			ei->script->GetDescription(),
			ei->script->GetAuthor(),
			ei->script->GetVersion(),
			ei->script->GetFilename().wstring(),
			ei->script->GetLoadedState() ? (ei->script->GetWarnings().empty() ? _("Correctly loaded") : _("Loaded with warnings")) : _("Failed to load")));

		boost::transform(ei->script->GetWarnings(), append_info, [](std::string s) {
			return fmt_tl("Warning: %s\n", s);
		});

		info.push_back(_("Features provided by script:\n"));

		boost::transform(ei->script->GetMacros(), append_info, [this](const cmd::Command *f) {
			return fmt_tl("    Macro: %s (%s)", f->StrDisplay(context), f->name());
		});
		boost::transform(ei->script->GetFilters(), append_info, [](const Automation4::ExportFilter* f) {
			return fmt_tl("    Export filter: %s", f->GetName());
		});
	}

	wxMessageBox(wxJoin(info, '\n', 0), _("Automation Script Info"));
}

void DialogAutomation::OnReloadAutoload(wxCommandEvent &)
{
	global_manager->Reload();
}
}

void ShowAutomationDialog(agi::Context *c) {
	c->dialog->Show<DialogAutomation>(c);
}

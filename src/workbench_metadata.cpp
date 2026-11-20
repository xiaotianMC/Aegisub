// SPDX-License-Identifier: ISC
#include "workbench_metadata.h"
#include "compat.h"

#include <libaegisub/exception.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>
#include <array>
#include <sstream>

wxString WorkbenchStatusLabel(std::string const& status) {
	if (status == "todo") return _("Not started");
	if (status == "doing") return _("In progress");
	if (status == "review") return _("Ready for review");
	if (status == "done") return _("Completed");
	if (status == "paused") return _("Paused");
	return to_wx(status);
}

std::optional<workbench::Manifest> EditWorkbenchMetadata(wxWindow* parent, workbench::Manifest const& project) {
	wxDialog dialog(parent, wxID_ANY, _("Project Details") + " — " + to_wx(project.name), wxDefaultPosition, wxDefaultSize,
	                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	dialog.SetName("workbench-metadata");
	auto outer = new wxBoxSizer(wxVERTICAL);
	auto form = new wxFlexGridSizer(2, dialog.FromDIP(6), dialog.FromDIP(12));
	form->AddGrowableCol(1);
	auto label = [&](wxString const& value) {
		form->Add(new wxStaticText(&dialog, wxID_ANY, value), wxSizerFlags().CenterVertical());
	};
	auto field = [&](wxString const& title, char const* name, std::string const& value, bool multiline = false) {
		label(title);
		auto text = new wxTextCtrl(&dialog, wxID_ANY, to_wx(value), wxDefaultPosition, wxDefaultSize, multiline ? wxTE_MULTILINE : 0);
		text->SetName(name);
		text->SetMinSize(dialog.FromDIP(wxSize(300, multiline ? 62 : -1)));
		form->Add(text, wxSizerFlags().Expand());
		return text;
	};
	auto const& current = project.metadata;
	auto description = field(_("Description"), "metadata-description", current.description, true);
	auto source = field(_("Source URL or video ID"), "metadata-source", current.source);
	auto original = field(_("Source language"), "metadata-source-language", current.source_language);
	auto target = field(_("Target language"), "metadata-target-language", current.target_language);
	auto owner = field(_("Project owner"), "metadata-owner", current.owner);
	std::string joined;
	for (auto const& tag : current.tags) { if (!joined.empty()) joined += '\n'; joined += tag; }
	auto tags = field(_("Tags (one per line)"), "metadata-tags", joined, true);
	label(_("Production status"));
	auto status = new wxChoice(&dialog, wxID_ANY);
	status->SetName("metadata-status");
	std::array<char const*, 5> states{"todo", "doing", "review", "done", "paused"};
	for (auto state : states) {
		auto index = status->Append(WorkbenchStatusLabel(state));
		if (current.status == state) status->SetSelection(index);
	}
	form->Add(status, wxSizerFlags().Expand());
	outer->Add(form, wxSizerFlags(1).Expand().Border(wxALL, dialog.FromDIP(12)));
	auto error = new wxStaticText(&dialog, wxID_ANY, {});
	outer->Add(error, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, dialog.FromDIP(12)));
	outer->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL, dialog.FromDIP(10)));
	dialog.SetSizerAndFit(outer);
	while (dialog.ShowModal() == wxID_OK) {
		workbench::Metadata values;
		values.description = from_wx(description->GetValue());
		values.source = from_wx(source->GetValue());
		values.source_language = from_wx(original->GetValue());
		values.target_language = from_wx(target->GetValue());
		values.owner = from_wx(owner->GetValue());
		values.status = states.at(status->GetSelection());
		std::istringstream lines(from_wx(tags->GetValue()));
		for (std::string tag; std::getline(lines, tag);) {
			if (tag.ends_with('\r')) tag.pop_back();
			if (!tag.empty()) values.tags.push_back(std::move(tag));
		}
		try { return workbench::UpdateMetadata(project, values); }
		catch (agi::Exception const& e) { error->SetLabelText(to_wx(e.GetMessage())); }
		catch (std::exception const& e) { error->SetLabelText(to_wx(std::string(e.what()))); }
		error->Wrap(std::max(200, dialog.GetClientSize().x - dialog.FromDIP(24)));
		dialog.Fit();
	}
	return {};
}

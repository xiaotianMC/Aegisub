#include "subtitle_project_access.h"
// Copyright (c) 2005, Rodrigo Braz Monteiro, Niels Martin Hansen
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

#include "ass_exporter.h"
#include "ass_file.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "flat_list.h"
#include "libresrc/libresrc.h"
#include "subs_controller.h"
#include "subtitle_format.h"
#include "theme.h"
#include "utils.h"

#include <libaegisub/charset_conv.h>
#include <libaegisub/log.h>
#include <libaegisub/split.h>

#include <algorithm>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace {
class DialogExport {
	wxDialog d;
	agi::Context *c;

	/// The export transform engine
	AssExporter exporter;

	/// A list of all registered export filters, each with its description
	FlatList *filter_list;

	/// A list of available target charsets
	FlatChoice *charset_list;

	wxSizer *opt_sizer;

	void OnProcess(wxCommandEvent &);

	/// Set all the checkboxes
	void SetAll(bool new_value);
	/// Update which options sizers are shown
	void RefreshOptions();

public:
	DialogExport(agi::Context *c);
	~DialogExport();
	void ShowModal() { d.ShowModal(); }
};

// Swap the items at idx and idx + 1
void swap(FlatList *list, int idx, int sel_dir) {
	if (idx < 0 || idx + 1 >= (int)list->GetCount()) return;

	auto row = list->GetRow(idx);
	bool checked = list->IsChecked(idx);
	list->SetRow(idx, list->GetRow(idx + 1));
	list->Check(idx, list->IsChecked(idx + 1));
	list->SetRow(idx + 1, row);
	list->Check(idx + 1, checked);
	list->SetSelection(idx + sel_dir);
}

DialogExport::DialogExport(agi::Context *c)
: d(c->parent, -1, _("Export"), wxDefaultPosition, DipSize(200, 100), wxCAPTION | wxCLOSE_BOX)
, c(c)
, exporter(c)
{
	d.SetIcons(GETICONS(export_menu));
	d.SetExtraStyle(wxWS_EX_VALIDATE_RECURSIVELY);

	theme::UseSurface(&d);
	int gap = d.FromDIP(10), row = d.FromDIP(6);

	// The filters, each ticked to run, in the order they run
	std::vector<std::string> filters = exporter.GetAllFilterNames();
	filter_list = new FlatList(&d, false, true);
	std::vector<FlatList::Row> rows;
	for (auto const& name : filters)
		rows.push_back({to_wx(name), to_wx(exporter.GetDescription(name)), wxString()});
	filter_list->Set(std::move(rows));
	filter_list->SetMinSize(DipSize(340, 220));
	filter_list->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent&) { RefreshOptions(); });

	// Get selected filters
	std::string const selected = c->subtitleAccess->Read().Properties.export_filters;
	for (auto token : agi::Split(selected, '|')) {
		auto it = find(begin(filters), end(filters), token);
		if (it != end(filters))
			filter_list->Check(distance(begin(filters), it));
	}

	auto btn_up = new FlatButton(&d, _("Move &Up"));
	auto btn_down = new FlatButton(&d, _("Move &Down"));
	auto btn_all = new FlatButton(&d, _("Select &All"));
	auto btn_none = new FlatButton(&d, _("Select &None"));

	btn_up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { swap(filter_list, filter_list->GetSelection() - 1, 0); });
	btn_down->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { swap(filter_list, filter_list->GetSelection(), 1); });
	btn_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetAll(true); });
	btn_none->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetAll(false); });

	wxSizer *top_buttons = new wxBoxSizer(wxHORIZONTAL);
	top_buttons->Add(btn_up, wxSizerFlags(1).Expand());
	top_buttons->Add(btn_down, wxSizerFlags(1).Expand().Border(wxLEFT, row));
	top_buttons->Add(btn_all, wxSizerFlags(1).Expand().Border(wxLEFT, gap));
	top_buttons->Add(btn_none, wxSizerFlags(1).Expand().Border(wxLEFT, row));

	// The text encoding written
	auto charset_label = new wxStaticText(&d, -1, wxString(_("Text encoding:")).BeforeLast(':'));
	charset_label->SetForegroundColour(flat::Colours(&d).dim);
	charset_list = new FlatChoice(&d, wxString(_("Text encoding:")).BeforeLast(':'));
	auto encodings = agi::charset::GetEncodingsList<wxArrayString>();
	charset_list->Set(std::vector<wxString>(encodings.begin(), encodings.end()));
	charset_list->SetStringSelection(to_wx(c->subtitleAccess->Read().Properties.export_encoding));
	if (charset_list->GetSelection() < 0)
		charset_list->SetStringSelection("Unicode (UTF-8)");
	wxSizer *charset_list_sizer = new wxBoxSizer(wxHORIZONTAL);
	charset_list_sizer->Add(charset_label, wxSizerFlags().CenterVertical());
	charset_list_sizer->Add(charset_list, wxSizerFlags(1).Expand().Border(wxLEFT, gap));

	auto top_sizer = new wxBoxSizer(wxVERTICAL);
	top_sizer->Add(flat::SectionTitle(&d, _("Filters")), wxSizerFlags().Expand());
	top_sizer->Add(filter_list, wxSizerFlags(1).Expand().Border(wxTOP, row));
	top_sizer->Add(top_buttons, wxSizerFlags().Expand().Border(wxTOP, row));
	top_sizer->Add(charset_list_sizer, wxSizerFlags().Expand().Border(wxTOP, gap));

	auto btn_sizer = new flat::DialogButtons(&d, wxOK | wxCANCEL | wxHELP, "Export");
	btn_sizer->Get(wxID_OK)->SetLabel(_("Export..."));
	d.Bind(wxEVT_BUTTON, &DialogExport::OnProcess, this, wxID_OK);

	wxSizer *horz_sizer = new wxBoxSizer(wxHORIZONTAL);
	opt_sizer = new wxBoxSizer(wxVERTICAL);
	exporter.DrawSettings(&d, opt_sizer);
	horz_sizer->Add(top_sizer, wxSizerFlags().Expand());
	horz_sizer->Add(opt_sizer, wxSizerFlags(1).Expand().Border(wxLEFT, d.FromDIP(16)));

	wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(horz_sizer, wxSizerFlags(1).Expand().Border(wxALL, gap));
	main_sizer->Add(btn_sizer, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, gap));
	d.SetSizerAndFit(main_sizer);
	RefreshOptions();
	d.CenterOnParent();
}

DialogExport::~DialogExport() {
	std::string filters;
	for (size_t i = 0; i < filter_list->GetCount(); ++i) {
		if (filter_list->IsChecked(i)) {
			if (!filters.empty())
				filters += "|";
			filters += from_wx(filter_list->GetString(i));
		}
	}
	// C1-C1c-11: a destructor must not throw. Metadata is sealed while a
	// subtitle model installs or awaits recovery, and metadata observers may
	// throw; the remembered filter selection is then simply not updated.
	if (c->subsController->IsInstalling() || c->subtitleEditGate.NeedsRebinding())
		return;
	try {
		c->subtitleAccess->SetMetadata(SubtitleProjectAccess::MetadataField::ExportFilters, std::move(filters));
	}
	catch (std::exception const& e) {
		LOG_W("dialog/export") << "Export filter selection not stored: " << e.what();
	}
	catch (...) {
		LOG_W("dialog/export") << "Export filter selection not stored";
	}
}

void DialogExport::OnProcess(wxCommandEvent &) {
	if (!d.TransferDataFromWindow()) return;

	auto filename = SaveFileSelector(c, _("Export Subtitles File"), "", "", "", SubtitleFormat::GetWildcards(1), &d);
	if (filename.empty()) return;

	for (size_t i = 0; i < filter_list->GetCount(); ++i) {
		if (filter_list->IsChecked(i))
			exporter.AddFilter(from_wx(filter_list->GetString(i)));
	}

	try {
		wxBusyCursor busy;
		// Export only reads the document; remembering the encoding is
		// best-effort and skipped during a comparison (C1-C1c-12).
		try {
			c->subtitleAccess->SetMetadata(SubtitleProjectAccess::MetadataField::ExportEncoding, from_wx(charset_list->GetStringSelection()));
		}
		catch (SubtitleProjectBusy const&) { }
		exporter.Export(filename, charset_list->GetStringSelection().utf8_str().data(), &d);
	}
	catch (agi::UserCancelException const&) { }
	catch (agi::Exception const& err) {
		wxMessageBox(to_wx(err.GetMessage()), _("Error exporting subtitles"), wxOK | wxICON_ERROR | wxCENTER, &d);
	}
	catch (std::exception const& err) {
		wxMessageBox(to_wx(err.what()), _("Error exporting subtitles"), wxOK | wxICON_ERROR | wxCENTER, &d);
	}
	catch (...) {
		wxMessageBox(_("Unknown error"), _("Error exporting subtitles"), wxOK | wxICON_ERROR | wxCENTER, &d);
	}

	d.EndModal(0);
}

void DialogExport::SetAll(bool new_value) {
	for (unsigned i = 0; i < filter_list->GetCount(); ++i)
		filter_list->Check(i, new_value);

	RefreshOptions();
}

void DialogExport::RefreshOptions() {
	for (size_t i = 0; i < filter_list->GetCount(); ++i) {
		if (wxSizer *sizer = exporter.GetSettingsSizer(from_wx(filter_list->GetString(i))))
			opt_sizer->Show(sizer, filter_list->IsChecked(i), true);
	}
	d.Layout();
	d.GetSizer()->Fit(&d);
}
}

void ShowExportDialog(agi::Context *c) {
	DialogExport(c).ShowModal();
}

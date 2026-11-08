// Copyright (c) 2006, Rodrigo Braz Monteiro
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

#include "flat_controls.h"
#include "flat_dialog.h"
#include "options.h"
#include "theme.h"

#include <algorithm>
#include <array>
#include <functional>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace {
struct DialogPasteOver {
	wxDialog d;
	std::array<FlatCheck*, 11> fields;

	void Check(std::initializer_list<size_t> which);
	void OnOK(wxCommandEvent &);

	DialogPasteOver(wxWindow *parent);
};

DialogPasteOver::DialogPasteOver(wxWindow *parent)
: d(parent, -1, _("Select Fields to Paste Over"))
{
	theme::UseSurface(&d);
	auto prompt = new wxStaticText(&d, -1, _("Please select the fields that you want to paste over:"));
	prompt->SetForegroundColour(flat::Colours(&d).dim);

	const wxString names[] = {_("Comment"), _("Layer"), _("Start Time"), _("End Time"), _("Style"), _("Actor"),
		_("Margin Left"), _("Margin Right"), _("Margin Vertical"), _("Effect"), _("Text")};
	std::vector<bool> options = OPT_GET("Tool/Paste Lines Over/Fields")->GetListBool();
	options.resize(fields.size(), false);
	for (size_t i = 0; i < fields.size(); ++i) {
		fields[i] = new FlatCheck(&d, names[i]);
		fields[i]->SetValue(options[i]);
	}

	// In the columns of the line: its kind and times, who and how, where, and what
	auto grid = new wxFlexGridSizer(3, d.FromDIP(4), d.FromDIP(18));
	const size_t order[] = {0, 4, 6, 1, 5, 7, 2, 9, 8, 3, 10};
	for (size_t i : order) grid->Add(fields[i]);

	auto quick = new wxBoxSizer(wxHORIZONTAL);
	auto add_quick = [&](wxString const& label, std::function<void()> action) {
		auto button = new FlatButton(&d, label);
		button->Bind(wxEVT_BUTTON, [action](wxCommandEvent &) { action(); });
		quick->Add(button, wxSizerFlags().Border(wxRIGHT, d.FromDIP(6)));
	};
	add_quick(_("&All"), [this] { for (auto field : fields) field->SetValue(true); });
	add_quick(_("&None"), [this] { Check({}); });
	add_quick(_("&Times"), [this] { Check({2, 3}); });
	add_quick(_("T&ext"), [this] { Check({10}); });

	d.Bind(wxEVT_BUTTON, &DialogPasteOver::OnOK, this, wxID_OK);

	wxSizer *MainSizer = new wxBoxSizer(wxVERTICAL);
	MainSizer->Add(prompt, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, d.FromDIP(10)));
	MainSizer->Add(grid, wxSizerFlags().Expand().Border(wxALL, d.FromDIP(10)));
	MainSizer->Add(quick, wxSizerFlags().Border(wxLEFT | wxRIGHT, d.FromDIP(10)));
	MainSizer->Add(new flat::DialogButtons(&d, wxOK | wxCANCEL | wxHELP, "Paste Over"), wxSizerFlags().Expand().Border(wxALL, d.FromDIP(10)));
	d.SetSizerAndFit(MainSizer);
	d.CenterOnParent();
}

void DialogPasteOver::OnOK(wxCommandEvent &) {
	std::vector<bool> options;
	for (auto field : fields)
		options.push_back(field->GetValue());
	OPT_SET("Tool/Paste Lines Over/Fields")->SetListBool(std::move(options));

	d.EndModal(wxID_OK);
}

/// Tick only the fields @p which
void DialogPasteOver::Check(std::initializer_list<size_t> which) {
	for (size_t i = 0; i < fields.size(); ++i)
		fields[i]->SetValue(std::find(which.begin(), which.end(), i) != which.end());
}
}

bool ShowPasteOverDialog(wxWindow *parent) {
	return DialogPasteOver(parent).d.ShowModal() == wxID_OK;
}

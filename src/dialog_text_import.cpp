// Copyright (c) 2007, Alysson Souza e Silva
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

#include "compat.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "options.h"
#include "theme.h"

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

/// A simple dialog to let the user select the format of a plain text file
/// being imported into Aegisub
bool ShowPlainTextImportDialog() {
	wxDialog d(nullptr, -1, _("Text import options"));
	theme::UseSurface(&d);

	// Each mark named in its field
	auto make_field = [&](wxString const& name, std::string const& value) {
		auto frame = new FlatFrame(&d, flat::WithoutColon(name));
		auto text = new wxTextCtrl(frame, -1, to_wx(value), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		frame->SetContent(text, 60);
		return std::make_pair(frame, text);
	};
	auto [separator_frame, separator] = make_field(_("Actor separator:"), OPT_GET("Tool/Import/Text/Actor Separator")->GetString());
	auto [comment_frame, comment] = make_field(_("Comment starter:"), OPT_GET("Tool/Import/Text/Comment Starter")->GetString());
	auto include_blank = new FlatCheck(&d, _("Include blank lines"));
	include_blank->SetValue(OPT_GET("Tool/Import/Text/Include Blank")->GetBool());

	int gap = d.FromDIP(10), row = d.FromDIP(6);
	auto fields = new wxBoxSizer(wxHORIZONTAL);
	fields->Add(separator_frame, wxSizerFlags(1).Expand());
	fields->Add(comment_frame, wxSizerFlags(1).Expand().Border(wxLEFT, row));

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(fields, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	main_sizer->Add(include_blank, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	main_sizer->Add(new flat::DialogButtons(&d, wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL, gap));
	d.SetSizerAndFit(main_sizer);
	d.CenterOnParent();

	d.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
		OPT_SET("Tool/Import/Text/Actor Separator")->SetString(from_wx(separator->GetValue()));
		OPT_SET("Tool/Import/Text/Comment Starter")->SetString(from_wx(comment->GetValue()));
		OPT_SET("Tool/Import/Text/Include Blank")->SetBool(include_blank->GetValue());
		d.EndModal(wxID_OK);
	}, wxID_OK);

	return d.ShowModal() == wxID_OK;
}

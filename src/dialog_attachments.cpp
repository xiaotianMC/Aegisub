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

#include "ass_attachment.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "subtitle_value_edit.h"
#include "compat.h"
#include "format.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "flat_list.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "theme.h"
#include "utils.h"

#include <algorithm>
#include <optional>

#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>

namespace {
bool is_safe_attachment_filename(std::string const& filename) {
	if (filename.empty() || filename == "." || filename == "..")
		return false;
	if (filename.find_first_of("/\\:") != std::string::npos)
		return false;
	if (std::any_of(filename.begin(), filename.end(), [](unsigned char c) { return c < 0x20; }))
		return false;

#ifdef _WIN32
	if (filename.back() == ' ' || filename.back() == '.')
		return false;

	auto basename = filename.substr(0, filename.find('.'));
	for (auto& c : basename) {
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
	}
	if (basename == "CON" || basename == "PRN" || basename == "AUX" || basename == "NUL" || basename == "CLOCK$")
		return false;
	if (basename.size() == 4 && (basename.starts_with("COM") || basename.starts_with("LPT")) && basename[3] >= '1' && basename[3] <= '9')
		return false;
#endif

	return true;
}

std::optional<agi::fs::path> attachment_destination(agi::fs::path const& directory, std::string const& filename) {
	if (!is_safe_attachment_filename(filename))
		return {};

	return directory / agi::fs::path(filename);
}

struct DialogAttachments {
	wxDialog d;
	agi::Context *c;

	FlatList *listView;
	FlatButton *extractButton;
	FlatButton *deleteButton;

	void OnAttachFont(wxCommandEvent &event);
	void OnAttachGraphics(wxCommandEvent &event);
	void OnExtract(wxCommandEvent &event);
	void OnDelete(wxCommandEvent &event);
	void OnListClick();
	/// The rows chosen, in order
	std::vector<int> Chosen() const;

	void UpdateList();
	void AttachFile(wxFileDialog &diag, wxString const& commit_msg);

public:
	DialogAttachments(agi::Context *c);
};

DialogAttachments::DialogAttachments(agi::Context *c)
: d(c->parent, -1, _("Attachment List"))
, c(c)
{
	d.SetIcons(GETICONS(attach_button));
	theme::UseSurface(&d);
	int gap = d.FromDIP(10), row = d.FromDIP(6);

	// Each attachment with its group under it and its size at the right
	listView = new FlatList(&d, true, false, _("No attachments"));
	listView->SetMinSize(d.FromDIP(wxSize(460, 240)));
	UpdateList();

	auto attachFont = new FlatButton(&d, _("Attach &Font"));
	auto attachGraphics = new FlatButton(&d, _("Attach &Graphics"));
	extractButton = new FlatButton(&d, _("E&xtract"));
	deleteButton = new FlatButton(&d, _("&Delete"));
	extractButton->Enable(false);
	deleteButton->Enable(false);

	// Adding at the left, what is done with those chosen at the right
	auto buttonSizer = new wxBoxSizer(wxHORIZONTAL);
	buttonSizer->Add(attachFont);
	buttonSizer->Add(attachGraphics, wxSizerFlags().Border(wxLEFT, row));
	buttonSizer->AddStretchSpacer(1);
	buttonSizer->Add(extractButton);
	buttonSizer->Add(deleteButton, wxSizerFlags().Border(wxLEFT, row));

	auto buttons = new flat::DialogButtons(&d, wxCANCEL | wxHELP, "Attachment Manager");
	buttons->Get(wxID_CANCEL)->SetLabel(_("&Close"));

	auto mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(listView, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	auto buttonRow = new wxBoxSizer(wxVERTICAL);
	buttonRow->Add(buttonSizer, wxSizerFlags().Expand().Border(wxTOP, row));
	mainSizer->Add(buttonRow, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, gap));
	mainSizer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, gap));
	d.SetSizerAndFit(mainSizer);
	d.CenterOnParent();

	attachFont->Bind(wxEVT_BUTTON, &DialogAttachments::OnAttachFont, this);
	attachGraphics->Bind(wxEVT_BUTTON, &DialogAttachments::OnAttachGraphics, this);
	extractButton->Bind(wxEVT_BUTTON, &DialogAttachments::OnExtract, this);
	deleteButton->Bind(wxEVT_BUTTON, &DialogAttachments::OnDelete, this);

	listView->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { OnListClick(); });
}

void DialogAttachments::UpdateList() {
	std::vector<FlatList::Row> rows;
	for (auto& attach : c->subtitleAccess->Read().Attachments)
		rows.push_back({to_wx(attach.GetFileName(true)), to_wx(attach.GroupHeader()).AfterFirst('[').BeforeLast(']'), PrettySize(attach.GetSize())});
	listView->Set(std::move(rows));
}

std::vector<int> DialogAttachments::Chosen() const {
	wxArrayInt selections;
	listView->GetSelections(selections);
	std::vector<int> chosen(selections.begin(), selections.end());
	std::sort(chosen.begin(), chosen.end());
	return chosen;
}

void DialogAttachments::AttachFile(wxFileDialog &diag, wxString const& commit_msg) {
	if (diag.ShowModal() == wxID_CANCEL) return;
    auto edit_scope = c->subtitleAccess->TryWrite();
    if (!edit_scope) return;
    SubtitleValueEdit staged(c);

	wxArrayString paths;
	diag.GetPaths(paths);

	for (auto const& fn : paths)
		staged.file.InsertAttachment(agi::fs::path(fn.wx_str()));

	staged.Commit(commit_msg, AssFile::COMMIT_ATTACHMENT);

	UpdateList();
}

void DialogAttachments::OnAttachFont(wxCommandEvent &) {
	wxFileDialog diag(&d,
		_("Choose file to be attached"),
		to_wx(OPT_GET("Path/Fonts Collector Destination")->GetString()), "", _("Font Files") + " (*.ttf)|*.ttf",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);

	AttachFile(diag, _("attach font file"));
}

void DialogAttachments::OnAttachGraphics(wxCommandEvent &) {
	wxFileDialog diag(&d,
		_("Choose file to be attached"),
		"", "",
		_("Graphic Files") + " (*.bmp, *.gif, *.jpg, *.ico, *.wmf)|*.bmp;*.gif;*.jpg;*.ico;*.wmf",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);

	AttachFile(diag, _("attach graphics file"));
}

void DialogAttachments::OnExtract(wxCommandEvent &) {
	auto chosen = Chosen();
	if (chosen.empty()) return;
	int first = chosen[0];

	agi::fs::path path;
	bool fullPath = false;

	// Multiple or single?
	if (chosen.size() > 1)
		path = wxDirSelector(_("Select the path to save the files to:"), to_wx(OPT_GET("Path/Fonts Collector Destination")->GetString())).utf8_str().data();
	else {
		auto default_filename = c->subtitleAccess->Read().Attachments[first].GetFileName();
		if (!is_safe_attachment_filename(default_filename))
			default_filename = "attachment";
		path = SaveFileSelector(
			c,
			_("Select the path to save the file to:"),
			"Path/Fonts Collector Destination",
			default_filename,
			"", from_wx(_("All Supported Formats") + " (*.bmp, *.gif, *.jpg, *.ico, *.ttf, *.wmf)|*.bmp;*.gif;*.jpg;*.ico;*.ttf;*.wmf|" +  _("Font Files") + " (*.ttf)|*.ttf|" + _("Graphic Files") + " (*.bmp, *.gif, *.jpg, *.ico, *.wmf)|*.bmp;*.gif;*.jpg;*.ico;*.wmf"),
			&d);
		fullPath = true;
	}
	if (path.empty()) return;

	for (int i : chosen) {
		auto& attach = c->subtitleAccess->Read().Attachments[i];
		auto destination = fullPath ? std::optional<agi::fs::path>(path) : attachment_destination(path, attach.GetFileName());
		if (!destination) {
			wxMessageBox(
				fmt_tl("The attachment \"%s\" was not extracted because its filename is unsafe.", to_wx(attach.GetFileName(true))),
				_("Attachment not extracted"), wxOK | wxICON_WARNING | wxCENTRE, &d);
			continue;
		}
		if (!fullPath && agi::fs::Exists(*destination) &&
			wxMessageBox(
				fmt_tl("The file \"%s\" already exists. Do you want to replace it?", destination->wstring()),
				_("Replace existing file?"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING | wxCENTRE, &d) != wxYES) {
			continue;
		}
		attach.Extract(*destination);
	}
}

void DialogAttachments::OnDelete(wxCommandEvent &) {
    auto edit_scope = c->subtitleAccess->TryWrite();
    if (!edit_scope) return;
    SubtitleValueEdit staged(c);
	size_t removed = 0;
	for (int i : Chosen())
		staged.file.Attachments.erase(staged.file.Attachments.begin() + i - removed++);

	staged.Commit(_("remove attachment"), AssFile::COMMIT_ATTACHMENT);

	UpdateList();
	extractButton->Enable(false);
	deleteButton->Enable(false);
}

void DialogAttachments::OnListClick() {
	bool hasSel = !Chosen().empty();
	extractButton->Enable(hasSel);
	deleteButton->Enable(hasSel);
}
}

void ShowAttachmentsDialog(agi::Context *c) {
	DialogAttachments(c).d.ShowModal();
}

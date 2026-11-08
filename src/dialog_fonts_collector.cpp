// Copyright (c) 2012, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#include "font_file_lister.h"
#include "subtitle_project_access.h"

#include "compat.h"
#include "dialog_manager.h"
#include "format.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "theme.h"
#include "utils.h"
#include "value_event.h"

#include <libaegisub/dispatch.h>
#include <libaegisub/format_path.h>
#include <libaegisub/fs.h>
#include <libaegisub/path.h>

#include <algorithm>

#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>
#include <wx/textctrl.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

namespace {
enum class FcMode {
	CheckFontsOnly = 0,
	CopyToFolder = 1,
	CopyToScriptFolder = 2,
	CopyToZip = 3,
	SymlinkToFolder = 4
};

class DialogFontsCollector final : public wxDialog {
    agi::Context *context;
    agi::Path path;
    std::unique_ptr<agi::dispatch::Queue> queue = agi::dispatch::Create();
	FcMode mode = FcMode::CheckFontsOnly;

	wxStyledTextCtrl *collection_log;
	FlatButton *close_btn;
	FlatButton *dest_browse_button;
	FlatButton *start_btn;
	FlatChoice *collection_mode;
	std::vector<FcMode> modes; ///< The modes in collection_mode, in order
	wxStaticText *dest_label;
	FlatFrame *dest_frame;
	wxTextCtrl *dest_ctrl;

	void OnStart(wxCommandEvent &);
	void OnBrowse(wxCommandEvent &);
	void OnMode(int index);

	/// Append text to log message from worker thread
	void OnAddText(ValueEvent<std::pair<int, wxString>>& event);
	/// Collection complete notification from the worker thread to reenable buttons
	void OnCollectionComplete(wxThreadEvent &);

	void UpdateControls();

public:
	DialogFontsCollector(agi::Context *c);
    ~DialogFontsCollector() override { queue->Sync([]{}); }
};

using color_str_pair = std::pair<int, wxString>;
wxDEFINE_EVENT(EVT_ADD_TEXT, ValueEvent<color_str_pair>);
wxDEFINE_EVENT(EVT_COLLECTION_DONE, wxThreadEvent);

void FontsCollectorThread(agi::dispatch::Queue& queue, std::shared_ptr<AssFile const> subs, agi::fs::path const& destination, FcMode oper, wxEvtHandler *collector) {
	queue.Async([=]{
		auto AppendText = [&](wxString text, int colour) {
			collector->AddPendingEvent(ValueEvent<color_str_pair>(EVT_ADD_TEXT, -1, {colour, text.Clone()}));
		};

		std::vector<agi::fs::path> paths;
		try {
			paths = FontCollector(AppendText).GetFontPaths(subs.get());
		}
		catch (agi::EnvironmentError const& err) {
			AppendText(fmt_tl("* An error occurred when enumerating the used fonts: %s.\n", err.GetMessage()), 2);
		}

		if (paths.empty()) {
			collector->AddPendingEvent(wxThreadEvent(EVT_COLLECTION_DONE));
			return;
		}

		// Copy fonts
		switch (oper) {
			case FcMode::CheckFontsOnly:
				collector->AddPendingEvent(wxThreadEvent(EVT_COLLECTION_DONE));
				return;
			case FcMode::SymlinkToFolder:
				AppendText(_("Symlinking fonts to folder...\n"), 0);
				break;
			case FcMode::CopyToScriptFolder:
			case FcMode::CopyToFolder:
				AppendText(_("Copying fonts to folder...\n"), 0);
				break;
			case FcMode::CopyToZip:
				AppendText(_("Copying fonts to archive...\n"), 0);
				break;
		}

		// Open zip stream if saving to compressed archive
		std::unique_ptr<wxFFileOutputStream> out;
		std::unique_ptr<wxZipOutputStream> zip;
		if (oper == FcMode::CopyToZip) {
			try {
				agi::fs::CreateDirectory(destination.parent_path());
			}
			catch (agi::fs::FileSystemError const& e) {
				AppendText(fmt_tl("* Failed to create directory '%s': %s.\n",
					destination.parent_path().wstring(), to_wx(e.GetMessage())), 2);
				collector->AddPendingEvent(wxThreadEvent(EVT_COLLECTION_DONE));
				return;
			}

			out = std::make_unique<wxFFileOutputStream>(destination.wstring());
			if (out->IsOk())
				zip = std::make_unique<wxZipOutputStream>(*out);

			if (!out->IsOk() || !zip || !zip->IsOk()) {
				AppendText(fmt_tl("* Failed to open %s.\n", destination), 2);
				collector->AddPendingEvent(wxThreadEvent(EVT_COLLECTION_DONE));
				return;
			}
		}

		int64_t total_size = 0;
		bool allOk = true;
		for (auto path : paths) {
			path.make_preferred();

			int ret = 0;
			total_size += agi::fs::Size(path);

			switch (oper) {
				case FcMode::SymlinkToFolder:
				case FcMode::CopyToScriptFolder:
				case FcMode::CopyToFolder: {
					auto dest = destination/path.filename();
					if (agi::fs::FileExists(dest))
						ret = 2;
#ifndef _WIN32
					else if (oper == FcMode::SymlinkToFolder) {
						// returns 0 on success, -1 on error...
						if (symlink(path.c_str(), dest.c_str()))
							ret = 0;
						else
							ret = 3;
					}
#endif
					else {
						try {
							agi::fs::Copy(path, dest);
							ret = true;
						}
						catch (...) {
							ret = false;
						}
					}
				}
				break;

				case FcMode::CopyToZip: {
					wxFFileInputStream in(path.wstring());
					if (!in.IsOk())
						ret = false;
					else {
						ret = zip->PutNextEntry(path.filename().wstring());
						zip->Write(in);
					}
				}
				default: break;
			}

			if (ret == 1)
				AppendText(fmt_tl("* Copied %s.\n", path), 1);
			else if (ret == 2)
				AppendText(fmt_tl("* %s already exists on destination.\n", path.filename()), 3);
			else if (ret == 3)
				AppendText(fmt_tl("* Symlinked %s.\n", path), 1);
			else {
				AppendText(fmt_tl("* Failed to copy %s.\n", path), 2);
				allOk = false;
			}
		}

		if (allOk)
			AppendText(_("Done. All fonts copied."), 1);
		else
			AppendText(_("Done. Some fonts could not be copied."), 2);

		if (total_size > 32 * 1024 * 1024)
			AppendText(_("\nOver 32 MB of fonts were copied. Some of the fonts may not be loaded by the player if they are all attached to a Matroska file."), 2);

		AppendText("\n", 0);

		collector->AddPendingEvent(wxThreadEvent(EVT_COLLECTION_DONE));
	});
}

DialogFontsCollector::DialogFontsCollector(agi::Context *c)
: wxDialog(c->parent, -1, _("Fonts Collector"))
, context(c)
, path(*c->path)
{
	SetIcons(GETICONS(font_collector_button));

	theme::UseSurface(this);
	int gap = FromDIP(10), row = FromDIP(6);

	// What to do, the subtitle file's folder only when it has one
	std::pair<FcMode, wxString> all_modes[] = {
		 {FcMode::CheckFontsOnly, _("Check fonts for availability")}
		,{FcMode::CopyToFolder, _("Copy fonts to folder")}
		,{FcMode::CopyToScriptFolder, _("Copy fonts to subtitle file's folder")}
		,{FcMode::CopyToZip, _("Copy fonts to zipped archive")}
#ifndef _WIN32
		,{FcMode::SymlinkToFolder, _("Symlink fonts to folder")}
#endif
	};
	bool has_script_folder = c->path->Decode("?script") != "?script";
	std::vector<wxString> names;
	for (auto const& [m, name] : all_modes) {
		if (m == FcMode::CopyToScriptFolder && !has_script_folder) continue;
		modes.push_back(m);
		names.push_back(name);
	}
	mode = static_cast<FcMode>(mid<int>(0, OPT_GET("Tool/Fonts Collector/Action")->GetInt(), std::size(all_modes) - 1));
	if (std::find(modes.begin(), modes.end(), mode) == modes.end()) mode = FcMode::CheckFontsOnly;
	collection_mode = new FlatChoice(this, _("Action"));
	collection_mode->Set(names);
	collection_mode->SetStringSelection(names[std::find(modes.begin(), modes.end(), mode) - modes.begin()]);
	collection_mode->SetToolTip(_("Action"));
	collection_mode->on_choose = [this](int index) { OnMode(index); };

	// Where to, with a word on what is wanted there
	dest_frame = new FlatFrame(this, _("Destination"));
	dest_ctrl = new wxTextCtrl(dest_frame, -1, to_wx(OPT_GET("Path/Fonts Collector Destination")->GetString()), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	dest_frame->SetContent(dest_ctrl, 300);
	dest_browse_button = new FlatButton(this, _("&Browse..."));
	dest_label = new wxStaticText(this, -1, " ");
	dest_label->SetForegroundColour(flat::Colours(this).dim);
	auto dest_browse_sizer = new wxBoxSizer(wxHORIZONTAL);
	dest_browse_sizer->Add(dest_frame, wxSizerFlags(1).Expand());
	dest_browse_sizer->Add(dest_browse_button, wxSizerFlags().Border(wxLEFT, row));

	collection_log = new wxStyledTextCtrl(this, -1, wxDefaultPosition, DipSize(600, 300));
	collection_log->SetWrapMode(wxSTC_WRAP_WORD);
	collection_log->SetMarginWidth(1, 0);
	collection_log->SetReadOnly(true);
	collection_log->StyleSetForeground(1, wxColour(0, 200, 0));
	collection_log->StyleSetForeground(2, wxColour(200, 0, 0));
	collection_log->StyleSetForeground(3, wxColour(200, 100, 0));

	auto buttons = new flat::DialogButtons(this, wxOK | wxCANCEL | wxHELP, "Fonts Collector");
	start_btn = buttons->Get(wxID_OK);
	close_btn = buttons->Get(wxID_CANCEL);
	start_btn->SetLabel(_("&Start!"));
	close_btn->SetLabel(_("Close"));

	wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(collection_mode, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	main_sizer->Add(dest_browse_sizer, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, row));
	main_sizer->Add(dest_label, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, row));
	main_sizer->Add(flat::SectionTitle(this, _("Log")), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	main_sizer->Add(collection_log, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, row));
	main_sizer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, gap));

	SetSizerAndFit(main_sizer);
	CenterOnParent();

	// Update the browse button and label
	UpdateControls();

	start_btn->Bind(wxEVT_BUTTON, &DialogFontsCollector::OnStart, this);
	dest_browse_button->Bind(wxEVT_BUTTON, &DialogFontsCollector::OnBrowse, this);
	Bind(EVT_ADD_TEXT, &DialogFontsCollector::OnAddText, this);
	Bind(EVT_COLLECTION_DONE, &DialogFontsCollector::OnCollectionComplete, this);
}

void DialogFontsCollector::OnStart(wxCommandEvent &) {
    path = *context->path;
	collection_log->SetReadOnly(false);
	collection_log->ClearAll();
	collection_log->SetReadOnly(true);

	agi::fs::path dest_path;
	if (mode != FcMode::CheckFontsOnly) {
		auto dest = mode == FcMode::CopyToScriptFolder ? "?script/" : from_wx(dest_ctrl->GetValue());
		dest_path = path.Decode(dest);

		if (mode != FcMode::CopyToZip) {
			if (agi::fs::FileExists(dest_path))
				wxMessageBox(_("Invalid destination."), _("Error"), wxOK | wxICON_ERROR | wxCENTER, this);
			try {
				agi::fs::CreateDirectory(dest_path);
			}
			catch (agi::Exception const&) {
				wxMessageBox(_("Could not create destination folder."), _("Error"), wxOK | wxICON_ERROR | wxCENTER, this);
				return;
			}
		}
		else if (agi::fs::DirectoryExists(dest_path) || dest_path.filename().empty()) {
			wxMessageBox(_("Invalid path for .zip file."), _("Error"), wxOK | wxICON_ERROR | wxCENTER, this);
			return;
		}

		OPT_SET("Path/Fonts Collector Destination")->SetString(dest);
	}

	// Disable the UI while it runs as we don't support canceling
    auto snapshot = std::shared_ptr<AssFile const>(context->subtitleAccess->Snapshot());
	EnableCloseButton(false);
	start_btn->Enable(false);
	dest_browse_button->Enable(false);
	dest_frame->Enable(false);
	dest_ctrl->Enable(false);
	close_btn->Enable(false);
	collection_mode->Enable(false);

	FontsCollectorThread(*queue, std::move(snapshot), dest_path, mode, GetEventHandler());
}

void DialogFontsCollector::OnBrowse(wxCommandEvent &) {
	wxString dest;
	if (mode == FcMode::CopyToZip) {
		dest = wxFileSelector(
			_("Select archive file name"),
			dest_ctrl->GetValue(),
			wxFileName(dest_ctrl->GetValue()).GetFullName(),
			".zip", _("Zip Archives") + " (*.zip)|*.zip",
			wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
	}
	else
		dest = wxDirSelector(_("Select folder to save fonts on"), dest_ctrl->GetValue(), 0);

	if (!dest.empty())
		dest_ctrl->SetValue(dest);
}

void DialogFontsCollector::OnMode(int index) {
	mode = modes[index];
	OPT_SET("Tool/Fonts Collector/Action")->SetInt(static_cast<int>(mode));
	UpdateControls();
}

void DialogFontsCollector::UpdateControls() {
	wxString dst = dest_ctrl->GetValue();

	if (mode == FcMode::CheckFontsOnly || mode == FcMode::CopyToScriptFolder) {
		dest_frame->Enable(false);
		dest_ctrl->Enable(false);
		dest_browse_button->Enable(false);
		dest_label->SetLabel(_("N/A"));
	}
	else {
		dest_frame->Enable(true);
		dest_ctrl->Enable(true);
		dest_browse_button->Enable(true);

		if (mode == FcMode::CopyToFolder || mode == FcMode::SymlinkToFolder) {
			dest_label->SetLabel(_("Choose the folder where the fonts will be collected to. It will be created if it doesn't exist."));

			// Remove filename from browse box
			if (dst.Right(4) == ".zip")
				dest_ctrl->SetValue(wxFileName(dst).GetPath());
		}
		else {
			dest_label->SetLabel(_("Enter the name of the destination zip file to collect the fonts to. If a folder is entered, a default name will be used."));

			// Add filename to browse box
			if (!dst.EndsWith(".zip")) {
				wxFileName fn(dst + "//");
				fn.SetFullName("fonts.zip");
				dest_ctrl->SetValue(fn.GetFullPath());
			}
		}
	}

#ifdef __APPLE__
	// wxStaticText auto-wraps everywhere but OS X
	dest_label->Wrap(dest_label->GetParent()->GetSize().GetWidth() - 20);
	Layout();
#endif
}

void DialogFontsCollector::OnAddText(ValueEvent<color_str_pair> &event) {
	auto const& str = event.Get();
	collection_log->SetReadOnly(false);
	int pos = collection_log->GetLength();
	auto const& utf8 = str.second.utf8_str();
	collection_log->AppendTextRaw(utf8.data(), utf8.length());
	if (str.first) {
		collection_log->StartStyling(pos);
		collection_log->SetStyling(utf8.length(), str.first);
	}
	collection_log->GotoPos(pos + utf8.length());
	collection_log->SetReadOnly(true);
}

void DialogFontsCollector::OnCollectionComplete(wxThreadEvent &) {
	EnableCloseButton(true);
	start_btn->Enable();
	close_btn->Enable();
	collection_mode->Enable();

	UpdateControls();
}
}

void ShowFontsCollectorDialog(agi::Context *c) {
	c->dialog->Show<DialogFontsCollector>(c);
}

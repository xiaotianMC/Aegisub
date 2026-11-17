// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "compat.h"
#include "dialog_progress.h"
#include "dialogs.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "format.h"
#include "options.h"
#include "theme.h"
#include "transcribe_models.h"

#include <libaegisub/exception.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>

#include <filesystem>

#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>

namespace {
/// What each model offered is good for, in Models()' order
wxString Describe(size_t i) {
	switch (i) {
		case 0: return _("Small: quick, for a computer without a fast graphics card");
		case 1: return _("Large v3 Turbo: more accurate; slow without a fast graphics card");
		default: return _("Large v3: the most accurate, and the slowest");
	}
}

/// The free space where @p folder is or would be made
uintmax_t FreeSpaceFor(agi::fs::path folder) {
	std::error_code ec;
	while (!folder.empty() && !std::filesystem::exists(folder, ec)) {
		auto parent = folder.parent_path();
		if (parent == folder) break;
		folder = agi::fs::path(parent);
	}
	try { return agi::fs::FreeSpace(folder); }
	catch (agi::Exception const&) { return 0; }
}
}

std::optional<agi::fs::path> ChooseWhisperModel(wxWindow *parent) {
	auto const& models = transcribe::Models();
	std::string source = OPT_GET("Tool/Transcribe/Local/Source")->GetString();
	std::string folder_setting = OPT_GET("Tool/Transcribe/Local/Model Folder")->GetString();
	auto folder = transcribe::ModelFolder();

	wxDialog d(parent, -1, _("Whisper Model"));
	theme::UseSurface(&d);
	int const gap = d.FromDIP(10), row = d.FromDIP(6);

	auto intro = new wxStaticText(&d, -1, _("Whisper hears the audio on this computer, free, and sends nothing anywhere.\nIt needs a model, downloaded once."));
	// A sentence a line, as wxWidgets wraps only at spaces, which Chinese and
	// Japanese have none of between words; wrapped still if one is longer
	intro->Wrap(d.FromDIP(560));

	auto model_choice = new FlatChoice(&d, _("Model"));
	auto source_choice = new FlatChoice(&d, _("Download from"));
	auto folder_text = new wxStaticText(&d, -1, "");
	auto change_folder = new FlatButton(&d, _("Change..."));
	auto status = new wxStaticText(&d, -1, "");

	auto buttons = new flat::DialogButtons(&d, wxCANCEL);
	auto have_file = buttons->AddLeft(_("Use a Model File I Have..."));
	auto go = buttons->AddRight(_("Download"), wxID_OK, true);

	int chosen = 0;
	if (auto current = transcribe::FindModel(agi::fs::path(OPT_GET("Tool/Transcribe/Local/Model")->GetString())))
		chosen = static_cast<int>(current - models.data());

	auto fill_models = [&] {
		std::vector<wxString> items;
		for (size_t i = 0; i < models.size(); ++i) {
			wxString item = Describe(i) + " (" + to_wx(transcribe::SizeText(models[i].bytes)) + ")";
			if (transcribe::Downloaded(models[i], folder)) item += " - " + _("downloaded");
			items.push_back(item);
		}
		model_choice->Set(items);
		model_choice->SetStringSelection(items[chosen]);
	};

	// Hugging Face, its mirror, an address of the user's if there is one,
	// and a way to give another
	auto fill_sources = [&] {
		std::vector<wxString> items = {_("Hugging Face"), _("hf-mirror.com, reachable from mainland China")};
		bool const custom = !source.empty() && source != "huggingface" && source != "hf-mirror";
		if (custom) items.push_back(to_wx(source));
		items.push_back(_("Another address..."));
		source_choice->Set(items);
		source_choice->SetStringSelection(items[source == "hf-mirror" ? 1 : custom ? 2 : 0]);
	};

	auto refresh = [&] {
		auto const& m = models[chosen];
		bool const there = transcribe::Downloaded(m, folder);
		folder_text->SetLabel(agi::wxformat(_("Kept in %s (%s free)"), folder.string(), transcribe::SizeText(static_cast<int64_t>(FreeSpaceFor(folder)))));
		status->SetLabel(there ? _("This model is downloaded already.")
			: agi::wxformat(_("%s is downloaded, then checked to be the model as published."), transcribe::SizeText(m.bytes)));
		go->SetLabel(there ? _("Use") : _("Download"));
		source_choice->Enable(!there);
		d.Layout();
	};

	model_choice->on_choose = [&](int i) {
		chosen = i;
		refresh();
	};
	source_choice->on_choose = [&](int i) {
		auto const& items = source_choice->GetItems();
		if (i == static_cast<int>(items.size()) - 1) {
			bool const custom = !source.empty() && source != "huggingface" && source != "hf-mirror";
			wxString entered = wxGetTextFromUser(_("The address of a folder holding the ggml model files, each file's name put after it:"),
				_("Download from"), custom ? to_wx(source) : wxString("https://"), &d);
			entered.Trim().Trim(false);
			if (!entered.empty() && entered != "https://") source = from_wx(entered);
		}
		else if (i == 0) source = "huggingface";
		else if (i == 1) source = "hf-mirror";
		else source = from_wx(items[i]);
		fill_sources();
	};
	change_folder->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
		wxDirDialog pick(&d, _("Where to keep Whisper models"), to_wx(folder.string()));
		if (pick.ShowModal() != wxID_OK) return;
		folder = agi::fs::path(from_wx(pick.GetPath()));
		folder_setting = folder.string();
		fill_models();
		refresh();
	});

	std::optional<agi::fs::path> result;
	have_file->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
		wxString file = wxFileSelector(_("Choose a Whisper model file, such as ggml-large-v3-turbo-q5_0.bin"),
			to_wx(folder.string()), "", "bin", _("Whisper models (*.bin)") + "|*.bin|" + _("All Files") + " (*.*)|*.*",
			wxFD_OPEN | wxFD_FILE_MUST_EXIST, &d);
		if (file.empty()) return;
		result = agi::fs::path(from_wx(file));
		d.EndModal(wxID_OK);
	});
	d.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
		OPT_SET("Tool/Transcribe/Local/Source")->SetString(source);
		OPT_SET("Tool/Transcribe/Local/Model Folder")->SetString(folder_setting);
		auto const& m = models[chosen];
		try {
			DialogProgress progress(&d, _("Whisper Model"), agi::wxformat(_("Downloading %s..."), std::string(m.file)));
			progress.Run([&](agi::ProgressSink *ps) {
				result = transcribe::GetModel(m, folder, source, ps);
				// Silence and music heard as such, not made into words: small,
				// and not needed, so not getting it is no failure
				try { transcribe::GetModel(transcribe::VadModel(), folder, source, ps); }
				catch (agi::EnvironmentError const& e) { LOG_W("transcribe/models") << "no VAD model: " << e.GetMessage(); }
			});
		}
		catch (agi::UserCancelException const&) {
			// What came is kept, to go on from next time
			return;
		}
		catch (agi::Exception const& e) {
			wxMessageBox(to_wx(e.GetMessage()), _("Whisper Model"), wxOK | wxICON_ERROR, &d);
			fill_models();
			refresh();
			return;
		}
		d.EndModal(wxID_OK);
	}, wxID_OK);

	// The fields lined up after their names
	auto fields = new wxFlexGridSizer(2, row, row);
	fields->AddGrowableCol(1);
	for (auto [name, control] : {std::pair<wxString, wxWindow *>{_("Model"), model_choice}, {_("Download from"), source_choice}}) {
		fields->Add(new wxStaticText(&d, -1, name), wxSizerFlags().CentreVertical());
		fields->Add(control, wxSizerFlags().Expand());
	}
	auto folder_line = new wxBoxSizer(wxHORIZONTAL);
	folder_line->Add(folder_text, wxSizerFlags(1).CentreVertical());
	folder_line->Add(change_folder, wxSizerFlags().Border(wxLEFT, row));

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	auto const side = wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap);
	main_sizer->Add(intro, side);
	main_sizer->Add(fields, side);
	main_sizer->Add(folder_line, side);
	main_sizer->Add(status, side);
	main_sizer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, gap));

	fill_models();
	fill_sources();
	refresh();
	d.SetSizerAndFit(main_sizer);
	d.CenterOnParent();
	if (d.ShowModal() != wxID_OK) return std::nullopt;
	return result;
}

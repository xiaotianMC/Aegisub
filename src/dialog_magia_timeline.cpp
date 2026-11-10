// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "dialog_magia_timeline.h"

#include "compat.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "include/aegisub/context.h"
#include "theme.h"

#include <algorithm>

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

using namespace magia_timeline;

namespace {
/// What each strategy is for, as its config.yml says
wxString Describe(std::string const& strategy) {
	if (strategy == "bcs") return _("General purpose (recommended)");
	if (strategy == "dtd") return _("General purpose, experimental");
	if (strategy == "otl") return _("Outlined text");
	if (strategy == "mr") return _("Magia Record quests");
	if (strategy == "mr-s0") return _("Magia Record Scene0 film");
	if (strategy == "md") return _("Magia Exedra quests");
	if (strategy == "lcb") return _("Limbus Company quests");
	if (strategy == "lcb-mech") return _("Limbus Company mechanics");
	if (strategy == "pkm") return _("Pokemon Emerald");
	if (strategy == "prk") return _("Parako anime storyboards");
	if (strategy == "bdr") return _("BanG Dream stories");
	return wxString();
}

class DialogMagiaTimeline final : public wxDialog {
	Installed const& installed;
	Values values;
	agi::fs::path config_file;

	FlatChoice *strategy = nullptr;
	FlatChoice *preset = nullptr;
	FlatSwitch *engine = nullptr;
	flat::NumberField *interval = nullptr;
	flat::NumberField *max_width = nullptr, *max_height = nullptr;
	FlatCheck *opencl = nullptr;
	FlatCheck *ocr = nullptr;
	FlatSwitch *ocr_engine = nullptr;
	FlatFrame *language_frame = nullptr;
	wxTextCtrl *language = nullptr;
	wxStaticText *ocr_note = nullptr;

	std::vector<std::string> const& Presets() const {
		static std::vector<std::string> const none;
		auto s = values.find("strategy");
		auto it = s == values.end() ? installed.presets.end() : installed.presets.find(s->second);
		return it == installed.presets.end() ? none : it->second;
	}

	bool Framewise() const { return values.count("engine") && values.at("engine") == "framewise"; }
	bool Tesseract() const { return values.count("ocr/doTesseract") && values.at("ocr/doTesseract") == "true" &&
		!(values.count("ocr/doPaddle") && values.at("ocr/doPaddle") == "true"); }
	/// The key the language is set with, for the OCR engine chosen
	std::string LanguageKey() const {
		if (Tesseract()) return installed.Has("ocr/tesseractLang") ? "ocr/tesseractLang" : "";
		return installed.Has("ocr/paddleLang") ? "ocr/paddleLang" : "";
	}
	bool ReadsText() const {
		auto jobs = ParseList(values.count("extraJobs") ? values.at("extraJobs") : "");
		return std::find(jobs.begin(), jobs.end(), "ocr") != jobs.end();
	}
	void SetReadsText(bool on) {
		auto jobs = ParseList(values["extraJobs"]);
		jobs.erase(std::remove(jobs.begin(), jobs.end(), "ocr"), jobs.end());
		if (on) jobs.push_back("ocr");
		values["extraJobs"] = FormatList(jobs);
	}

	void ChooseStrategy(std::string const& name) {
		values["strategy"] = name;
		auto const& presets = Presets();
		auto own = installed.Get("preset");
		if (std::find(presets.begin(), presets.end(), values["preset"]) == presets.end())
			values["preset"] = std::find(presets.begin(), presets.end(), own) != presets.end() ? own : presets.empty() ? own : presets.front();
		// The others look at every frame
		if (engine && !CanSkipFrames(name)) values["engine"] = "framewise";
		UpdateControls();
	}

	/// The controls as the values are
	void UpdateControls() {
		std::vector<wxString> names;
		int chosen = -1;
		for (auto const& s : installed.strategies) {
			auto d = Describe(s);
			names.push_back(d.empty() ? to_wx(s) : to_wx(s) + wxS(" — ") + d);
			if (s == values["strategy"]) chosen = static_cast<int>(names.size()) - 1;
		}
		strategy->Set(names);
		if (chosen >= 0) strategy->SetStringSelection(names[chosen]);

		std::vector<wxString> presets;
		for (auto const& p : Presets()) presets.push_back(to_wx(p));
		preset->Set(presets);
		preset->SetStringSelection(to_wx(values["preset"]));

		bool const can_skip = CanSkipFrames(values["strategy"]);
		if (engine) {
			engine->EnableItem(0, can_skip);
			engine->SetSelection(Framewise() ? 1 : 0);
		}
		if (interval) {
			interval->SetValue(std::atoi(values["framewise/sampleInterval"].c_str()));
			interval->Enable(!engine || Framewise());
		}
		if (max_width) {
			max_width->SetValue(std::atoi(values["maxResWidth"].c_str()));
			max_height->SetValue(std::atoi(values["maxResHeight"].c_str()));
		}
		if (opencl) opencl->SetValue(values["enableOpenCL"] == "true");
		if (ocr) {
			// Extra jobs are run only by some strategies, and only when
			// frames are skipped
			bool const can_read = CanReadText(values["strategy"]) && (!engine || !Framewise());
			ocr->Enable(can_read);
			ocr->SetValue(can_read && ReadsText());
			ocr_note->SetLabel(can_read ? wxString() : CanReadText(values["strategy"])
				? _("Only when unchanged frames are skipped")
				: _("Not with this strategy"));
			bool const on = ocr->GetValue();
			if (ocr_engine) {
				ocr_engine->SetSelection(Tesseract() ? 1 : 0);
				ocr_engine->Enable(on);
			}
			if (language_frame) {
				auto key = LanguageKey();
				language_frame->Enable(on && !key.empty());
				language->ChangeValue(key.empty() ? wxString() : to_wx(Unquote(values[key])));
			}
		}
		Layout();
	}

	void Restore() {
		values = installed.values;
		UpdateControls();
	}

public:
	DialogMagiaTimeline(agi::Context *c, Installed const& installed, Values start, agi::fs::path config_file)
	: wxDialog(c->parent, -1, _("Time Hardsubs with MagiaTimeline"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
	, installed(installed)
	, values(std::move(start))
	, config_file(std::move(config_file))
	{
		theme::UseSurface(this);
		auto dim = [this](wxString const& text) { return flat::DimLabel(this, text); };

		auto grid = new wxFlexGridSizer(2, FromDIP(6), FromDIP(10));
		grid->AddGrowableCol(1, 1);
		auto row = [&](wxString const& name, wxWindow *control, wxSizer *sizer = nullptr) {
			grid->Add(dim(name), wxSizerFlags().CenterVertical());
			if (control) grid->Add(control, wxSizerFlags().Expand());
			else grid->Add(sizer, wxSizerFlags().Expand());
		};

		strategy = new FlatChoice(this, _("The way it finds subtitles"));
		strategy->SetName("strategy");
		strategy->on_choose = [this](int i) { ChooseStrategy(this->installed.strategies[i]); };
		row(_("Strategy"), strategy);
		preset = new FlatChoice(this, _("The set of its settings for the strategy"));
		preset->SetName("preset");
		preset->on_choose = [this](int i) { values["preset"] = Presets()[i]; UpdateControls(); };
		row(_("Preset"), preset);

		if (installed.Has("engine")) {
			engine = new FlatSwitch(this, {_("&Skip Unchanged Frames"), _("&Every Frame")});
			engine->SetName("engine");
			engine->SetItemToolTip(0, _("Look again only where the picture may have changed: much faster"));
			engine->SetItemToolTip(1, _("Look at every frame, or every few, as set below: the only way for most of the strategies made for one game"));
			engine->on_choose = [this](int i) { values["engine"] = i ? "framewise" : "speculative"; UpdateControls(); };
			row(_("Frames"), engine);
		}
		if (installed.Has("framewise/sampleInterval")) {
			interval = flat::NumberField::Create(this, _("Every"), 1, 1, 1000);
			interval->Frame()->SetName("sampleInterval");
			interval->SetToolTip(_("Look at one frame in this many when looking at every frame"));
			interval->on_change = [this](double v) { values["framewise/sampleInterval"] = std::to_string(static_cast<int>(v)); };
			auto line = new wxBoxSizer(wxHORIZONTAL);
			line->Add(interval->Frame());
			line->Add(dim(_("frames")), wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(6)));
			grid->AddSpacer(0);
			grid->Add(line);
		}
		if (installed.Has("maxResWidth") && installed.Has("maxResHeight")) {
			max_width = flat::NumberField::Create(this, wxGETTEXT_IN_CONTEXT("resolution", "Width"), 0, 0, 100000, 10, 0, 56);
			max_height = flat::NumberField::Create(this, wxGETTEXT_IN_CONTEXT("resolution", "Height"), 0, 0, 100000, 10, 0, 56);
			max_width->Frame()->SetName("maxResWidth");
			max_height->Frame()->SetName("maxResHeight");
			max_width->on_change = [this](double v) { values["maxResWidth"] = std::to_string(static_cast<int>(v)); };
			max_height->on_change = [this](double v) { values["maxResHeight"] = std::to_string(static_cast<int>(v)); };
			auto line = new wxBoxSizer(wxHORIZONTAL);
			line->Add(max_width->Frame());
			line->Add(dim(wxS("×")), wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(6)));
			line->Add(max_height->Frame());
			auto tip = _("A larger video is halved until it fits, which is faster; 0 leaves that side be");
			max_width->SetToolTip(tip);
			max_height->SetToolTip(tip);
			row(_("Largest Size"), nullptr, line);
		}
		if (installed.Has("enableOpenCL")) {
			opencl = new FlatCheck(this, _("Use the &graphics card (OpenCL)"));
			opencl->SetName("enableOpenCL");
			opencl->SetToolTip(_("May be faster with a graphics card; turn it off if it is slower or fails"));
			opencl->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { values["enableOpenCL"] = opencl->GetValue() ? "true" : "false"; });
			grid->AddSpacer(0);
			grid->Add(opencl);
		}

		if (installed.Has("extraJobs") && (installed.Has("ocr/doPaddle") || installed.Has("ocr/doTesseract"))) {
			ocr = new FlatCheck(this, _("&Read the text (OCR)"));
			ocr->SetName("ocr");
			ocr->SetToolTip(_("Fill the lines with the text it reads, which needs its OCR installed; without it the lines are empty"));
			ocr->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SetReadsText(ocr->GetValue()); UpdateControls(); });
			ocr_note = dim(wxString());
			auto line = new wxBoxSizer(wxHORIZONTAL);
			line->Add(ocr, wxSizerFlags().CenterVertical());
			line->Add(ocr_note, wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(8)));
			row(_("Text"), nullptr, line);
			if (installed.Has("ocr/doPaddle") && installed.Has("ocr/doTesseract")) {
				ocr_engine = new FlatSwitch(this, {wxS("&PaddleOCR"), wxS("&Tesseract")});
				ocr_engine->SetName("ocrEngine");
				ocr_engine->on_choose = [this](int i) {
					values["ocr/doPaddle"] = i ? "false" : "true";
					values["ocr/doTesseract"] = i ? "true" : "false";
					UpdateControls();
				};
				grid->AddSpacer(0);
				grid->Add(ocr_engine, wxSizerFlags().Expand());
			}
			if (installed.Has("ocr/tesseractLang") || installed.Has("ocr/paddleLang")) {
				language_frame = new FlatFrame(this, _("Language"));
				language = new wxTextCtrl(language_frame, -1, wxString(), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
				language_frame->SetContent(language, 80);
				language_frame->SetName("ocrLanguage");
				language_frame->SetToolTip(_("As the OCR names it: jpn, chi_sim... for Tesseract; japan, ch... for PaddleOCR"));
				language->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
					auto key = LanguageKey();
					if (!key.empty()) values[key] = QuoteValue(from_wx(language->GetValue()));
				});
				grid->AddSpacer(0);
				grid->Add(language_frame);
			}
		}

		auto buttons = new flat::DialogButtons(this, wxOK | wxCANCEL);
		buttons->Get(wxID_OK)->SetLabel(_("&Run"));
		auto open = buttons->AddLeft(_("Open &config.yml"));
		open->SetName("openConfig");
		open->SetToolTip(_("Its other settings are there"));
		open->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { wxLaunchDefaultApplication(this->config_file.wstring()); });
		auto restore = buttons->AddLeft(_("Its &Own Settings"));
		restore->SetName("restore");
		restore->SetToolTip(_("Back to what its config.yml says"));
		restore->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Restore(); });

		auto main = new wxBoxSizer(wxVERTICAL);
		main->Add(grid, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(10)));
		main->Add(buttons, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10)));
		UpdateControls();
		SetSizerAndFit(main);
		CenterOnParent();
	}

	Values const& Chosen() const { return values; }
};
}

std::optional<Values> ShowMagiaTimelineDialog(agi::Context *c, Installed const& installed, Values const& start,
                                              agi::fs::path const& config_file) {
	DialogMagiaTimeline d(c, installed, start, config_file);
	if (d.ShowModal() != wxID_OK) return std::nullopt;
	return d.Chosen();
}

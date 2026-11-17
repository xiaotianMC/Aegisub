// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "dialog_magia_timeline.h"

#include "async_video_provider.h"
#include "compat.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "include/aegisub/context.h"
#include "project.h"
#include "theme.h"
#include "video_controller.h"
#include "video_frame.h"

#include <algorithm>
#include <cmath>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

using namespace magia_timeline;

namespace {
Rect Clamp(Rect r) {
	auto c = [](double v) { return std::clamp(v, 0.0, 1.0); };
	return {c(r.left), c(r.right), c(r.top), c(r.bottom)};
}
/// @p r, in shares of @p within, in shares of what @p within is in
Rect Outward(Rect const& within, Rect const& r) {
	double const w = within.right - within.left, h = within.bottom - within.top;
	return {within.left + r.left * w, within.left + r.right * w, within.top + r.top * h, within.top + r.bottom * h};
}
Rect Inward(Rect const& within, Rect const& r) {
	double const w = std::max(within.right - within.left, 1e-9), h = std::max(within.bottom - within.top, 1e-9);
	return Clamp({(r.left - within.left) / w, (r.right - within.left) / w, (r.top - within.top) / h, (r.bottom - within.top) / h});
}
double Round(double v) { return std::round(v * 1000) / 1000; }
}

MagiaRectPicker::MagiaRectPicker(wxWindow *parent)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	Bind(wxEVT_PAINT, &MagiaRectPicker::OnPaint, this);
	Bind(wxEVT_LEFT_DOWN, &MagiaRectPicker::OnMouse, this);
	Bind(wxEVT_LEFT_UP, &MagiaRectPicker::OnMouse, this);
	Bind(wxEVT_MOTION, &MagiaRectPicker::OnMouse, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { grip = none; });
}

wxSize MagiaRectPicker::DoGetBestClientSize() const {
	return FromDIP(wxSize(480, 270));
}

void MagiaRectPicker::SetImage(wxImage const& frame) {
	image = frame;
	scaled = wxBitmap();
	Refresh();
}

void MagiaRectPicker::SetRects(std::optional<Rect> new_within, std::optional<Rect> rect, std::vector<Rect> shown) {
	editing = new_within && rect;
	within = new_within.value_or(Rect{});
	edited = editing ? Outward(within, *rect) : Rect{};
	others = std::move(shown);
	grip = none;
	Refresh();
}

Rect MagiaRectPicker::GetRect() const {
	auto r = Inward(within, edited);
	return {Round(r.left), Round(r.right), Round(r.top), Round(r.bottom)};
}

wxRect MagiaRectPicker::ImageArea() const {
	auto size = GetClientSize();
	double aspect = image.IsOk() && image.GetHeight() ? double(image.GetWidth()) / image.GetHeight() : 16.0 / 9.0;
	int w = size.x, h = static_cast<int>(w / aspect);
	if (h > size.y) {
		h = size.y;
		w = static_cast<int>(h * aspect);
	}
	return wxRect((size.x - w) / 2, (size.y - h) / 2, std::max(w, 1), std::max(h, 1));
}

void MagiaRectPicker::At(wxPoint p, double &x, double &y) const {
	auto area = ImageArea();
	x = std::clamp(double(p.x - area.x) / area.width, 0.0, 1.0);
	y = std::clamp(double(p.y - area.y) / area.height, 0.0, 1.0);
}

int MagiaRectPicker::GripAt(wxPoint p) const {
	if (!editing) return none;
	auto area = ImageArea();
	double const tol_x = FromDIP(6) / double(area.width), tol_y = FromDIP(6) / double(area.height);
	double x, y;
	At(p, x, y);
	bool const across = x > edited.left - tol_x && x < edited.right + tol_x;
	bool const along = y > edited.top - tol_y && y < edited.bottom + tol_y;
	int g = none;
	if (along && std::abs(x - edited.left) <= tol_x) g |= left;
	else if (along && std::abs(x - edited.right) <= tol_x) g |= right;
	if (across && std::abs(y - edited.top) <= tol_y) g |= top;
	else if (across && std::abs(y - edited.bottom) <= tol_y) g |= bottom;
	if (g) return g;
	if (x > edited.left && x < edited.right && y > edited.top && y < edited.bottom) return move;
	return fresh;
}

void MagiaRectPicker::Press(wxPoint p) {
	grip = GripAt(p);
	if (!grip) return;
	At(p, press_x, press_y);
	pressed = edited;
	if (grip == fresh) {
		double x = std::clamp(press_x, within.left, within.right), y = std::clamp(press_y, within.top, within.bottom);
		edited = {x, x, y, y};
		pressed = edited;
		grip = right | bottom;
	}
}

void MagiaRectPicker::Drag(wxPoint p) {
	if (!grip) return;
	double x, y;
	At(p, x, y);
	x = std::clamp(x, within.left, within.right);
	y = std::clamp(y, within.top, within.bottom);
	Rect r = pressed;
	if (grip == move) {
		double dx = std::clamp(x - press_x, within.left - r.left, within.right - r.right);
		double dy = std::clamp(y - press_y, within.top - r.top, within.bottom - r.bottom);
		r = {r.left + dx, r.right + dx, r.top + dy, r.bottom + dy};
	}
	else {
		if (grip & left) r.left = x;
		if (grip & right) r.right = x;
		if (grip & top) r.top = y;
		if (grip & bottom) r.bottom = y;
		// Dragged past the other side: the sides swap
		if (r.left > r.right) std::swap(r.left, r.right);
		if (r.top > r.bottom) std::swap(r.top, r.bottom);
	}
	edited = r;
	Refresh();
}

void MagiaRectPicker::Release(wxPoint p) {
	if (!grip) return;
	Drag(p);
	grip = none;
	// Too small to be meant: what it was
	auto area = ImageArea();
	if ((edited.right - edited.left) * area.width < FromDIP(4) || (edited.bottom - edited.top) * area.height < FromDIP(4)) {
		edited = pressed.right > pressed.left ? pressed : edited;
		Refresh();
		return;
	}
	if (on_change) on_change(GetRect());
}

void MagiaRectPicker::OnMouse(wxMouseEvent &event) {
	if (event.LeftDown()) {
		Press(event.GetPosition());
		if (grip && !HasCapture()) CaptureMouse();
	}
	else if (event.LeftUp()) {
		if (HasCapture()) ReleaseMouse();
		Release(event.GetPosition());
	}
	else if (event.Dragging() && grip)
		Drag(event.GetPosition());
	else if (event.Moving()) {
		int g = GripAt(event.GetPosition());
		wxStockCursor cursor = wxCURSOR_ARROW;
		if (g == move) cursor = wxCURSOR_SIZING;
		else if (g == fresh) cursor = wxCURSOR_CROSS;
		else if (g == (left | top) || g == (right | bottom)) cursor = wxCURSOR_SIZENWSE;
		else if (g == (right | top) || g == (left | bottom)) cursor = wxCURSOR_SIZENESW;
		else if (g & (left | right)) cursor = wxCURSOR_SIZEWE;
		else if (g & (top | bottom)) cursor = wxCURSOR_SIZENS;
		SetCursor(wxCursor(cursor));
	}
}

void MagiaRectPicker::OnPaint(wxPaintEvent &) {
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);
	auto const p = flat::Colours(this);
	dc.SetBackground(wxBrush(p.field));
	dc.Clear();
	auto area = ImageArea();
	if (image.IsOk()) {
		if (!scaled.IsOk() || scaled_for != area.GetSize()) {
			scaled = wxBitmap(image.Scale(area.width, area.height, wxIMAGE_QUALITY_BILINEAR));
			scaled_for = area.GetSize();
		}
		dc.DrawBitmap(scaled, area.GetTopLeft());
	}
	auto on_screen = [&](Rect const& r) {
		return wxRect(wxPoint(area.x + static_cast<int>(std::lround(r.left * area.width)), area.y + static_cast<int>(std::lround(r.top * area.height))),
		              wxPoint(area.x + static_cast<int>(std::lround(r.right * area.width)) - 1, area.y + static_cast<int>(std::lround(r.bottom * area.height)) - 1));
	};
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.SetPen(wxPen(wxColour(255, 255, 255, 150), FromDIP(1), wxPENSTYLE_SHORT_DASH));
	for (auto const& r : others) dc.DrawRectangle(on_screen(r));
	if (!editing) return;

	// What is outside it darkened: what it will not look at
	auto box = on_screen(edited);
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(wxColour(0, 0, 0, 120)));
	dc.DrawRectangle(area.x, area.y, area.width, box.y - area.y);
	dc.DrawRectangle(area.x, box.GetBottom() + 1, area.width, area.GetBottom() - box.GetBottom());
	dc.DrawRectangle(area.x, box.y, box.x - area.x, box.height);
	dc.DrawRectangle(box.GetRight() + 1, box.y, area.GetRight() - box.GetRight(), box.height);
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.SetPen(wxPen(p.accent, FromDIP(2)));
	dc.DrawRectangle(box);
	int const handle = FromDIP(6);
	dc.SetBrush(wxBrush(p.accent));
	dc.SetPen(*wxTRANSPARENT_PEN);
	for (auto corner : {box.GetTopLeft(), box.GetTopRight(), box.GetBottomLeft(), box.GetBottomRight()})
		dc.DrawRectangle(corner.x - handle / 2, corner.y - handle / 2, handle, handle);
}

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

	agi::Context *c;
	MagiaRectPicker *picker = nullptr;
	FlatChoice *region = nullptr;
	flat::NumberField *frame = nullptr;
	/// The rectangles to drag: contentRect and the preset's
	std::vector<std::string> regions;
	std::string region_chosen;

	struct Tuning {
		std::string key;
		flat::NumberField *field;
		wxSizer *row;
	};
	std::vector<Tuning> tunings;
	wxSizer *tuning_box = nullptr;
	wxSizer *tuning_title = nullptr;

	/// The path of a key of the preset chosen
	std::string InPreset(std::string const& key) {
		return values["strategy"] + "/" + values["preset"] + "/" + key;
	}

	/// The frame without its black bars, in shares of the frame
	Rect Content() {
		auto r = ParseRect(values["contentRect"]);
		return r ? *r : Rect{};
	}

	/// In shares of the frame
	std::optional<Rect> RegionInFrame(std::string const& path) {
		auto r = ParseRect(values[path]);
		if (!r) return std::nullopt;
		return path == "contentRect" ? *r : Outward(Content(), *r);
	}

	wxString RegionName(std::string const& path) {
		if (path == "contentRect") return _("Picture (without black bars)");
		auto key = path.substr(path.rfind('/') + 1);
		if (key == "dialogRect") return wxGETTEXT_IN_CONTEXT("region", "Subtitles");
		return to_wx(key);
	}

	void ShowRegions() {
		regions.clear();
		if (installed.Has("contentRect") && ParseRect(values["contentRect"])) regions.push_back("contentRect");
		auto prefix = InPreset("");
		for (auto const& [path, value] : installed.values) {
			if (path.rfind(prefix, 0) != 0 || path.size() < 4 || path.compare(path.size() - 4, 4, "Rect") || !ParseRect(values[path]))
				continue;
			// Subtitles first: the one to set
			if (path == prefix + "dialogRect") regions.insert(regions.begin(), path);
			else regions.push_back(path);
		}
		if (std::find(regions.begin(), regions.end(), region_chosen) == regions.end())
			region_chosen = regions.empty() ? std::string() : regions.front();
		std::vector<wxString> names;
		for (auto const& r : regions) names.push_back(RegionName(r));
		region->Set(names);
		if (!region_chosen.empty()) region->SetStringSelection(RegionName(region_chosen));
		region->Enable(!regions.empty());

		std::vector<Rect> shown;
		for (auto const& r : regions)
			if (r != region_chosen)
				if (auto in_frame = RegionInFrame(r)) shown.push_back(*in_frame);
		if (region_chosen.empty())
			picker->SetRects(std::nullopt, std::nullopt, shown);
		else
			picker->SetRects(region_chosen == "contentRect" ? Rect{} : Content(), ParseRect(values[region_chosen]), shown);
	}

	void ShowFrame(int n) {
		auto provider = c->project->VideoProvider();
		if (!provider) return;
		n = std::clamp(n, 0, std::max(provider->GetFrameCount() - 1, 0));
		try {
			auto f = provider->GetFrame(n, c->videoController->TimeAtFrame(n), true);
			if (f) picker->SetImage(GetImage(*f));
		}
		catch (...) { }
	}

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
				// Shown only where the engine chosen has a language to set
				language_frame->Show(!key.empty());
				language_frame->Enable(on);
				language->ChangeValue(key.empty() ? wxString() : to_wx(Unquote(values[key])));
			}
		}
		ShowRegions();
		bool any = false;
		for (auto& t : tunings) {
			auto path = InPreset(t.key);
			bool const has = installed.Has(path);
			tuning_box->Show(t.row, has);
			if (has) t.field->SetValue(std::atof(values[path].c_str()));
			any = any || has;
		}
		tuning_box->Show(tuning_title, any);
		Layout();
	}

	void Restore() {
		values = installed.values;
		UpdateControls();
	}

public:
	DialogMagiaTimeline(agi::Context *context, Installed const& installed, Values start, agi::fs::path config_file)
	: wxDialog(context->parent, -1, _("Time Hardsubs with MagiaTimeline"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, installed(installed)
	, values(std::move(start))
	, config_file(std::move(config_file))
	, c(context)
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

		// The frame, the rectangle dragged on it
		picker = new MagiaRectPicker(this);
		picker->SetName("picker");
		picker->SetToolTip(_("Drag a rectangle round where the subtitles are, or its sides to move them; MagiaTimeline looks only inside it"));
		picker->on_change = [this](Rect r) {
			if (region_chosen.empty()) return;
			values[region_chosen] = FormatRect(r);
			ShowRegions();
		};
		region = new FlatChoice(this, _("The rectangle to drag"));
		region->SetName("region");
		region->on_choose = [this](int i) { region_chosen = regions[i]; ShowRegions(); };
		int const frames = c->project->VideoProvider() ? c->project->VideoProvider()->GetFrameCount() : 1;
		frame = flat::NumberField::Create(this, _("Frame"), c->videoController->GetFrameN(), 0, std::max(frames - 1, 0), 1, 0, 64);
		frame->Frame()->SetName("frame");
		frame->SetToolTip(_("A frame with subtitles shows best where they are: drag the name to go through the video"));
		frame->on_change = [this](double v) { ShowFrame(static_cast<int>(v)); };
		auto under = new wxBoxSizer(wxHORIZONTAL);
		under->Add(dim(_("Region")), wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(6)));
		under->Add(region, wxSizerFlags(1).Expand());
		under->Add(frame->Frame(), wxSizerFlags().Border(wxLEFT, FromDIP(10)));
		auto left = new wxBoxSizer(wxVERTICAL);
		left->Add(picker, wxSizerFlags(1).Expand());
		left->Add(under, wxSizerFlags().Expand().Border(wxTOP, FromDIP(6)));

		// The settings most often tuned, of the strategies that have them
		tuning_box = new wxBoxSizer(wxVERTICAL);
		tuning_title = flat::SectionTitle(this, _("Sensitivity"));
		tuning_box->Add(tuning_title, wxSizerFlags().Expand().Border(wxBOTTOM, FromDIP(6)));
		struct Spec { const char *key; wxString name, unit, tip; double min, max, step; int precision; };
		for (auto const& spec : {
			Spec{"featureThreshold", _("Threshold"), wxString(), _("How much must be there to count as a subtitle: higher finds fewer"), 0, 1000, 0.1, 3},
			Spec{"colourTolerance", _("Colour Tolerance"), wxString(), _("How far a colour may be from the text's and still count as it"), 0, 255, 1, 0},
			Spec{"iirPassDenoiseMinTime", _("Shortest"), _("ms"), _("A subtitle shown for less than this is taken for noise and dropped"), 0, 10000, 50, 0},
		}) {
			auto field = flat::NumberField::Create(this, spec.name, 0, spec.min, spec.max, spec.step, spec.precision, 64);
			field->Frame()->SetName(spec.key);
			field->SetToolTip(spec.tip);
			std::string key = spec.key;
			field->on_change = [this, key, precision = spec.precision](double v) {
				values[InPreset(key)] = precision ? from_wx(flat::FormatNumber(v, precision)) : std::to_string(static_cast<int>(v));
			};
			auto line = new wxBoxSizer(wxHORIZONTAL);
			line->Add(field->Frame());
			if (!spec.unit.empty()) line->Add(dim(spec.unit), wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(6)));
			tuning_box->Add(line, wxSizerFlags().Border(wxBOTTOM, FromDIP(4)));
			tunings.push_back({key, field, line});
		}

		auto right = new wxBoxSizer(wxVERTICAL);
		right->Add(grid, wxSizerFlags().Expand());
		right->Add(tuning_box, wxSizerFlags().Expand().Border(wxTOP, FromDIP(12)));
		auto body = new wxBoxSizer(wxHORIZONTAL);
		body->Add(left, wxSizerFlags(1).Expand());
		body->Add(right, wxSizerFlags().Expand().Border(wxLEFT, FromDIP(14)));

		auto main = new wxBoxSizer(wxVERTICAL);
		main->Add(body, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(10)));
		main->Add(buttons, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10)));
		ShowFrame(frame->GetInt());
		UpdateControls();
		SetSizerAndFit(main);
		CenterOnParent();
	}

	/// What it is run with: the OCR engine as shown, the other one off.
	/// 1.0's config has both on, shown as PaddleOCR: left so, it would
	/// run Tesseract too, which may not be installed.
	Values Chosen() const {
		Values chosen = values;
		if (ocr_engine && ocr && ocr->IsEnabled() && ocr->GetValue()) {
			bool const tesseract = Tesseract();
			chosen["ocr/doPaddle"] = tesseract ? "false" : "true";
			chosen["ocr/doTesseract"] = tesseract ? "true" : "false";
		}
		return chosen;
	}
};
}

std::optional<Values> ShowMagiaTimelineDialog(agi::Context *c, Installed const& installed, Values const& start,
                                              agi::fs::path const& config_file) {
	DialogMagiaTimeline d(c, installed, start, config_file);
	if (d.ShowModal() != wxID_OK) return std::nullopt;
	return d.Chosen();
}

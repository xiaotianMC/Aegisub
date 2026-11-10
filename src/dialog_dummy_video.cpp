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

#include "colour_button.h"
#include "compat.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "format.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "theme.h"
#include "video_provider_dummy.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/color.h>

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/valtext.h>

namespace {
struct DialogDummyVideo {
	wxDialog d;

	wxString fps     = OPT_GET("Video/Dummy/FPS String")->GetString();
	int width        = OPT_GET("Video/Dummy/Last/Width")->GetInt();
	int height       = OPT_GET("Video/Dummy/Last/Height")->GetInt();
	int length       = OPT_GET("Video/Dummy/Last/Length")->GetInt();
	agi::Color color = OPT_GET("Colour/Video Dummy/Last Colour")->GetColor();
	bool pattern     = OPT_GET("Video/Dummy/Pattern")->GetBool();

	FlatChoice *shortcuts;
	flat::NumberField *width_field, *height_field;
	wxStaticText *length_display;
	wxWindow *ok;

	/// Show the shortcut for the size chosen, if there is one
	void ShowShortcut();
	void OnResolutionShortcut(int index);
	/// Show the duration; the OK button only with a frame rate to make it
	void UpdateLengthDisplay();

	DialogDummyVideo(wxWindow *parent);
};

struct ResolutionShortcut {
	const wchar_t *name;
	int width;
	int height;
};

static ResolutionShortcut resolutions[] = {
	{wxTRANSLATE(L"640×480 (SD fullscreen)"), 640, 480},        // U+00D7 multiplication sign
	{wxTRANSLATE(L"704×480 (SD anamorphic)"), 704, 480},
	{wxTRANSLATE(L"640×360 (SD widescreen)"), 640, 360},
	{wxTRANSLATE(L"704×396 (SD widescreen)"), 704, 396},
	{wxTRANSLATE(L"640×352 (SD widescreen MOD16)"), 640, 352},
	{wxTRANSLATE(L"704×400 (SD widescreen MOD16)"), 704, 400},
	{wxTRANSLATE(L"1024×576 (SuperPAL widescreen)"), 1024, 576},
	{wxTRANSLATE(L"1280×720 (HD 720p)"), 1280, 720},
	{wxTRANSLATE(L"1920×1080 (FHD 1080p)"), 1920, 1080},
	{wxTRANSLATE(L"2560×1440 (QHD 1440p)"), 2560, 1440},
	{wxTRANSLATE(L"3840×2160 (4K UHD 2160p)"), 3840, 2160},
	{wxTRANSLATE(L"1080×1920 (FHD vertical)"), 1080, 1920},
};

DialogDummyVideo::DialogDummyVideo(wxWindow *parent)
: d(parent, -1, _("Dummy video options"))
{
	d.SetIcons(GETICONS(use_dummy_video_menu));
	theme::UseSurface(&d);
	int gap = d.FromDIP(10), row = d.FromDIP(6);

	// The size: a shortcut, or the width and the height
	wxString const resolution_name = flat::WithoutColon(_("Video resolution:"));
	shortcuts = new FlatChoice(&d, resolution_name);
	std::vector<wxString> names;
	for (auto const& res : resolutions) names.push_back(wxGetTranslation(res.name));
	shortcuts->Set(std::move(names));
	shortcuts->on_choose = [this](int index) { OnResolutionShortcut(index); };
	width_field = flat::NumberField::Create(&d, wxGETTEXT_IN_CONTEXT("resolution", "Width"), width, 1, 10000, 1, 0, 52);
	height_field = flat::NumberField::Create(&d, wxGETTEXT_IN_CONTEXT("resolution", "Height"), height, 1, 10000, 1, 0, 52);
	width_field->on_change = [this](double v) { width = static_cast<int>(v); ShowShortcut(); };
	height_field->on_change = [this](double v) { height = static_cast<int>(v); ShowShortcut(); };
	ShowShortcut();
	auto size_sizer = new wxBoxSizer(wxHORIZONTAL);
	size_sizer->Add(width_field->Frame(), wxSizerFlags(1).Expand());
	size_sizer->Add(flat::DimLabel(&d, L"×"), wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, row));
	size_sizer->Add(height_field->Frame(), wxSizerFlags(1).Expand());

	// The colour, plain or a checkerboard
	auto color_btn = new ColourButton(&d, wxSize(30, 14), false, color);
	color_btn->Bind(EVT_COLOR, [this](ValueEvent<agi::Color>& e) { color = e.Get(); });
	auto pattern_check = new FlatCheck(&d, _("Checkerboard &pattern"));
	pattern_check->SetValue(pattern);
	pattern_check->Bind(wxEVT_CHECKBOX, [this, pattern_check](wxCommandEvent&) { pattern = pattern_check->GetValue(); });
	auto color_sizer = new wxBoxSizer(wxHORIZONTAL);
	color_sizer->Add(color_btn, wxSizerFlags().CenterVertical());
	color_sizer->Add(pattern_check, wxSizerFlags().CenterVertical().Border(wxLEFT, gap));

	// The frame rate as typed: a number or a fraction
	auto fps_frame = new FlatFrame(&d, wxString());
	wxTextValidator fps_chars(wxFILTER_INCLUDE_CHAR_LIST);
	fps_chars.SetCharIncludes("0123456789./");
	auto fps_text = new wxTextCtrl(fps_frame, -1, fps, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE, fps_chars);
	fps_frame->SetContent(fps_text, 80);
	fps_text->Bind(wxEVT_TEXT, [this, fps_text](wxCommandEvent&) { fps = fps_text->GetValue(); UpdateLengthDisplay(); });

	// The length in frames, and how long that is at the frame rate
	auto length_field = flat::NumberField::Create(&d, wxString(), length, 2, 36000000, 1, 0, 80); // Ten hours of 1k FPS
	length_field->on_change = [this](double v) { length = static_cast<int>(v); UpdateLengthDisplay(); };
	length_display = flat::DimLabel(&d, "");
	auto length_sizer = new wxBoxSizer(wxHORIZONTAL);
	length_sizer->Add(length_field->Frame(), wxSizerFlags().Expand());
	length_sizer->Add(length_display, wxSizerFlags(1).CenterVertical().Border(wxLEFT, gap));

	auto grid = new wxFlexGridSizer(2, row, gap);
	grid->AddGrowableCol(1, 1);
	auto add = [&](wxString const& label, wxSizer *ctrl) {
		grid->Add(flat::DimLabel(&d, flat::WithoutColon(label)), wxSizerFlags().CenterVertical());
		grid->Add(ctrl, wxSizerFlags().Expand());
	};
	auto shortcut_sizer = new wxBoxSizer(wxHORIZONTAL);
	shortcut_sizer->Add(shortcuts, wxSizerFlags(1).Expand());
	add(_("Video resolution:"), shortcut_sizer);
	grid->AddSpacer(0);
	grid->Add(size_sizer, wxSizerFlags().Expand());
	add(_("Color:"), color_sizer);
	auto fps_sizer = new wxBoxSizer(wxHORIZONTAL);
	fps_sizer->Add(fps_frame, wxSizerFlags().Expand());
	add(_("Frame rate (fps):"), fps_sizer);
	add(_("Duration (frames):"), length_sizer);

	auto buttons = new flat::DialogButtons(&d, wxOK | wxCANCEL | wxHELP, "Dummy Video");
	ok = buttons->Get(wxID_OK);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(grid, wxSizerFlags(1).Expand().Border(wxALL, gap));
	main_sizer->Add(buttons, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, gap));

	UpdateLengthDisplay();
	// Room for the longest duration shown
	length_display->SetMinSize(wxSize(length_display->GetTextExtent(fmt_tl("Resulting duration: %s", "0:00:00.000")).x, -1));
	d.SetSizerAndFit(main_sizer);
	d.CenterOnParent();
}

void DialogDummyVideo::ShowShortcut() {
	for (size_t i = 0; i < std::size(resolutions); ++i) {
		if (resolutions[i].width == width && resolutions[i].height == height) {
			shortcuts->SetStringSelection(shortcuts->GetItems()[i]);
			return;
		}
	}
	shortcuts->SetStringSelection(wxString());
}

void DialogDummyVideo::OnResolutionShortcut(int index) {
	if (index < 0) return;
	width = resolutions[index].width;
	height = resolutions[index].height;
	width_field->SetValue(width);
	height_field->SetValue(height);
}

void DialogDummyVideo::UpdateLengthDisplay() {
	std::string dur = "-";
	auto fr = DummyVideoProvider::TryParseFramerate(from_wx(fps));
	if (fr.has_value())
		dur = agi::Time(fr.value().TimeAtFrame(length)).GetAssFormatted(true);
	length_display->SetLabel(fmt_tl("Resulting duration: %s", dur));
	ok->Enable(fr.has_value());
}
}

std::string CreateDummyVideo(wxWindow *parent) {
	DialogDummyVideo dlg(parent);
	if (dlg.d.ShowModal() != wxID_OK)
		return "";

	OPT_SET("Video/Dummy/FPS String")->SetString(from_wx(dlg.fps));
	OPT_SET("Video/Dummy/Last/Width")->SetInt(dlg.width);
	OPT_SET("Video/Dummy/Last/Height")->SetInt(dlg.height);
	OPT_SET("Video/Dummy/Last/Length")->SetInt(dlg.length);
	OPT_SET("Colour/Video Dummy/Last Colour")->SetColor(dlg.color);
	OPT_SET("Video/Dummy/Pattern")->SetBool(dlg.pattern);

	return DummyVideoProvider::MakeFilename(from_wx(dlg.fps), dlg.length, dlg.width, dlg.height, dlg.color, dlg.pattern);
}

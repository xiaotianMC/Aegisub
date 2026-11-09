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

#include "ass_file.h"
#include "subtitle_project_access.h"
#include "async_video_provider.h"
#include "compat.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "project.h"
#include "resolution_resampler.h"
#include "theme.h"

#include <boost/range/size.hpp>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace {

// Intentionally kept separate from agi::ycbcr::valid_header_strings:
// Resampling to or from "None" does not immediately make sense as the
// behavior of "None" depends on the (real) color space of the loaded video.
// Instead, allow choosing either explicit matrices for source and destination
// or a blank option to opt out of resampling the matrix.
constexpr std::string_view MatrixOptions[] = {
	"",
	"TV.601", "PC.601",
	"TV.709", "PC.709",
	"TV.FCC", "PC.FCC",
	"TV.240M", "PC.240M",
};

int MatrixOptionFromHeader(agi::ycbcr::Header header) {
	std::string header_string = header.to_existing_string();
	if (auto pos = std::ranges::find(MatrixOptions, header_string); pos != std::end(MatrixOptions))
		return std::distance(std::begin(MatrixOptions), pos);
	return 0;
}

/// @class DialogResample
/// @brief Configuration dialog for resolution resampling
///
/// Populate a ResampleSettings structure with data from the user
struct DialogResample {
	wxDialog d;
	agi::Context *c; ///< Project context

	ResampleSettings &settings;

	int script_w;
	int script_h;
	agi::ycbcr::Header script_mat;
	int video_w = 0;
	int video_h = 0;
	agi::ycbcr::header_colorspace video_mat;

	flat::NumberField *source_x;
	flat::NumberField *source_y;
	flat::NumberField *dest_x;
	flat::NumberField *dest_y;
	FlatChoice *source_matrix;
	FlatChoice *dest_matrix;
	FlatCheck *symmetrical;
	FlatSwitch *ar_mode;
	flat::NumberField *margin_ctrl[4];

	FlatButton *from_script;
	FlatButton *from_video;

	void OnMatrixChange();
	void SetSourceFromScript(wxCommandEvent &);
	/// Set the destination resolution to the video's resolution
	void SetDestFromVideo(wxCommandEvent &);
	/// Symmetrical checkbox toggle handler
	void OnSymmetrical(wxCommandEvent &);
	/// Copy margin values over if symmetrical is enabled
	void OnMarginChange(int src, int dst);
	void UpdateButtons();

public:
	/// Constructor
	/// @param context Project context
	/// @param[out] settings Settings struct to populate
	DialogResample(agi::Context *context, ResampleSettings &settings);
};

enum {
	LEFT = 0,
	RIGHT = 1,
	TOP = 2,
	BOTTOM = 3
};

DialogResample::DialogResample(agi::Context *c, ResampleSettings &settings)
: d(c->parent, -1, _("Resample Resolution"))
, c(c)
, settings(settings)
, script_mat(agi::ycbcr::Header(std::string(c->subtitleAccess->Read().GetScriptInfo("YCbCr Matrix"))).to_effective())
{
	d.SetIcons(GETICONS(resample_toolbutton));

	c->subtitleAccess->Read().GetResolution(script_w, script_h);
	settings.source_x = script_w;
	settings.source_y = script_h;

	if (auto provider = c->project->VideoProvider()) {
		settings.dest_x = video_w = provider->GetWidth();
		settings.dest_y = video_h = provider->GetHeight();
		video_mat = provider->GetRealColorSpace();
	}
	else {
		settings.dest_x = script_w;
		settings.dest_y = script_h;
	}

	theme::UseSurface(&d);
	wxWindow *parent = &d;
	int gap = d.FromDIP(10), row = d.FromDIP(6);
	auto dim = [&](wxString const& text) { return flat::DimLabel(parent, text); };
	// The matrices by name; the first, no name, leaves the colours alone
	std::vector<wxString> matrices;
	for (auto name : MatrixOptions) matrices.push_back(name.empty() ? wxString(L"\u2014") : to_wx(name));

	// A resolution: width × height, the button setting it back, and the matrix
	auto resolution = [&](wxString const& title, flat::NumberField *&x, flat::NumberField *&y, int &value_x, int &value_y,
	                      FlatButton *&reset, wxString const& reset_label, FlatChoice *&matrix) {
		x = flat::NumberField::Create(parent, _("Width"), value_x, 1, 999999, 1, 0, 56);
		y = flat::NumberField::Create(parent, _("Height"), value_y, 1, 999999, 1, 0, 56);
		x->on_change = [this, &value_x](double v) { value_x = static_cast<int>(v); UpdateButtons(); };
		y->on_change = [this, &value_y](double v) { value_y = static_cast<int>(v); UpdateButtons(); };
		reset = new FlatButton(parent, reset_label);
		matrix = new FlatChoice(parent, flat::WithoutColon(_("YCbCr Matrix:")));
		matrix->Set(matrices);
		matrix->SetStringSelection(matrices[0]);
		matrix->SetToolTip(flat::WithoutColon(_("YCbCr Matrix:")));
		matrix->on_choose = [this](int) { OnMatrixChange(); };

		auto line = new wxBoxSizer(wxHORIZONTAL);
		line->Add(x->Frame(), wxSizerFlags(1).Expand());
		line->Add(dim(L"\u00D7"), wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, d.FromDIP(4)));
		line->Add(y->Frame(), wxSizerFlags(1).Expand());
		line->Add(reset, wxSizerFlags().Border(wxLEFT, row));
		auto matrix_line = new wxBoxSizer(wxHORIZONTAL);
		matrix_line->Add(dim(flat::WithoutColon(_("YCbCr Matrix:"))), wxSizerFlags().CenterVertical());
		matrix_line->Add(matrix, wxSizerFlags(1).Expand().Border(wxLEFT, gap));
		auto group = new wxBoxSizer(wxVERTICAL);
		group->Add(flat::SectionTitle(parent, title), wxSizerFlags().Expand());
		group->Add(line, wxSizerFlags().Expand().Border(wxTOP, row));
		group->Add(matrix_line, wxSizerFlags().Expand().Border(wxTOP, row));
		return group;
	};
	auto source = resolution(_("Source Resolution"), source_x, source_y, settings.source_x, settings.source_y,
		from_script, _("From s&cript"), source_matrix);
	auto dest = resolution(_("Destination Resolution"), dest_x, dest_y, settings.dest_x, settings.dest_y,
		from_video, _("From &video"), dest_matrix);

	// What to do when the aspect ratio changes
	ar_mode = new FlatSwitch(parent, {_("Stretch"), _("Add borders"), _("Remove borders"), _("Manual")});
	ar_mode->SetSelection(static_cast<int>(settings.ar_mode));
	ar_mode->on_choose = [this](int mode) { this->settings.ar_mode = static_cast<ResampleARMode>(mode); UpdateButtons(); };

	// The margins, around their tick for keeping them even
	const wxString margin_names[] = {_("Left"), _("Right"), _("Top"), _("Bottom")};
	for (int i = 0; i < 4; ++i) {
		margin_ctrl[i] = flat::NumberField::Create(parent, margin_names[i], settings.margin[i], -9999, 9999, 1, 0, 40);
		margin_ctrl[i]->on_change = [this, i](double v) {
			this->settings.margin[i] = static_cast<int>(v);
			if (i == LEFT) OnMarginChange(LEFT, RIGHT);
			if (i == TOP) OnMarginChange(TOP, BOTTOM);
		};
	}
	symmetrical = new FlatCheck(parent, _("&Symmetrical"));
	symmetrical->SetValue(true);
	auto margin_sizer = new wxGridSizer(3, 3, row, row);
	margin_sizer->AddSpacer(1);
	margin_sizer->Add(margin_ctrl[TOP]->Frame(), wxSizerFlags(1).Expand());
	margin_sizer->AddSpacer(1);
	margin_sizer->Add(margin_ctrl[LEFT]->Frame(), wxSizerFlags(1).Expand());
	margin_sizer->Add(symmetrical, wxSizerFlags(1).Center());
	margin_sizer->Add(margin_ctrl[RIGHT]->Frame(), wxSizerFlags(1).Expand());
	margin_sizer->AddSpacer(1);
	margin_sizer->Add(margin_ctrl[BOTTOM]->Frame(), wxSizerFlags(1).Expand());
	margin_sizer->AddSpacer(1);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(source, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	main_sizer->Add(dest, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	main_sizer->Add(flat::SectionTitle(parent, _("Aspect Ratio Handling")), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	main_sizer->Add(ar_mode, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, row));
	main_sizer->Add(flat::SectionTitle(parent, _("Margin offset")), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
	main_sizer->Add(margin_sizer, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, row));
	main_sizer->Add(new flat::DialogButtons(&d, wxOK | wxCANCEL | wxHELP, "Resample resolution"), wxSizerFlags().Expand().Border(wxALL, gap));
	d.SetSizerAndFit(main_sizer);
	d.CenterOnParent();

	UpdateButtons();

	from_video->Bind(wxEVT_BUTTON, &DialogResample::SetDestFromVideo, this);
	from_script->Bind(wxEVT_BUTTON, &DialogResample::SetSourceFromScript, this);
	symmetrical->Bind(wxEVT_CHECKBOX, &DialogResample::OnSymmetrical, this);

	// Find out if we want to suggest resampling colors.
	// If the script matrix is None, we do not know what matrix to resample from, so we don't suggest to resample.
	// If the video matrix is unspecified or does not correspond to a valid YCbCr Header value, we cannot currently resample to it.
	// -> FIXME: The latter is not a theoretical reason but only due to the fact that resampling is not currently implemented for any other matrices in ycbcr_conv.h:
	//    We could in theory have a situation where a script with no YCbCr Header (so effectively TV.601) was authored on a BT.2020 video using color matching,
	//    and the user now wants to adjust the colors so that they can set the matrix to None instead.
	// Otherwise, i.e. if e.g. the script has a TV.601 header but the video uses TV.709, we suggest to resample.
	// (The assumption here being that, just like when resampling PlayRes, the user only uses the resample dialog when the script *currently* looks correct on the current video.)
	agi::ycbcr::Header video_mat_header(video_mat);
	if (video_mat_header != script_mat
			&& video_mat_header.valid()		//< (Not necessary in theory, see FIXME above)
			&& script_mat.valid()			//< (Should always be true since we use to_effective() above)
			&& std::holds_alternative<agi::ycbcr::header_colorspace>(video_mat_header)
			&& std::holds_alternative<agi::ycbcr::header_colorspace>(script_mat)
	) {
		source_matrix->SetStringSelection(source_matrix->GetItems()[MatrixOptionFromHeader(script_mat)]);
		dest_matrix->SetStringSelection(dest_matrix->GetItems()[MatrixOptionFromHeader(video_mat_header)]);
	}
	OnMatrixChange();
}

void DialogResample::OnMatrixChange() {
	auto matrix = [](FlatChoice *choice) {
		int index = std::max(0, choice->GetSelection());
		return agi::ycbcr::Header(std::string(MatrixOptions[index]));
	};
	agi::ycbcr::Header src_mat(matrix(source_matrix));
	agi::ycbcr::Header dst_mat(matrix(dest_matrix));

	if (std::holds_alternative<agi::ycbcr::header_colorspace>(src_mat) && std::holds_alternative<agi::ycbcr::header_colorspace>(dst_mat)) {
		settings.matrix_conversion = std::make_pair(std::get<agi::ycbcr::header_colorspace>(src_mat), std::get<agi::ycbcr::header_colorspace>(dst_mat));
	} else {
		settings.matrix_conversion = std::nullopt;
	}
}

void DialogResample::SetDestFromVideo(wxCommandEvent &) {
	dest_x->SetValue(settings.dest_x = video_w);
	dest_y->SetValue(settings.dest_y = video_h);
	dest_matrix->SetStringSelection(dest_matrix->GetItems()[MatrixOptionFromHeader(agi::ycbcr::Header(video_mat))]);
	OnMatrixChange();
	UpdateButtons();
}

void DialogResample::SetSourceFromScript(wxCommandEvent&) {
	source_x->SetValue(settings.source_x = script_w);
	source_y->SetValue(settings.source_y = script_h);
	source_matrix->SetStringSelection(source_matrix->GetItems()[MatrixOptionFromHeader(script_mat)]);
	OnMatrixChange();
	UpdateButtons();
}

void DialogResample::UpdateButtons() {
	from_video->Enable(c->project->VideoProvider() &&
		(dest_x->GetInt() != video_w || dest_y->GetInt() != video_h));
	from_script->Enable(source_x->GetInt() != script_w || source_y->GetInt() != script_h);

	auto source_ar = double(source_x->GetInt()) / source_y->GetInt();
	auto dest_ar = double(dest_x->GetInt()) / dest_y->GetInt();
	bool ar_changed = std::abs(source_ar - dest_ar) / dest_ar > .01;

	ar_mode->Enable(ar_changed);

	bool margins = ar_changed && ar_mode->GetSelection() == (int)ResampleARMode::Manual;
	symmetrical->Enable(margins);
	margin_ctrl[LEFT]->Enable(margins);
	margin_ctrl[TOP]->Enable(margins);
	margin_ctrl[RIGHT]->Enable(margins && !symmetrical->GetValue());
	margin_ctrl[BOTTOM]->Enable(margins && !symmetrical->GetValue());
}

void DialogResample::OnSymmetrical(wxCommandEvent &) {
	UpdateButtons();
	OnMarginChange(LEFT, RIGHT);
	OnMarginChange(TOP, BOTTOM);
}

void DialogResample::OnMarginChange(int src, int dst) {
	if (!symmetrical->IsChecked()) return;
	settings.margin[dst] = settings.margin[src];
	margin_ctrl[dst]->SetValue(settings.margin[src]);
}
}

bool PromptForResampleSettings(agi::Context *c, ResampleSettings &settings) {
	return DialogResample(c, settings).d.ShowModal() == wxID_OK;
}

// Copyright (c) 2007, Rodrigo Braz Monteiro
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

/// @file subs_preview.cpp
/// @brief Preview control using a dummy video provider and subtitles provider to render a preview
/// @ingroup custom_control
///

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "dialog_progress.h"
#include "subs_preview.h"
#include "include/aegisub/subtitles_provider.h"
#include "video_frame.h"
#include "video_provider_dummy.h"


#include <algorithm>
#include <cmath>

#include <wx/dcclient.h>
#include <wx/msgdlg.h>

SubtitlesPreview::SubtitlesPreview(wxWindow *parent, wxSize size, int winStyle, agi::Color col)
: wxWindow(parent, -1, wxDefaultPosition, size, winStyle)
, style(new AssStyle)
, back_color(col)
, sub_file(std::make_unique<AssFile>())
, line(new AssDialogue)
{
	line->Text = "{\\q2}preview";

	SetStyle(*style);

	sub_file->LoadDefault();
	sub_file->Styles.push_back(*style);
	sub_file->Events.push_back(*line);

	SetSizeHints(size.GetWidth(), size.GetHeight(), -1, -1);
	wxSizeEvent evt(size);
	OnSize(evt);
	UpdateBitmap();

	Bind(wxEVT_PAINT, &SubtitlesPreview::OnPaint, this);
	Bind(wxEVT_SIZE, &SubtitlesPreview::OnSize, this);
}

SubtitlesPreview::~SubtitlesPreview() {
}

namespace {
/// Relative luminance, as WCAG reckons it
double Luminance(agi::Color c) {
	auto channel = [](unsigned char v) {
		double x = v / 255.;
		return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
	};
	return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}

double Contrast(agi::Color a, agi::Color b) {
	double la = Luminance(a), lb = Luminance(b);
	return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
}

agi::Color PreviewBackdrop(AssStyle const& style, agi::Color back) {
	// An alpha of 255 is transparent
	bool fill = style.primary.a < 255;
	bool outline = style.outline_w > 0 && style.outline.a < 255;
	// An outline set apart from the fill frames the text on any background
	if (fill && outline && Contrast(style.primary, style.outline) >= 4.5) return back;
	if (!fill && !outline) return back;
	// Otherwise the text is one colour, which the background must stand
	// apart from. Dark text on a middling grey is darker than the contrast
	// ratio says: it wants a light background.
	agi::Color ink = fill ? style.primary : style.outline;
	bool dark = Luminance(ink) < 0.18;
	if (dark ? Luminance(back) >= 0.45 : Contrast(ink, back) >= 4.5) return back;
	return dark ? agi::Color(232, 234, 238) : agi::Color(40, 44, 52);
}

agi::Color SubtitlesPreview::Backdrop() const {
	return PreviewBackdrop(*style, back_color);
}

void SubtitlesPreview::UpdateBackdrop() {
	auto wanted = Backdrop();
	if (vid && wanted == shown_back) return;
	shown_back = wanted;
	if (bmp) vid = std::make_unique<DummyVideoProvider>(0.0, 10, bmp->GetWidth(), bmp->GetHeight(), shown_back, true);
}

void SubtitlesPreview::SetStyle(AssStyle const& new_style) {
	if (provider && style->font != new_style.font)
		provider->Reinitialize();

	*style = new_style;
	style->name = "Default";
	style->alignment = 5;
	std::fill(style->Margin.begin(), style->Margin.end(), 0);
	style->UpdateData();
	UpdateBackdrop();
	UpdateBitmap();
}

void SubtitlesPreview::SetText(std::string const& text) {
	std::string new_text = "{\\q2}" + text;
	if (new_text != line->Text) {
		line->Text = new_text;
		UpdateBitmap();
	}
}

void SubtitlesPreview::SetColour(agi::Color col) {
	if (col != back_color) {
		back_color = col;
		UpdateBackdrop();
		UpdateBitmap();
	}
}

void SubtitlesPreview::UpdateBitmap() {
	if (!vid) return;

	VideoFrame frame;
	vid->GetFrame(0, frame);

	if (provider) {
		try {
			provider->LoadSubtitles(sub_file.get());
			provider->DrawSubtitles(frame, 0.1);
		}
		catch (...) { }
	}

	// Convert frame to bitmap
	*bmp = static_cast<wxBitmap>(GetImage(frame));
	Refresh();
}

void SubtitlesPreview::OnPaint(wxPaintEvent &) {
	wxPaintDC(this).DrawBitmap(*bmp, 0, 0);
}

void SubtitlesPreview::OnSize(wxSizeEvent &evt) {
	if (bmp.get() && evt.GetSize() == bmp->GetSize()) return;

	int w = evt.GetSize().GetWidth();
	int h = evt.GetSize().GetHeight();

	bmp = std::make_unique<wxBitmap>(w, h, -1);
	shown_back = Backdrop();
	vid = std::make_unique<DummyVideoProvider>(0.0, 10, w, h, shown_back, true);
	try {
		if (!progress)
			progress = std::make_unique<DialogProgress>(this);
		if (!provider)
			provider = SubtitlesProviderFactory::GetProvider(progress.get());
	}
	catch (...) {
		wxMessageBox(
			_("Could not get any subtitles provider for the preview box. Make sure that you have a provider installed."),
			_("No subtitles provider"), wxOK | wxICON_ERROR | wxCENTER);
	}

	sub_file->SetScriptInfo("PlayResX", std::to_string(w));
	sub_file->SetScriptInfo("PlayResY", std::to_string(h));

	UpdateBitmap();
}

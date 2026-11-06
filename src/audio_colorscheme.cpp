// Copyright (c) 2009-2010, Niels Martin Hansen
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

#include "audio_colorscheme.h"

#include "audio_rendering_style.h"
#include "colorspace.h"
#include "options.h"

#include <libaegisub/exception.h>
#include <libaegisub/string.h>

#include <algorithm>

AudioColorScheme::AudioColorScheme(int prec, std::string const& scheme_name, int audio_rendering_style)
: palette((3<<prec) + 3)
, factor((size_t)1<<prec)
{
	std::string opt_base = agi::Str("Colour/Schemes/", scheme_name, "/");
	switch (static_cast<AudioRenderingStyle>(audio_rendering_style))
	{
		case AudioStyle_Normal:   opt_base += "Normal/"; break;
		case AudioStyle_Inactive: opt_base += "Inactive/"; break;
		case AudioStyle_Selected: opt_base += "Selection/"; break;
		case AudioStyle_Primary:  opt_base += "Primary/"; break;
		default: throw agi::InternalError("Unknown audio rendering styling");
	}

	double h_base  = OPT_GET(opt_base + "Hue Offset")->GetDouble();
	double h_scale = OPT_GET(opt_base + "Hue Scale")->GetDouble();
	double s_base  = OPT_GET(opt_base + "Saturation Offset")->GetDouble();
	double s_scale = OPT_GET(opt_base + "Saturation Scale")->GetDouble();
	double l_base  = OPT_GET(opt_base + "Lightness Offset")->GetDouble();
	double l_scale = OPT_GET(opt_base + "Lightness Scale")->GetDouble();

	for (size_t i = 0; i <= factor; ++i)
	{
		auto t = (double)i / factor;
		hsl_to_rgb(
			mid<int>(0, h_base + t * h_scale, 255),
			mid<int>(0, s_base + t * s_scale, 255),
			mid<int>(0, l_base + t * l_scale, 255),
			&palette[i * 3 + 0],
			&palette[i * 3 + 1],
			&palette[i * 3 + 2]);
	}
}

void MigrateAudioColourSchemes() {
	// The first Studio drew its time ruler and scrollbar green on black; they
	// are grey now. Only colours still at that first value change.
	auto update = [](const char *name, agi::Color old_value, agi::Color new_value) {
		auto opt = OPT_SET(std::string("Colour/Schemes/Studio/") + name);
		if (opt->GetColor() == old_value) opt->SetColor(new_value);
	};
	update("UI/Light", agi::Color(64, 220, 152), agi::Color(150, 150, 150));
	update("UI/Dark", agi::Color(0, 0, 0), agi::Color(38, 38, 38));
	update("UI/Selection", agi::Color(18, 64, 44), agi::Color(28, 84, 58));
	update("UI Focused/Light", agi::Color(120, 240, 186), agi::Color(196, 196, 196));
	update("UI Focused/Dark", agi::Color(0, 0, 0), agi::Color(38, 38, 38));
	update("UI Focused/Selection", agi::Color(18, 64, 44), agi::Color(28, 84, 58));

	auto schemes = OPT_GET("Audio/Colour Schemes")->GetListString();
	if (std::find(schemes.begin(), schemes.end(), "Studio") != schemes.end()) return;
	schemes.insert(schemes.begin(), "Studio");
	OPT_SET("Audio/Colour Schemes")->SetListString(std::move(schemes));
	if (OPT_GET("Colour/Audio Display/Waveform")->GetString() == "Green")
		OPT_SET("Colour/Audio Display/Waveform")->SetString("Studio");
}

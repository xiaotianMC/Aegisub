// Copyright (c) 2005, Rodrigo Braz Monteiro
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

#include "export_framerate.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "async_video_provider.h"
#include "compat.h"
#include "flat_controls.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "project.h"

#include <libaegisub/of_type_adaptor.h>

#include <utility>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

AssTransformFramerateFilter::AssTransformFramerateFilter()
: AssExportFilter(from_wx(_("Transform Framerate")),
	from_wx(_("Transform subtitle times, including those in override tags, from an input framerate to an output framerate.\n\nThis is useful for converting regular time subtitles to VFRaC time subtitles for hardsubbing.\nIt can also be used to convert subtitles to a different speed video, such as NTSC to PAL speedup.")),
	1000)
{
}

void AssTransformFramerateFilter::ProcessSubs(AssFile *subs, wxWindow *) {
	TransformFrameRate(subs);
}

wxWindow *AssTransformFramerateFilter::GetConfigDialogWindow(wxWindow *parent, agi::Context *c) {
	LoadSettings(true, c);

	wxWindow *base = new wxPanel(parent, -1);
	auto dim = [base](wxString const& text) {
		auto label = new wxStaticText(base, -1, text);
		label->SetForegroundColour(flat::Colours(base).dim);
		return label;
	};
	auto field = [base](wxTextCtrl *&text, wxString const& value) {
		auto frame = new FlatFrame(base, wxString());
		text = new wxTextCtrl(frame, -1, value, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		frame->SetContent(text, 64);
		return frame;
	};

	// Input: a frame rate, or the video's
	wxString initialInput;
	auto FromVideo = new FlatButton(base, _("From &video"));
	if (Input.IsLoaded()) {
		initialInput = fmt_wx("%2.3f", Input.FPS());
		FromVideo->Bind(wxEVT_BUTTON, [c, this](wxCommandEvent&) {
			InputFramerate->SetValue(fmt_wx("%g", c->project->Timecodes().FPS()));
		});
	}
	else {
		initialInput = "23.976";
		FromVideo->Enable(false);
	}
	auto InputSizer = new wxBoxSizer(wxHORIZONTAL);
	InputSizer->Add(field(InputFramerate, initialInput), wxSizerFlags().Expand());
	InputSizer->Add(FromVideo, wxSizerFlags().Border(wxLEFT, base->FromDIP(6)));

	// Output: the timecodes as they vary, or a constant frame rate
	OutputMode = new FlatSwitch(base, {_("V&ariable"), wxString(_("&Constant: ")).Trim().BeforeLast(':')});
	auto output_frame = field(OutputFramerate, initialInput);
	if (!Output.IsVFR()) {
		OutputMode->EnableItem(0, false);
		OutputMode->SetSelection(1);
	}
	output_frame->Enable(OutputMode->GetSelection() == 1);
	OutputFramerate->Enable(OutputMode->GetSelection() == 1);
	OutputMode->on_choose = [=, this](int mode) {
		output_frame->Enable(mode == 1);
		OutputFramerate->Enable(mode == 1);
	};
	auto OutputSizer = new wxBoxSizer(wxHORIZONTAL);
	OutputSizer->Add(OutputMode, wxSizerFlags().Expand());
	OutputSizer->Add(output_frame, wxSizerFlags().Expand().Border(wxLEFT, base->FromDIP(6)));

	Reverse = new FlatCheck(base, _("&Reverse transformation"));

	auto MainSizer = new wxFlexGridSizer(2, base->FromDIP(6), base->FromDIP(10));
	MainSizer->Add(dim(wxString(_("Input framerate: ")).Trim().BeforeLast(':')), wxSizerFlags().CenterVertical());
	MainSizer->Add(InputSizer);
	MainSizer->Add(dim(wxString(_("Output: ")).Trim().BeforeLast(':')), wxSizerFlags().CenterVertical());
	MainSizer->Add(OutputSizer);
	MainSizer->AddSpacer(0);
	MainSizer->Add(Reverse);

	// Window
	base->SetSizerAndFit(MainSizer);
	return base;
}

void AssTransformFramerateFilter::LoadSettings(bool is_default, agi::Context *c) {
	this->c = c;

	if (is_default) {
		auto provider = c->project->VideoProvider();
		Output = c->project->Timecodes();
		Input = provider ? provider->GetFPS() : Output;
	}
	else {
		double temp;
		InputFramerate->GetValue().ToDouble(&temp);
		Input = temp;
		if (OutputMode->GetSelection() == 1) {
			OutputFramerate->GetValue().ToDouble(&temp);
			Output = temp;
		}
		else Output = c->project->Timecodes();

		if (Reverse->IsChecked())
			std::swap(Input, Output);
	}
}

/// Truncate a time to centisecond precision
static int trunc_cs(int time) {
	return (time / 10) * 10;
}

void AssTransformFramerateFilter::TransformTimeTags(std::string const&, AssOverrideParameter *curParam, void *curData) {
	VariableDataType type = curParam->GetType();
	if (type != VariableDataType::INT && type != VariableDataType::FLOAT) return;

	AssTransformFramerateFilter *instance = static_cast<AssTransformFramerateFilter*>(curData);
	AssDialogue *curDiag = instance->line;

	int parVal = curParam->Get<int>();

	switch (curParam->classification) {
		case AssParameterClass::RELATIVE_TIME_START: {
			int value = instance->ConvertTime(trunc_cs(curDiag->Start) + parVal) - instance->newStart;

			// An end time of 0 is actually the end time of the line, so ensure
			// nonzero is never converted to 0
			// Needed here rather than the end case because start/end here mean
			// which end of the line the time is relative to, not whether it's
			// the start or end time (compare \move and \fad)
			if (value == 0 && parVal != 0) value = 1;
			curParam->Set(value);
			break;
		}
		case AssParameterClass::RELATIVE_TIME_END:
			curParam->Set(instance->newEnd - instance->ConvertTime(trunc_cs(curDiag->End) - parVal));
			break;
		case AssParameterClass::KARAOKE: {
			int start = curDiag->Start / 10 + instance->oldK + parVal;
			int value = (instance->ConvertTime(start * 10) - instance->newStart) / 10 - instance->newK;
			instance->oldK += parVal;
			instance->newK += value;
			curParam->Set(value);
			break;
		}
		default:
			return;
	}
}

void AssTransformFramerateFilter::TransformFrameRate(AssFile *subs) {
	if (!Input.IsLoaded() || !Output.IsLoaded()) return;
	for (auto& curDialogue : subs->Events) {
		line = &curDialogue;
		newK = 0;
		oldK = 0;
		newStart = trunc_cs(ConvertTime(curDialogue.Start));
		newEnd = trunc_cs(ConvertTime(curDialogue.End) + 9);

		// Process stuff
		auto blocks = line->ParseTags();
		for (auto block : blocks | agi::of_type<AssDialogueBlockOverride>())
			block->ProcessParameters(TransformTimeTags, this);
		curDialogue.Start = newStart;
		curDialogue.End = newEnd;
		curDialogue.UpdateText(blocks);
	}
}

int AssTransformFramerateFilter::ConvertTime(int time) {
	int frame = Output.FrameAtTime(time);
	int frameStart = Output.TimeAtFrame(frame);
	int frameEnd = Output.TimeAtFrame(frame + 1);
	int frameDur = frameEnd - frameStart;
	double dist = double(time - frameStart) / frameDur;

	int newStart = Input.TimeAtFrame(frame);
	int newEnd = Input.TimeAtFrame(frame + 1);
	int newDur = newEnd - newStart;

	return newStart + newDur * dist;
}

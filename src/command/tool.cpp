// Copyright (c) 2005-2010, Niels Martin Hansen
// Copyright (c) 2005-2010, Rodrigo Braz Monteiro
// Copyright (c) 2010, Amar Takhar
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

#include "command.h"
#include "../subtitle_project_access.h"
#include "../subtitle_value_edit.h"

#include "../compat.h"
#include "../dialog_manager.h"
#include "../dialog_translation.h"
#include "../dialogs.h"
#include "../dialog_magia_timeline.h"
#include "../include/aegisub/context.h"
#include "../libresrc/libresrc.h"
#include "../resolution_resampler.h"
#include "../video_controller.h"

#include <libaegisub/fs.h>
#include <libaegisub/path.h>

#include "../ass_dialogue.h"
#include "../ass_file.h"
#include "../format.h"
#include "../frame_main.h"
#include "../magia_timeline.h"
#include "../options.h"
#include "../project.h"
#include "../subtitle_format.h"
#include "../subtitle_row_list_edit.h"

#include <libaegisub/io.h>

#include <algorithm>
#include <optional>

#include <wx/dirdlg.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>

namespace {
	using cmd::Command;

struct tool_export final : public Command {
	CMD_NAME("tool/export")
	CMD_ICON(export_menu)
	STR_MENU("&Export Subtitles...")
	STR_DISP("Export Subtitles")
	STR_HELP("Save a copy of subtitles in a different format or with processing applied to it")

	void operator()(agi::Context *c) override {
		c->videoController->Stop();
		ShowExportDialog(c);
	}
};

struct tool_magiatimeline final : public Command {
	CMD_NAME("tool/magiatimeline")
	STR_MENU("Time &Hardsubs with MagiaTimeline...")
	STR_DISP("Time Hardsubs with MagiaTimeline")
	STR_HELP("Run MagiaTimeline on the video to find when the subtitles burnt into it show, and add a line for each, with the text it reads if it is set to")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return !!c->project->VideoProvider() && !c->project->VideoName().empty();
	}

	static void Fail(agi::Context *c, wxString const& message) {
		wxMessageBox(message, _("Time Hardsubs with MagiaTimeline"), wxOK | wxICON_ERROR, c->parent);
	}

	void operator()(agi::Context *c) override {
		c->videoController->Stop();
		auto const video = c->project->VideoName();

		// Where it is installed: asked for once, then kept
		auto launch = magia_timeline::FindLaunch(OPT_GET("Tool/MagiaTimeline/Path")->GetString());
		if (!launch) {
			wxString folder = wxDirSelector(_("Choose the folder MagiaTimeline is installed in"), "", wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST, wxDefaultPosition, c->parent);
			if (folder.empty()) return;
			launch = magia_timeline::FindLaunch(from_wx(folder));
			if (!launch) {
				Fail(c, _("There is no MagiaTimeline in that folder: it should hold MagiaTimeline.py (set up with its install script) or MagiaTimeline.exe."));
				return;
			}
			OPT_SET("Tool/MagiaTimeline/Path")->SetString(from_wx(folder));
		}

		// Its own config, with the video and where to write put in
		std::string config;
		try {
			auto in = agi::io::Open(launch->folder / "config.yml");
			config.assign(std::istreambuf_iterator<char>(*in), std::istreambuf_iterator<char>());
		}
		catch (agi::Exception const&) {
			Fail(c, fmt_tl("MagiaTimeline's config.yml could not be read in %s.", launch->folder.string()));
			return;
		}
		// What to run it with: its config, with what was chosen before over
		// it, as chosen now
		auto const installed = magia_timeline::Inspect(config);
		auto const saved = magia_timeline::ParseSaved(OPT_GET("Tool/MagiaTimeline/Settings")->GetString());
		auto chosen = ShowMagiaTimelineDialog(c, installed, magia_timeline::Effective(installed, saved),
			launch->folder / "config.yml");
		if (!chosen) return;
		auto overrides = magia_timeline::Changes(installed, *chosen);
		OPT_SET("Tool/MagiaTimeline/Settings")->SetString(magia_timeline::FormatSaved(overrides));
		auto jobs = magia_timeline::ParseList(chosen->count("extraJobs") ? chosen->at("extraJobs") : "");
		bool const reads_text = std::find(jobs.begin(), jobs.end(), "ocr") != jobs.end();
		// The text it reads put in the lines it writes, where it can
		if (reads_text && installed.Has("ocr/standaloneOutput")) overrides["ocr/standaloneOutput"] = "false";

		auto const work = config::path->Decode("?temp/");
		auto const destination = work / "aegisub-magiatimeline";
		auto const output = work / "aegisub-magiatimeline.ass";
		auto const config_file = work / "aegisub-magiatimeline.yml";
		// 1.0 writes the text to a file of its own beside it
		std::optional<agi::fs::path> ocr_output;
		if (reads_text && installed.Has("ocr/suffix"))
			ocr_output = agi::fs::path(destination.string() + magia_timeline::Unquote(installed.Get("ocr/suffix")));
		auto written = magia_timeline::WriteConfig(config, video.string(), destination.string(), overrides);
		if (!written) {
			Fail(c, _("MagiaTimeline's config.yml has no source and destination to fill in."));
			return;
		}
		try {
			agi::fs::CreateDirectory(work);
			if (agi::fs::FileExists(output)) agi::fs::Remove(output);
			if (ocr_output && agi::fs::FileExists(*ocr_output)) agi::fs::Remove(*ocr_output);
			agi::io::Save(config_file).Get() << *written;
		}
		catch (agi::Exception const& e) {
			Fail(c, to_wx(e.GetMessage()));
			return;
		}

		auto outcome = magia_timeline::Run(*launch, config_file, c->parent);
		if (outcome.cancelled) return;
		bool const wrote = agi::fs::FileExists(output);
		if (outcome.reported_error || !wrote) {
			Fail(c, _("MagiaTimeline did not finish. The last of what it said:") + "\n\n" +
				to_wx(magia_timeline::LastLines(magia_timeline::WithoutProgress(outcome.output), 15)));
			return;
		}

		AssFile result;
		try {
			auto reader = SubtitleFormat::GetReader(output, "UTF-8");
			reader->ReadFile(&result, output, c->project->Timecodes(), "UTF-8");
		}
		catch (agi::Exception const& e) {
			Fail(c, to_wx(e.GetMessage()));
			return;
		}
		auto intervals = magia_timeline::ReadTimeline(result);
		if (ocr_output) {
			try {
				auto in = agi::io::Open(*ocr_output);
				std::string text(std::istreambuf_iterator<char>(*in), std::istreambuf_iterator<char>{});
				magia_timeline::AddReadText(intervals, text);
			}
			catch (agi::Exception const&) { }
		}
		if (intervals.empty()) {
			wxMessageBox(_("MagiaTimeline found no subtitles in the video."), _("Time Hardsubs with MagiaTimeline"), wxOK | wxICON_INFORMATION, c->parent);
			return;
		}

		// A line for each, after the others, in the style of the current one
		auto edit_scope = c->subtitleAccess->TryWrite();
		if (!edit_scope) return;
		SubtitleRowListEdit staged(c);
		std::string const style = staged.active ? staged.active->Style.get() : std::string("Default");
		std::set<AssDialogue*> added;
		AssDialogue *first = nullptr;
		for (auto const& interval : intervals) {
			auto line = new AssDialogue;
			line->Start = interval.start;
			line->End = interval.end;
			line->Style = style;
			line->Text = interval.text;
			staged.file.Events.push_back(*line);
			added.insert(line);
			if (!first) first = line;
		}
		staged.Commit(_("MagiaTimeline"), AssFile::COMMIT_DIAG_ADDREM);
		staged.Select(added, first);
		if (c->frame)
			c->frame->StatusTimeout(fmt_plural(intervals.size(), "Added %d line timed by MagiaTimeline", "Added %d lines timed by MagiaTimeline", static_cast<int>(intervals.size())));
	}
};

struct tool_font_collector final : public Command {
	CMD_NAME("tool/font_collector")
	CMD_ICON(font_collector_button)
	STR_MENU("&Fonts Collector...")
	STR_DISP("Fonts Collector")
	STR_HELP("Open fonts collector")

	void operator()(agi::Context *c) override {
		ShowFontsCollectorDialog(c);
	}
};

struct tool_line_select final : public Command {
	CMD_NAME("tool/line/select")
	CMD_ICON(select_lines_button)
	STR_MENU("S&elect Lines...")
	STR_DISP("Select Lines")
	STR_HELP("Select lines based on defined criteria")

	void operator()(agi::Context *c) override {
		ShowSelectLinesDialog(c);
	}
};

struct tool_resampleres final : public Command {
	CMD_NAME("tool/resampleres")
	CMD_ICON(resample_toolbutton)
	STR_MENU("&Resample Resolution...")
	STR_DISP("Resample Resolution")
	STR_HELP("Resample subtitles to maintain their current appearance at a different script resolution")

	void operator()(agi::Context *c) override {
		// C1-C1c-13 (audit B6): refuse before the dialog, but stage and write
		// the live model only after it closed; the lease is not held across it.
		if (!c->subtitleAccess->TryWrite()) return;
		c->videoController->Stop();
		ResampleSettings settings;
		if (!PromptForResampleSettings(c, settings)) return;
        auto edit_scope = c->subtitleAccess->TryWrite();
        if (!edit_scope) return;
        SubtitleValueEdit staged(c);
		ResampleResolution(&staged.file, settings);
		staged.Publish(AssFile::COMMIT_SCRIPTINFO|AssFile::COMMIT_DIAG_FULL|AssFile::COMMIT_STYLES);
		staged.Commit(_("resolution resampling"),AssFile::COMMIT_SCRIPTINFO|AssFile::COMMIT_DIAG_FULL);
	}
};

struct tool_style_manager final : public Command {
	CMD_NAME("tool/style/manager")
	CMD_ICON(style_toolbutton)
	STR_MENU("&Styles Manager...")
	STR_DISP("Styles Manager")
	STR_HELP("Open the styles manager")

	void operator()(agi::Context *c) override {
		ShowStyleManagerDialog(c);
	}
};

struct tool_time_kanji final : public Command {
	CMD_NAME("tool/time/kanji")
	CMD_ICON(kara_timing_copier)
	STR_MENU("&Kanji Timer...")
	STR_DISP("Kanji Timer")
	STR_HELP("Open the Kanji timer copier")

	void operator()(agi::Context *c) override {
		ShowKanjiTimerDialog(c);
	}
};

struct tool_time_postprocess final : public Command {
	CMD_NAME("tool/time/postprocess")
	CMD_ICON(timing_processor_toolbutton)
	STR_MENU("&Timing Post-Processor...")
	STR_DISP("Timing Post-Processor")
	STR_HELP("Post-process the subtitle timing to add lead-ins and lead-outs, snap timing to scene changes, etc.")

	void operator()(agi::Context *c) override {
		ShowTimingProcessorDialog(c);
	}
};

struct tool_translation_assistant final : public Command {
	CMD_NAME("tool/translation_assistant")
	CMD_ICON(translation_toolbutton)
	STR_MENU("&Translation Assistant...")
	STR_DISP("Translation Assistant")
	STR_HELP("Open translation assistant")

	void operator()(agi::Context *c) override {
		c->videoController->Stop();
		try {
			c->dialog->ShowModal<DialogTranslation>(c);
		}
		catch (DialogTranslation::NothingToTranslate const&) {
			wxMessageBox(_("There is nothing to translate in the file."));
		}
	}
};

struct tool_translation_assistant_validator : public Command {
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return !!c->dialog->Get<DialogTranslation>();
	}
};

struct tool_translation_assistant_commit final : public tool_translation_assistant_validator {
	CMD_NAME("tool/translation_assistant/commit")
	STR_MENU("&Accept changes")
	STR_DISP("Accept changes")
	STR_HELP("Commit changes and move to the next line")

	void operator()(agi::Context *c) override {
		c->dialog->Get<DialogTranslation>()->Commit(true);
	}
};

struct tool_translation_assistant_preview final : public tool_translation_assistant_validator {
	CMD_NAME("tool/translation_assistant/preview")
	STR_MENU("&Preview changes")
	STR_DISP("Preview changes")
	STR_HELP("Commit changes and stay on the current line")

	void operator()(agi::Context *c) override {
		c->dialog->Get<DialogTranslation>()->Commit(false);
	}
};

struct tool_translation_assistant_next final : public tool_translation_assistant_validator {
	CMD_NAME("tool/translation_assistant/next")
	STR_MENU("&Next Line")
	STR_DISP("Next Line")
	STR_HELP("Move to the next line without committing changes")

	void operator()(agi::Context *c) override {
		c->dialog->Get<DialogTranslation>()->NextBlock();
	}
};

struct tool_translation_assistant_prev final : public tool_translation_assistant_validator {
	CMD_NAME("tool/translation_assistant/prev")
	STR_MENU("&Previous Line")
	STR_DISP("Previous Line")
	STR_HELP("Move to the previous line without committing changes")

	void operator()(agi::Context *c) override {
		c->dialog->Get<DialogTranslation>()->PrevBlock();
	}
};

struct tool_translation_assistant_insert final : public tool_translation_assistant_validator {
	CMD_NAME("tool/translation_assistant/insert_original")
	STR_MENU("&Insert Original")
	STR_DISP("Insert Original")
	STR_HELP("Insert the untranslated text")

	void operator()(agi::Context *c) override {
		c->dialog->Get<DialogTranslation>()->InsertOriginal();
	}
};
}

namespace cmd {
	void init_tool() {
		reg(std::make_unique<tool_export>());
		reg(std::make_unique<tool_font_collector>());
		reg(std::make_unique<tool_magiatimeline>());
		reg(std::make_unique<tool_line_select>());
		reg(std::make_unique<tool_resampleres>());
		reg(std::make_unique<tool_style_manager>());
		reg(std::make_unique<tool_time_kanji>());
		reg(std::make_unique<tool_time_postprocess>());
		reg(std::make_unique<tool_translation_assistant>());
		reg(std::make_unique<tool_translation_assistant_commit>());
		reg(std::make_unique<tool_translation_assistant_preview>());
		reg(std::make_unique<tool_translation_assistant_next>());
		reg(std::make_unique<tool_translation_assistant_prev>());
		reg(std::make_unique<tool_translation_assistant_insert>());
	}
}

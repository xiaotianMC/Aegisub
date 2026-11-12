// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "command.h"

#include "../ass_dialogue.h"
#include "../ass_file.h"
#include "../ass_style.h"
#include "../async_video_provider.h"
#include "../bilingual.h"
#include "../compat.h"
#include "../format.h"
#include "../frame_main.h"
#include "../include/aegisub/context.h"
#include "../keyframe_snap.h"
#include "../options.h"
#include "../project.h"
#include "../selection_controller.h"
#include "../speech_lines.h"
#include "../subtitle_project_access.h"
#include "../subtitle_row_list_edit.h"
#include "../subtitle_value_edit.h"
#include "../utils.h"

#include <libaegisub/exception.h>
#include <libaegisub/io.h>
#include <libaegisub/vfr.h>

#include <set>

#include <wx/msgdlg.h>

namespace {
using cmd::Command;

/// The JP and CN styles, those the file lacks made from @p base_name's (or
/// the first style), in a step of their own named @p message that what
/// follows joins. @return That step, or -1 when nothing was added
int AddBilingualStyles(agi::Context *c, std::string const& base_name, wxString const& message) {
	std::string const source(bilingual::source_style), target(bilingual::target_style);
	auto const& live = c->subtitleAccess->Read();
	if (live.GetStyle(source) && live.GetStyle(target)) return -1;
	SubtitleValueEdit styles(c);
	AssStyle const *base = styles.file.GetStyle(base_name);
	if (!base && !styles.file.Styles.empty()) base = &styles.file.Styles.front();
	for (auto const& name : {source, target}) {
		if (styles.file.GetStyle(name)) continue;
		auto style = base ? new AssStyle(*base) : new AssStyle;
		style->name = name;
		style->UpdateData();
		styles.file.Styles.push_back(*style);
	}
	return styles.Commit(message, AssFile::COMMIT_STYLES);
}

/// Whether the subtitles hold nothing yet: no lines, or only lines with no text
bool NothingWritten(agi::Context *c) {
	for (auto const& line : c->subtitleAccess->Read().Events)
		if (line.Text.get().find_first_not_of(" \t\r\n") != std::string::npos) return false;
	return true;
}

struct autotiming_bilingual final : public Command {
	CMD_NAME("autotiming/bilingual")
	STR_MENU("Make &Bilingual")
	STR_DISP("Make Bilingual")
	STR_HELP("Give each selected line an empty line at the same times for its translation: the line takes the JP style, the translation the CN style")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return !c->selectionController->ReadSelectedSet().empty();
	}

	void operator()(agi::Context *c) override {
		auto scope = c->subtitleAccess->TryWrite();
		if (!scope) return;
		std::string const source(bilingual::source_style), target(bilingual::target_style);

		// What is to change, looked at first: nothing to do, no undo step
		auto const& live = c->subtitleAccess->Read();
		bool rows_change = false;
		for (auto row : c->selectionController->ReadSelectedLines()) {
			if (row->Comment || bilingual::IsTarget(*row)) continue;
			if (!bilingual::IsSource(*row) || !bilingual::Partner(live, *row)) rows_change = true;
		}
		if (!rows_change) return;

		// The two styles, if the file lacks them, start as the style of the
		// active line: what they should look like is for the user to set
		auto active = c->selectionController->ReadActiveLine();
		int step = AddBilingualStyles(c, active ? active->Style.get() : std::string(), _("make bilingual"));

		SubtitleRowListEdit staged(c);
		for (auto row : staged.SortedSelection()) {
			if (row->Comment || bilingual::IsTarget(*row)) continue;
			row->Style = source;
			if (bilingual::Partner(staged.file, *row)) continue;
			auto line = new AssDialogue;
			line->Start = row->Start;
			line->End = row->End;
			line->Layer = row->Layer;
			line->Margin = row->Margin;
			line->Actor = row->Actor;
			line->Style = target;
			staged.file.Events.insert(++staged.file.Events.iterator_to(*row), *line);
		}
		staged.Commit(_("make bilingual"), AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_META, step);
		staged.Select(staged.selection, staged.active);
	}
};

struct autotiming_import_speech final : public Command {
	CMD_NAME("autotiming/import_speech")
	STR_MENU("&Import Speech Recognition...")
	STR_DISP("Import Speech Recognition")
	STR_HELP("Make lines from what WhisperX or faster-whisper heard, a sentence a line: their JSON with each word's times, timed to the scene changes if they are known")

	void operator()(agi::Context *c) override {
		auto filename = OpenFileSelector(c, _("Import Speech Recognition"), "Path/Last/Speech", "", ".json",
			from_wx(_("Speech recognition JSON") + " (*.json)|*.json|" + _("All Files") + " (*.*)|*.*"), c->parent, "?script");
		if (filename.empty()) return;
		speech::Transcript transcript;
		try {
			transcript = speech::ReadJson(*agi::io::Open(filename));
		}
		catch (agi::Exception const& e) {
			wxMessageBox(to_wx(e.GetMessage()), _("Import Speech Recognition"), wxOK | wxICON_ERROR, c->parent);
			return;
		}
		size_t const lines = speech::Import(c, transcript);
		if (c->frame)
			c->frame->StatusTimeout(lines ? fmt_plural(lines, "Imported %d line", "Imported %d lines", static_cast<int>(lines))
				: _("The file holds nothing heard"));
	}

};
}

size_t speech::Import(agi::Context *c, speech::Transcript const& transcript) {
	auto scope = c->subtitleAccess->TryWrite();
	if (!scope) return 0;
	speech::Rules rules;
	rules.max_duration = OPT_GET("Tool/Speech Import/Max Duration")->GetInt();
	rules.min_duration = OPT_GET("Tool/Speech Import/Min Duration")->GetInt();
	rules.unsure_below = OPT_GET("Tool/Speech Import/Unsure Below")->GetDouble();
	rules.lead_in = OPT_GET("Tool/Timing Post Processor/Lead/IN")->GetInt();
	rules.lead_out = OPT_GET("Tool/Timing Post Processor/Lead/OUT")->GetInt();
	rules.adjacent_gap = OPT_GET("Tool/Timing Post Processor/Threshold/Adjacent Gap")->GetInt();

	auto const& fps = c->project->Timecodes();
	auto const& keyframes = c->project->Keyframes();
	bool const have_cuts = !keyframes.empty() && fps.IsLoaded();
	std::vector<int> cuts;
	if (have_cuts)
		for (int frame : keyframes) if (frame > 0) cuts.push_back(fps.TimeAtFrame(frame, agi::vfr::START));
	auto made = speech::MakeLines(transcript, rules, cuts);
	if (made.empty()) return 0;

	bool const pair = OPT_GET("Tool/Speech Import/Bilingual")->GetBool();
	bool const replace = NothingWritten(c);
	auto const& live = c->subtitleAccess->Read();
	std::string style = live.Events.empty() ? std::string("Default") : live.Events.front().Style.get();
	int step = -1;
	if (pair) {
		step = AddBilingualStyles(c, style, _("import speech recognition"));
		style = std::string(bilingual::source_style);
	}

	std::vector<std::unique_ptr<AssDialogue>> lines;
	for (auto const& m : made) {
		auto line = std::make_unique<AssDialogue>();
		line->Start = m.start;
		line->End = m.end;
		line->Text = m.text;
		line->Actor = m.speaker;
		line->Style = style;
		line->Comment = m.note;
		bilingual::Marks marks;
		marks.group = m.group;
		marks.unsure = m.unsure;
		line->Effect = bilingual::FormatMarks(marks);
		lines.push_back(std::move(line));
	}
	// The frames: a start on a cut's first frame, an end on the frame
	// before one, as far as the snapping distances reach
	if (have_cuts) {
		auto provider = c->project->VideoProvider();
		std::vector<AssDialogueBase*> rows;
		for (auto& l : lines) if (!l->Comment) rows.push_back(l.get());
		SnapToKeyframes(rows, keyframes, provider ? provider->GetFrameCount() : 0, fps, KeyframeSnapThresholds::FromSceneSnap());
	}

	SubtitleRowListEdit staged(c);
	std::vector<std::unique_ptr<AssDialogue>> removed;
	if (replace)
		staged.file.Events.clear_and_dispose([&](AssDialogue *line) { removed.emplace_back(line); });
	std::set<AssDialogue*> added;
	AssDialogue *first = nullptr;
	size_t const count = lines.size();
	for (auto& owned : lines) {
		auto line = owned.release();
		// Among the lines there are, by time
		auto before = staged.file.Events.end();
		for (auto it = staged.file.Events.begin(); it != staged.file.Events.end(); ++it)
			if (!added.count(&*it) && it->Start > line->Start) { before = it; break; }
		staged.file.Events.insert(before, *line);
		added.insert(line);
		if (!first) first = line;
		if (pair && !line->Comment) {
			auto partner = new AssDialogue;
			partner->Start = line->Start;
			partner->End = line->End;
			partner->Actor = line->Actor;
			partner->Style = std::string(bilingual::target_style);
			staged.file.Events.insert(before, *partner);
			added.insert(partner);
		}
	}
	staged.Commit(_("import speech recognition"), AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL, step);
	staged.Select(added, first);
	return count;
}

namespace {
struct autotiming_clear_marks final : public Command {
	CMD_NAME("autotiming/clear_marks")
	STR_MENU("Clear Auto-Timing &Marks")
	STR_DISP("Clear Auto-Timing Marks")
	STR_HELP("Take the marks auto-timing leaves in the Effect field, #group and ?word, off every line, leaving what else the field holds")

	void operator()(agi::Context *c) override {
		auto scope = c->subtitleAccess->TryWrite();
		if (!scope) return;
		std::vector<AssDialogueBase> changed;
		for (auto const& row : c->subtitleAccess->Read().Events) {
			auto cleared = bilingual::ClearMarks(row.Effect.get());
			if (cleared == row.Effect.get()) continue;
			changed.emplace_back(row);
			changed.back().Effect = cleared;
		}
		if (c->frame)
			c->frame->StatusTimeout(changed.empty() ? _("No line has auto-timing marks")
				: fmt_plural(changed.size(), "Cleared the marks of %d line", "Cleared the marks of %d lines", static_cast<int>(changed.size())));
		if (changed.empty()) return;
		auto edit = c->subtitleAccess->Edit(scope->Lease());
		edit.UpdateRows(changed);
		edit.Commit(_("clear auto-timing marks"), AssFile::COMMIT_DIAG_META);
	}
};
}

namespace cmd {
	void init_autotiming() {
		reg(std::make_unique<autotiming_bilingual>());
		reg(std::make_unique<autotiming_clear_marks>());
		reg(std::make_unique<autotiming_import_speech>());
	}
}

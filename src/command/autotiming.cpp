// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "command.h"

#include "../ass_dialogue.h"
#include "../ass_file.h"
#include "../ass_style.h"
#include "../bilingual.h"
#include "../format.h"
#include "../frame_main.h"
#include "../include/aegisub/context.h"
#include "../selection_controller.h"
#include "../subtitle_project_access.h"
#include "../subtitle_row_list_edit.h"
#include "../subtitle_value_edit.h"

#include <set>

namespace {
using cmd::Command;

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
		int step = -1;
		if (!live.GetStyle(source) || !live.GetStyle(target)) {
			SubtitleValueEdit styles(c);
			auto active = c->selectionController->ReadActiveLine();
			AssStyle const *base = active ? styles.file.GetStyle(active->Style.get()) : nullptr;
			if (!base && !styles.file.Styles.empty()) base = &styles.file.Styles.front();
			for (auto const& name : {source, target}) {
				if (styles.file.GetStyle(name)) continue;
				auto style = base ? new AssStyle(*base) : new AssStyle;
				style->name = name;
				style->UpdateData();
				styles.file.Styles.push_back(*style);
			}
			step = styles.Commit(_("make bilingual"), AssFile::COMMIT_STYLES);
		}

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
	}
}

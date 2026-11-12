// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
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

#pragma once

#include <libaegisub/exception.h>
#include <libaegisub/signal.h>

#include <memory>
#include <vector>
#include <wx/dialog.h>

namespace agi { struct Context; }
class AssDialogue;
class SubtitleDialogEdit;
class AssDialogueBlock;
class PersistLocation;
class SubsTextEditCtrl;
class FlatCheck;
class wxStaticText;
class wxStyledTextCtrl;

/// Assistant for translating subtitles in one language to another language
class DialogTranslation final : public wxDialog {
protected:
    // Declared first: expires only after all owned bindings/cache members are destroyed.
    std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);
private:
	agi::Context *c;

	agi::signal::Connection file_change_connection;
	agi::signal::Connection active_line_connection;

	/// The active line
	std::unique_ptr<AssDialogue> active_line;
	std::unique_ptr<SubtitleDialogEdit> edit;
	bool updating_display = false;
	/// The parsed dialogue blocks for the active line
	std::vector<std::unique_ptr<AssDialogueBlock>> blocks;
	/// Which dialogue block in the active line is currently being translated
	size_t cur_block = 0;

	/// Bilingual, the line its translation goes in, at the same times in the
	/// target style; 0 when the translation replaces the original
	int target_id = 0;
	/// The active line, and its translation line if it has one, read again
	bool LoadActive();
	/// A translation line, passed over in a bilingual file
	bool SkipLine() const;
	/// The translation line's parts, if they match the original's, tag for tag
	std::vector<std::unique_ptr<AssDialogueBlock>> TargetBlocks() const;

	/// Total number of dialogue lines in the file
	size_t line_count;

	/// Should active line change announcements be ignored?
	bool switching_lines = false;

	wxStaticText *line_number_display;
	wxStyledTextCtrl *original_text;
	SubsTextEditCtrl *translated_text;
	FlatCheck *seek_video;

	std::unique_ptr<PersistLocation> persist;

	void OnPlayAudioButton(wxCommandEvent &);
	void OnPlayVideoButton(wxCommandEvent &);
	void OnKeyDown(wxKeyEvent &evt);
	void OnShow(wxShowEvent &evt);
	void OnExternalCommit(int commit_type);

	void UpdateDisplay();

	void OnActiveLineChanged(bool installing = false);

public:
	DialogTranslation(agi::Context *context);
	~DialogTranslation();

	bool NextBlock();
	bool PrevBlock();
	void Commit(bool next);
	void InsertOriginal();

	struct NothingToTranslate { };
};

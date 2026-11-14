// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "command.h"

#include "../ass_dialogue.h"
#include "../ass_file.h"
#include "../ass_style.h"
#include "../audio_controller.h"
#include "../async_video_provider.h"
#include "../dialog_progress.h"
#include "../bilingual.h"
#include "../compat.h"
#include "../format.h"
#include "../frame_main.h"
#include "../include/aegisub/context.h"
#include "../keyframe_snap.h"
#include "../magia_timeline.h"
#include "../options.h"
#include "../project.h"
#include "../secret.h"
#include "../selection_controller.h"
#include "../speech_lines.h"
#include "../speech_transcribe.h"
#include "../subtitle_project_access.h"
#include "../subtitle_row_list_edit.h"
#include "../subtitle_value_edit.h"
#include "../utils.h"
#include "../video_controller.h"

#include <libaegisub/audio/provider.h>
#include <libaegisub/exception.h>
#include <libaegisub/io.h>
#include <libaegisub/vfr.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <set>
#include <sstream>

#include <wx/msgdlg.h>
#include <wx/richmsgdlg.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

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
	STR_HELP("Make lines from what WhisperX, faster-whisper or ElevenLabs Scribe heard, a sentence a line: their JSON with each word's times, timed to the scene changes if they are known")

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

size_t speech::Import(agi::Context *c, speech::Transcript const& said) {
	// The pauses an aligner put inside its words found again in the audio
	speech::Transcript transcript = said;
	std::vector<std::pair<int, int>> quiet;
	if (auto audio = c->project->AudioProvider()) {
		int const step = 10;
		quiet = speech::FindQuiet(speech::Levels(*audio, step), step);
		speech::TrimToSpeech(transcript, quiet);
	}

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
	auto made = speech::MakeLines(transcript, rules, cuts, quiet);
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
/// Where a run's files go: a folder of its own under the system's, made now
agi::fs::path TranscribeFolder() {
	auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	agi::fs::path folder = agi::fs::path(std::filesystem::temp_directory_path()) / agi::format("aegisub-transcribe-%d-%d", static_cast<int>(wxGetProcessId()), static_cast<int>(stamp % 1000000));
	agi::fs::CreateDirectory(folder);
	return folder;
}

struct autotiming_transcribe final : public Command {
	CMD_NAME("autotiming/transcribe")
	STR_MENU("&Transcribe the Audio...")
	STR_DISP("Transcribe the Audio")
	STR_HELP("Have the audio heard through, by ElevenLabs Scribe or a program of your own, and make a line of each sentence, timed to the scene changes if they are known")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return c->project->AudioProvider() && !c->project->AudioName().empty();
	}

	static void Fail(agi::Context *c, wxString const& message) {
		wxMessageBox(message, _("Transcribe the Audio"), wxOK | wxICON_ERROR, c->parent);
	}

	void operator()(agi::Context *c) override {
		c->videoController->Stop();
		c->audioController->Stop();
		auto const audio = c->project->AudioProvider();
		auto const media = c->project->AudioName();
		int const duration_ms = static_cast<int>(audio->GetNumSamples() * 1000 / std::max<int64_t>(1, audio->GetSampleRate()));

		transcribe::Request request;
		request.media = media;
		request.language = OPT_GET("Tool/Transcribe/Language")->GetString();
		request.secondary_languages = transcribe::SplitCodes(OPT_GET("Tool/Transcribe/Secondary Languages")->GetString());
		request.diarize = OPT_GET("Tool/Transcribe/Diarize")->GetBool();
		auto extra = transcribe::SplitList(OPT_GET("Tool/Transcribe/Keyterms")->GetString());
		if (OPT_GET("Tool/Transcribe/Keyterms From Actors")->GetBool())
			request.keyterms = transcribe::Keyterms(c->subtitleAccess->Read(), std::move(extra));
		else
			request.keyterms = transcribe::Keyterms(AssFile(), std::move(extra));

		bool const by_elevenlabs = OPT_GET("Tool/Transcribe/Backend")->GetInt() == 0;
		agi::fs::path folder;
		std::string json;
		try {
			if (by_elevenlabs) {
				if (!transcribe::CanUpload()) {
					Fail(c, _("This build of Aegisub has no libcurl, so nothing can be sent to ElevenLabs. Set a program of your own to run in Preferences, Audio, Transcription."));
					return;
				}
				transcribe::ElevenLabs settings;
				settings.model = OPT_GET("Tool/Transcribe/ElevenLabs/Model")->GetString();
				auto found = transcribe::FindKey({std::getenv("ELEVENLABS_API_KEY"), secret::Load(transcribe::key_service),
					secret::Held(transcribe::key_service)});
				if (found.key.empty()) {
					bool const keychain = secret::Available();
					wxString how = keychain ? _("It is kept in the system's keychain.")
						: _("This machine has no keychain Aegisub can use, so it is kept only until Aegisub is closed.");
					wxString key = wxGetPasswordFromUser(_("Enter your ElevenLabs API key, from the API keys page of elevenlabs.io.") + "\n\n" + how + "\n"
						+ _("To give it another way, set the ELEVENLABS_API_KEY environment variable."), _("Transcribe the Audio"), "", c->parent);
					found.key = transcribe::TrimKey(from_wx(key));
					if (found.key.empty()) return;
					if (!keychain || !secret::Save(transcribe::key_service, found.key))
						secret::Hold(transcribe::key_service, found.key);
				}
				settings.key = found.key;
				if (OPT_GET("Tool/Transcribe/Ask")->GetBool()) {
					wxRichMessageDialog ask(c->parent,
						agi::wxformat(_("Send %s of audio, %s, to ElevenLabs to hear?"), transcribe::Duration(duration_ms), media.filename().string()),
						_("Transcribe the Audio"), wxYES_NO | wxICON_QUESTION);
					ask.SetExtendedMessage(_("The audio leaves this computer for ElevenLabs' servers, which charge your account by its length. Aegisub keeps nothing of the answer but the lines it makes."));
					ask.ShowCheckBox(_("Don't ask again"));
					if (ask.ShowModal() != wxID_YES) return;
					if (ask.IsCheckBoxChecked()) OPT_SET("Tool/Transcribe/Ask")->SetBool(false);
				}
				if (!transcribe::Accepted(media)) {
					// Not a kind of file it takes: the audio as WAV
					folder = TranscribeFolder();
					request.media = folder / (media.stem().string() + ".wav");
					agi::SaveAudioClip(*audio, request.media, 0, duration_ms);
				}
				DialogProgress progress(c->parent, _("Transcribe the Audio"), _("Sending the audio to ElevenLabs..."));
				progress.Run([&](agi::ProgressSink *ps) { json = transcribe::Upload(request, settings, ps); });
			}
			else {
				auto const templ = OPT_GET("Tool/Transcribe/Command")->GetString();
				if (templ.find_first_not_of(" \t") == std::string::npos) {
					Fail(c, _("There is no command to run: set one in Preferences, Audio, Transcription."));
					return;
				}
				folder = TranscribeFolder();
				auto argv = transcribe::CommandLine(templ, media, folder, request.language);
				transcribe::Program program(argv, folder);
				DialogProgress progress(c->parent, _("Transcribe the Audio"), agi::wxformat(_("Running %s..."), argv.empty() ? std::string() : argv[0]));
				progress.Run([&](agi::ProgressSink *ps) { program.Wait(ps); });
				if (program.ExitCode() != 0)
					throw agi::EnvironmentError(agi::format(from_wx(_("%s ended with %d:\n%s")), argv[0], program.ExitCode(), magia_timeline::LastLines(program.Output(), 8)));
				auto file = transcribe::OutputJson(folder, media);
				if (!file)
					throw agi::EnvironmentError(agi::format(from_wx(_("%s wrote no JSON file in the folder it was given:\n%s")), argv[0], magia_timeline::LastLines(program.Output(), 8)));
				auto in = agi::io::Open(*file, true);
				json.assign(std::istreambuf_iterator<char>(*in), std::istreambuf_iterator<char>());
			}
		}
		catch (agi::UserCancelException const&) {
			Clean(folder);
			return;
		}
		catch (transcribe::KeyRefused const& e) {
			secret::Forget(transcribe::key_service);
			secret::Drop(transcribe::key_service);
			Clean(folder);
			Fail(c, to_wx(e.GetMessage()) + "\n\n" + _("The key is forgotten; it is asked for again next time."));
			return;
		}
		catch (agi::Exception const& e) {
			Clean(folder);
			Fail(c, to_wx(e.GetMessage()));
			return;
		}
		Clean(folder);

		speech::Transcript transcript;
		try {
			std::istringstream in(json);
			transcript = speech::ReadJson(in);
		}
		catch (agi::Exception const& e) {
			Fail(c, to_wx(e.GetMessage()));
			return;
		}
		size_t const lines = speech::Import(c, transcript);
		if (c->frame)
			c->frame->StatusTimeout(lines ? fmt_plural(lines, "Heard %d line", "Heard %d lines", static_cast<int>(lines))
				: _("Nothing was heard"));
	}

	static void Clean(agi::fs::path const& folder) {
		if (folder.empty()) return;
		std::error_code ec;
		std::filesystem::remove_all(std::filesystem::path(folder), ec);
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
		reg(std::make_unique<autotiming_import_speech>());
		reg(std::make_unique<autotiming_transcribe>());
	}
}

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

#include "../ass_file.h"
#include "../compat.h"
#include "../format.h"
#include "../dialog_progress.h"
#include "../frame_main.h"
#include "../include/aegisub/video_provider.h"
#include "../ass_dialogue.h"
#include "../async_video_provider.h"
#include "../scene_detect.h"
#include "../subtitle_row_list_edit.h"
#include "../subtitle_project_access.h"
#include "../video_provider_manager.h"
#include "../include/aegisub/context.h"
#include "../libresrc/libresrc.h"
#include "../options.h"
#include "../project.h"
#include "../utils.h"

#include <libaegisub/keyframe.h>
#include <libaegisub/path.h>
#include <libaegisub/vfr.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <thread>
#include <wx/msgdlg.h>

namespace {
	using cmd::Command;

struct keyframe_close final : public Command {
	CMD_NAME("keyframe/close")
	CMD_ICON(close_keyframes_menu)
	STR_MENU("Close Keyframes")
	STR_DISP("Close Keyframes")
	STR_HELP("Discard the currently loaded keyframes and use those from the video, if any")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return c->project->CanCloseKeyframes();
	}

	void operator()(agi::Context *c) override {
		c->project->CloseKeyframes();
	}
};

struct keyframe_open final : public Command {
	CMD_NAME("keyframe/open")
	CMD_ICON(open_keyframes_menu)
	STR_MENU("Open Keyframes...")
	STR_DISP("Open Keyframes")
	STR_HELP("Open a keyframe list file")

	void operator()(agi::Context *c) override {
		auto filename = OpenFileSelector(
			c,
			_("Open Keyframes File"),
			"Path/Last/Keyframes", "" ,".txt",
			from_wx(_("All Supported Formats") +
				" (*.txt, *.pass, *.stats, *.log)|*.txt;*.pass;*.stats;*.log|" +
				_("All Files") + " (*.*)|*.*"),
			c->parent,
			"?script");

		if (!filename.empty())
			c->project->LoadKeyframes(filename);
	}
};

struct keyframe_save final : public Command {
	CMD_NAME("keyframe/save")
	CMD_ICON(save_keyframes_menu)
	STR_MENU("Save Keyframes...")
	STR_DISP("Save Keyframes")
	STR_HELP("Save the current list of keyframes to a file")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return !c->project->Keyframes().empty();
	}

	void operator()(agi::Context *c) override {
		auto filename = SaveFileSelector(c, _("Save Keyframes File"), "Path/Last/Keyframes", "", "*.key.txt", from_wx(_("Text Files") + " (*.txt)|*.txt"), c->parent);
		if (filename.empty()) return;

		agi::keyframe::Save(filename, c->project->Keyframes());
		config::mru->Add("Keyframes", filename);
	}
};

/// What names a video's files in the scene cache: its path, and its size
/// and time of change so that a file replaced in place is read again
std::string SceneCacheKey(agi::fs::path const& video) {
	std::string key = video.string();
	try {
		if (agi::fs::FileExists(video))
			key += "|" + std::to_string(agi::fs::Size(video)) + "|" +
				std::to_string(agi::fs::ModifiedTime(video).time_since_epoch().count());
	}
	catch (agi::fs::FileSystemError const&) { }
	return key;
}

/// Raised when frames are compared another way: what an earlier way found
/// is not kept on as if it had been found by this one
constexpr const char *scene_method = "cells4";

agi::fs::path SceneCacheFile(std::string const& key, const char *suffix) {
	char name[48];
	snprintf(name, sizeof name, "%016zx%s", std::hash<std::string>{}(key), suffix);
	return config::path->Decode("?user/scenes/") / name;
}

/// What is compared: the share of the picture left out for subtitles
std::string SceneArea(int subtitle_band) {
	return std::string(scene_method) + "|band" + std::to_string(subtitle_band);
}

/// Where a video's cuts found with some settings are kept, so that they
/// are found once
agi::fs::path SceneCachePath(agi::fs::path const& video, int sensitivity, bool gradual, int subtitle_band) {
	auto key = SceneCacheKey(video);
	return SceneCacheFile(key + "|" + SceneArea(subtitle_band) + "|" + std::to_string(sensitivity) + (gradual ? "|gradual" : ""), ".key.txt");
}

/// Where what reading a video through found is kept, whatever the
/// sensitivity and whether dissolves are looked for: their cuts come from
/// it without reading the video again
agi::fs::path SceneMeasuresPath(agi::fs::path const& video, int subtitle_band) {
	return SceneCacheFile(SceneCacheKey(video) + "|" + SceneArea(subtitle_band), ".measures");
}

/// Whether the subtitles hold nothing yet: no lines, or only lines with no
/// text, as a new file's one empty line
bool NothingWritten(agi::Context *c) {
	for (auto const& line : c->subtitleAccess->Read().Events)
		if (line.Text.get().find_first_not_of(" \t\r\n") != std::string::npos) return false;
	return true;
}

/// A blank line for each shot found: from its first frame to the frame
/// before the next cut, in one undo step. With nothing written yet, the
/// lines there were, all empty, make way for them; otherwise every line is
/// kept and only the shots no line is shown in get one, among the others by
/// time. @p always false does nothing unless nothing is written.
/// @return How many lines were added
size_t TimelineFromCuts(agi::Context *c, std::vector<int> const& cuts, bool always) {
	auto provider = c->project->VideoProvider();
	auto const& fps = c->project->Timecodes();
	if (!provider || !fps.IsLoaded()) return 0;
	bool const replace = NothingWritten(c);
	if (!replace && !always) return 0;
	int const frames = provider->GetFrameCount();
	if (frames <= 0) return 0;
	std::vector<int> starts;
	for (int cut : cuts)
		if (cut >= 0 && cut < frames && (starts.empty() || cut > starts.back())) starts.push_back(cut);
	if (starts.empty() || starts.front() != 0) starts.insert(starts.begin(), 0);

	auto scope = c->subtitleAccess->TryWrite();
	if (!scope) return 0;
	SubtitleRowListEdit staged(c);
	std::string style = "Default";
	if (!staged.file.Events.empty()) style = staged.file.Events.front().Style.get();
	std::vector<std::unique_ptr<AssDialogue>> removed;
	if (replace)
		staged.file.Events.clear_and_dispose([&](AssDialogue *line) { removed.emplace_back(line); });
	std::set<AssDialogue*> added;
	AssDialogue *first = nullptr;
	for (size_t i = 0; i < starts.size(); ++i) {
		int const last_frame = (i + 1 < starts.size() ? starts[i + 1] : frames) - 1;
		int const start = fps.TimeAtFrame(starts[i], agi::vfr::START);
		int const end = std::max<int>(fps.TimeAtFrame(last_frame, agi::vfr::END), start);
		auto before = staged.file.Events.end();
		if (!replace) {
			bool shown = false;
			for (auto it = staged.file.Events.begin(); it != staged.file.Events.end(); ++it) {
				if (added.count(&*it)) continue;
				if (!it->Comment && it->Start < end && it->End > start) shown = true;
				if (before == staged.file.Events.end() && it->Start > start) before = it;
			}
			if (shown) continue;
		}
		auto line = new AssDialogue;
		line->Start = start;
		line->End = end;
		line->Style = style;
		staged.file.Events.insert(before, *line);
		added.insert(line);
		if (!first) first = line;
	}
	if (added.empty()) return 0;
	staged.Commit(_("timeline from scene changes"), AssFile::COMMIT_DIAG_ADDREM);
	staged.Select(added, first);
	return added.size();
}

struct keyframe_timeline final : public Command {
	CMD_NAME("keyframe/timeline")
	STR_MENU("&Timeline from Scene Changes")
	STR_DISP("Timeline from Scene Changes")
	STR_HELP("Add an empty line for each shot between the scene changes that no line is shown in yet")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return !c->project->Keyframes().empty() && c->project->Timecodes().IsLoaded() && c->project->VideoProvider();
	}

	void operator()(agi::Context *c) override {
		if (!Validate(c)) return;
		size_t const lines = TimelineFromCuts(c, c->project->Keyframes(), true);
		if (!c->frame) return;
		if (lines)
			c->frame->StatusTimeout(fmt_plural(lines, "Added an empty line for %d shot", "Added an empty line for each of %d shots", static_cast<int>(lines)));
		else
			c->frame->StatusTimeout(_("Every shot already has a line"));
	}
};

struct keyframe_detect final : public Command {
	CMD_NAME("keyframe/detect")
	STR_MENU("&Detect Scene Changes...")
	STR_DISP("Detect Scene Changes")
	STR_HELP("Find the frames where the picture cuts to another shot and use them as the keyframes, to snap the lines' times to")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return !!c->project->VideoProvider();
	}

	void operator()(agi::Context *c) override {
		auto const& video = c->project->VideoName();
		if (video.empty()) return;
		int sensitivity = OPT_GET("Tool/Scene Detection/Sensitivity")->GetInt();
		auto params = scene_detect::ForSensitivity(sensitivity);
		params.gradual = OPT_GET("Tool/Scene Detection/Gradual")->GetBool();
		int const band = std::clamp(static_cast<int>(OPT_GET("Tool/Scene Detection/Subtitle Band")->GetInt()), 0, 50);
		auto cache = SceneCachePath(video, sensitivity, params.gradual, band);

		std::vector<int> cuts;
		try { if (agi::fs::FileExists(cache)) cuts = agi::keyframe::Load(cache); }
		catch (agi::Exception const&) { cuts.clear(); }

		if (cuts.empty()) {
			auto measures_file = SceneMeasuresPath(video, band);
			auto measures = scene_detect::LoadMeasures(measures_file);
			if (measures.flash_span != params.flash_span) measures = {};
			bool const read_video = measures.empty();
			if (read_video) {
				// A provider of its own, read through from the start, so the
				// video shown is not moved about; with no frames kept for
				// going back, and the frames small
				DialogProgress progress(c->parent, _("Detect Scene Changes"), _("Reading every frame of the video..."));
				try {
					auto matrix = c->subtitleAccess->Read().GetYCbCrMatrix();
					// A long video in parts, each read by a decoder of its
					// own; a core is left to the decoders' own threads
					std::vector<std::unique_ptr<VideoProvider>> providers;
					std::vector<VideoProvider*> readers;
					auto open = [&] {
						providers.push_back(VideoProviderFactory::GetProvider(video, matrix, &progress, false));
						providers.back()->ScaleDownForAnalysis(scene_detect::analysis_width);
						readers.push_back(providers.back().get());
					};
					open();
					int const cores = static_cast<int>(std::thread::hardware_concurrency());
					int const length = readers[0]->GetFrameCount();
					int const parts = std::clamp(std::min(cores - 1, length / scene_detect::min_part_frames), 1, 4);
					while (static_cast<int>(readers.size()) < parts) open();
					progress.Run([&](agi::ProgressSink *ps) {
						// The picture within its black bars, a few frames
						// looked at first, without where subtitles are burnt in
						auto area = scene_detect::WithoutSubtitles(scene_detect::FindPicture(*readers[0]), band);
						measures = scene_detect::Measure(readers, ps, params.flash_span, area);
					});
				}
				catch (agi::UserCancelException const&) { return; }
				catch (agi::Exception const& e) {
					wxMessageBox(to_wx(e.GetMessage()), _("Detect Scene Changes"), wxOK | wxICON_ERROR, c->parent);
					return;
				}
				if (measures.empty()) return;
			}
			cuts = scene_detect::Cuts(measures, params);
			try {
				agi::fs::CreateDirectory(cache.parent_path());
				if (read_video) scene_detect::SaveMeasures(measures_file, measures);
				agi::keyframe::Save(cache, cuts);
			}
			catch (agi::Exception const& e) {
				wxMessageBox(to_wx(e.GetMessage()), _("Detect Scene Changes"), wxOK | wxICON_ERROR, c->parent);
				return;
			}
		}
		c->project->LoadKeyframes(cache);
		size_t const lines = TimelineFromCuts(c, c->project->Keyframes(), false);
		if (!c->frame) return;
		if (lines)
			c->frame->StatusTimeout(fmt_plural(lines, "Found %d shot and added an empty line for each", "Found %d shots and added an empty line for each", static_cast<int>(lines)));
		else
			c->frame->StatusTimeout(fmt_plural(cuts.size() - 1, "Found %d scene change", "Found %d scene changes", static_cast<int>(cuts.size() - 1)));
	}
};
}

namespace cmd {
	void init_keyframe() {
		reg(std::make_unique<keyframe_detect>());
		reg(std::make_unique<keyframe_close>());
		reg(std::make_unique<keyframe_open>());
		reg(std::make_unique<keyframe_save>());
		reg(std::make_unique<keyframe_timeline>());
	}
}

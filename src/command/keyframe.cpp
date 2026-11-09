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
#include "../scene_detect.h"
#include "../subtitle_project_access.h"
#include "../video_provider_manager.h"
#include "../include/aegisub/context.h"
#include "../libresrc/libresrc.h"
#include "../options.h"
#include "../project.h"
#include "../utils.h"

#include <libaegisub/keyframe.h>
#include <libaegisub/path.h>

#include <functional>
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

/// Where a video's cuts found at a sensitivity are kept, so that they are
/// found once
agi::fs::path SceneCachePath(agi::fs::path const& video, int sensitivity) {
	std::string key = video.string() + "|" + std::to_string(sensitivity);
	try {
		if (agi::fs::FileExists(video))
			key += "|" + std::to_string(agi::fs::Size(video)) + "|" +
				std::to_string(agi::fs::ModifiedTime(video).time_since_epoch().count());
	}
	catch (agi::fs::FileSystemError const&) { }
	char name[32];
	snprintf(name, sizeof name, "%016zx.key.txt", std::hash<std::string>{}(key));
	return config::path->Decode("?user/scenes/") / name;
}

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
		auto cache = SceneCachePath(video, sensitivity);

		std::vector<int> cuts;
		try { if (agi::fs::FileExists(cache)) cuts = agi::keyframe::Load(cache); }
		catch (agi::Exception const&) { cuts.clear(); }

		if (cuts.empty()) {
			// A provider of its own, read through from the start, so the
			// video shown is not moved about
			DialogProgress progress(c->parent, _("Detect Scene Changes"), _("Reading every frame of the video..."));
			try {
				auto provider = VideoProviderFactory::GetProvider(video, c->subtitleAccess->Read().GetYCbCrMatrix(), &progress);
				auto params = scene_detect::ForSensitivity(sensitivity);
				progress.Run([&](agi::ProgressSink *ps) { cuts = scene_detect::Detect(*provider, ps, params); });
			}
			catch (agi::UserCancelException const&) { return; }
			catch (agi::Exception const& e) {
				wxMessageBox(to_wx(e.GetMessage()), _("Detect Scene Changes"), wxOK | wxICON_ERROR, c->parent);
				return;
			}
			if (cuts.empty()) return;
			try {
				agi::fs::CreateDirectory(cache.parent_path());
				agi::keyframe::Save(cache, cuts);
			}
			catch (agi::Exception const& e) {
				wxMessageBox(to_wx(e.GetMessage()), _("Detect Scene Changes"), wxOK | wxICON_ERROR, c->parent);
				return;
			}
		}
		c->project->LoadKeyframes(cache);
		if (c->frame)
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
	}
}

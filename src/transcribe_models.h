// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <libaegisub/fs.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace agi { class ProgressSink; }

/// The Whisper models Aegisub offers to download, and where they go
namespace transcribe {
struct Model {
	std::string_view file;
	/// Where on Hugging Face it is published
	std::string_view repository;
	/// The whole file, and its SHA-256 as published, to know it came whole
	int64_t bytes;
	std::string_view sha256;
};

/// small, large-v3-turbo and large-v3, in that order: quicker to better
std::vector<Model> const& Models();
/// The one with the file name of @p path, if offered
Model const* FindModel(agi::fs::path const& path);
/// Silero's voice activity detection, for whisper.cpp to hear only where
/// there is speech and so not make up words in silence and music; fetched
/// with the Whisper model, small as it is
Model const& VadModel();
/// The VAD model, if it is in @p folder or with @p model
agi::fs::path FindVad(agi::fs::path const& folder, agi::fs::path const& model);

/// @p bytes as "190 MB" or "1.1 GB", in thousands as the sizes are published
std::string SizeText(int64_t bytes);

/// Where to download @p model from: @p source "huggingface" or empty for
/// its repository on Hugging Face, "hf-mirror" for the same on
/// hf-mirror.com, reachable from mainland China, else the address of a
/// folder holding the files, the file name put after it
std::string ModelUrl(Model const& model, std::string const& source);

/// Where models are downloaded to: Tool/Transcribe/Local/Model Folder, or
/// when that is empty the models folder of ?cache, which in a portable
/// install is beside Aegisub itself; on Linux and the like where Aegisub
/// keeps the XDG layout, $XDG_DATA_HOME/aegisub/models instead, as
/// $XDG_CACHE_HOME may be emptied at any time
agi::fs::path ModelFolder();

/// Whether @p model is in @p folder, whole
bool Downloaded(Model const& model, agi::fs::path const& folder);

/// The file's SHA-256, how far it has got shown on @p ps
/// @throws agi::UserCancelException when cancelled on @p ps
std::string Sha256File(agi::fs::path const& path, agi::ProgressSink *ps);

/// @p model in @p folder: downloaded from @p source if not there, going on
/// from what an earlier try left, checked against its SHA-256 and only then
/// put in place. @return Its path
/// @throws agi::EnvironmentError with what went wrong, and
/// agi::UserCancelException when cancelled on @p ps, what came kept
agi::fs::path GetModel(Model const& model, agi::fs::path const& folder, std::string const& source, agi::ProgressSink *ps);

/// @p part, downloaded for @p model, put at @p dest if its SHA-256 is the
/// model's; else removed, so that the next try starts afresh
/// @throws agi::EnvironmentError when it is not the model
void TakeDownload(agi::fs::path const& part, agi::fs::path const& dest, Model const& model, agi::ProgressSink *ps);
}

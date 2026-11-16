// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "transcribe_models.h"

#include "compat.h"
#include "format.h"
#include "options.h"
#include "speech_transcribe.h"

#include <libaegisub/background_runner.h>
#include <libaegisub/exception.h>
#include <libaegisub/path.h>
#include <libaegisub/sha256.h>

#include <filesystem>
#include <fstream>

#include <wx/intl.h>

namespace transcribe {
std::vector<Model> const& Models() {
	// As published at huggingface.co/ggerganov/whisper.cpp
	static const std::vector<Model> models = {
		{"ggml-small-q5_1.bin", "ggerganov/whisper.cpp", 190085487, "ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb"},
		{"ggml-large-v3-turbo-q5_0.bin", "ggerganov/whisper.cpp", 574041195, "394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2"},
		{"ggml-large-v3-q5_0.bin", "ggerganov/whisper.cpp", 1081140203, "d75795ecff3f83b5faa89d1900604ad8c780abd5739fae406de19f23ecd98ad1"},
	};
	return models;
}

Model const& VadModel() {
	// As published at huggingface.co/ggml-org/whisper-vad
	static const Model vad{"ggml-silero-v6.2.0.bin", "ggml-org/whisper-vad", 885098, "2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987"};
	return vad;
}

agi::fs::path FindVad(agi::fs::path const& folder, agi::fs::path const& model) {
	for (auto const& where : {folder, agi::fs::path(model.parent_path())})
		if (!where.empty() && Downloaded(VadModel(), where)) return where / std::string(VadModel().file);
	return {};
}

Model const* FindModel(agi::fs::path const& path) {
	auto const name = path.filename().string();
	for (auto const& m : Models())
		if (m.file == name) return &m;
	return nullptr;
}

std::string SizeText(int64_t bytes) {
	if (bytes < 0) bytes = 0;
	if (bytes >= 1000000000) return agi::format("%.1f GB", bytes / 1e9);
	if (bytes >= 1000000) return agi::format("%d MB", static_cast<int>((bytes + 500000) / 1000000));
	return agi::format("%d KB", static_cast<int>((bytes + 500) / 1000));
}

std::string ModelUrl(Model const& model, std::string const& source) {
	std::string base;
	std::string const repository(model.repository);
	if (source.empty() || source == "huggingface") base = "https://huggingface.co/" + repository + "/resolve/main";
	else if (source == "hf-mirror") base = "https://hf-mirror.com/" + repository + "/resolve/main";
	else base = source;
	while (!base.empty() && (base.back() == '/' || base.back() == ' ')) base.pop_back();
	return base + "/" + std::string(model.file);
}

agi::fs::path ModelFolder() {
	auto const chosen = OPT_GET("Tool/Transcribe/Local/Model Folder")->GetString();
	if (!chosen.empty()) return config::path->Decode(chosen);
	return config::path->Decode("?local/models");
}

bool Downloaded(Model const& model, agi::fs::path const& folder) {
	std::error_code ec;
	auto const path = folder / std::string(model.file);
	return std::filesystem::is_regular_file(path, ec) && static_cast<int64_t>(std::filesystem::file_size(path, ec)) == model.bytes && !ec;
}

std::string Sha256File(agi::fs::path const& path, agi::ProgressSink *ps) {
	std::ifstream in(path, std::ios::binary);
	if (!in) throw agi::EnvironmentError(agi::format(from_wx(_("Could not read %s.")), path.string()));
	std::error_code ec;
	auto const total = static_cast<int64_t>(std::filesystem::file_size(path, ec));
	agi::Sha256 hash;
	std::vector<char> buf(1 << 20);
	int64_t done = 0;
	while (in) {
		if (ps) {
			if (ps->IsCancelled()) throw agi::UserCancelException("cancelled");
			if (total > 0) ps->SetProgress(done, total);
		}
		in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
		auto const n = static_cast<size_t>(in.gcount());
		hash.Update(buf.data(), n);
		done += static_cast<int64_t>(n);
	}
	return hash.HexDigest();
}

void TakeDownload(agi::fs::path const& part, agi::fs::path const& dest, Model const& model, agi::ProgressSink *ps) {
	if (ps) ps->SetMessage(from_wx(_("Checking the download...")));
	std::error_code ec;
	if (Sha256File(part, ps) != model.sha256) {
		std::filesystem::remove(part, ec);
		throw agi::EnvironmentError(agi::format(from_wx(_("What was downloaded is not %s as published: it has been thrown away. Try again, or another source.")), std::string(model.file)));
	}
	std::filesystem::remove(dest, ec);
	std::filesystem::rename(part, dest, ec);
	if (ec)
		throw agi::EnvironmentError(agi::format(from_wx(_("Could not put the model at %s: %s.")), dest.string(), ec.message()));
}

agi::fs::path GetModel(Model const& model, agi::fs::path const& folder, std::string const& source, agi::ProgressSink *ps) {
	auto const dest = folder / std::string(model.file);
	if (Downloaded(model, folder)) return dest;
	agi::fs::CreateDirectory(folder);
	auto part = dest;
	part += ".part";

	std::error_code ec;
	int64_t const have = std::filesystem::exists(part, ec) ? static_cast<int64_t>(std::filesystem::file_size(part, ec)) : 0;
	auto const free = agi::fs::FreeSpace(folder);
	if (static_cast<int64_t>(free) < model.bytes - have)
		throw agi::EnvironmentError(agi::format(from_wx(_("There is not room for the model in %s: %s more is needed.")),
			folder.string(), SizeText(model.bytes - have - static_cast<int64_t>(free))));

	auto const url = ModelUrl(model, source);
	if (ps) ps->SetMessage(agi::format(from_wx(_("Downloading %s, %s...")), std::string(model.file), SizeText(model.bytes)));
	Download(url, part, model.bytes, ps);
	TakeDownload(part, dest, model, ps);
	return dest;
}
}

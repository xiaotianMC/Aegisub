// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Reader/writer registration is separate so model tests can link real readers
// without pulling in every format's interactive import dependencies.
#include "subtitle_format.h"
#include "compat.h"
#include "subtitle_format_ass.h"
#include "subtitle_format_ebu3264.h"
#include "subtitle_format_encore.h"
#include "subtitle_format_microdvd.h"
#include "subtitle_format_mkv.h"
#include "subtitle_format_srt.h"
#include "subtitle_format_ssa.h"
#include "subtitle_format_transtation.h"
#include "subtitle_format_ttxt.h"
#include "subtitle_format_txt.h"
#include <libaegisub/string.h>
#include <algorithm>
#include <memory>
#include <wx/intl.h>
namespace { std::vector<std::unique_ptr<SubtitleFormat>> formats; }

void SubtitleFormat::LoadFormats() {
	if (formats.empty()) {
		formats.emplace_back(std::make_unique<AssSubtitleFormat>());
		formats.emplace_back(std::make_unique<Ebu3264SubtitleFormat>());
		formats.emplace_back(std::make_unique<EncoreSubtitleFormat>());
		formats.emplace_back(std::make_unique<MKVSubtitleFormat>());
		formats.emplace_back(std::make_unique<MicroDVDSubtitleFormat>());
		formats.emplace_back(std::make_unique<SRTSubtitleFormat>());
		formats.emplace_back(std::make_unique<SsaSubtitleFormat>());
		formats.emplace_back(std::make_unique<TTXTSubtitleFormat>());
		formats.emplace_back(std::make_unique<TXTSubtitleFormat>());
		formats.emplace_back(std::make_unique<TranStationSubtitleFormat>());
	}
}

template<class Cont, class Pred>
SubtitleFormat *find_or_throw(Cont &container, Pred pred) {
	auto it = find_if(container.begin(), container.end(), pred);
	if (it == container.end())
		throw UnknownSubtitleFormatError("Subtitle format for extension not found");
	return it->get();
}

const SubtitleFormat *SubtitleFormat::GetReader(agi::fs::path const& filename, const char *encoding) {
	LoadFormats();
	return find_or_throw(formats, [&](std::unique_ptr<SubtitleFormat> const& f) {
		return f->CanReadFile(filename, encoding);
	});
}

const SubtitleFormat *SubtitleFormat::GetWriter(agi::fs::path const& filename) {
	LoadFormats();
	return find_or_throw(formats, [&](std::unique_ptr<SubtitleFormat> const& f) {
		return f->CanWriteFile(filename);
	});
}

std::string SubtitleFormat::GetWildcards(int mode) {
	LoadFormats();

	std::vector<std::string> all;
	std::string final;

	for (auto const& format : formats) {
		auto cur = mode == 0 ? format->GetReadWildcards() : format->GetWriteWildcards();
		if (cur.empty()) continue;

		for (auto& str : cur) str.insert(0, "*.");
		all.insert(all.end(), begin(cur), end(cur));
		agi::AppendStr(final, "|", format->GetName(), " (");
		agi::AppendJoin(final, ",", cur);
		final += ")|";
		agi::AppendJoin(final, ";", cur);
	}

	return agi::Str(from_wx(_("All Supported Formats")), " (", agi::Join(",", all), ")|", agi::Join(";", all), final);
}

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "bilingual.h"

#include "ass_dialogue.h"
#include "ass_file.h"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>

namespace bilingual {
namespace {
std::vector<std::string_view> Words(std::string_view text) {
	std::vector<std::string_view> words;
	size_t pos = 0;
	while (pos < text.size()) {
		while (pos < text.size() && text[pos] == ' ') ++pos;
		size_t end = text.find(' ', pos);
		if (end == std::string_view::npos) end = text.size();
		if (end > pos) words.push_back(text.substr(pos, end - pos));
		pos = end;
	}
	return words;
}

/// "#" and a group number of no more than nine digits
bool Group(std::string_view word, int &group) {
	if (word.size() < 2 || word.size() > 10 || word[0] != '#') return false;
	int n = 0;
	for (char ch : word.substr(1)) {
		if (ch < '0' || ch > '9') return false;
		n = n * 10 + (ch - '0');
	}
	group = n;
	return true;
}

/// The Effect values a renderer acts on: a field starting with one is
/// never read as marks
bool RendererEffect(std::string_view effect) {
	for (std::string_view known : {"banner;", "scroll up;", "scroll down;"})
		if (boost::istarts_with(effect, known)) return true;
	return false;
}
}

bool IsSource(AssDialogueBase const& line) { return boost::iequals(line.Style.get(), source_style); }
bool IsTarget(AssDialogueBase const& line) { return boost::iequals(line.Style.get(), target_style); }

AssDialogue const *Partner(AssFile const& file, AssDialogueBase const& source) {
	for (auto const& line : file.Events)
		if (!line.Comment && line.Id != source.Id && IsTarget(line) && line.Start == source.Start && line.End == source.End)
			return &line;
	return nullptr;
}

Marks ParseMarks(std::string_view effect) {
	Marks marks;
	if (RendererEffect(effect)) {
		marks.rest = std::string(effect);
		return marks;
	}
	std::string rest;
	for (auto word : Words(effect)) {
		int group;
		if (Group(word, group)) marks.group = group;
		else if (word[0] == '?') marks.unsure.emplace_back(word.substr(1));
		else {
			if (!rest.empty()) rest += ' ';
			rest += word;
		}
	}
	marks.rest = std::move(rest);
	return marks;
}

std::string FormatMarks(Marks const& marks) {
	std::string out = marks.rest;
	auto add = [&](std::string word) {
		if (!out.empty()) out += ' ';
		out += word;
	};
	if (marks.group > 0) add("#" + std::to_string(marks.group));
	for (auto word : marks.unsure) {
		boost::replace_all(word, " ", "");
		boost::replace_all(word, ",", "\xEF\xBC\x8C");
		add("?" + word);
	}
	return out;
}

std::string ClearMarks(std::string_view effect) {
	auto marks = ParseMarks(effect);
	if (marks.group == 0 && marks.unsure.empty()) return std::string(effect);
	return marks.rest;
}
}

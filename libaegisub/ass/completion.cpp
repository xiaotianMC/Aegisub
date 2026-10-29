// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <libaegisub/ass/completion.h>
#include <libaegisub/calltip_provider.h>

#include <algorithm>

namespace agi::ass {
namespace {
bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool starts(std::string_view text, std::string_view prefix) { return text.substr(0, prefix.size()) == prefix; }
}

Completion GetCompletion(std::string_view text, size_t pos, bool template_line,
	std::vector<std::string> const& styles, std::vector<std::string> const& fonts) {
	Completion result;
	if (pos > text.size()) return result;
	// Template expressions have their own language; never treat their slashes as ASS.
	if (template_line) {
		bool expression = false;
		for (size_t i = 0; i < pos; ++i)
			if (text[i] == '!') expression = !expression;
		if (expression) return result;
	}
	const auto before = text.substr(0, pos);
	auto open = before.rfind('{'), close = before.rfind('}');
	bool in_block = open != text.npos && (close == text.npos || open > close);
	auto slash = before.rfind('\\');
	if (slash == text.npos || (in_block && slash < open)) return result;
	result.start = slash;
	result.end = pos;
	auto prefix = text.substr(slash + 1, pos - slash - 1);
	if (!in_block) {
		if (prefix.size() > 1) return result;
		for (auto name : {"N", "n", "h"}) {
			if (!starts(name, prefix)) continue;
			std::string value = std::string("\\") + name;
			result.items.push_back({value, value, value.size()});
		}
		if (pos < text.size() && (text[pos] == 'N' || text[pos] == 'n' || text[pos] == 'h')) ++result.end;
		return result;
	}

	// Find the entire tag name, including the suffix to the right of the caret,
	// but leave an existing parameter (including a font/style name) untouched.
	size_t name_end = slash + 1;
	if (name_end < text.size() && text[name_end] >= '1' && text[name_end] <= '4') ++name_end;
	while (name_end < text.size() && letter(text[name_end])) ++name_end;
	if (name_end < text.size() && (text[name_end] == '+' || text[name_end] == '-')) ++name_end;
	std::string_view full = text.substr(slash + 1, name_end - slash - 1);
	size_t known_length = 0;
	for (auto const& tag : GetAssTagCatalog())
		if (starts(full, tag.name)) known_length = std::max(known_length, tag.name.size());
	result.end = known_length ? slash + 1 + known_length : name_end;
	if (prefix.size() > known_length) result.end = name_end;

	std::string previous;
	for (auto const& tag : GetAssTagCatalog()) {
		if (tag.name == previous || !starts(tag.name, prefix)) continue;
		previous = tag.name;
		std::string value = "\\" + tag.name;
		bool parens = tag.signature.find('(') != std::string::npos;
		size_t caret = value.size();
		if (parens && (result.end == text.size() || text[result.end] != '(')) {
			value += "()";
			++caret;
		}
		else if (parens) ++caret;
		result.items.push_back({tag.signature, value, caret});
	}
	return result;
}
}

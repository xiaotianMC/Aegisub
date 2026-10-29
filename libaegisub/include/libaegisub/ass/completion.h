// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace agi { struct Calltip; }

namespace agi::ass {
struct CompletionItem {
	std::string label;
	std::string text;
	size_t caret;
	std::string description;
};

struct Completion {
	size_t start = 0;
	size_t end = 0;
	bool font = false;
	std::vector<CompletionItem> items;
};

/// Positions and caret offsets are UTF-8 byte offsets, as in Scintilla.
Completion GetCompletion(std::string_view text, size_t pos, bool template_line = false,
	std::vector<std::string> const& styles = {}, std::vector<std::string> const& fonts = {});

agi::Calltip GetDrawingCalltip(std::string_view text, size_t pos, bool template_line = false);
}

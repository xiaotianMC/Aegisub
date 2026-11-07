// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "style_edit.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_override.h"
#include "ass_style.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include "style_look.h"
#include "subtitle_project_access.h"
#include "subtitle_row_edit.h"
#include "text_selection_controller.h"

#include <libaegisub/format.h>

#include <algorithm>
#include <cmath>

#include <wx/intl.h>

namespace style_edit {
namespace {
/// A {…} block of a line's text
struct Block {
	int begin, end;   ///< Byte offsets: the opening brace, one past the closing one
	bool tags;        ///< Holds override tags rather than a comment
	int plain;        ///< Plain characters before it: it applies from that one on
};

/// The blocks of @p text, found as AssDialogue::ParseTags does
std::vector<Block> Blocks(std::string const& text, int *plain_length = nullptr) {
	std::vector<Block> blocks;
	int plain = 0;
	for (size_t i = 0; i < text.size(); ) {
		if (text[i] == '{') {
			size_t close = text.find('}', i);
			if (close != std::string::npos) {
				std::string_view content(text.data() + i + 1, close - i - 1);
				bool tags = content.empty() || content.find('\\') != std::string_view::npos;
				blocks.push_back({static_cast<int>(i), static_cast<int>(close + 1), tags, plain});
				i = close + 1;
				continue;
			}
		}
		++plain;
		++i;
	}
	if (plain_length) *plain_length = plain;
	return blocks;
}

/// Byte offset of plain character @p n, after the blocks in front of it;
/// the end of the text for the character after the last
int OffsetOf(std::string const& text, int n) {
	int plain = 0;
	for (size_t i = 0; i < text.size(); ) {
		if (text[i] == '{') {
			size_t close = text.find('}', i);
			if (close != std::string::npos) {
				i = close + 1;
				continue;
			}
		}
		if (plain == n) return static_cast<int>(i);
		++plain;
		++i;
	}
	return static_cast<int>(text.size());
}

/// Byte offset just after plain character @p n - 1, before any blocks
/// that follow it
int OffsetAfter(std::string const& text, int n) {
	return n == 0 ? 0 : OffsetOf(text, n - 1) + 1;
}

/// The block @p block without the tags @p names. Everything else in it
/// stays as written; \t(…) is one tag, so what is inside it stays too.
std::string Strip(std::string_view block, std::vector<std::string> const& names, bool *found = nullptr) {
	std::string_view content = block.substr(1, block.size() - 2);
	std::string result = "{";
	int depth = 0;
	size_t start = 0;
	auto keep = [&](std::string_view piece) {
		if (!piece.empty() && piece[0] == '\\') {
			std::string name = AssOverrideTag(std::string(piece)).Name;
			if (std::find(names.begin(), names.end(), name) != names.end()) {
				if (found) *found = true;
				return;
			}
		}
		result += piece;
	};
	for (size_t i = 1; i < content.size(); ++i) {
		if (depth > 0) {
			if (content[i] == ')') --depth;
		}
		else if (content[i] == '\\') {
			keep(content.substr(start, i - start));
			start = i;
		}
		else if (content[i] == '(')
			++depth;
	}
	if (!content.empty())
		keep(content.substr(start));
	return result + "}";
}

/// @p text with the blocks for which @p strip is true rewritten by Strip,
/// and blocks left empty by it removed
std::string StripBlocks(std::string const& text, std::vector<std::string> const& names, std::function<bool(Block const&)> const& strip) {
	std::string result;
	size_t done = 0;
	for (auto const& block : Blocks(text)) {
		if (!block.tags || !strip(block)) continue;
		result.append(text, done, block.begin - done);
		std::string_view original(text.data() + block.begin, block.end - block.begin);
		bool found = false;
		std::string stripped = Strip(original, names, &found);
		if (!found) result += original;
		else if (stripped != "{}") result += stripped;
		done = block.end;
	}
	result.append(text, done);
	return result;
}

std::string Number(double value) {
	double rounded = std::round(value * 1000) / 1000;
	if (rounded == 0) rounded = 0;
	return agi::format("%g", rounded);
}

std::string Alpha(int alpha) {
	return agi::format("&H%02X&", alpha);
}

wxString UndoName(Property property) {
	switch (property) {
		case Property::Font: return _("set font");
		case Property::Size: return _("set font size");
		case Property::Weight: return _("set font weight");
		case Property::Italic: return _("toggle italic");
		case Property::Underline: return _("toggle underline");
		case Property::Strikeout: return _("toggle strikeout");
		case Property::Spacing: return _("set letter spacing");
		case Property::Alignment: return _("set alignment");
		case Property::FillColour: case Property::OutlineColour: case Property::ShadowColour: return _("set color");
		case Property::FillAlpha: case Property::OutlineAlpha: case Property::ShadowAlpha: return _("set opacity");
		case Property::Border: return _("set outline width");
		case Property::Shadow: return _("set shadow");
		case Property::Blur: return _("set blur");
	}
	return _("style panel");
}
}

std::vector<std::string> const& TagNames(Property property) {
	static const std::vector<std::string> names[] = {
		{"\\fn"}, {"\\fs", "\\fs+", "\\fs-"}, {"\\b"}, {"\\i"}, {"\\u"}, {"\\s"}, {"\\fsp"}, {"\\an", "\\a"},
		{"\\c", "\\1c"}, {"\\1a"}, {"\\3c"}, {"\\3a"}, {"\\bord", "\\xbord", "\\ybord"},
		{"\\4c"}, {"\\4a"}, {"\\shad", "\\xshad", "\\yshad"}, {"\\blur"},
	};
	return names[static_cast<int>(property)];
}

bool LineWide(Property property) {
	return property == Property::Alignment;
}

std::string TagFor(Property property, StyleLook const& look) {
	switch (property) {
		case Property::Font: return "\\fn" + look.font;
		case Property::Size: return "\\fs" + Number(look.size);
		case Property::Weight:
			return look.weight == 400 ? "\\b0" : look.weight == 700 ? "\\b1" : "\\b" + std::to_string(look.weight);
		case Property::Italic: return look.italic ? "\\i1" : "\\i0";
		case Property::Underline: return look.underline ? "\\u1" : "\\u0";
		case Property::Strikeout: return look.strikeout ? "\\s1" : "\\s0";
		case Property::Spacing: return "\\fsp" + Number(look.spacing);
		case Property::Alignment: return "\\an" + std::to_string(look.alignment);
		case Property::FillColour: return "\\c" + look.primary.GetAssOverrideFormatted();
		case Property::FillAlpha: return "\\1a" + Alpha(look.primary.a);
		case Property::OutlineColour: return "\\3c" + look.outline.GetAssOverrideFormatted();
		case Property::OutlineAlpha: return "\\3a" + Alpha(look.outline.a);
		case Property::Border: return "\\bord" + Number(look.border);
		case Property::ShadowColour: return "\\4c" + look.shadow.GetAssOverrideFormatted();
		case Property::ShadowAlpha: return "\\4a" + Alpha(look.shadow.a);
		case Property::Shadow:
			if (look.shadow_x == look.shadow_y) return "\\shad" + Number(look.shadow_x);
			return "\\xshad" + Number(look.shadow_x) + "\\yshad" + Number(look.shadow_y);
		case Property::Blur: return "\\blur" + Number(look.blur);
	}
	return {};
}

std::string SetForLine(std::string const& text, std::vector<std::string> const& names, std::string const& tag) {
	std::string result = StripBlocks(text, names, [](Block const&) { return true; });
	if (tag.empty()) return result;
	auto blocks = Blocks(result);
	if (!blocks.empty() && blocks.front().begin == 0 && blocks.front().tags)
		return result.insert(blocks.front().end - 1, tag);
	return "{" + tag + "}" + result;
}

RangeResult SetForRange(std::string const& text, int start, int end,
	std::vector<std::string> const& names, std::string const& tag, std::string const& restore)
{
	if (start > end) std::swap(start, end);
	int first = PlainPosition(text, start), last = PlainPosition(text, end);
	int length = 0;
	Blocks(text, &length);

	// The tags inside the range go; those at its end stay, and if they
	// already set the property nothing needs restoring after it
	bool set_after = false;
	for (auto const& block : Blocks(text))
		if (block.tags && block.plain == last && last < length) {
			bool found = false;
			Strip(std::string_view(text.data() + block.begin, block.end - block.begin), names, &found);
			set_after |= found;
		}
	std::string result = StripBlocks(text, names, [&](Block const& block) {
		return block.plain > first && block.plain < last;
	});

	// After the range first, so that the offset of its start stays put
	if (!restore.empty() && !set_after && last < length) {
		int at = OffsetOf(result, last);
		auto blocks = Blocks(result);
		auto before = std::find_if(blocks.begin(), blocks.end(), [&](Block const& b) { return b.end == at && b.tags; });
		if (before != blocks.end()) result.insert(before->end - 1, restore);
		else result.insert(at, "{" + restore + "}");
	}

	// The start: into a tag block right in front of the text, replacing
	// the property there, or a new one
	int at = OffsetOf(result, first);
	auto blocks = Blocks(result);
	auto before = std::find_if(blocks.begin(), blocks.end(), [&](Block const& b) { return b.end == at && b.tags; });
	if (before != blocks.end()) {
		std::string stripped = Strip(std::string_view(result.data() + before->begin, before->end - before->begin), names);
		stripped.insert(stripped.size() - 1, tag);
		result.replace(before->begin, before->end - before->begin, stripped);
	}
	else
		result.insert(at, "{" + tag + "}");

	return {result, OffsetOf(result, first), OffsetAfter(result, last)};
}

int Apply(agi::Context *c, Property property, std::function<void(StyleLook&)> const& change, int amend_id) {
	auto scope = c->subtitleAccess->TryWrite();
	if (!scope) return -1;
	SubtitleRowEdit staged(c);
	auto const& file = c->subtitleAccess->Read();
	StyleLookup find = [&](std::string const& name) { return file.GetStyle(name); };
	auto active = c->selectionController->ReadActiveLine();
	auto const& names = TagNames(property);

	const int start = c->textSelectionController->GetSelectionStart();
	const int end = c->textSelectionController->GetSelectionEnd();
	AssDialogue *active_row = nullptr;
	for (auto row : staged.rows)
		if (active && row->Id == active->Id) active_row = row;
	if (staged.rows.empty()) return -1;

	std::string const* active_text = active_row ? &active_row->Text.get() : nullptr;
	int first = active_text ? PlainPosition(*active_text, std::min(start, end)) : 0;
	int last = active_text ? PlainPosition(*active_text, std::max(start, end)) : 0;
	int new_start = start, new_end = end;
	bool changed = false;

	if (active_row && staged.rows.size() == 1 && first != last && !LineWide(property)) {
		// The selected text: set at its start, and what came after it
		// goes back to what it was
		std::string text = active_row->Text;
		StyleLook look = LookAt(*active_row, first, find);
		std::string restore = TagFor(property, LookAt(*active_row, last, find));
		change(look);
		auto result = SetForRange(text, start, end, names, TagFor(property, look), restore);
		changed = result.text != text;
		active_row->Text = result.text;
		new_start = result.start;
		new_end = result.end;
	}
	else {
		// Whole lines: the tag at the start, the property's other tags gone,
		// and no tag where the line's style already has the value
		int cursor = active_text ? PlainPosition(*active_text, start) : 0;
		for (auto row : staged.rows) {
			std::string text = row->Text;
			AssStyle fallback;
			AssStyle const* style = find(row->Style.get());
			StyleLook look = LookAt(*row, 0, find);
			change(look);
			std::string tag = TagFor(property, look);
			if (tag == TagFor(property, LookOf(style ? *style : fallback)))
				tag.clear();
			std::string result = SetForLine(text, names, tag);
			if (result == text) continue;
			changed = true;
			row->Text = result;
			if (row == active_row)
				new_start = new_end = OffsetOf(result, cursor);
		}
	}
	if (!changed) return amend_id;

	auto const& sel = c->selectionController->ReadSelectedSet();
	auto edit = c->subtitleAccess->Edit(scope->Lease());
	edit.UpdateRows(staged.Values());
	int id = edit.Commit(UndoName(property), AssFile::COMMIT_DIAG_TEXT, amend_id, sel.size() == 1 ? (*sel.begin())->Id : 0);
	if (active_row && (new_start != start || new_end != end))
		c->textSelectionController->SetSelection(new_start, new_end);
	return id;
}
}

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#include "style_look.h"

#include "ass_dialogue.h"
#include "ass_style.h"

#include <algorithm>

namespace {
StyleLook FromStyle(AssStyle const& style) {
	StyleLook look;
	look.style = style.name;
	look.font = style.font;
	look.size = style.fontsize;
	look.weight = style.bold ? 700 : 400;
	look.italic = style.italic;
	look.underline = style.underline;
	look.strikeout = style.strikeout;
	look.spacing = style.spacing;
	look.alignment = style.alignment;
	look.primary = style.primary;
	look.outline = style.outline;
	look.shadow = style.shadow;
	look.border = style.outline_w;
	look.shadow_x = look.shadow_y = style.shadow_w;
	look.border_style = style.borderstyle;
	look.scale_x = style.scalex;
	look.scale_y = style.scaley;
	look.angle = style.angle;
	return look;
}

/// \r: back to a style, keeping what is not the text's look
void Reset(StyleLook &look, AssStyle const& style) {
	std::string name = look.style;
	int alignment = look.alignment;
	look = FromStyle(style);
	look.style = std::move(name);
	look.alignment = alignment;
}

struct Applier {
	StyleLook &look;
	AssStyle const& line_style;
	StyleLookup const& find_style;
	double border_x, border_y;

	void Colour(AssOverrideTag const& tag, agi::Color &colour, agi::Color const& original) {
		auto const& p = tag.Params[0];
		agi::Color value = p.omitted ? original : p.Get<agi::Color>();
		value.a = colour.a;
		colour = value;
	}

	void Alpha(AssOverrideTag const& tag, agi::Color &colour, agi::Color const& original) {
		auto const& p = tag.Params[0];
		colour.a = p.omitted ? original.a : static_cast<unsigned char>(p.Get<int>());
	}

	template<class T>
	T Value(AssOverrideTag const& tag, T original) {
		return tag.Params.empty() ? original : tag.Params[0].Get<T>(original);
	}

	void Apply(AssOverrideTag const& tag) {
		auto const& name = tag.Name;
		if (name == "\\r") {
			AssStyle const* style = &line_style;
			if (!tag.Params.empty() && !tag.Params[0].omitted) {
				auto named = tag.Params[0].Get<std::string>();
				if (!named.empty())
					if (auto found = find_style(named)) style = found;
			}
			Reset(look, *style);
			border_x = border_y = look.border;
		}
		else if (name == "\\fn") look.font = Value<std::string>(tag, line_style.font);
		else if (name == "\\fs") look.size = Value(tag, line_style.fontsize);
		else if (name == "\\b") {
			int b = Value(tag, line_style.bold ? 1 : 0);
			look.weight = b == 0 ? 400 : b == 1 ? 700 : b;
		}
		else if (name == "\\i") look.italic = Value(tag, line_style.italic);
		else if (name == "\\u") look.underline = Value(tag, line_style.underline);
		else if (name == "\\s") look.strikeout = Value(tag, line_style.strikeout);
		else if (name == "\\fsp") look.spacing = Value(tag, line_style.spacing);
		else if (name == "\\c" || name == "\\1c") Colour(tag, look.primary, line_style.primary);
		else if (name == "\\3c") Colour(tag, look.outline, line_style.outline);
		else if (name == "\\4c") Colour(tag, look.shadow, line_style.shadow);
		else if (name == "\\1a") Alpha(tag, look.primary, line_style.primary);
		else if (name == "\\3a") Alpha(tag, look.outline, line_style.outline);
		else if (name == "\\4a") Alpha(tag, look.shadow, line_style.shadow);
		else if (name == "\\alpha") {
			Alpha(tag, look.primary, line_style.primary);
			Alpha(tag, look.outline, line_style.outline);
			Alpha(tag, look.shadow, line_style.shadow);
		}
		else if (name == "\\bord") border_x = border_y = Value(tag, line_style.outline_w);
		else if (name == "\\xbord") border_x = Value(tag, line_style.outline_w);
		else if (name == "\\ybord") border_y = Value(tag, line_style.outline_w);
		else if (name == "\\shad") look.shadow_x = look.shadow_y = Value(tag, line_style.shadow_w);
		else if (name == "\\xshad") look.shadow_x = Value(tag, line_style.shadow_w);
		else if (name == "\\yshad") look.shadow_y = Value(tag, line_style.shadow_w);
		else if (name == "\\blur") look.blur = Value(tag, 0.);
		else if (name == "\\fscx") look.scale_x = Value(tag, line_style.scalex);
		else if (name == "\\fscy") look.scale_y = Value(tag, line_style.scaley);
		else if (name == "\\frz" || name == "\\fr") look.angle = Value(tag, line_style.angle);
	}
};
}

StyleLook LookAt(AssDialogue const& line, int plain_pos, StyleLookup const& find_style) {
	AssStyle fallback;
	AssStyle const* style = find_style(line.Style.get());
	if (!style) style = &fallback;

	StyleLook look = FromStyle(*style);
	look.style = line.Style.get();
	Applier applier{look, *style, find_style, look.border, look.border};

	auto blocks = line.ParseTags();
	bool aligned = false;
	int plain = 0;
	for (auto const& block : blocks) {
		if (block->GetType() != AssBlockType::OVERRIDE) {
			if (block->GetType() != AssBlockType::COMMENT)
				plain += static_cast<int>(block->GetText().size());
			continue;
		}
		auto ovr = static_cast<AssDialogueBlockOverride const*>(block.get());
		for (auto const& tag : ovr->Tags) {
			// Only the first alignment in a line counts, wherever it is
			if (!aligned && (tag.Name == "\\an" || tag.Name == "\\a") && !tag.Params.empty() && !tag.Params[0].omitted) {
				int value = tag.Params[0].Get<int>();
				if (tag.Name == "\\a") value = AssStyle::SsaToAss(value);
				if (value >= 1 && value <= 9) {
					look.alignment = value;
					aligned = true;
				}
			}
			if (plain <= plain_pos)
				applier.Apply(tag);
		}
	}
	look.border = std::max(applier.border_x, applier.border_y);
	return look;
}

int PlainPosition(std::string const& text, int raw_pos) {
	int plain = 0;
	bool in_block = false;
	for (int i = 0, max = static_cast<int>(text.size()); i < raw_pos && i < max; ++i) {
		if (text[i] == '{' && text.find('}', i) != std::string::npos)
			in_block = true;
		if (!in_block)
			++plain;
		if (text[i] == '}' && in_block)
			in_block = false;
	}
	return plain;
}

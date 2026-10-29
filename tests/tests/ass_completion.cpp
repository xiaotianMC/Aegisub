// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <libaegisub/ass/completion.h>
#include <libaegisub/calltip_provider.h>
#include <main.h>
#include <algorithm>

namespace {
agi::ass::Completion complete(std::string marked, bool templ = false,
	std::vector<std::string> const& styles = {}, std::vector<std::string> const& fonts = {}) {
	auto pos = marked.find('|');
	marked.erase(pos, 1);
	return agi::ass::GetCompletion(marked, pos, templ, styles, fonts);
}
bool has(agi::ass::Completion const& result, std::string const& text) {
	return std::any_of(result.items.begin(), result.items.end(), [&](auto const& item) { return item.text == text; });
}
std::string accept(std::string marked, std::string const& value) {
	auto result = complete(marked);
	marked.erase(marked.find('|'), 1);
	marked.replace(result.start, result.end - result.start, value);
	return marked;
}
}

TEST(ass_completion, partial_and_unclosed_tags) {
	EXPECT_TRUE(has(complete("{\\bo|"), "\\bord"));
	EXPECT_TRUE(has(complete("{\\fs|}"), "\\fscx"));
	EXPECT_TRUE(has(complete("{\\|}"), "\\pos()"));
	EXPECT_TRUE(has(complete("{\\1|}"), "\\1c"));
	EXPECT_TRUE(complete("{\\fs30|}").items.empty());
	EXPECT_TRUE(complete("hello|").items.empty());
	EXPECT_TRUE(complete("{comment|}").items.empty());
}

TEST(ass_completion, replacement_preserves_parameters_and_unicode) {
	EXPECT_EQ("中文{\\bord3\\c&HFFFFFF&}正文", accept("中文{\\bo|rd3\\c&HFFFFFF&}正文", "\\bord"));
	EXPECT_EQ("{\\fnArial}", accept("{\\f|nArial}", "\\fn"));
	EXPECT_EQ("{\\pos(10,20)}", accept("{\\po|s(10,20)}", "\\pos"));
	EXPECT_TRUE(has(complete("{\\po|s(10,20)}"), "\\pos"));
}

TEST(ass_completion, escapes_and_case) {
	EXPECT_TRUE(has(complete("text\\|"), "\\N"));
	EXPECT_FALSE(has(complete("text\\|"), "\\pos()"));
	EXPECT_TRUE(has(complete("{\\K|}"), "\\K"));
	EXPECT_FALSE(has(complete("{\\K|}"), "\\k"));
	EXPECT_FALSE(has(complete("{\\k|}"), "\\K"));
}

TEST(ass_completion, template_expressions) {
	EXPECT_TRUE(complete("!some_code(\\bo|)!", true).items.empty());
	EXPECT_TRUE(has(complete("!code!{\\bo|}", true), "\\bord"));
}

TEST(ass_completion, contextual_parameters) {
	EXPECT_TRUE(has(complete("{\\an|}"), "5"));
	EXPECT_FALSE(has(complete("{\\an|}"), "0"));
	EXPECT_TRUE(has(complete("{\\q|}"), "2"));
	EXPECT_TRUE(has(complete("{\\i|}"), "1"));
	EXPECT_TRUE(has(complete("{\\1c&H|}"), "&H0000FF&"));
	EXPECT_TRUE(has(complete("{\\alpha|}"), "&HFF&"));
	EXPECT_EQ("{\\an5\\bord2}正文", accept("{\\an|9\\bord2}正文", "5"));
	EXPECT_TRUE(complete("{\\pos(10,|20)}").items.empty());
}

TEST(ass_completion, dynamic_names) {
	auto styles = complete("{\\r字|幕样式}", false, {"字幕样式", "字牌", "Default"});
	EXPECT_TRUE(has(styles, "字幕样式"));
	EXPECT_TRUE(has(styles, "字牌"));
	EXPECT_FALSE(has(styles, "Default"));
	EXPECT_EQ(3u, styles.start);
	auto fonts = complete("{\\fna|}", false, {}, {"Arial", "Arial", "Arial Narrow", "Noto Sans"});
	EXPECT_TRUE(fonts.font);
	EXPECT_EQ(2u, fonts.items.size());
	EXPECT_TRUE(has(fonts, "Arial Narrow"));
	EXPECT_TRUE(complete("{\\r|}", false, {"New Style"}).items.size() == 1);
	EXPECT_TRUE(has(complete("{\\r|}", false, {"Changed"}), "Changed"));
}

TEST(ass_completion, transform_restricts_candidates) {
	EXPECT_TRUE(has(complete("{\\t(0,500,\\|)}"), "\\blur"));
	EXPECT_FALSE(has(complete("{\\t(0,500,\\|)}"), "\\pos()"));
	EXPECT_FALSE(has(complete("{\\t(\\|)}"), "\\fn"));
	EXPECT_TRUE(has(complete("{\\t(\\clip(1,2,3,4)\\|)}"), "\\bord"));
	EXPECT_TRUE(has(complete("{\\t(\\bord2)\\|}"), "\\pos()"));
	EXPECT_TRUE(complete("{\\clip(m 0 0 \\|)}").items.empty());
	auto clipping = complete("{\\t(\\cl|)}");
	ASSERT_EQ(1u, clipping.items.size());
	EXPECT_NE(std::string::npos, clipping.items[0].label.find("X1"));
}

TEST(ass_completion, drawing_commands_follow_mode_and_spline_state) {
	EXPECT_TRUE(has(complete("{\\p1}|"), "m "));
	EXPECT_FALSE(has(complete("{\\p1}|"), "l "));
	EXPECT_TRUE(has(complete("{\\p2}m 0 0 |"), "b "));
	EXPECT_FALSE(has(complete("{\\p2}m 0 0 |"), "p "));
	EXPECT_TRUE(has(complete("{\\p1}m 0 0 s 0 0 10 10 20 20 |"), "p "));
	EXPECT_TRUE(has(complete("{\\p1}m 0 0 s 0 0 10 10 20 20 |"), "c"));
	EXPECT_TRUE(complete("{\\p1}m 0 0{\\p0}|").items.empty());
	EXPECT_TRUE(complete("{\\p1}m 0 0{\\r}|").items.empty());
	EXPECT_TRUE(has(complete("{\\p1}m 0 0{\\bo|}"), "\\bord"));
	EXPECT_EQ("{\\p1}m 0 0 b  10 10", accept("{\\p1}m 0 0 b|  10 10", "b"));
}

TEST(ass_completion, vector_clips_and_rectangles) {
	EXPECT_TRUE(has(complete("{\\clip(|)}"), "m "));
	EXPECT_TRUE(has(complete("{\\clip(2,m 0 0 |"), "l "));
	EXPECT_TRUE(has(complete("{\\iclip(m 0 0 |)}"), "b "));
	EXPECT_TRUE(complete("{\\clip(0,0,|100,100)}").items.empty());
	EXPECT_TRUE(complete("{\\clip(0,20|)}").items.empty());
	EXPECT_TRUE(complete("{\\t(\\clip(|))}").items.empty());
	EXPECT_TRUE(complete("!{\\p1}m |!", true).items.empty());
}

TEST(ass_completion, drawing_parameter_highlights) {
	std::string text = "{\\p1}m 0 0 b 1 2 ";
	auto tip = agi::ass::GetDrawingCalltip(text, text.size());
	ASSERT_NE(nullptr, tip.text);
	EXPECT_STREQ("b x1 y1 x2 y2 x3 y3", tip.text);
	EXPECT_EQ("x2", std::string(tip.text).substr(tip.highlight_start, tip.highlight_end - tip.highlight_start));
	text = "{\\clip(m 10 ";
	tip = agi::ass::GetDrawingCalltip(text, text.size());
	ASSERT_NE(nullptr, tip.text);
	EXPECT_EQ("y", std::string(tip.text).substr(tip.highlight_start, tip.highlight_end - tip.highlight_start));
	EXPECT_EQ(nullptr, agi::ass::GetDrawingCalltip("text", 4).text);
	EXPECT_EQ(nullptr, agi::ass::GetDrawingCalltip("{\\p0}text", 9).text);
}

TEST(ass_completion, resume_parameters_from_manually_entered_text) {
	for (std::string marked : {"{\\pos(100,|", "{\\pos(100,|}", "{\\pos(100,|200)}中文"}) {
		auto pos = marked.find('|');
		marked.erase(pos, 1);
		for (int resume = 0; resume < 2; ++resume) {
			auto tip = agi::ass::GetEditingCalltip(marked, pos);
			ASSERT_NE(nullptr, tip.text);
			EXPECT_STREQ("\\pos(X,Y)", tip.text);
			EXPECT_EQ("Y", std::string(tip.text).substr(tip.highlight_start, tip.highlight_end - tip.highlight_start));
		}
	}
}

TEST(ass_completion, resumed_assistance_respects_context) {
	EXPECT_TRUE(has(complete("中文{\\po|}"), "\\pos()"));
	EXPECT_EQ(nullptr, agi::ass::GetEditingCalltip("{\\pos(1,2)}正文", std::string("{\\pos(1,2)}正文").size()).text);
	std::string expression = "!{\\pos(1,";
	EXPECT_EQ(nullptr, agi::ass::GetEditingCalltip(expression, expression.size(), true).text);
}

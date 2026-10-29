// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <libaegisub/ass/completion.h>
#include <main.h>
#include <algorithm>

namespace {
agi::ass::Completion complete(std::string marked, bool templ = false) {
	auto pos = marked.find('|');
	marked.erase(pos, 1);
	return agi::ass::GetCompletion(marked, pos, templ);
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

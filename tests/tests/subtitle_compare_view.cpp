// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// C2-B comparison UI view model.
#include <main.h>
#include "../../src/subtitle_compare_view.h"
#include "../../src/subtitle_compare.h"
#include "../../src/ass_dialogue.h"

namespace {
using namespace subtitle_compare_view;
AssDialogue& line(AssFile& f, std::string const& text, int start, int end) {
    auto l = new AssDialogue; l->Text = text; l->Start = start; l->End = end; f.Events.push_back(*l); return *l;
}
struct Fixture {
    AssFile cur, base;
    std::vector<model::Item> items;
    Fixture() {
        line(cur, "same", 0, 100);
        auto& c = line(cur, "old", 1000, 2000); c.Actor = "a";
        line(cur, "gone", 3000, 3100);
        line(cur, "timed", 4000, 4500);
        line(base, "same", 0, 100);
        auto& b = line(base, "new", 1000, 2000); b.Actor = "b"; b.Layer = 2;
        line(base, "timed", 4000, 4501);
        line(base, "added", 5000, 5100);
        items = model::Compare(subtitle_compare::ProjectDocument(base).document, subtitle_compare::ProjectDocument(cur).document);
    }
    size_t row(State const& s, model::Kind kind) {
        for (size_t i = 0; i < s.Rows().size(); ++i) if (s.Rows()[i].kind == kind) return i;
        ADD_FAILURE(); return 0;
    }
};
}

TEST(subtitle_compare_view, rows_list_changes_with_field_values_and_nothing_checked) {
    Fixture f;
    State s(f.cur, f.base, model::All, f.items);
    ASSERT_EQ(4u, s.Rows().size()); // Unchanged is not listed
    EXPECT_EQ(2u, s.Count(model::Kind::Modified)); EXPECT_EQ(1u, s.Count(model::Kind::Added)); EXPECT_EQ(1u, s.Count(model::Kind::Deleted));
    EXPECT_EQ(0u, s.CheckedCount()); EXPECT_TRUE(s.Choices().empty());
    auto const& m = s.Rows()[f.row(s, model::Kind::Modified)];
    EXPECT_EQ(2u, *m.current_row); EXPECT_EQ(2u, *m.baseline_row);
    EXPECT_EQ("old", m.current_text); EXPECT_EQ("new", m.baseline_text);
    ASSERT_EQ(3u, m.diffs.size()); // field display order: Text, Actor, Layer
    EXPECT_EQ(model::Text, m.diffs[0].field);
    EXPECT_EQ(model::Actor, m.diffs[1].field); EXPECT_EQ("a", m.diffs[1].current); EXPECT_EQ("b", m.diffs[1].baseline);
    EXPECT_EQ(model::Layer, m.diffs[2].field); EXPECT_EQ("2", m.diffs[2].baseline);
    auto t = *std::find_if(s.Rows().begin(), s.Rows().end(), [](Row const& r) { return r.current_text == "timed"; });
    ASSERT_EQ(1u, t.diffs.size()); EXPECT_EQ(model::End, t.diffs[0].field);
    EXPECT_EQ("0:00:04.500", t.diffs[0].current); EXPECT_EQ("0:00:04.501", t.diffs[0].baseline); // 1 ms is visible
    auto const& d = s.Rows()[f.row(s, model::Kind::Deleted)];
    EXPECT_FALSE(d.baseline_row); EXPECT_EQ("gone", d.current_text);
}

TEST(subtitle_compare_view, field_choices_follow_checks) {
    Fixture f;
    State s(f.cur, f.base, model::All, f.items);
    auto m = f.row(s, model::Kind::Modified);
    s.Check(m, true);
    ASSERT_EQ(1u, s.Choices().size());
    EXPECT_EQ(model::Text | model::Actor | model::Layer, s.Choices()[0].fields);
    s.SetField(m, model::Actor, false);
    EXPECT_EQ(model::Text | model::Layer, s.Choices()[0].fields);
    s.SetField(m, model::Text, false); s.SetField(m, model::Layer, false);
    EXPECT_FALSE(s.Checked(m)); EXPECT_TRUE(s.Choices().empty()); // no field left: unchecked
    s.SetField(m, model::Layer, true);
    EXPECT_TRUE(s.Checked(m)); EXPECT_EQ(model::Layer, s.Choices()[0].fields);
    s.Check(m, false); s.Check(m, true);
    EXPECT_EQ(model::Layer, s.Choices()[0].fields); // a partial choice is kept across uncheck/check
    EXPECT_THROW(s.SetField(m, model::End, true), std::invalid_argument);
    // Choosing a field on an unchecked row takes that field alone.
    s.CheckAll(false); s.SetField(m, model::Actor, true);
    EXPECT_EQ(model::Actor, s.Choices()[0].fields);
    EXPECT_THROW(s.SetField(f.row(s, model::Kind::Added), model::Text, true), std::logic_error);
}

TEST(subtitle_compare_view, check_all_by_kind_and_choices_feed_the_planner) {
    Fixture f;
    State s(f.cur, f.base, model::All, f.items);
    s.CheckAll(true, model::Kind::Added);
    EXPECT_EQ(1u, s.CheckedCount());
    s.CheckAll(true);
    EXPECT_EQ(4u, s.CheckedCount());
    auto plan = subtitle_apply::Prepare(f.cur, f.base, model::All, f.items, s.Choices());
    ASSERT_TRUE(plan.report.Ok());
    std::vector<std::string> texts;
    for (auto e : plan.events) texts.push_back(e->Text.get());
    EXPECT_EQ((std::vector<std::string>{"same", "new", "timed", "added"}), texts);
    s.CheckAll(false);
    EXPECT_EQ(0u, s.CheckedCount());
}

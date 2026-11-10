// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// C2-C diff editor view model.
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
    size_t entry(State const& s, Change change) {
        for (size_t i = 0; i < s.Entries().size(); ++i) if (s.Entries()[i].change == change) return i;
        ADD_FAILURE(); return 0;
    }
    size_t text(State const& s, std::string const& t) {
        for (size_t i = 0; i < s.Entries().size(); ++i) {
            auto const& e = s.Entries()[i];
            if ((e.left && e.left->Value(model::Text) == t) || (e.right && e.right->Value(model::Text) == t)) return i;
        }
        ADD_FAILURE(); return 0;
    }
};
std::vector<std::string> planned(Fixture const& f, State const& s) {
    auto plan = subtitle_apply::Prepare(f.cur, f.base, model::All, f.items, s.Choices());
    EXPECT_TRUE(plan.report.Ok());
    std::vector<std::string> texts;
    for (auto e : plan.events) texts.push_back(e->Text.get());
    return texts;
}
}

TEST(subtitle_compare_view, entries_align_both_sides_with_the_project_on_the_right) {
    Fixture f;
    State s(f.cur, f.base, model::All, f.items);
    ASSERT_EQ(f.items.size(), s.Entries().size()); // unchanged lines are kept for context
    ASSERT_EQ(4u, s.Diffs().size());
    EXPECT_EQ(Side::Right, s.ProjectSide());
    auto const& same = s.Entries()[0];
    EXPECT_FALSE(same.IsDiff()); EXPECT_TRUE(same.left && same.right);

    auto const& m = s.Entries()[f.entry(s, Change::Modified)];
    EXPECT_EQ("new", m.left->Value(model::Text)); EXPECT_EQ("old", m.right->Value(model::Text)); // left: the file
    EXPECT_EQ(2u, m.left->number); EXPECT_EQ(2u, m.right->number);
    EXPECT_EQ(model::Text | model::Actor | model::Layer, m.changed);
    EXPECT_EQ("b", m.left->Value(model::Actor)); EXPECT_EQ("2", m.left->Value(model::Layer));

    auto const& inserted = s.Entries()[f.text(s, "gone")];  // only the project has it
    EXPECT_EQ(Change::Inserted, inserted.change); EXPECT_FALSE(inserted.left); EXPECT_TRUE(inserted.right);
    auto const& removed = s.Entries()[f.text(s, "added")];  // only the file has it
    EXPECT_EQ(Change::Removed, removed.change); EXPECT_TRUE(removed.left); EXPECT_FALSE(removed.right);

    auto const& t = s.Entries()[f.text(s, "timed")];
    EXPECT_EQ(model::Field(model::End), t.changed);
    EXPECT_EQ("0:00:04.501", t.left->Value(model::End)); EXPECT_EQ("0:00:04.500", t.right->Value(model::End)); // 1 ms is visible
}

TEST(subtitle_compare_view, accept_keeps_the_right_side_and_reject_the_left_on_either_project_side) {
    Fixture f;
    State s(f.cur, f.base, model::All, f.items);
    auto m = f.entry(s, Change::Modified);
    EXPECT_EQ(Decision::Pending, s.GetDecision(m)); EXPECT_FALSE(s.Winner(m));
    EXPECT_TRUE(s.Choices().empty());

    // Project on the right: accepting keeps the project, rejecting restores the file's line.
    EXPECT_TRUE(s.Decide({m}, Decision::Accepted));
    EXPECT_EQ(Decision::Accepted, s.GetDecision(m)); EXPECT_EQ(Side::Right, *s.Winner(m));
    EXPECT_TRUE(s.Choices().empty());
    s.Decide({m}, Decision::Rejected);
    ASSERT_EQ(1u, s.Choices().size());
    EXPECT_EQ(s.Entries()[m].item, s.Choices()[0].item);
    EXPECT_EQ(model::Text | model::Actor | model::Layer, s.Choices()[0].fields);
    s.Decide({f.text(s, "gone")}, Decision::Rejected);   // remove the project's extra line
    s.Decide({f.text(s, "added")}, Decision::Rejected);  // restore the file's line
    s.Decide({f.text(s, "timed")}, Decision::Accepted);  // keep the project's end time
    EXPECT_EQ((std::vector<std::string>{"same", "new", "timed", "added"}), planned(f, s));

    // Swapping sides keeps the effect on the project: rejected becomes accepted.
    s.SetProjectSide(Side::Left);
    EXPECT_EQ(Decision::Accepted, s.GetDecision(m));
    EXPECT_EQ("old", s.Entries()[m].left->Value(model::Text));
    EXPECT_EQ(Change::Removed, s.Entries()[f.text(s, "gone")].change);
    EXPECT_EQ(Change::Inserted, s.Entries()[f.text(s, "added")].change);
    EXPECT_EQ(Decision::Rejected, s.GetDecision(f.text(s, "timed")));
    EXPECT_EQ((std::vector<std::string>{"same", "new", "timed", "added"}), planned(f, s));

    // Project on the left: accepting takes the file's version.
    State l(f.cur, f.base, model::All, f.items, Side::Left);
    l.Decide(l.Diffs(), Decision::Accepted);
    EXPECT_EQ(4u, l.Choices().size());
    EXPECT_EQ((std::vector<std::string>{"same", "new", "timed", "added"}), planned(f, l));
    l.Decide(l.Diffs(), Decision::Rejected);
    EXPECT_TRUE(l.Choices().empty());
    EXPECT_THROW(l.Decide({m}, Decision::Partial), std::invalid_argument);
}

TEST(subtitle_compare_view, single_fields_make_a_partial_decision) {
    Fixture f;
    State s(f.cur, f.base, model::All, f.items);
    auto m = f.entry(s, Change::Modified);
    // Restoring one field of a pending line leaves the others as the project has them.
    EXPECT_TRUE(s.SetField(m, model::Actor, Side::Left));
    EXPECT_EQ(Decision::Partial, s.GetDecision(m));
    EXPECT_EQ(Side::Left, *s.Winner(m, model::Actor)); EXPECT_EQ(Side::Right, *s.Winner(m, model::Text));
    ASSERT_EQ(1u, s.Choices().size()); EXPECT_EQ(model::Field(model::Actor), s.Choices()[0].fields);
    EXPECT_FALSE(s.SetField(m, model::Actor, Side::Left)); // no change, no undo step
    s.SetField(m, model::Text, Side::Left); s.SetField(m, model::Layer, Side::Left);
    EXPECT_EQ(Decision::Rejected, s.GetDecision(m));
    s.SetField(m, model::Text, Side::Right);
    EXPECT_EQ(model::Actor | model::Layer, s.Choices()[0].fields);
    EXPECT_THROW(s.SetField(m, model::End, Side::Left), std::invalid_argument);
    EXPECT_THROW(s.SetField(f.text(s, "gone"), model::Text, Side::Left), std::logic_error);
    auto plan = subtitle_apply::Prepare(f.cur, f.base, model::All, f.items, s.Choices());
    ASSERT_TRUE(plan.report.Ok());
    EXPECT_EQ("old", plan.events[1]->Text.get()); EXPECT_EQ("b", plan.events[1]->Actor.get()); EXPECT_EQ(2, plan.events[1]->Layer);
}

TEST(subtitle_compare_view, decisions_undo_and_redo_as_steps) {
    Fixture f;
    State s(f.cur, f.base, model::All, f.items);
    EXPECT_FALSE(s.CanUndo());
    EXPECT_EQ(4u, s.Changing(s.Diffs(), Decision::Rejected));
    EXPECT_TRUE(s.Decide(s.Diffs(), Decision::Rejected)); // one step for the batch
    EXPECT_EQ(4u, s.Count(Decision::Rejected));
    EXPECT_FALSE(s.Decide(s.Diffs(), Decision::Rejected)); // nothing changes: no step
    auto m = f.entry(s, Change::Modified);
    s.Decide({m}, Decision::Accepted);
    EXPECT_EQ(1u, s.Changing(s.Diffs(), Decision::Rejected));
    EXPECT_TRUE(s.Undo());
    EXPECT_EQ(Decision::Rejected, s.GetDecision(m));
    EXPECT_TRUE(s.Undo());
    EXPECT_EQ(4u, s.Count(Decision::Pending)); EXPECT_FALSE(s.CanUndo());
    EXPECT_TRUE(s.Redo());
    EXPECT_EQ(4u, s.Count(Decision::Rejected)); EXPECT_TRUE(s.CanRedo());
    s.Decide({m}, Decision::Pending);                    // a new step drops the redo history
    EXPECT_FALSE(s.CanRedo()); EXPECT_EQ(Decision::Pending, s.GetDecision(m));
    EXPECT_EQ(3u, s.Choices().size());
    // Unchanged entries take no decision.
    EXPECT_FALSE(s.Decide({0}, Decision::Accepted));
    EXPECT_EQ(Decision::Pending, s.GetDecision(0));
}

TEST(subtitle_compare_view, checks_cover_differences_only) {
    Fixture f;
    State s(f.cur, f.base, model::All, f.items);
    s.Check(0, true);                                    // unchanged: ignored
    EXPECT_TRUE(s.CheckedEntries().empty());
    s.CheckAll(true);
    EXPECT_EQ(s.Diffs(), s.CheckedEntries());
    s.Check(s.Diffs()[1], false);
    EXPECT_EQ(3u, s.CheckedEntries().size());
    s.Decide(s.CheckedEntries(), Decision::Rejected);
    EXPECT_EQ(Decision::Pending, s.GetDecision(s.Diffs()[1]));
    s.CheckAll(false);
    EXPECT_TRUE(s.CheckedEntries().empty());
    EXPECT_EQ(1u, *s.DiffIndex(s.Diffs()[1])); EXPECT_FALSE(s.DiffIndex(0));
}

TEST(subtitle_compare_view, layout_folds_unchanged_runs_around_context) {
    AssFile cur, base;
    for (int i = 0; i < 20; ++i) {
        line(cur, "line " + std::to_string(i), i * 1000, i * 1000 + 500);
        line(base, i == 10 ? "changed" : "line " + std::to_string(i), i * 1000, i * 1000 + 500);
    }
    auto items = model::Compare(subtitle_compare::ProjectDocument(base).document, subtitle_compare::ProjectDocument(cur).document);
    State s(cur, base, model::All, items);
    ASSERT_EQ(1u, s.Diffs().size()); ASSERT_EQ(10u, s.Diffs()[0]);
    auto rows = s.Layout(false, {});
    ASSERT_EQ(9u, rows.size());
    EXPECT_TRUE(rows[0].IsFold()); EXPECT_EQ(0u, rows[0].entry); EXPECT_EQ(7u, rows[0].hidden);
    EXPECT_EQ(7u, rows[1].entry); EXPECT_EQ(10u, rows[4].entry);
    EXPECT_EQ(13u, rows[7].entry);
    EXPECT_TRUE(rows[8].IsFold()); EXPECT_EQ(14u, rows[8].entry); EXPECT_EQ(6u, rows[8].hidden);
    EXPECT_EQ(14u, s.Layout(false, {14}).size());        // one fold opened
    EXPECT_EQ(20u, s.Layout(true, {}).size());           // show everything
    auto tight = s.Layout(false, {}, 0);
    EXPECT_EQ(3u, tight.size());                         // fold, difference, fold
    // A fold would hide a single line: shown instead.
    EXPECT_EQ(20u, s.Layout(false, {}, 9).size());
}

TEST(subtitle_compare_view, categories_follow_the_changed_field_groups_and_ignored_fields) {
    Fixture f;
    State s(f.cur, f.base, model::All, f.items);
    auto m = f.entry(s, Change::Modified), timed = f.text(s, "timed");
    auto gone = f.text(s, "gone"), added = f.text(s, "added");
    EXPECT_EQ(Category::Multiple, s.CategoryOf(m));            // text, actor and layer
    EXPECT_EQ(Category::Time, s.CategoryOf(timed));
    EXPECT_EQ(Category::Inserted, s.CategoryOf(gone));
    EXPECT_EQ(Category::Removed, s.CategoryOf(added));
    EXPECT_FALSE(s.CategoryOf(0));                              // unchanged
    // Ignoring fields narrows what counts: only the actor is left, or nothing.
    EXPECT_EQ(Category::StyleActor, s.CategoryOf(m, model::Text | model::Layer));
    EXPECT_EQ(model::Fields(model::Actor), s.Shown(m, model::Text | model::Layer));
    model::Fields time = CategoryFields(Category::Time);
    EXPECT_FALSE(s.CategoryOf(timed, time));
    EXPECT_FALSE(s.Visible(timed, {time, std::nullopt}));
    EXPECT_EQ(3u, s.VisibleDiffs({time, std::nullopt}).size());
    EXPECT_EQ(Category::Inserted, s.CategoryOf(gone, model::All)); // whole lines are never ignored

    auto only_time = s.VisibleDiffs({0, Category::Time});
    ASSERT_EQ(1u, only_time.size()); EXPECT_EQ(timed, only_time[0]);
    // The layout treats hidden differences like unchanged lines.
    Filter filter{0, Category::Time};
    size_t shown = 0, folded = 0;
    for (auto const& row : s.Layout(false, {}, 0, filter)) {
        if (row.IsFold()) { folded += row.hidden; continue; }
        if (s.Visible(row.entry, filter)) { EXPECT_EQ(timed, row.entry); ++shown; }
    }
    EXPECT_EQ(1u, shown);
    EXPECT_LE(2u, folded); // the other differences fold away (a lone line is shown, not folded)
    EXPECT_EQ(s.Entries().size(), s.Layout(true, {}, 0, {0, Category::Time}).size());
}

TEST(subtitle_compare_view, scoped_decisions_touch_only_the_fields_in_scope) {
    Fixture f;
    State s(f.cur, f.base, model::All, f.items);
    auto m = f.entry(s, Change::Modified), gone = f.text(s, "gone");
    EXPECT_EQ(1u, s.Changing({m}, Decision::Rejected, model::Text));
    ASSERT_TRUE(s.Decide({m}, Decision::Rejected, model::Text));
    EXPECT_EQ(Decision::Partial, s.GetDecision(m));
    EXPECT_EQ(Side::Left, s.Winner(m, model::Text)); EXPECT_EQ(Side::Right, s.Winner(m, model::Actor));
    ASSERT_TRUE(s.Decide({m}, Decision::Rejected, model::Actor)); // adds to the earlier choice
    EXPECT_EQ(Side::Left, s.Winner(m, model::Text)); EXPECT_EQ(Side::Left, s.Winner(m, model::Actor));
    EXPECT_EQ(Side::Right, s.Winner(m, model::Layer));
    ASSERT_TRUE(s.Decide({m}, Decision::Accepted, model::Text));
    EXPECT_EQ(Side::Right, s.Winner(m, model::Text)); EXPECT_EQ(Side::Left, s.Winner(m, model::Actor));
    EXPECT_EQ(0u, s.Changing({m}, Decision::Accepted, model::Text));
    EXPECT_FALSE(s.Decide({m}, Decision::Accepted, model::Text));
    // A scope covering every changed field is a plain decision.
    ASSERT_TRUE(s.Decide({m}, Decision::Rejected, model::All));
    EXPECT_EQ(Decision::Rejected, s.GetDecision(m));
    // Whole lines ignore the scope.
    ASSERT_TRUE(s.Decide({gone}, Decision::Rejected, model::Text));
    EXPECT_EQ(Decision::Rejected, s.GetDecision(gone));
    // Undo walks back one scoped step at a time.
    ASSERT_TRUE(s.Undo()); ASSERT_TRUE(s.Undo());
    EXPECT_EQ(Side::Right, s.Winner(m, model::Text)); EXPECT_EQ(Side::Left, s.Winner(m, model::Actor));
}

TEST(subtitle_compare_view, text_diff_marks_changed_tokens) {
    auto d = DiffText("Hello world", "Hello there world");
    EXPECT_TRUE(d.left.empty());
    ASSERT_EQ(1u, d.right.size());
    EXPECT_EQ(6u, d.right[0].begin); EXPECT_EQ(12u, d.right[0].end); // "there "

    d = DiffText("今天天气很好", "今天天气不好");      // CJK: by character
    ASSERT_EQ(1u, d.left.size()); ASSERT_EQ(1u, d.right.size());
    EXPECT_EQ(12u, d.left[0].begin); EXPECT_EQ(15u, d.left[0].end);
    EXPECT_EQ(12u, d.right[0].begin); EXPECT_EQ(15u, d.right[0].end);

    d = DiffText("{\\b1}abc\\Nxyz", "{\\b0}abc\\Nxyz");   // an override block is one token
    ASSERT_EQ(1u, d.left.size()); EXPECT_EQ(0u, d.left[0].begin); EXPECT_EQ(5u, d.left[0].end);

    d = DiffText("same", "same");
    EXPECT_TRUE(d.left.empty()); EXPECT_TRUE(d.right.empty());
    d = DiffText("", "new");
    ASSERT_EQ(1u, d.right.size()); EXPECT_EQ(3u, d.right[0].end);
}

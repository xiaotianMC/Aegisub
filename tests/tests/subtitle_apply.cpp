// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// C2 Apply Engine planner: pure planning over two independent models.
#include <main.h>
#include "../../src/subtitle_apply.h"
#include "../../src/subtitle_compare.h"
#include "../../src/ass_dialogue.h"
#include "../../src/ass_style.h"

namespace {
using namespace subtitle_apply;
namespace model = agi::ass::compare;
constexpr auto binding = "aegisub.enhanced.subtitle-binding";

AssDialogue& add(AssFile& f, std::string const& text, int start, int end) {
    auto line = new AssDialogue; line->Text = text; line->Start = start; line->End = end;
    f.Events.push_back(*line); return *line;
}
void style(AssFile& f, std::string const& name, double size = 48) {
    auto s = new AssStyle; s->name = name; s->fontsize = size; s->UpdateData(); f.Styles.push_back(*s);
}
void extra(AssFile& f, AssDialogue& line, uint32_t id, std::string key, std::string value) {
    f.Extradata.push_back({id, std::move(key), std::move(value)});
    f.next_extradata_id = std::max(f.next_extradata_id, id + 1);
    auto ids = line.ExtradataIds.get(); ids.push_back(id); line.ExtradataIds = ids;
}
std::vector<model::Item> compare(AssFile const& baseline, AssFile const& current, model::Fields fields = model::All) {
    return model::Compare(subtitle_compare::ProjectDocument(baseline, fields).document,
        subtitle_compare::ProjectDocument(current).document);
}
size_t find(std::vector<model::Item> const& items, model::Kind kind, size_t n = 0) {
    for (size_t i = 0; i < items.size(); ++i) if (items[i].kind == kind && n-- == 0) return i;
    ADD_FAILURE() << "no such item"; return 0;
}
std::vector<std::string> texts(Plan const& plan) {
    std::vector<std::string> out;
    for (auto e : plan.events) out.push_back(e->Text.get());
    return out;
}
bool refused(Report const& r, Problem p, std::string const& detail = {}) {
    for (auto const& x : r.refusals) if (x.problem == p && (detail.empty() || x.detail == detail)) return true;
    return false;
}
}

TEST(subtitle_apply, modify_add_delete_keep_ids_order_and_inputs) {
    AssFile cur, base;
    auto& a = add(cur, "a", 0, 1000); auto& b = add(cur, "b", 1000, 2000); add(cur, "c", 2000, 3000);
    add(base, "a2", 0, 1000); base.Events.back().Actor = "actor";
    add(base, "b", 1000, 2000); add(base, "new", 2500, 2600);
    auto items = compare(base, cur);
    auto mod = find(items, model::Kind::Modified), added = find(items, model::Kind::Added), del = find(items, model::Kind::Deleted);
    ASSERT_EQ(model::Text | model::Actor, items[mod].changed);
    auto a_id = a.Id, b_id = b.Id;
    // Only the Text of the modified row; the Actor change is not chosen.
    auto plan = Prepare(cur, base, model::All, items, {{mod, model::Text}, {added}, {del}});
    ASSERT_TRUE(plan.report.Ok());
    EXPECT_EQ(1u, plan.report.modified); EXPECT_EQ(1u, plan.report.added); EXPECT_EQ(1u, plan.report.deleted);
    EXPECT_EQ((std::vector<std::string>{"a2", "b", "new"}), texts(plan));
    EXPECT_EQ(a_id, plan.events[0]->Id); EXPECT_EQ("", plan.events[0]->Actor.get());
    EXPECT_EQ(&b, plan.events[1]); EXPECT_EQ(b_id, plan.events[1]->Id); // untouched row is the current node itself
    EXPECT_NE(a_id, plan.events[2]->Id); EXPECT_NE(b_id, plan.events[2]->Id);
    EXPECT_EQ(2500, int(plan.events[2]->Start));
    EXPECT_EQ((std::vector<int>{a_id, plan.events[2]->Id}), plan.applied_ids);
    EXPECT_EQ(AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL, plan.commit_type);
    // Inputs untouched.
    EXPECT_EQ("a", a.Text.get()); EXPECT_EQ(3u, std::distance(cur.Events.begin(), cur.Events.end()));
}

TEST(subtitle_apply, additions_use_the_comparison_boundary_in_baseline_order) {
    AssFile cur, base;
    add(cur, "x", 0, 100); add(cur, "y", 5000, 5100);
    add(base, "x", 0, 100); add(base, "n1", 9000, 9100); add(base, "n2", 1, 2); add(base, "y", 5000, 5100);
    auto items = compare(base, cur);
    auto plan = Prepare(cur, base, model::All, items, {{find(items, model::Kind::Added, 1)}, {find(items, model::Kind::Added, 0)}});
    ASSERT_TRUE(plan.report.Ok());
    // Not sorted by time: placed at the boundary, baseline order within it.
    EXPECT_EQ((std::vector<std::string>{"x", "n1", "n2", "y"}), texts(plan));
}

TEST(subtitle_apply, invalid_choices_are_refused_and_produce_no_result) {
    AssFile cur, base;
    add(cur, "same", 0, 100); add(cur, "old", 200, 300);
    add(base, "same", 0, 100); add(base, "new", 200, 300);
    auto items = compare(base, cur);
    auto unchanged = find(items, model::Kind::Unchanged), mod = find(items, model::Kind::Modified);
    auto plan = Prepare(cur, base, model::All, items, {{unchanged}, {mod, model::Actor}, {99}, {mod}, {mod}});
    EXPECT_FALSE(plan.report.Ok());
    EXPECT_TRUE(refused(plan.report, Problem::NothingToApply));
    EXPECT_TRUE(refused(plan.report, Problem::UnknownItem));
    EXPECT_TRUE(refused(plan.report, Problem::DuplicateChoice));
    EXPECT_TRUE(plan.events.empty()); EXPECT_TRUE(plan.pool.empty()); EXPECT_FALSE(plan.Modifies());
    // Items that do not describe the documents are a programming error.
    auto wrong = items; wrong[0].current = 50;
    EXPECT_THROW(Prepare(cur, base, model::All, wrong, {}), std::invalid_argument);
}

TEST(subtitle_apply, unsupported_fields_keep_current_or_default_values) {
    AssFile cur, base;
    auto& c = add(cur, "old", 0, 100); c.Actor = "kept"; c.Layer = 3;
    auto& m = add(base, "new", 0, 100); m.Actor = "ignored"; m.Layer = 9;
    auto& n = add(base, "added", 500, 600); n.Actor = "ignored"; n.Style = "Fancy";
    model::Fields srt = model::Text | model::Start | model::End;
    auto items = compare(base, cur, srt);
    auto mod = find(items, model::Kind::Modified);
    EXPECT_EQ(model::Text, items[mod].changed);
    auto plan = Prepare(cur, base, srt, items, {{mod}, {find(items, model::Kind::Added)}});
    ASSERT_TRUE(plan.report.Ok());
    EXPECT_EQ("new", plan.events[0]->Text.get()); EXPECT_EQ("kept", plan.events[0]->Actor.get()); EXPECT_EQ(3, plan.events[0]->Layer);
    EXPECT_EQ("added", plan.events[1]->Text.get()); EXPECT_EQ("", plan.events[1]->Actor.get());
    EXPECT_EQ("Default", plan.events[1]->Style.get()); // Style is not a source field
    EXPECT_TRUE(plan.report.imported_styles.empty());
}

TEST(subtitle_apply, partial_binding_groups_are_refused_whole_groups_are_applied) {
    AssFile cur, base;
    add(cur, "keep", 0, 100);
    add(base, "keep", 0, 100);
    auto& g1 = add(base, "g1", 1000, 1100); auto& g2 = add(base, "g2", 1100, 1200);
    extra(base, g1, 4, binding, "T"); extra(base, g2, 5, binding, "T");
    auto items = compare(base, cur);
    auto a0 = find(items, model::Kind::Added, 0), a1 = find(items, model::Kind::Added, 1);
    auto partial = Prepare(cur, base, model::All, items, {{a0}});
    EXPECT_TRUE(refused(partial.report, Problem::PartialBinding, "T"));
    EXPECT_TRUE(partial.events.empty());
    auto whole = Prepare(cur, base, model::All, items, {{a0}, {a1}});
    ASSERT_TRUE(whole.report.Ok());
    // Binding entries are remapped like any other extradata, one entry per value.
    EXPECT_EQ(1u, whole.extradata_additions.size());
    EXPECT_EQ(binding, whole.extradata_additions[0].key);
    EXPECT_EQ(whole.events[1]->ExtradataIds.get(), whole.events[2]->ExtradataIds.get());
    EXPECT_TRUE(whole.report.imported_keys.empty()); // the binding key is known
    EXPECT_TRUE(whole.commit_type & AssFile::COMMIT_EXTRADATA);
}

TEST(subtitle_apply, deleting_part_of_a_current_group_is_refused) {
    AssFile cur, base;
    auto& g1 = add(cur, "g1", 0, 100); auto& g2 = add(cur, "g2", 100, 200);
    extra(cur, g1, 1, binding, "T"); extra(cur, g2, 2, binding, "T");
    auto items = compare(base, cur);
    auto d0 = find(items, model::Kind::Deleted, 0), d1 = find(items, model::Kind::Deleted, 1);
    EXPECT_TRUE(refused(Prepare(cur, base, model::All, items, {{d0}}).report, Problem::PartialBinding, "T"));
    auto all = Prepare(cur, base, model::All, items, {{d0}, {d1}});
    ASSERT_TRUE(all.report.Ok()); EXPECT_TRUE(all.events.empty()); EXPECT_EQ(2u, all.report.deleted);
}

TEST(subtitle_apply, styles_import_missing_definitions_and_refuse_conflicts) {
    AssFile cur, base;
    style(cur, "Default"); style(base, "Default"); style(base, "Imported", 70); style(base, "Clash", 10); style(cur, "clash", 20);
    add(cur, "x", 0, 100);
    add(base, "x", 0, 100);
    auto& imp = add(base, "{\\rImported}i", 1000, 1100);
    auto& missing = add(base, "m", 2000, 2100); missing.Style = "Nowhere";
    auto& clash = add(base, "c", 3000, 3100); clash.Style = "Clash";
    (void)imp;
    auto items = compare(base, cur);
    auto ai = find(items, model::Kind::Added, 0), am = find(items, model::Kind::Added, 1), ac = find(items, model::Kind::Added, 2);
    auto ok = Prepare(cur, base, model::All, items, {{ai}, {am}});
    ASSERT_TRUE(ok.report.Ok());
    EXPECT_EQ((std::vector<std::string>{"Imported"}), ok.report.imported_styles);
    EXPECT_EQ((std::vector<std::string>{"Nowhere"}), ok.report.missing_styles);
    ASSERT_EQ(3u, ok.styles.size()); EXPECT_EQ(70, ok.styles.back()->fontsize);
    EXPECT_TRUE(ok.commit_type & AssFile::COMMIT_STYLES);
    EXPECT_TRUE(refused(Prepare(cur, base, model::All, items, {{ac}}).report, Problem::StyleConflict, "Clash"));
}

TEST(subtitle_apply, text_only_change_does_not_depend_on_the_rows_style) {
    AssFile cur, base;
    style(cur, "S", 10); style(base, "S", 99); // conflicting definitions
    auto& c = add(cur, "old", 0, 100); c.Style = "S";
    auto& b = add(base, "new", 0, 100); b.Style = "S";
    auto items = compare(base, cur);
    auto plan = Prepare(cur, base, model::All, items, {{find(items, model::Kind::Modified)}});
    ASSERT_TRUE(plan.report.Ok()); // the style is not taken from the baseline
    EXPECT_EQ("new", plan.events[0]->Text.get()); EXPECT_EQ(&cur.Styles.front(), plan.styles[0]);
}

TEST(subtitle_apply, unknown_extradata_is_imported_with_remapped_ids) {
    AssFile cur, base;
    auto& c = add(cur, "x", 0, 100); extra(cur, c, 7, "shared", "v");
    auto& b = add(base, "x", 0, 100); extra(base, b, 1, "shared", "v"); extra(base, b, 2, "opaque", "data");
    auto& n = add(base, "n", 500, 600); extra(base, n, 3, "opaque", "other");
    auto items = compare(base, cur);
    auto mod = find(items, model::Kind::Modified);
    EXPECT_EQ(model::Extra, items[mod].changed);
    auto plan = Prepare(cur, base, model::All, items, {{mod}, {find(items, model::Kind::Added)}});
    ASSERT_TRUE(plan.report.Ok());
    EXPECT_EQ((std::vector<std::string>{"opaque", "shared"}), plan.report.imported_keys);
    ASSERT_EQ(2u, plan.extradata_additions.size()); // "shared"/"v" reuses ID 7
    EXPECT_EQ(8u, plan.extradata_additions[0].id); EXPECT_EQ(9u, plan.extradata_additions[1].id);
    EXPECT_EQ(10u, plan.next_extradata_id);
    EXPECT_EQ((std::vector<uint32_t>{7, 8}), plan.events[0]->ExtradataIds.get());
    EXPECT_EQ((std::vector<uint32_t>{9}), plan.events[1]->ExtradataIds.get());
    EXPECT_EQ((std::vector<uint32_t>{7}), c.ExtradataIds.get());
}

TEST(subtitle_apply, unresolvable_extradata_is_refused) {
    AssFile cur, base;
    add(cur, "x", 0, 100);
    auto& b = add(base, "y", 0, 100); b.ExtradataIds = std::vector<uint32_t>{42};
    std::vector<model::Item> items{{0, 0, model::Kind::Modified, model::Text}};
    auto plan = Prepare(cur, base, model::All, items, {{0}});
    EXPECT_TRUE(refused(plan.report, Problem::InvalidExtradata));
}

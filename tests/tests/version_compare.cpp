// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <main.h>
#include <libaegisub/ass/version_compare.h>
#include <libaegisub/ass/time.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <random>

using namespace agi::ass::compare;
namespace {
Row row(std::string text, int time) {
    Row r; r.text = std::move(text); r.start_ms = time; r.end_ms = time + 900; r.style = "Default"; return r;
}
Document document(std::initializer_list<Row> rows) { return {rows, All}; }
size_t count(std::vector<Item> const& result, Kind kind) {
    return std::count_if(result.begin(), result.end(), [=](auto const& i) { return i.kind == kind; });
}
void invariants(Document const& b, Document const& c, std::vector<Item> const& result) {
    std::vector<size_t> bs, cs;
    for (auto const& item : result) {
        EXPECT_LE(item.insertion_boundary, c.rows.size());
        if (item.baseline) { EXPECT_LT(*item.baseline, b.rows.size()); bs.push_back(*item.baseline); }
        if (item.current) { EXPECT_LT(*item.current, c.rows.size()); cs.push_back(*item.current); }
        if (item.kind == Kind::Modified || item.kind == Kind::Unchanged) {
            ASSERT_TRUE(item.baseline); ASSERT_TRUE(item.current);
            EXPECT_EQ(item.changed, ChangedFields(b.rows[*item.baseline], c.rows[*item.current], b.fields & c.fields));
            EXPECT_NE(MatchReason::None, item.reason);
        }
    }
    EXPECT_TRUE(std::is_sorted(bs.begin(), bs.end())); EXPECT_TRUE(std::is_sorted(cs.begin(), cs.end()));
    EXPECT_EQ(bs.size(), b.rows.size()); EXPECT_EQ(cs.size(), c.rows.size());
    EXPECT_EQ(std::adjacent_find(bs.begin(), bs.end()), bs.end());
    EXPECT_EQ(std::adjacent_find(cs.begin(), cs.end()), cs.end());
}
}

TEST(version_compare, identical_with_duplicates_and_empty_documents) {
    auto b = document({row("重复", 0), row("重复", 0), row("x", 1000), row("重复", 0)});
    auto r = Compare(b, b); EXPECT_EQ(4u, count(r, Kind::Unchanged)); invariants(b, b, r);
    EXPECT_TRUE(Compare({}, {}).empty());
}
TEST(version_compare, middle_insert_delete_and_original_insertion_boundary) {
    auto c = document({row("a", 0), row("c", 2000)});
    auto b = document({row("a", 0), row("b", 1000), row("b2", 1500), row("c", 2000)});
    auto r = Compare(b, c); EXPECT_EQ(2u, count(r, Kind::Added)); EXPECT_EQ(2u, count(r, Kind::Unchanged));
    EXPECT_EQ(1u, r[1].insertion_boundary); EXPECT_EQ(1u, r[2].insertion_boundary); invariants(b, c, r);
    EXPECT_EQ(2u, count(Compare(c, b), Kind::Deleted));
}
TEST(version_compare, text_time_and_noncontiguous_modifications) {
    auto c = document({row("a", 0), row("b", 1000), row("c", 2000), row("d", 3000)});
    auto b = c; b.rows[0].text = "改"; b.rows[2].start_ms += 1;
    auto r = Compare(b, c); EXPECT_EQ(2u, count(r, Kind::Modified));
    EXPECT_EQ(MatchReason::UniqueTime, r[0].reason); EXPECT_EQ(Text, r[0].changed);
    EXPECT_EQ(MatchReason::UniqueText, r[2].reason); EXPECT_EQ(Start, r[2].changed); invariants(b, c, r);
}
TEST(version_compare, one_millisecond_is_exact_even_when_ass_rounding_is_equal) {
    EXPECT_EQ(1001, agi::Time(1001).GetMilliseconds());
    EXPECT_EQ(int(agi::Time(1000)), int(agi::Time(1001)));
    auto b = document({row("台词", 1001)}), c = document({row("台词", 1000)});
    EXPECT_EQ(Start | End, Compare(b, c)[0].changed);
}
TEST(version_compare, preserves_chinese_spaces_and_ass_tag_changes) {
    for (auto const& texts : {std::pair<std::string, std::string>{" 中 文  ", " 中文 "},
         {"{\\pos(1,2)\\t(0,100,\\clip(1,2,3,4))}台词", "{\\pos(2,2)\\t(0,100,\\clip(1,2,3,4))}台词"},
         {"{\\k10\\kf20\\ko30}字", "{\\k11\\kf20\\ko30}字"}}) {
        auto b = document({row(texts.first, 0)}), c = document({row(texts.second, 0)});
        auto r = Compare(b, c); ASSERT_EQ(1u, r.size()); EXPECT_EQ(Text, r[0].changed);
    }
}
TEST(version_compare, compares_every_field_and_honors_capability_mask) {
    Row a = row("a", 0), b = row("b", 1);
    b.style = "other"; b.actor = "actor"; b.effect = "effect"; b.layer = 3;
    b.margins = {1, 2, 3}; b.comment = true; b.extra = {{"k", "v"}};
    EXPECT_EQ(All, ChangedFields(a, b, All));
    EXPECT_EQ(Text | Start | End, ChangedFields(a, b, Capabilities(Format::Srt)));
    EXPECT_EQ(Text | Actor | Comment, Capabilities(Format::Txt));
    EXPECT_EQ(0u, Capabilities(Format::Unknown));
    auto B = document({a}), C = document({a}); B.fields = Capabilities(Format::Srt);
    C.rows[0].style = "edited-in-memory"; C.rows[0].extra = {{"k", "v"}};
    EXPECT_EQ(Kind::Unchanged, Compare(B, C)[0].kind);
    B.fields = 0; EXPECT_THROW(Compare(B, C), std::invalid_argument);
}
TEST(version_compare, simultaneous_text_and_time_change_does_not_force_pair) {
    auto b = document({row("new", 0)}), c = document({row("old", 1000)});
    auto r = Compare(b, c); EXPECT_EQ(1u, count(r, Kind::Added)); EXPECT_EQ(1u, count(r, Kind::Deleted));
    for (auto const& i : r) EXPECT_EQ(Ambiguity::NoReliableMatch, i.ambiguity);
}
TEST(version_compare, repeated_text_can_use_unique_time_but_duplicate_evidence_is_retained) {
    auto c = document({row("same", 0), row("same", 1000)});
    auto b = c; b.rows[0].text = "edited";
    EXPECT_EQ(1u, count(Compare(b, c), Kind::Modified));
    c = document({row("repeat", 0), row("repeat", 0)});
    b = document({row("different", 0)});
    auto r = Compare(b, c); EXPECT_EQ(0u, count(r, Kind::Modified));
    EXPECT_TRUE(std::any_of(r.begin(), r.end(), [](Item const& i) { return i.ambiguity == Ambiguity::Duplicate; }));
}
TEST(version_compare, same_time_uses_unique_text_without_forcing_duplicate_time_match) {
    auto c = document({row("a", 0), row("b", 0)}), b = c;
    b.rows[0].layer = 2; b.rows[1].layer = 3;
    auto r = Compare(b, c); EXPECT_EQ(2u, count(r, Kind::Modified));
    for (auto const& i : r) EXPECT_EQ(MatchReason::UniqueText, i.reason);
}
TEST(version_compare, crossing_matches_and_reordering_are_additions_and_deletions) {
    auto c = document({row("a", 0), row("b", 1000), row("c", 2000)});
    auto b = document({c.rows[2], c.rows[1], c.rows[0]});
    auto r = Compare(b, c); EXPECT_EQ(0u, count(r, Kind::Modified)); EXPECT_EQ(3u, count(r, Kind::Added));
    for (auto const& i : r) EXPECT_EQ(Ambiguity::Crossing, i.ambiguity);
    invariants(b, c, r);
}
TEST(version_compare, repeated_exact_anchors_between_modified_endpoints_remain_unchanged) {
    auto c = document({row("old-left",0), row("duplicate",1000), row("duplicate",1000), row("old-right",2000)});
    auto b = c; b.rows.front().text = "new-left"; b.rows.back().text = "new-right";
    auto r = Compare(b,c);
    EXPECT_EQ(2u,count(r,Kind::Modified)); EXPECT_EQ(2u,count(r,Kind::Unchanged));
    EXPECT_EQ(0u,count(r,Kind::Added)); EXPECT_EQ(0u,count(r,Kind::Deleted)); invariants(b,c,r);
    b = c; ++b.rows.front().start_ms; ++b.rows.back().start_ms;
    r = Compare(b,c); EXPECT_EQ(2u,count(r,Kind::Modified)); EXPECT_EQ(2u,count(r,Kind::Unchanged));
    invariants(b,c,r);
}
TEST(version_compare, conflicting_time_and_text_evidence_is_not_silently_resolved) {
    auto c = document({row("a", 0), row("b", 1000)});
    auto b = document({row("b", 0), row("a", 1000)});
    auto r = Compare(b, c); EXPECT_EQ(0u, count(r, Kind::Modified));
    EXPECT_TRUE(std::all_of(r.begin(), r.end(), [](Item const& i) { return i.ambiguity == Ambiguity::ConflictingEvidence; }));
}
TEST(version_compare, completely_different_files_and_empty_sides) {
    auto b = document({row("x", 0), row("y", 1000)}), c = document({row("a", 9000), row("b", 10000)});
    auto r = Compare(b, c); EXPECT_EQ(2u, count(r, Kind::Added)); EXPECT_EQ(2u, count(r, Kind::Deleted)); invariants(b, c, r);
    EXPECT_EQ(2u, count(Compare(b, {}), Kind::Added)); EXPECT_EQ(2u, count(Compare({}, c), Kind::Deleted));
}
TEST(version_compare, extradata_identity_is_key_value_not_id_or_entry_order) {
    auto a = ResolveExtras({{2, "k", "v"}, {9, "other", "中文 "}}, {{9, 2}});
    auto b = ResolveExtras({{200, "other", "中文 "}, {3, "k", "v"}}, {{3, 200}});
    ASSERT_TRUE(a.valid); ASSERT_TRUE(b.valid); EXPECT_EQ(a.rows, b.rows);
    auto A = document({row("a", 0)}), B = A; A.rows[0].extra = a.rows[0]; B.rows[0].extra = b.rows[0];
    EXPECT_EQ(Kind::Unchanged, Compare(A, B)[0].kind);
    B.rows[0].extra["k"] = "changed"; EXPECT_EQ(Extra, Compare(A, B)[0].changed);
    EXPECT_EQ(ExtraIssueKind::UnknownDependency, a.issues[0].kind);
}
TEST(version_compare, extradata_rejects_missing_duplicate_ids_and_duplicate_keys) {
    auto r = ResolveExtras({{4, "k", "v"}}, {{3}});
    EXPECT_FALSE(r.valid); EXPECT_TRUE(r.rows[0].empty()); EXPECT_EQ(ExtraIssueKind::MissingId, r.issues[0].kind);
    EXPECT_FALSE(ResolveExtras({{4, "k", "v"}, {4, "x", "y"}}, {{4}}).valid);
    EXPECT_FALSE(ResolveExtras({{1, "k", "v"}, {2, "k", "different"}}, {{1, 2}}).valid);
    auto binding = ResolveExtras({{7, "aegisub.enhanced.subtitle-binding", "0:token"}}, {{7}});
    EXPECT_TRUE(binding.valid); EXPECT_EQ(ExtraIssueKind::BindingPolicyRequired, binding.issues[0].kind);
}
TEST(version_compare, extradata_mapping_is_pure_deduplicated_and_collision_safe) {
    std::vector<ExtraEntry> source{{1, "k", "v"}, {2, "new", "a"}, {3, "new", "a"}};
    std::vector<ExtraEntry> dest{{10, "k", "v"}, {1, "unrelated", "z"}};
    auto r = PlanExtraMapping(source, dest, 0);
    EXPECT_EQ(10u, r.ids.at(1)); EXPECT_EQ(11u, r.ids.at(2)); EXPECT_EQ(r.ids.at(2), r.ids.at(3));
    EXPECT_EQ(1u, r.additions.size()); EXPECT_EQ(12u, r.next_id); EXPECT_EQ(2u, dest.size());
    EXPECT_THROW(PlanExtraMapping({{1,"k","v"},{1,"x","y"}}, {}, 0), std::invalid_argument);
    EXPECT_THROW(PlanExtraMapping({{1,"k","v"}}, {}, std::numeric_limits<uint32_t>::max()), std::overflow_error);
}
TEST(version_compare, cancellation_never_returns_partial_results) {
    Document b;
    for (int i = 0; i < 1000; ++i) b.rows.push_back(row(std::to_string(i), i * 1000));
    auto c = b; for (auto& r : c.rows) r.text += "!";
    int calls = 0; EXPECT_THROW(Compare(b, c, [&] { return ++calls > 100; }), Cancelled);
    EXPECT_EQ("0", b.rows[0].text); EXPECT_EQ("0!", c.rows[0].text);
    EXPECT_THROW(ResolveExtras({}, {{}}, [] { return true; }), Cancelled);
    EXPECT_THROW(PlanExtraMapping({}, {{1,"k","v"}}, 0, [] { return true; }), Cancelled);
}
TEST(version_compare, randomized_coverage_order_and_identity_invariants) {
    std::mt19937 random(27);
    for (int iteration = 0; iteration < 150; ++iteration) {
        Document b, c;
        for (int i = 0; i < 35; ++i) {
            b.rows.push_back(row(std::to_string(random() % 8), (random() % 8) * 1000));
            c.rows.push_back(row(std::to_string(random() % 8), (random() % 8) * 1000));
        }
        invariants(b, c, Compare(b, c));
    }
}
TEST(version_compare, performance_1000_5000_10000) {
    for (int n : {1000, 5000, 10000}) for (int scenario = 0; scenario < 5; ++scenario) {
        Document b;
        for (int i = 0; i < n; ++i) b.rows.push_back(row(std::to_string(i), i * 1000));
        auto c = b;
        if (scenario == 0) c.rows[n/2].text += " change";
        if (scenario == 1) std::reverse(c.rows.begin(), c.rows.end());
        if (scenario == 2) for (auto& r : c.rows) { r.text += "x"; r.start_ms += 1; r.end_ms += 1; }
        if (scenario == 3) for (int i = 0; i < n; ++i) b.rows[i] = c.rows[i] = row("duplicate", 0);
        if (scenario == 4) for (auto& r : c.rows) r.text += " changed";
        auto start = std::chrono::steady_clock::now(); auto result = Compare(b, c);
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "COMPARE_BENCH rows=" << n << " scenario=" << scenario << " ms=" << ms << '\n';
        invariants(b, c, result);
    }
}

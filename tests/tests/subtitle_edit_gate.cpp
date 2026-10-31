// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <main.h>
#include <libaegisub/subtitle_edit_gate.h>
#include <libaegisub/id_allocator.h>
#include <algorithm>
#include <future>
#include <thread>

TEST(subtitle_edit_gate, nested_writers_keep_comparison_excluded_until_last_release) {
    agi::SubtitleEditGate gate;
    auto initial = gate.Revision();
    auto a = gate.TryBeginWrite(); ASSERT_TRUE(a); EXPECT_TRUE(gate.Authorizes(*a));
    auto b = gate.TryBeginWrite(); ASSERT_TRUE(b);
    EXPECT_GT(gate.Revision().generation, initial.generation);
    EXPECT_FALSE(gate.TryBeginComparison()); a.reset();
    EXPECT_TRUE(b->Valid()); EXPECT_FALSE(gate.TryBeginComparison()); b.reset();
    EXPECT_TRUE(gate.TryBeginComparison());
}
TEST(subtitle_edit_gate, comparison_cancellation_and_exception_restore_writes) {
    agi::SubtitleEditGate gate;
    try {
        auto comparison = gate.TryBeginComparison(); ASSERT_TRUE(comparison);
        EXPECT_TRUE(gate.Authorizes(*comparison));
        EXPECT_FALSE(gate.TryBeginWrite()); EXPECT_FALSE(gate.TryBeginComparison());
        throw std::runtime_error("cancel");
    } catch (std::runtime_error const&) { }
    EXPECT_TRUE(gate.TryBeginWrite());
    try { auto write = gate.TryBeginWrite(); throw std::runtime_error("task failed"); }
    catch (std::runtime_error const&) { }
    EXPECT_TRUE(gate.TryBeginComparison());
}
TEST(subtitle_edit_gate, projects_and_tokens_are_independent) {
    agi::SubtitleEditGate a, b;
    EXPECT_NE(a.Revision().document, b.Revision().document);
    auto write = a.TryBeginWrite(); ASSERT_TRUE(write);
    EXPECT_FALSE(b.Authorizes(*write)); EXPECT_TRUE(b.TryBeginComparison());
    write.reset(); auto comparison = a.TryBeginComparison(); ASSERT_TRUE(comparison);
    EXPECT_FALSE(b.Authorizes(*comparison)); EXPECT_TRUE(b.TryBeginWrite());
}
TEST(subtitle_edit_gate, registered_background_task_holds_lease_for_whole_lifetime) {
    agi::SubtitleEditGate gate;
    auto write = gate.TryBeginWrite(); ASSERT_TRUE(write);
    std::promise<void> started, finish; auto finished = finish.get_future();
    std::thread task([lease=std::move(*write), &started, wait=std::move(finished)]() mutable {
        started.set_value(); wait.wait(); EXPECT_TRUE(lease.Valid());
    });
    started.get_future().wait(); EXPECT_FALSE(gate.TryBeginComparison());
    finish.set_value(); task.join(); EXPECT_TRUE(gate.TryBeginComparison());
}
TEST(subtitle_edit_gate, revisions_do_not_reuse_cancelled_or_previous_states) {
    agi::SubtitleEditGate gate; auto initial = gate.Revision();
    auto write = gate.TryBeginWrite(); write.reset(); auto after = gate.Revision();
    EXPECT_GT(after.generation, initial.generation); EXPECT_FALSE(gate.IsCurrent(initial));
    auto comparison = gate.TryBeginComparison(); ASSERT_TRUE(comparison);
    auto next = comparison->AdvanceRevision();
    EXPECT_GT(next.generation, after.generation); EXPECT_FALSE(gate.IsCurrent(after));
    comparison.reset(); EXPECT_EQ(next, gate.Revision());
}
TEST(subtitle_edit_gate, owner_close_invalidates_outstanding_and_moved_tokens) {
    agi::SubtitleEditGate gate; auto write = gate.TryBeginWrite(); ASSERT_TRUE(write);
    auto moved = std::move(*write); EXPECT_FALSE(write->Valid()); EXPECT_TRUE(moved.Valid());
    auto stamp = gate.Revision(); gate.Close(); EXPECT_FALSE(moved.Valid());
    EXPECT_FALSE(gate.IsCurrent(stamp)); EXPECT_FALSE(gate.TryBeginWrite()); EXPECT_FALSE(gate.TryBeginComparison());
}
TEST(subtitle_edit_gate, in_flight_operation_pins_registration_but_cannot_extend_new_authority) {
    agi::SubtitleEditGate gate; auto lease=gate.TryBeginWrite();
    auto token=lease->Capture(); auto operation=token.Hold();
    lease.reset(); EXPECT_FALSE(token.Valid());
    EXPECT_THROW(token.Hold(),std::logic_error);
    EXPECT_FALSE(gate.TryBeginComparison());
    operation.reset(); EXPECT_TRUE(gate.TryBeginComparison());
}
TEST(subtitle_id_allocator, bounded_exhaustion_never_wraps_or_resets) {
    agi::IdAllocator ids(2); EXPECT_EQ(1, ids.Next()); EXPECT_EQ(2, ids.Next());
    EXPECT_THROW(ids.Next(), std::overflow_error);
    EXPECT_THROW(ids.Next(), std::overflow_error);
    EXPECT_THROW(agi::IdAllocator(0), std::invalid_argument);
}
TEST(subtitle_id_allocator, multithreaded_ids_are_unique_and_positive) {
    agi::IdAllocator ids;
    std::vector<std::vector<int>> values(8); std::vector<std::thread> threads;
    for (auto& output : values) threads.emplace_back([&ids, &output] {
        for (int i=0;i<2000;++i) output.push_back(ids.Next());
    });
    for (auto& thread : threads) thread.join();
    std::vector<int> all;
    for (auto const& output : values) all.insert(all.end(),output.begin(),output.end());
    std::sort(all.begin(),all.end()); ASSERT_EQ(16000u,all.size());
    EXPECT_EQ(1,all.front()); EXPECT_EQ(16000,all.back());
    EXPECT_EQ(all.end(),std::adjacent_find(all.begin(),all.end()));
}

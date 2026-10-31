// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <main.h>
#include "../../src/selection_controller.h"
#include "../../src/ass_dialogue.h"
#include "../../src/ass_file.h"
#include "../../src/ass_attachment.h"
#include "../../src/ass_info.h"
#include "../../src/ass_style.h"

static_assert(std::is_same_v<decltype(std::declval<SelectionController>().ReadActiveLine()), AssDialogue const*>);
namespace {
struct subtitle_selection : testing::Test {
    AssFile file;
    agi::SubtitleEditGate gate;
    SelectionController selection{file, gate};
    AssDialogue *a = new AssDialogue, *b = new AssDialogue;
    void SetUp() override {
        a->Row = 0; b->Row = 1; file.Events.push_back(*a); file.Events.push_back(*b);
        file.Extradata = {{0, "aegisub.enhanced.subtitle-binding", "0:002"}};
        a->ExtradataIds = std::vector<uint32_t>{0}; b->ExtradataIds = std::vector<uint32_t>{0};
        selection.SetSelectionAndActive({a}, a, false);
    }
};
TEST_F(subtitle_selection, silent_install_does_not_expand_bindings_or_notify) {
    int active = 0, selected = 0;
    auto active_connection = selection.AddActiveLineListener([&](AssDialogue*) { ++active; });
    auto selected_connection = selection.AddSelectionListener([&] { ++selected; });
    auto session = gate.TryBeginComparison(); ASSERT_TRUE(session);
    auto prepared = selection.PrepareSilent(*session, {b->Id}, b->Id);
    EXPECT_EQ(a, selection.ReadActiveLine());
    selection.InstallSilent(*session, std::move(prepared));
    EXPECT_EQ(b, selection.ReadActiveLine()); EXPECT_EQ(1u, selection.ReadSelectedLines().size());
    EXPECT_EQ(1, file.Properties.active_row); EXPECT_EQ(0, active); EXPECT_EQ(0, selected);
    EXPECT_THROW(selection.InstallSilent(*session, std::move(prepared)), std::logic_error);
    session.reset(); selection.SetSelectedSet({a});
    EXPECT_EQ(2u, selection.GetSelectedSet().size()); // Ordinary binding behavior preserved.
}
TEST_F(subtitle_selection, preparation_failure_or_stale_install_keeps_original_selection) {
    auto session = gate.TryBeginComparison(); ASSERT_TRUE(session);
    EXPECT_THROW(selection.PrepareSilent(*session, {b->Id, -1}, b->Id), std::out_of_range);
    EXPECT_EQ(a, selection.ReadActiveLine()); EXPECT_EQ((Selection{a}), selection.GetSelectedSet());
    auto prepared = selection.PrepareSilent(*session, {b->Id}, b->Id);
    session->AdvanceRevision();
    EXPECT_THROW(selection.InstallSilent(*session, std::move(prepared)), std::logic_error);
    EXPECT_EQ(a, selection.ReadActiveLine()); EXPECT_EQ((Selection{a}), selection.GetSelectedSet());
}
TEST_F(subtitle_selection, foreign_sessions_moved_preparations_and_empty_state) {
    agi::SubtitleEditGate other; auto foreign = other.TryBeginComparison();
    EXPECT_THROW(selection.PrepareSilent(*foreign, {}, {}), std::logic_error);
    auto session = gate.TryBeginComparison(); ASSERT_TRUE(session);
    auto original = selection.PrepareSilent(*session, {}, {}); auto moved = std::move(original);
    EXPECT_THROW(selection.InstallSilent(*session, std::move(original)), std::logic_error);
    EXPECT_THROW(selection.InstallSilent(*foreign, std::move(moved)), std::logic_error);
    EXPECT_EQ(a, selection.ReadActiveLine());
    selection.InstallSilent(*session, std::move(moved));
    EXPECT_EQ(nullptr, selection.ReadActiveLine()); EXPECT_TRUE(selection.ReadSelectedLines().empty());
}
TEST_F(subtitle_selection, preparation_rejects_duplicate_model_identity_before_changing_selection) {
    b->Id = a->Id; auto session = gate.TryBeginComparison();
    EXPECT_THROW(selection.PrepareSilent(*session, {a->Id}, a->Id), std::invalid_argument);
    EXPECT_EQ(a, selection.ReadActiveLine());
}
TEST_F(subtitle_selection, stale_observers_cannot_read_rebound_selection) {
    auto token = gate.Capture(); EXPECT_EQ(a, selection.ReadActiveLine(token));
    auto session = gate.TryBeginComparison(); session->AdvanceRevision();
    auto prepared = selection.PrepareSilent(*session, {b->Id}, b->Id);
    selection.InstallSilent(*session, std::move(prepared));
    EXPECT_THROW(selection.ReadActiveLine(token), std::logic_error);
    EXPECT_THROW(selection.ReadSelectedLines(token), std::logic_error);
    EXPECT_EQ(b, selection.ReadActiveLine(session->Capture()));
}
}

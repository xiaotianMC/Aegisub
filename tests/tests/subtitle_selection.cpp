// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <main.h>
#include "../../src/subtitle_project_access.h"
#include <thread>
#include <wx/string.h>
#include "../../src/selection_controller.h"
#include "../../src/ass_dialogue.h"
#include "../../src/ass_file.h"
#include "../../src/ass_attachment.h"
#include "../../src/ass_info.h"
#include "../../src/ass_style.h"

static_assert(std::is_same_v<decltype(std::declval<SelectionController>().ReadActiveLine()), AssDialogue const*>);
static_assert(std::is_same_v<Selection::value_type, AssDialogue const*>);
static_assert(std::is_same_v<decltype(std::declval<SelectionController>().ReadSortedSelection()), std::vector<AssDialogue const*>>);
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
    agi::signal::Connection active_connection = selection.AddActiveLineListener([&] { ++active; });
    agi::signal::Connection selected_connection = selection.AddSelectionListener([&] { ++selected; });
    auto session = gate.TryBeginComparison(); ASSERT_TRUE(session);
    auto prepared = selection.PrepareSilent(*session, {b->Id}, b->Id);
    EXPECT_EQ(a, selection.ReadActiveLine());
    selection.InstallSilent(*session, std::move(prepared));
    EXPECT_EQ(b, selection.ReadActiveLine()); EXPECT_EQ(1u, selection.ReadSelectedLines().size());
    EXPECT_EQ(1, file.Properties.active_row); EXPECT_EQ(0, active); EXPECT_EQ(0, selected);
    EXPECT_THROW(selection.InstallSilent(*session, std::move(prepared)), std::logic_error);
    session.reset(); selection.SetSelectedSet({a});
    EXPECT_EQ(2u, selection.ReadSelectedSet().size()); // Ordinary binding behavior preserved.
}
TEST_F(subtitle_selection, preparation_failure_or_stale_install_keeps_original_selection) {
    auto session = gate.TryBeginComparison(); ASSERT_TRUE(session);
    EXPECT_THROW(selection.PrepareSilent(*session, {b->Id, -1}, b->Id), std::out_of_range);
    EXPECT_EQ(a, selection.ReadActiveLine()); EXPECT_EQ((Selection{a}), selection.ReadSelectedSet());
    auto prepared = selection.PrepareSilent(*session, {b->Id}, b->Id);
    session->AdvanceRevision();
    EXPECT_THROW(selection.InstallSilent(*session, std::move(prepared)), std::logic_error);
    EXPECT_EQ(a, selection.ReadActiveLine()); EXPECT_EQ((Selection{a}), selection.ReadSelectedSet());
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

namespace {
TEST_F(subtitle_selection, id_changes_preserve_order_repetition_and_independent_active) {
    std::vector<char> notices;
    agi::signal::Connection selected=selection.AddSelectionListener([&] {
        notices.push_back('S');
        EXPECT_EQ(b->Id,selection.ActiveID());
        EXPECT_EQ((std::set<int>{b->Id}),selection.SelectedIDs());
    });
    agi::signal::Connection active = selection.AddActiveLineListener([&] {notices.push_back('A');});
    auto revision=gate.Revision();
    selection.SetSelectionAndActiveIDs({b->Id},b->Id,false);
    selection.SetSelectionAndActiveIDs({b->Id},b->Id,false);
    EXPECT_EQ((std::vector<char>{'S','A','S'}),notices);
    selected.Disconnect();
    selection.SetActiveID(a->Id);
    EXPECT_EQ((std::set<int>{b->Id}),selection.SelectedIDs());
    EXPECT_EQ(revision,gate.Revision());
    auto comparison=gate.TryBeginComparison(); ASSERT_TRUE(comparison);
    selection.SetActiveID(b->Id); // Pure UI state also works without a write lease.
    EXPECT_EQ(b,selection.ReadActiveLine());
}
TEST_F(subtitle_selection, invalid_ids_thread_and_closed_owner_leave_state_intact) {
    auto before=selection.SelectedIDs();int active=selection.ActiveID();
    int notices=0;agi::signal::Connection observer=selection.AddSelectionListener([&]{++notices;});
    EXPECT_THROW(selection.SetSelectionAndActiveIDs({b->Id,-1},b->Id),std::out_of_range);
    EXPECT_THROW(selection.SetSelectionAndActiveIDs({b->Id},-1),std::out_of_range);
    EXPECT_THROW(selection.SetActiveID(-1),std::out_of_range);
    EXPECT_EQ(before,selection.SelectedIDs());EXPECT_EQ(active,selection.ActiveID());EXPECT_EQ(0,notices);
    std::thread worker([&] {
        EXPECT_THROW(selection.SetActiveID(b->Id),std::logic_error);
        EXPECT_THROW(selection.ReadSelectedLines(),std::logic_error);
    });worker.join();
    gate.Close();
    EXPECT_THROW(selection.SetSelectedIDs({b->Id}),std::logic_error);
    EXPECT_THROW(selection.ReadActiveLine(),std::logic_error);
}
TEST_F(subtitle_selection, stable_ids_resolve_reconstructed_nodes_and_rebind_active) {
    int id=a->Id;AssDialogueBase value(*a);
    file.Events.erase_and_dispose(file.Events.iterator_to(*a),[](auto row){delete row;});
    a=new AssDialogue(value);file.Events.push_front(*a);
    file.Commit(wxString{},AssFile::COMMIT_NEW);
    EXPECT_EQ(a,selection.ReadActiveLine());EXPECT_EQ((std::set<int>{id}),selection.SelectedIDs());
    int active=0;agi::signal::Connection observer = selection.AddActiveLineListener([&]{++active;});
    selection.SetSelectionAndActiveIDs({id},id,false);
    EXPECT_EQ(1,active);EXPECT_EQ(a,selection.ReadActiveLine());
    selection.SetActiveID(id);EXPECT_EQ(1,active);
}
TEST_F(subtitle_selection, selection_observers_cannot_mutate_model_even_with_existing_authority) {
    SubtitleProjectAccess access(file,gate);
    auto scope=access.TryWrite();ASSERT_TRUE(scope);auto edit=access.Edit(scope->Lease());
    auto revision=gate.Revision();
    int visits=0;
    agi::signal::Connection observer=selection.AddSelectionListener([&] {
        ++visits;EXPECT_TRUE(file.IsNotifying());EXPECT_FALSE(access.TryWrite());
        EXPECT_THROW(edit.SetDialogueField(a->Id,&AssDialogueBase::Layer,9),std::logic_error);
        EXPECT_THROW(edit.Commit(wxString{},AssFile::COMMIT_DIAG_META),std::logic_error);
    });
    selection.SetSelectedIDs({a->Id});
    EXPECT_EQ(1,visits);EXPECT_EQ(0,a->Layer);EXPECT_EQ(revision,gate.Revision());
    EXPECT_FALSE(file.IsNotifying());
    observer.Disconnect();
    agi::signal::Connection throwing = selection.AddActiveLineListener([]{throw std::runtime_error("observer");});
    EXPECT_THROW(selection.SetActiveID(b->Id),std::runtime_error);
    EXPECT_FALSE(file.IsNotifying());EXPECT_TRUE(access.TryWrite());
}
TEST_F(subtitle_selection, nested_ui_navigation_has_current_views_and_no_stale_payload) {
    bool nested=false;std::vector<int> observed;
    agi::signal::Connection reader = selection.AddActiveLineListener([&]{observed.push_back(selection.ReadActiveLine()->Id);});
    agi::signal::Connection navigator = selection.AddActiveLineListener([&] {
        if(!nested){nested=true;selection.PrevLine();}
    });
    selection.SetSelectionAndActiveIDs({b->Id},b->Id,false);
    EXPECT_EQ(a->Id,selection.ActiveID());
    EXPECT_EQ((std::vector<int>{a->Id,a->Id}),observed);
    EXPECT_FALSE(file.IsNotifying());
}
TEST_F(subtitle_selection, silent_prepared_install_rejects_uncommitted_identity_changes) {
    auto session=gate.TryBeginComparison();ASSERT_TRUE(session);
    auto prepared=selection.PrepareSilent(*session,{b->Id},b->Id);
    b->Id+=10000;
    EXPECT_THROW(selection.InstallSilent(*session,std::move(prepared)),std::logic_error);
    EXPECT_EQ(a,selection.ReadActiveLine());
}
}

TEST(subtitle_selection_empty, empty_commit_does_not_invent_an_active_change) {
    AssFile file;agi::SubtitleEditGate gate;SelectionController selection(file,gate);
    int notices=0;agi::signal::Connection active=selection.AddActiveLineListener([&]{++notices;});
    file.Commit(wxString{},AssFile::COMMIT_NEW);
    selection.SetSelectionAndActiveIDs({},0);
    EXPECT_EQ(0,notices);
}

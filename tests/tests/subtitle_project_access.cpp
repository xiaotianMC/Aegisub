// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <main.h>
#include "../../src/subtitle_project_access.h"
#include "../../src/ass_attachment.h"
#include "../../src/ass_info.h"
#include <future>
#include <libaegisub/subtitle_rebind.h>
#include <thread>
#include <wx/string.h>

static_assert(std::is_same_v<decltype(std::declval<SubtitleProjectAccess>().Read()), AssFile const&>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<AssFile const&>().Events.front())>>);

TEST(subtitle_project_access, lease_scoped_fields_styles_and_extradata) {
    AssFile file; auto line = new AssDialogue; file.Events.push_back(*line);
    auto style = new AssStyle; file.Styles.push_back(*style);
    auto extra = file.AddExtradata("opaque", "before");
    agi::SubtitleEditGate gate, other; SubtitleProjectAccess access(file, gate);
    auto lease = gate.TryBeginWrite(); ASSERT_TRUE(lease);
    auto edit = access.Edit(*lease);
    edit.SetDialogueField(line->Id, &AssDialogueBase::Layer, 8);
    edit.SetDialogueField(line->Id, &AssDialogueBase::Text, std::string(" 中 文 {\\i1} "));
    edit.SetStyleField("Default", &AssStyle::fontsize, 52.);
    edit.SetExtradataValue(extra, "after");
    EXPECT_EQ(8, access.Read().Events.front().Layer);
    EXPECT_EQ(" 中 文 {\\i1} ", access.Read().Events.front().Text.get());
    EXPECT_EQ(52., access.Read().Styles.front().fontsize);
    EXPECT_EQ("after", access.Read().Extradata.front().value);
    EXPECT_THROW(edit.SetDialogueField(line->Id, &AssDialogueBase::Id, 2), std::invalid_argument);
    auto foreign = other.TryBeginWrite();
    EXPECT_THROW(access.Edit(*foreign), std::logic_error);
    lease.reset();
    EXPECT_THROW(edit.SetDialogueField(line->Id, &AssDialogueBase::Layer, 9), std::logic_error);
    EXPECT_EQ(8, line->Layer);
    auto comparison = gate.TryBeginComparison(); ASSERT_TRUE(comparison);
    EXPECT_FALSE(gate.TryBeginWrite());
}
TEST(subtitle_project_access, callbacks_cannot_outlive_access_owner_or_write_from_worker) {
    AssFile file; auto line = new AssDialogue; file.Events.push_back(*line);
    agi::SubtitleEditGate gate; auto lease = gate.TryBeginWrite();
    std::optional<SubtitleProjectAccess::WriteAccess> edit;
    {
        SubtitleProjectAccess access(file, gate); edit.emplace(access.Edit(*lease));
        std::thread worker([&] {
            EXPECT_THROW(edit->SetDialogueField(line->Id, &AssDialogueBase::Layer, 1), std::logic_error);
        }); worker.join();
    }
    EXPECT_THROW(edit->SetDialogueField(line->Id, &AssDialogueBase::Layer, 2), std::logic_error);
    EXPECT_EQ(0, line->Layer);
}
TEST(subtitle_project_access, revisions_reject_stale_and_foreign_observers_and_track_legacy_commits) {
    AssFile file; agi::SubtitleEditGate gate, other; SubtitleProjectAccess access(file, gate);
    auto old = gate.Capture(); EXPECT_EQ(&file, &access.Read(old));
    EXPECT_THROW(access.Read(other.Capture()), std::logic_error);
    file.Commit(wxString{}, AssFile::COMMIT_NEW);
    EXPECT_FALSE(old.Current());
    EXPECT_THROW(access.Read(old), std::logic_error);
    auto current = gate.Capture(); auto lease = gate.TryBeginWrite();
    EXPECT_THROW(access.Read(current), std::logic_error);
}
TEST(subtitle_project_access, detached_files_and_snapshots_keep_existing_mutable_semantics) {
    AssFile file; auto line = new AssDialogue; file.Events.push_back(*line);
    agi::SubtitleEditGate gate; SubtitleProjectAccess access(file, gate);
    auto stamp = gate.Revision(); AssFile copy(file);
    copy.Events.front().Layer = 7; copy.Commit(wxString{}, AssFile::COMMIT_NEW);
    EXPECT_EQ(stamp, gate.Revision()); EXPECT_EQ(0, line->Layer);
    EXPECT_NE(line->Id, copy.Events.front().Id);
    AssDialogue duplicate(*line); EXPECT_NE(line->Id, duplicate.Id);
    AssDialogueBase snapshot(*line); AssDialogue restored(snapshot);
    EXPECT_EQ(line->Id, restored.Id);
}
TEST(subtitle_project_access, production_line_ids_remain_unique_across_threads_and_cancelled_builds) {
    std::vector<std::vector<int>> ids(4); std::vector<std::thread> tasks;
    for (auto& output : ids) tasks.emplace_back([&output] {
        for (int i=0; i<500; ++i) { AssDialogue line; output.push_back(line.Id); }
    });
    for (auto& task : tasks) task.join();
    std::set<int> unique;
    for (auto const& output : ids) unique.insert(output.begin(), output.end());
    EXPECT_EQ(2000u, unique.size()); AssDialogue after;
    EXPECT_GT(after.Id, *unique.rbegin());
}
TEST(subtitle_project_access, retired_intrusive_nodes_live_until_required_observer_rebinds) {
    auto retired = std::make_shared<AssFile>(); std::weak_ptr<AssFile> lifetime = retired;
    auto node = new AssDialogue; node->Layer = 42; retired->Events.push_back(*node);
    agi::SubtitleEditGate gate; auto session = gate.TryBeginComparison();
    agi::SubtitleRebindBatch batch(std::move(*session), retired, {"grid", "editor"}); retired.reset();
    batch.Dispatch(0, [&](auto ticket) {
        EXPECT_EQ(42, node->Layer); ticket.Acknowledge(ticket.Revision());
    });
    EXPECT_FALSE(batch.Complete()); EXPECT_FALSE(lifetime.expired());
    batch.Dispatch(1, [&](auto ticket) {
        EXPECT_EQ(42, node->Layer); node = nullptr; ticket.Acknowledge(ticket.Revision());
    });
    EXPECT_TRUE(batch.Complete()); EXPECT_TRUE(lifetime.expired());
}
TEST(subtitle_project_access, ordinary_nested_edits_share_one_c0_lease_and_release_after_last_scope) {
    AssFile file; agi::SubtitleEditGate gate; SubtitleProjectAccess access(file, gate);
    auto outer=access.TryWrite(); ASSERT_TRUE(outer); auto revision=gate.Revision();
    auto inner=access.TryWrite(); ASSERT_TRUE(inner);
    EXPECT_EQ(&outer->Lease(), &inner->Lease()); EXPECT_EQ(revision, gate.Revision());
    outer.reset(); EXPECT_FALSE(gate.TryBeginComparison()); EXPECT_TRUE(inner->Lease().Valid());
    inner.reset(); auto comparison=gate.TryBeginComparison(); ASSERT_TRUE(comparison);
    EXPECT_FALSE(access.TryWrite()); comparison.reset(); EXPECT_TRUE(access.TryWrite());
}
TEST(subtitle_project_access, ordinary_nested_failure_releases_authority_and_expired_access_cannot_write) {
    AssFile file; auto line=new AssDialogue; file.Events.push_back(*line);
    agi::SubtitleEditGate gate; SubtitleProjectAccess access(file, gate);
    std::optional<SubtitleProjectAccess::WriteAccess> stale;
    try {
        auto outer=access.TryWrite(); auto inner=access.TryWrite();
        stale.emplace(access.Edit(inner->Lease()));
        stale->SetDialogueField(line->Id, &AssDialogueBase::Layer, 2);
        throw std::runtime_error("ordinary edit cancelled");
    } catch (std::runtime_error const&) { }
    EXPECT_THROW(stale->SetDialogueField(line->Id, &AssDialogueBase::Layer, 3), std::logic_error);
    EXPECT_TRUE(gate.TryBeginComparison()); EXPECT_EQ(2, line->Layer);
}
TEST(subtitle_project_access, ordinary_commit_observers_cannot_reenter_writes_and_throwing_observers_release_phase) {
    AssFile file; agi::SubtitleEditGate gate; SubtitleProjectAccess access(file,gate);
    bool visited=false;
    agi::signal::Connection observer=file.AddCommitListener([&](int,const AssDialogue*) {
        visited=true; EXPECT_TRUE(file.IsNotifying()); EXPECT_FALSE(access.TryWrite());
        throw std::runtime_error("observer");
    });
    auto outer=access.TryWrite();
    EXPECT_THROW(file.Commit(wxString{},AssFile::COMMIT_NEW),std::runtime_error);
    EXPECT_TRUE(visited); EXPECT_FALSE(file.IsNotifying()); EXPECT_TRUE(access.TryWrite());
}
TEST(subtitle_interaction, callbacks_keep_task_registered_and_own_commits_advance_baseline) {
    AssFile file;auto line=new AssDialogue;file.Events.push_back(*line);
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    auto session=access.TryBeginInteraction({line->Id});ASSERT_TRUE(session);
    EXPECT_FALSE(gate.TryBeginComparison());EXPECT_TRUE(session->Current());
    AssDialogueBase changed(*line);changed.Start=1001;changed.End=2002;
    session->UpdateRows({changed});session->Commit(wxString{},AssFile::COMMIT_DIAG_TIME);
    EXPECT_TRUE(session->Current());EXPECT_EQ(1001,line->Start.GetMilliseconds());
    changed.Layer=4;session->UpdateRows({changed});session->Commit(wxString{},AssFile::COMMIT_DIAG_META);
    EXPECT_TRUE(session->Current());session->End();EXPECT_TRUE(gate.TryBeginComparison());
    EXPECT_THROW(session->UpdateRows({changed}),std::logic_error);
}
TEST(subtitle_interaction, external_revision_rejects_even_same_line_id_and_releases_on_close) {
    AssFile file;auto line=new AssDialogue;file.Events.push_back(*line);
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    auto session=access.TryBeginInteraction({line->Id});AssDialogueBase old(*line);
    {auto write=access.TryWrite();line->Text="external";file.Commit(wxString{},AssFile::COMMIT_DIAG_TEXT);}
    EXPECT_FALSE(session->Current());
    EXPECT_THROW(session->UpdateRows({old}),std::logic_error);
    EXPECT_EQ("external",line->Text.get());session.reset();EXPECT_TRUE(gate.TryBeginComparison());
}
TEST(subtitle_interaction, missing_and_invalid_targets_fail_before_any_row_changes) {
    AssFile file;auto a=new AssDialogue;auto b=new AssDialogue;file.Events.push_back(*a);file.Events.push_back(*b);
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    EXPECT_THROW(access.TryBeginInteraction({a->Id,a->Id}),std::invalid_argument);
    EXPECT_THROW(access.TryBeginInteraction({-1}),std::out_of_range);
    auto session=access.TryBeginInteraction({a->Id,b->Id});AssDialogueBase changed(*a);changed.Layer=8;
    delete b;
    EXPECT_THROW(session->UpdateRows({changed}),std::out_of_range);
    EXPECT_EQ(0,a->Layer);session.reset();EXPECT_TRUE(gate.TryBeginComparison());
}
TEST(subtitle_interaction, comparison_foreign_target_and_non_owner_thread_cannot_write) {
    AssFile file,other;auto a=new AssDialogue;file.Events.push_back(*a);auto b=new AssDialogue;other.Events.push_back(*b);
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    {auto comparison=gate.TryBeginComparison();EXPECT_FALSE(access.TryBeginInteraction({a->Id}));}
    auto session=access.TryBeginInteraction({a->Id});AssDialogueBase changed(*a);changed.Layer=5;
    AssDialogueBase foreign(*b);
    EXPECT_THROW(session->UpdateRows({changed,foreign}),std::invalid_argument);
    EXPECT_EQ(0,a->Layer);
    std::thread worker([&]{EXPECT_THROW(session->UpdateRows({changed}),std::logic_error);});worker.join();
    EXPECT_EQ(0,a->Layer);session.reset();EXPECT_TRUE(gate.TryBeginComparison());
}
TEST(subtitle_interaction, failed_commit_releases_task_and_preserves_normal_signal_exceptions) {
    AssFile file;auto line=new AssDialogue;file.Events.push_back(*line);
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    agi::signal::Connection observer=file.AddCommitListener([](int,const AssDialogue*){throw std::runtime_error("observer");});
    auto session=access.TryBeginInteraction({line->Id});
    EXPECT_THROW(session->Commit(wxString{},AssFile::COMMIT_DIAG_TEXT),std::runtime_error);
    EXPECT_FALSE(session->Current());EXPECT_TRUE(gate.TryBeginComparison());
}
TEST(subtitle_interaction, owner_close_invalidates_outstanding_session_without_dangling_model_access) {
    AssFile file;auto line=new AssDialogue;file.Events.push_back(*line);AssDialogueBase changed(*line);
    agi::SubtitleEditGate gate;std::optional<SubtitleProjectAccess::Interaction> session;
    {SubtitleProjectAccess access(file,gate);session=access.TryBeginInteraction({line->Id});access.Close();EXPECT_FALSE(session->Current());}
    EXPECT_FALSE(session->Current());
    EXPECT_THROW(session->UpdateRows({changed}),std::logic_error);
    gate.Close();session.reset();
}
TEST(subtitle_interaction, repeated_open_close_and_exception_unwind_leave_no_task_registration) {
    AssFile file;auto line=new AssDialogue;file.Events.push_back(*line);
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    for(int i=0;i<100;++i) {
        try {auto session=access.TryBeginInteraction({line->Id});EXPECT_FALSE(gate.TryBeginComparison());throw std::runtime_error("cancel");}
        catch(std::runtime_error const&){}
        EXPECT_TRUE(gate.TryBeginComparison());
    }
}
TEST(subtitle_interaction, independent_documents_do_not_share_interaction_authority) {
    AssFile a,b;auto x=new AssDialogue;auto y=new AssDialogue;a.Events.push_back(*x);b.Events.push_back(*y);
    agi::SubtitleEditGate ga,gb;SubtitleProjectAccess aa(a,ga),ab(b,gb);
    auto first=aa.TryBeginInteraction({x->Id});EXPECT_TRUE(gb.TryBeginComparison());
    auto second=ab.TryBeginInteraction({y->Id});ASSERT_TRUE(second);
    first.reset();EXPECT_TRUE(ga.TryBeginComparison());EXPECT_FALSE(gb.TryBeginComparison());
    second.reset();EXPECT_TRUE(gb.TryBeginComparison());
}
TEST(subtitle_interaction, own_commit_does_not_adopt_unexpected_nested_revision) {
    AssFile file;auto line=new AssDialogue;file.Events.push_back(*line);
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);bool nested=false;
    agi::signal::Connection observer=file.AddCommitListener([&](int,const AssDialogue*) {
        if(!nested) {nested=true;file.Commit(wxString{},AssFile::COMMIT_DIAG_TEXT);}
    });
    auto session=access.TryBeginInteraction({line->Id});
    EXPECT_THROW(session->Commit(wxString{},AssFile::COMMIT_DIAG_TEXT),std::logic_error);
    EXPECT_FALSE(session->Current());EXPECT_TRUE(gate.TryBeginComparison());
}

TEST(subtitle_project_access, ordinary_value_batch_validates_all_rows_and_preserves_live_nodes) {
    AssFile file; auto first=new AssDialogue; auto second=new AssDialogue;
    file.Events.push_back(*first); file.Events.push_back(*second); first->Row=0;second->Row=1;
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    auto scope=access.TryWrite();ASSERT_TRUE(scope);auto edit=access.Edit(scope->Lease());
    AssDialogueBase a=*first,b=*second;a.Text="candidate";b.Id=-1;
    EXPECT_THROW(edit.UpdateRows({a,b}),std::out_of_range);EXPECT_EQ("",first->Text.get());
    EXPECT_THROW(edit.UpdateRows({a,a}),std::invalid_argument);EXPECT_EQ("",first->Text.get());
    b=*second;b.Start=123;edit.UpdateRows({a,b});
    EXPECT_EQ(first,&file.Events.front());EXPECT_EQ("candidate",first->Text.get());EXPECT_EQ(123,second->Start.GetMilliseconds());
    scope.reset();EXPECT_THROW(edit.UpdateRows({a}),std::logic_error);
}
TEST(subtitle_project_access, const_queries_subscribe_and_all_write_handles_reject_notification_reentry) {
    AssFile file;file.Events.push_back(*new AssDialogue);file.Styles.push_back(*new AssStyle);
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    auto scope=access.TryWrite();auto edit=access.Edit(scope->Lease());
    static_assert(std::is_same_v<decltype(access.Read().GetStyle("Default")),AssStyle const*>);
    bool called=false;
    agi::signal::Connection observer=access.Read().AddCommitListener([&] {
        called=true;EXPECT_THROW(edit.SetDialogueField(file.Events.front().Id,&AssDialogueBase::Text,"bad"),std::logic_error);
        auto direct=gate.TryBeginWrite();ASSERT_TRUE(direct);auto bypass=access.Edit(*direct);
        EXPECT_THROW(bypass.UpdateRows({file.Events.front()}),std::logic_error);
        EXPECT_THROW(bypass.Commit("bad",AssFile::COMMIT_DIAG_TEXT),std::logic_error);
    });
    edit.Commit("ok",AssFile::COMMIT_DIAG_TEXT);EXPECT_TRUE(called);EXPECT_EQ("",file.Events.front().Text.get());
}

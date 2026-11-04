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
    int commits=0;agi::signal::Connection counted=file.AddCommitListener([&](int,const AssDialogue*){++commits;});
    // C1-C1c-11 B3: the own commit succeeded, so it is not reported as failed;
    // the session ends instead of adopting the foreign nested revision.
    EXPECT_NO_THROW(session->Commit(wxString{},AssFile::COMMIT_DIAG_TEXT));
    EXPECT_EQ(2,commits);
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

TEST(subtitle_project_access, ordinary_row_list_rejects_stale_or_duplicate_results_before_detaching_any_node) {
    AssFile file;auto a=new AssDialogue;auto b=new AssDialogue;file.Events.push_back(*a);file.Events.push_back(*b);a->Row=0;b->Row=1;
    agi::SubtitleEditGate gate,foreign;SubtitleProjectAccess access(file,gate);auto scope=access.TryWrite();auto edit=access.Edit(scope->Lease());
    auto stamp=gate.Capture();std::vector<AssDialogueBase> original={*a,*b};auto changed=original;changed[0].Text="changed";
    EXPECT_THROW(edit.ReplaceRows(stamp,original,{changed[0],changed[0]}),std::invalid_argument);EXPECT_EQ("",a->Text.get());
    EXPECT_THROW(edit.ReplaceRows(foreign.Capture(),original,changed),std::logic_error);
    auto stale=original;stale[1].Start=1;EXPECT_THROW(edit.ReplaceRows(stamp,stale,changed),std::logic_error);
    EXPECT_EQ(0u,access.RetainedRetiredNodes());
    auto a_id=a->Id;edit.ReplaceRows(stamp,original,{original[1]});
    // The detached node is project-owned until a successful Commit settles it.
    ASSERT_EQ(1u,file.Events.size());EXPECT_EQ(b,&file.Events.front());EXPECT_EQ(1u,access.RetainedRetiredNodes());EXPECT_EQ(a_id,a->Id);
}

TEST(subtitle_project_access, ordinary_dependent_values_prepare_every_part_and_preserve_project_properties) {
    AssFile file;auto row=new AssDialogue;row->Row=0;file.Events.push_back(*row);auto style=new AssStyle;file.Styles.push_back(*style);
    file.SetScriptInfo("Title","original");file.Properties.video_position=17;
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);auto scope=access.TryWrite();auto stamp=gate.Capture();
    AssFile original(file),candidate(file);original.Events.front().Id=candidate.Events.front().Id=row->Id;
    candidate.Styles.front().name="Renamed";candidate.Styles.front().UpdateData();
    candidate.Events.front().Style="Renamed";candidate.Events.front().Text="new";
    auto id=candidate.AddExtradata("opaque","value");candidate.Events.front().ExtradataIds=std::vector<uint32_t>{id};
    candidate.SetScriptInfo("Title","updated");candidate.Properties.video_position=99;
    auto edit=access.Edit(scope->Lease());
    EXPECT_THROW(edit.UpdateValues(stamp,original,candidate,{1},AssFile::COMMIT_STYLES|AssFile::COMMIT_DIAG_FULL),std::invalid_argument);
    EXPECT_EQ("",row->Text.get());EXPECT_EQ("Default",style->name);EXPECT_EQ("original",file.GetScriptInfo("Title"));
    edit.UpdateValues(stamp,original,candidate,{0},AssFile::COMMIT_STYLES|AssFile::COMMIT_DIAG_FULL|AssFile::COMMIT_SCRIPTINFO|AssFile::COMMIT_EXTRADATA);
    EXPECT_EQ(0u,access.RetainedRetiredNodes());EXPECT_EQ(style,&file.Styles.front());EXPECT_EQ(row,&file.Events.front());
    EXPECT_EQ("Renamed",row->Style.get());EXPECT_EQ("updated",file.GetScriptInfo("Title"));EXPECT_EQ(1u,file.Extradata.size());EXPECT_EQ(17,file.Properties.video_position);
}
TEST(subtitle_project_access, ordinary_values_reject_silent_dependency_changes_and_unknown_new_extra_before_writing) {
    AssFile file;auto row=new AssDialogue;row->Row=0;file.Events.push_back(*row);file.Styles.push_back(*new AssStyle);
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);auto scope=access.TryWrite();auto stamp=gate.Capture();
    AssFile original(file),candidate(file);original.Events.front().Id=candidate.Events.front().Id=row->Id;candidate.Events.front().Text="blocked";
    row->Start=1;auto edit=access.Edit(scope->Lease());EXPECT_THROW(edit.UpdateValues(stamp,original,candidate,{0},AssFile::COMMIT_DIAG_FULL),std::logic_error);
    row->Start=0;candidate.Events.front().ExtradataIds=std::vector<uint32_t>{100};
    EXPECT_THROW(edit.UpdateValues(stamp,original,candidate,{0},AssFile::COMMIT_DIAG_FULL|AssFile::COMMIT_EXTRADATA),std::invalid_argument);EXPECT_EQ("",row->Text.get());
}

TEST(subtitle_project_access, snapshot_is_detached_complete_and_preserves_row_identity_without_revision) {
    AssFile file;auto row=new AssDialogue;row->Row=0;file.Events.push_back(*row);
    file.Properties.video_position=31;file.Properties.automation_settings["test"]="value";
    auto id=row->Id;agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    auto revision=gate.Revision();auto snapshot=access.Snapshot();
    EXPECT_EQ(id,snapshot->Events.front().Id);EXPECT_NE(row,&snapshot->Events.front());
    EXPECT_EQ(31,snapshot->Properties.video_position);EXPECT_EQ("value",snapshot->Properties.automation_settings.at("test"));
    row->ExtradataIds = std::vector<uint32_t>{123};
    auto saved = access.Snapshot();saved->CleanExtradata();
    EXPECT_TRUE(saved->Events.front().ExtradataIds.get().empty());
    EXPECT_EQ((std::vector<uint32_t>{123}),row->ExtradataIds.get());
    snapshot->Events.front().Text="independent";snapshot->Properties.video_position=50;
    EXPECT_EQ("",row->Text.get());EXPECT_EQ(31,file.Properties.video_position);EXPECT_EQ(revision,gate.Revision());
    access.Close();EXPECT_THROW(access.Snapshot(),std::logic_error);
}

template<class T> concept HasMetadataInUI = requires(T ui) { ui.export_encoding; };
static_assert(!HasMetadataInUI<ProjectUIState>);
TEST(subtitle_project_access, metadata_and_ui_have_separate_revisions_dirty_state_and_readonly_notifications) {
    AssFile file;agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    auto revision=gate.Revision();auto comparison=gate.TryBeginComparison();ASSERT_TRUE(comparison);
    auto ui=access.UIState();ui.video_zoom=2;ui.scroll_position=7;access.UpdateUIState(ui);
    EXPECT_EQ(revision,gate.Revision());EXPECT_EQ(0u,access.MetadataRevision());EXPECT_FALSE(access.MetadataDirty());
    EXPECT_EQ("",access.AutomationSetting("missing"));EXPECT_TRUE(file.Properties.automation_settings.empty());
    int notices=0;agi::signal::Connection listener=access.AddMetadataListener([&] {
        ++notices;EXPECT_FALSE(access.TryWrite());EXPECT_TRUE(file.IsNotifying());
    });
    using Field=SubtitleProjectAccess::MetadataField;
    // C1-C1c-12: view state above may change during a comparison; metadata may not.
    EXPECT_THROW(access.SetMetadata(Field::ExportEncoding,"utf-8"),SubtitleProjectBusy);
    EXPECT_EQ(0,notices);EXPECT_EQ(0u,access.MetadataRevision());
    comparison.reset();
    access.SetMetadata(Field::ExportEncoding,"utf-8");access.SetMetadata(Field::ExportEncoding,"utf-8");
    EXPECT_EQ(1,notices);EXPECT_EQ(1u,access.MetadataRevision());EXPECT_TRUE(access.MetadataDirty());
    access.SetMetadata(Field::ExportEncoding,"");EXPECT_FALSE(access.MetadataDirty());
    access.SetAutomationSetting("test","value");EXPECT_EQ("value",access.AutomationSetting("test"));
    EXPECT_EQ(3u,access.MetadataRevision());EXPECT_EQ(revision,gate.Revision());
    EXPECT_THROW(access.SetMetadata(static_cast<Field>(999),"bad"),std::invalid_argument);
    std::thread worker([&]{
        EXPECT_THROW(access.UpdateUIState(ui),std::logic_error);
        EXPECT_THROW(access.SetAutomationSetting("test","worker"),std::logic_error);
    });worker.join();
    access.Close();EXPECT_THROW(access.SetMetadata(Field::ExportEncoding,"utf-8"),std::logic_error);
}

static_assert(std::is_same_v<decltype(std::declval<AssFileCommit>().single_line),AssDialogue const*>);
TEST(subtitle_project_access, live_history_registration_is_private_but_independent_history_stays_usable) {
    AssFile independent;int notices=0;
    agi::signal::Connection listener=independent.AddUndoManager([&](AssFileCommit const&){++notices;});
    independent.Commit(wxString{},AssFile::COMMIT_NEW);EXPECT_EQ(1,notices);
    AssFile live;agi::SubtitleEditGate gate;SubtitleProjectAccess access(live,gate);
    EXPECT_THROW(live.AddUndoManager([](AssFileCommit const&){}),std::logic_error);
    auto scope=access.TryWrite();ASSERT_TRUE(scope);
    access.Edit(scope->Lease()).Commit(wxString{},AssFile::COMMIT_NEW);
}

#include "../../src/subtitle_model_candidate.h"
#include "../../src/subtitle_model_observers.h"
TEST(model_installation_preparation, candidate_is_independent_complete_and_cancellable) {
    AssFile source;source.Properties.active_row=200;source.Properties.export_encoding="utf-8";
    SubtitleModelCandidate prepared(source);ASSERT_TRUE(prepared.Available());
    EXPECT_TRUE(source.Events.empty());EXPECT_TRUE(source.Styles.empty());
    EXPECT_EQ(1u,prepared.Read().Events.size());EXPECT_EQ(1u,prepared.Read().Styles.size());
    EXPECT_EQ("utf-8",prepared.Read().Properties.export_encoding);EXPECT_EQ(0,prepared.Read().Properties.active_row);
    EXPECT_EQ((std::set<int>{prepared.ActiveID()}),prepared.SelectedIDs());
    prepared.Cancel();EXPECT_FALSE(prepared.Available());
}
TEST(model_installation_preparation, duplicate_ids_and_missing_extra_references_fail_before_source_changes) {
    AssFile source;source.Events.push_back(*new AssDialogue);auto other=new AssDialogue;other->Id=source.Events.front().Id;source.Events.push_back(*other);
    EXPECT_THROW(SubtitleModelCandidate{source},std::invalid_argument);
    other->Id+=100000;source.Events.front().ExtradataIds=std::vector<uint32_t>{123};
    EXPECT_THROW(SubtitleModelCandidate{source},std::invalid_argument);
    EXPECT_EQ(2u,source.Events.size());EXPECT_EQ((std::vector<uint32_t>{123}),source.Events.front().ExtradataIds.get());
}
TEST(model_installation_preparation, stable_ids_bindings_and_input_nodes_remain_independent) {
    AssFile source;source.Events.push_back(*new AssDialogue);auto a=&source.Events.front();auto b=new AssDialogue;source.Events.push_back(*b);
    auto extra=source.AddExtradata("aegisub.enhanced.subtitle-binding","0:install");
    a->ExtradataIds=std::vector<uint32_t>{extra};b->ExtradataIds=std::vector<uint32_t>{extra};source.Properties.active_row=1;
    SubtitleModelCandidate prepared(source);EXPECT_NE(a,&prepared.Read().Events.front());
    EXPECT_EQ(a->Id,prepared.Read().Events.front().Id);EXPECT_EQ(b->Id,prepared.ActiveID());
    EXPECT_EQ((std::set<int>{a->Id,b->Id}),prepared.SelectedIDs());a->Text="source changed";
    EXPECT_EQ("",prepared.Read().Events.front().Text.get());
}
TEST(model_installation_preparation, normal_observer_order_and_exceptions_are_preserved) {
    SubtitleModelObservers<int> observers("normal");std::vector<int> seen;
    agi::signal::Connection a=observers.Add([&](int value){seen.push_back(value);});
    agi::signal::Connection b=observers.Add([&]{throw std::runtime_error("normal");});
    // C1-C1c-11: order (newest first) and propagation are preserved, but a
    // throwing observer no longer stops delivery to older ones.
    EXPECT_THROW(observers(3),std::runtime_error);EXPECT_EQ((std::vector<int>{3}),seen);
    b.Disconnect();observers(4);EXPECT_EQ((std::vector<int>{3,4}),seen);
}
TEST(model_installation_preparation, explicit_acknowledgements_isolate_failures_and_retain_old_model) {
    SubtitleModelObservers<int> observers("install");using Ticket=agi::SubtitleRebindBatch::Ticket;
    std::vector<int> seen;bool fail=true;
    agi::signal::Connection a=observers.Add([&](int,Ticket ticket){seen.push_back(1);ticket.Acknowledge(ticket.Revision());});
    agi::signal::Connection b=observers.Add([&](int,Ticket ticket){seen.push_back(2);if(fail)throw std::runtime_error("observer");ticket.Acknowledge(ticket.Revision());});
    agi::signal::Connection c=observers.Add([&](int,Ticket ticket){seen.push_back(3);ticket.Acknowledge(ticket.Revision());});
    auto prepared=observers.PrepareObservers();prepared.Prepare();
    agi::SubtitleEditGate gate;auto session=gate.TryBeginComparison();auto old=std::make_shared<AssFile>();std::weak_ptr<AssFile> lifetime=old;
    agi::SubtitleRebindBatch batch(std::move(*session),old,prepared.Names());old.reset();
    prepared.Dispatch(batch,0,7);EXPECT_EQ((std::vector<int>{3,2,1}),seen);EXPECT_FALSE(batch.Complete());
    EXPECT_EQ(1u,batch.Pending().size());EXPECT_FALSE(lifetime.expired());EXPECT_FALSE(gate.TryBeginWrite());EXPECT_FALSE(gate.TryBeginComparison());
    fail=false;prepared.Dispatch(batch,0,7);EXPECT_TRUE(batch.Complete());EXPECT_TRUE(lifetime.expired());EXPECT_TRUE(gate.TryBeginWrite());
}
TEST(model_installation_preparation, return_without_ack_is_not_success_and_unsubscription_withdraws_safely) {
    SubtitleModelObservers<int> observers("legacy");int calls=0;
    auto owner=std::make_shared<int>(0);
    agi::signal::Connection listener=observers.Observe("owned",[]{},[&](int){++calls;},std::weak_ptr<void const>(owner));auto prepared=observers.PrepareObservers();
    agi::SubtitleEditGate gate;auto session=gate.TryBeginComparison();auto old=std::make_shared<AssFile>();
    agi::SubtitleRebindBatch batch(std::move(*session),old,prepared.Names());
    prepared.Dispatch(batch,0,1);EXPECT_EQ(1,calls);EXPECT_FALSE(batch.Complete());
    EXPECT_THROW(observers.Add([]{}),std::logic_error);
    listener.Disconnect();prepared.Dispatch(batch,0,1);EXPECT_EQ(1,calls);EXPECT_FALSE(batch.Complete());
    owner.reset();prepared.Dispatch(batch,0,1);EXPECT_TRUE(batch.Complete());
}
TEST(model_installation_preparation, observer_prepare_failure_is_not_a_publication_or_revision_change) {
    SubtitleModelObservers<> observers("prepare");using Ticket=agi::SubtitleRebindBatch::Ticket;
    agi::signal::Connection listener=observers.Observe("failing",[]{throw std::runtime_error("prepare");},[](Ticket ticket){ticket.Acknowledge(ticket.Revision());});
    agi::SubtitleEditGate gate;auto revision=gate.Revision();
    {auto prepared=observers.PrepareObservers();EXPECT_THROW(prepared.Prepare(),std::runtime_error);}
    EXPECT_EQ(revision,gate.Revision());EXPECT_TRUE(gate.TryBeginComparison());
    agi::signal::Connection later=observers.Add([]{});
}

template<class T> concept PublicDocumentPublication = requires(T& value) { value.PublishDocumentRevision(); };
static_assert(!PublicDocumentPublication<agi::SubtitleRebindBatch>);
static_assert(std::is_same_v<decltype(std::declval<SubtitleModelCandidate const&>().Read()),AssFile const&>);

// C1-C1c-11 A1: a throwing observer no longer stops delivery to older observers,
// and nodes detached by the edit stay alive while any observer may hold them.
TEST(subtitle_project_access, throwing_newer_observer_still_notifies_older_ones_and_parks_retired_rows) {
    AssFile file;auto a=new AssDialogue;auto b=new AssDialogue;file.Events.push_back(*a);file.Events.push_back(*b);a->Row=0;b->Row=1;
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    int older=0;bool fail=true;
    agi::signal::Connection first=file.AddCommitListener([&](int,const AssDialogue*){++older;});
    agi::signal::Connection second=file.AddCommitListener([&](int,const AssDialogue*){if(fail)throw std::runtime_error("newer");});
    auto a_id=a->Id;
    {
        auto scope=access.TryWrite();ASSERT_TRUE(scope);auto edit=access.Edit(scope->Lease());
        std::vector<AssDialogueBase> original={*a,*b};
        edit.ReplaceRows(gate.Capture(),original,{original[1]});
        EXPECT_THROW(edit.Commit("delete",AssFile::COMMIT_DIAG_ADDREM),std::runtime_error);
    }
    EXPECT_EQ(1,older);
    // The announcing commit failed: the detached row is parked, not freed.
    EXPECT_EQ(1u,access.RetainedRetiredNodes());EXPECT_EQ(a_id,a->Id);
    EXPECT_FALSE(gate.NeedsRebinding());
    fail=false;
    {
        auto scope=access.TryWrite();ASSERT_TRUE(scope);auto edit=access.Edit(scope->Lease());
        std::vector<AssDialogueBase> original={*b};auto added=original;added.push_back(*b);added.back().Id=b->Id+1000;added.back().Row=1;
        edit.ReplaceRows(gate.Capture(),original,added);
        edit.ReplaceRows(gate.Capture(),added,{added[1]});
        EXPECT_EQ(2u,access.RetainedRetiredNodes());
        edit.Commit("ok",AssFile::COMMIT_DIAG_ADDREM);
        // Settled nodes live until the outermost write scope ends.
        EXPECT_EQ(2u,access.RetainedRetiredNodes());
    }
    EXPECT_EQ(2,older);
    EXPECT_EQ(1u,access.RetainedRetiredNodes()) << "only the parked row remains after the scope";
}
TEST(subtitle_project_access, observer_dispatch_rethrows_first_exception_after_all_observers_ran) {
    AssFile file;file.Events.push_back(*new AssDialogue);
    std::vector<int> order;
    agi::signal::Connection a=file.AddCommitListener([&](int,const AssDialogue*){order.push_back(1);throw std::runtime_error("oldest");});
    agi::signal::Connection b=file.AddCommitListener([&](int,const AssDialogue*){order.push_back(2);});
    agi::signal::Connection c=file.AddCommitListener([&](int,const AssDialogue*){order.push_back(3);throw std::logic_error("newest");});
    EXPECT_THROW(file.Commit("x",AssFile::COMMIT_DIAG_TEXT),std::logic_error);
    EXPECT_EQ((std::vector<int>{3,2,1}),order);
}

// C1-C1c-12 (audit C1): project metadata and automation settings cannot change
// during a comparison session; a refusal changes nothing and notifies no one.
TEST(subtitle_project_access, metadata_and_automation_settings_are_refused_during_comparison) {
    AssFile file;file.Events.push_back(*new AssDialogue);
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    access.SetMetadata(SubtitleProjectAccess::MetadataField::AudioFile,"a.wav");
    int notified=0;agi::signal::Connection listener=access.AddMetadataListener([&]{++notified;});
    auto revision=access.MetadataRevision();auto row_revision=gate.Revision();
    {
        auto comparison=gate.TryBeginComparison();ASSERT_TRUE(comparison);
        EXPECT_THROW(access.SetMetadata(SubtitleProjectAccess::MetadataField::AudioFile,"b.wav"),SubtitleProjectBusy);
        EXPECT_THROW(access.SetAutomationSetting("k","v"),SubtitleProjectBusy);
        EXPECT_THROW((void)access.BeginProjectWrite(),SubtitleProjectBusy);
        // A no-op is not a change and is not refused.
        EXPECT_NO_THROW(access.SetMetadata(SubtitleProjectAccess::MetadataField::AudioFile,"a.wav"));
        EXPECT_EQ("a.wav",access.Read().Properties.audio_file);EXPECT_EQ("",access.AutomationSetting("k"));
        EXPECT_EQ(revision,access.MetadataRevision());EXPECT_EQ(0,notified);
    }
    access.SetMetadata(SubtitleProjectAccess::MetadataField::AudioFile,"b.wav");
    access.SetAutomationSetting("k","v");
    EXPECT_EQ("b.wav",access.Read().Properties.audio_file);EXPECT_EQ(2,notified);
    // Metadata changes never advance the row revision.
    EXPECT_EQ(row_revision,gate.Revision());
}
TEST(subtitle_project_access, comparison_cannot_begin_while_metadata_observers_run) {
    AssFile file;file.Events.push_back(*new AssDialogue);
    agi::SubtitleEditGate gate;SubtitleProjectAccess access(file,gate);
    bool began=true;
    agi::signal::Connection listener=access.AddMetadataListener([&]{began=gate.TryBeginComparison().has_value();});
    access.SetMetadata(SubtitleProjectAccess::MetadataField::VideoFile,"v.mkv");
    EXPECT_FALSE(began);EXPECT_TRUE(gate.TryBeginComparison());
}

// C1-C1c-14 (audit C3): ordinary row writes may keep a row's existing
// references, but every new or changed one must resolve; refusals happen
// before the first write.
TEST(subtitle_project_access, row_writes_reject_new_unresolved_extradata_references) {
    AssFile file; auto line = new AssDialogue; file.Events.push_back(*line);
    auto second = new AssDialogue; file.Events.push_back(*second);
    auto known = file.AddExtradata("opaque", "kept");
    line->ExtradataIds = std::vector<uint32_t>{known, 4242}; // 4242: unresolved, already present
    agi::SubtitleEditGate gate; SubtitleProjectAccess access(file, gate);
    auto lease = gate.TryBeginWrite(); ASSERT_TRUE(lease);
    auto edit = access.Edit(*lease);

    AssDialogueBase same = *line; same.Text = "existing references are kept";
    AssDialogueBase other = *second; other.Text = "untouched";
    EXPECT_NO_THROW(edit.UpdateRows({same, other}));
    EXPECT_EQ("existing references are kept", access.Read().Events.front().Text.get());

    AssDialogueBase dangling = *second; dangling.Text = "never written"; dangling.ExtradataIds = std::vector<uint32_t>{999};
    AssDialogueBase first = *line; first.Text = "not written either";
    EXPECT_THROW(edit.UpdateRows({first, dangling}), std::invalid_argument);
    EXPECT_EQ("existing references are kept", access.Read().Events.front().Text.get());
    EXPECT_TRUE(access.Read().Events.back().ExtradataIds.get().empty());

    AssDialogueBase resolved = *second; resolved.ExtradataIds = std::vector<uint32_t>{known};
    EXPECT_NO_THROW(edit.UpdateRows({resolved}));

    std::vector<AssDialogueBase> originals(access.Read().Events.begin(), access.Read().Events.end());
    auto inserted = originals; AssDialogue fresh; AssDialogueBase added = fresh;
    added.Row = 2; added.ExtradataIds = std::vector<uint32_t>{31337};
    inserted.push_back(added);
    try { edit.ReplaceRows(gate.Capture(), originals, inserted); ADD_FAILURE() << "accepted a dangling reference"; }
    catch (std::invalid_argument const& e) { EXPECT_STREQ("Unresolved new extradata reference", e.what()); }
    EXPECT_EQ(2u, access.Read().Events.size());
    inserted.back().ExtradataIds = std::vector<uint32_t>{known};
    EXPECT_NO_THROW(edit.ReplaceRows(gate.Capture(), originals, inserted));
    EXPECT_EQ(3u, access.Read().Events.size());
}

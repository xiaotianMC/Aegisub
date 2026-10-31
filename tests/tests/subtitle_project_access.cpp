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

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
//
// C1-C1c-10: compile-time owner boundary of the live document. Every concept
// below is evaluated outside any friend of agi::Context / SubsController, so an
// inaccessible member is a substitution failure (false), exactly as it would be
// for ordinary business code. Real compiler rejections of the same routes are
// exercised by tests/owner_access_check.py.
#include <main.h>
#include "../../src/include/aegisub/context.h"
#include "../../src/ass_dialogue.h"
#include "../../src/ass_file.h"
#include "../../src/ass_style.h"
#include "../../src/selection_controller.h"
#include "../../src/subs_controller.h"
#include "../../src/subtitle_model_candidate.h"
#include "../../src/subtitle_project_access.h"
#include <type_traits>
#include <wx/string.h>

namespace {
// ---- Owner: no member, getter, conversion or accessor reaches the live AssFile.
template<class T> concept LiveMember = requires(T& c) { c.ass; };
template<class T> concept LiveGetter =
    requires(T& c) { c.Ass(); } || requires(T& c) { c.GetAss(); } || requires(T& c) { c.Model(); } ||
    requires(T& c) { c.LiveModel(); } || requires(T& c) { c.File(); } || requires(T& c) { c.Subtitles(); } ||
    requires(T& c) { c.Mutable(); } || requires(T& c) { c.LegacyMutable(); };
template<class T> concept OwnerMemberAccessor = requires(T& s) { s.Live(); };
template<class T> concept OwnerStaticAccessor = requires(agi::Context* c) { T::Live(c); };
static_assert(!LiveMember<agi::Context> && !LiveMember<agi::Context const>);
static_assert(!LiveGetter<agi::Context> && !LiveGetter<SubtitleProjectAccess> && !LiveGetter<SubsController>);
static_assert(!OwnerMemberAccessor<SubsController> && !OwnerStaticAccessor<SubsController>);
template<class From> constexpr bool reaches_live =
    std::is_convertible_v<From&, AssFile&> || std::is_convertible_v<From&, AssFile*> ||
    std::is_convertible_v<From*, AssFile*> || std::is_constructible_v<AssFile&, From&>;
static_assert(!reaches_live<agi::Context> && !reaches_live<SubtitleProjectAccess> && !reaches_live<SubsController>);
static_assert(!reaches_live<SelectionController> && !reaches_live<SubtitleProjectAccess::WriteAccess>);
static_assert(!reaches_live<SubtitleProjectAccess::WriteScope> && !reaches_live<SubtitleProjectAccess::Interaction>);
static_assert(!reaches_live<SubtitleModelCandidate>);

// ---- Read-only view: every reachable part of Read() is const.
using Live = decltype(std::declval<SubtitleProjectAccess&>().Read());
using LiveAt = decltype(std::declval<SubtitleProjectAccess&>().Read(std::declval<agi::SubtitleEditGate::RevisionToken const&>()));
static_assert(std::is_same_v<Live, AssFile const&> && std::is_same_v<LiveAt, AssFile const&>);
template<class R> constexpr bool is_const_ref = std::is_const_v<std::remove_reference_t<R>>;
static_assert(is_const_ref<decltype(*std::declval<Live>().Events.begin())>);
static_assert(is_const_ref<decltype(*std::declval<Live>().Styles.begin())>);
static_assert(is_const_ref<decltype(std::declval<Live>().Info.front())>);
static_assert(is_const_ref<decltype(std::declval<Live>().Extradata.front())>);
static_assert(is_const_ref<decltype(std::declval<Live>().Attachments.front())>);
static_assert(is_const_ref<decltype((std::declval<Live>().Properties))>);
static_assert(std::is_same_v<decltype(std::declval<Live>().GetStyle("")), AssStyle const*>);
static_assert(std::is_same_v<decltype(std::declval<Live>().iterator_to(std::declval<AssDialogue const&>())),
    EntryList<AssDialogue>::const_iterator>);

// The same operations, required to hold for an independent model and fail for the live view.
template<class F> concept EditsRows = requires(F& f) { f.Events.front().Text = "x"; };
template<class F> concept EditsStructure = requires(F& f) { f.Events.clear(); f.Styles.clear(); f.Info.clear(); };
template<class F> concept EditsProject = requires(F& f) { f.Properties.style_storage = "x"; f.Extradata.clear(); };
template<class F> concept Commits = requires(F& f) { f.Commit(wxString(), 0); };
template<class F> concept AddsData = requires(F& f) { f.AddExtradata("k", "v"); f.SetScriptInfo("k", "v"); };
template<class F> concept Replaces = requires(F& f, AssFile& other) { f.swap(other); f.LoadDefault(); f.Sort(); };
template<class F> concept RegistersHistory = requires(F& f) { f.AddUndoManager([](AssFileCommit const&) {}); };
template<class F> constexpr bool fully_mutable = EditsRows<F> && EditsStructure<F> && EditsProject<F> &&
    Commits<F> && AddsData<F> && Replaces<F> && RegistersHistory<F>;
template<class F> constexpr bool any_mutation = EditsRows<F> || EditsStructure<F> || EditsProject<F> ||
    Commits<F> || AddsData<F> || Replaces<F> || RegistersHistory<F>;
static_assert(!any_mutation<std::remove_reference_t<Live>>);
static_assert(fully_mutable<AssFile>, "independent AssFiles keep their complete mutable API");
static_assert(std::is_same_v<decltype(std::declval<SubtitleProjectAccess&>().Snapshot()), std::unique_ptr<AssFile>>);

// ---- Selection: const rows only, no legacy mutable getters or row setters.
static_assert(std::is_same_v<decltype(std::declval<SelectionController&>().ReadActiveLine()), AssDialogue const*>);
static_assert(std::is_same_v<decltype(std::declval<SelectionController&>().ReadSelectedLines()), std::vector<AssDialogue const*>>);
static_assert(std::is_same_v<Selection::value_type, AssDialogue const*>);
template<class T> concept LegacySelection = requires(T& s) { s.GetActiveLine(); } || requires(T& s) { s.GetSelectedSet(); } ||
    requires(T& s, AssDialogue* row) { s.SetActiveLine(row); };
static_assert(!LegacySelection<SelectionController>);

// ---- Restricted write capabilities cannot be minted or opened by business code.
template<class T> concept Mintable = requires { T{}; };
static_assert(!Mintable<SubtitleProjectAccess::WriteAccess> && !Mintable<SubtitleProjectAccess::WriteScope>);
static_assert(!Mintable<SubtitleProjectAccess::Interaction> && !Mintable<SelectionController::PreparedSelection>);
static_assert(!std::is_copy_constructible_v<SubtitleProjectAccess::Interaction>);
template<class T> concept OpensBorrow = requires(T const& w) { w.Check(); };
static_assert(!OpensBorrow<SubtitleProjectAccess::WriteAccess>);
template<class T> concept LegacyRowsForWrite = requires(T& a) { a.RowsForWrite(); } || requires(T& a) { a.MutableRows(); };
static_assert(!LegacyRowsForWrite<SubtitleProjectAccess> && !LegacyRowsForWrite<SubtitleProjectAccess::WriteAccess>);
template<class T> concept MutableCandidate = requires(T& c) { c.Read().Events.clear(); };
static_assert(!MutableCandidate<SubtitleModelCandidate>);
}

// Runtime half: the type boundary changes nothing about independent models.
TEST(subtitle_owner_access, independent_models_keep_full_mutability_and_snapshots_are_detached) {
    // Built by hand: LoadDefault reads application options, absent in this model-only runner.
    AssFile live; live.Styles.push_back(*new AssStyle);
    auto line = new AssDialogue; line->Text = "original"; live.Events.push_back(*line);
    agi::SubtitleEditGate gate; SubtitleProjectAccess access(live, gate);
    auto before = access.Read().Events.front().Text.get();

    auto snapshot = access.Snapshot();
    snapshot->Events.front().Text = "snapshot edit";
    snapshot->AddExtradata("k", "v");
    snapshot->SetScriptInfo("Title", "snapshot");
    int commits = 0;
    agi::signal::Connection history = snapshot->AddUndoManager([&](AssFileCommit const&) { ++commits; });
    snapshot->Commit("independent", AssFile::COMMIT_DIAG_TEXT);
    EXPECT_EQ(1, commits);
    EXPECT_EQ("original", before);
    EXPECT_EQ(before, access.Read().Events.front().Text.get());
    EXPECT_EQ(line->Id, access.Read().Events.front().Id);
    EXPECT_TRUE(access.Read().Extradata.empty());
    EXPECT_EQ("", std::string(access.Read().GetScriptInfo("Title")));

    AssFile copy(access.Read());
    copy.Events.clear(); copy.Events.push_back(*new AssDialogue); copy.Sort();
    agi::signal::Connection copy_history = copy.AddUndoManager([](AssFileCommit const&) {});
    EXPECT_EQ(before, access.Read().Events.front().Text.get());
    // The bound live file still refuses ordinary history registration.
    EXPECT_THROW((void)live.AddUndoManager([](AssFileCommit const&) {}), std::logic_error);
}

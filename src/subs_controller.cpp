// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#include "subs_controller.h"

#include "ass_attachment.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "subtitle_model_candidate.h"
#include <boost/scope_exit.hpp>
#include "ass_info.h"
#include "ass_style.h"
#include "compat.h"
#include "command/command.h"
#include "format.h"
#include "frame_main.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "subtitle_format.h"
#include "text_selection_controller.h"

#include <libaegisub/dispatch.h>
#include <libaegisub/format_path.h>
#include <libaegisub/fs.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>
#include <libaegisub/util.h>

#include <wx/msgdlg.h>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace {
	void autosave_timer_changed(wxTimer *timer) {
		int freq = OPT_GET("App/Auto/Save Every Seconds")->GetInt();
		if (freq > 0 && OPT_GET("App/Auto/Save")->GetBool())
			timer->Start(freq * 1000);
		else
			timer->Stop();
	}
}

struct SubsController::UndoInfo {
	wxString undo_description;
	int commit_id;

	std::vector<std::pair<std::string, std::string>> script_info;
	std::vector<AssStyle> styles;
	std::vector<AssDialogueBase> events;
	std::vector<AssAttachment> attachments;
	std::vector<ExtradataEntry> extradata;

	mutable std::vector<int> selection;
	int active_line_id = 0;
	int pos = 0, sel_start = 0, sel_end = 0;

    UndoInfo(AssFile const& file, int id, std::set<int> const& selected, int active)
    : commit_id(id), attachments(file.Attachments), extradata(file.Extradata),
      selection(selected.begin(),selected.end()), active_line_id(active) {
        for (auto const& info : file.Info) script_info.emplace_back(info.Key(),info.Value());
        styles.assign(file.Styles.begin(),file.Styles.end());
        events.assign(file.Events.begin(),file.Events.end());
    }

    // A prepared Lua history step: the same fields an ordinary Commit records,
    // taken from independent values instead of the live model. Selection, active
    // line and text selection are captured from the host as OnCommit does.
    UndoInfo(const agi::Context *c, wxString const& d, int commit_id, std::vector<AssInfo> const& info,
             std::vector<AssStyle const*> const& step_styles, std::vector<AssDialogue const*> const& step_events,
             std::vector<ExtradataEntry> const& step_extradata)
    : undo_description(d), commit_id(commit_id), attachments(SubsController::Live(c).Attachments), extradata(step_extradata) {
        script_info.reserve(info.size());
        for (auto const& entry : info) script_info.emplace_back(entry.Key(), entry.Value());
        styles.reserve(step_styles.size());
        for (auto style : step_styles) styles.push_back(*style);
        events.reserve(step_events.size());
        for (auto event : step_events) events.push_back(*event);
        UpdateActiveLine(c);
        UpdateSelection(c);
        UpdateTextSelection(c);
    }

	UndoInfo(const agi::Context *c, wxString const& d, int commit_id)
	: undo_description(d)
	, commit_id(commit_id)
	, attachments(SubsController::Live(c).Attachments)
	, extradata(SubsController::Live(c).Extradata)
	{
		script_info.reserve(SubsController::Live(c).Info.size());
		for (auto const& info : SubsController::Live(c).Info)
			script_info.emplace_back(info.Key(), info.Value());

		styles.reserve(SubsController::Live(c).Styles.size());
		styles.assign(SubsController::Live(c).Styles.begin(), SubsController::Live(c).Styles.end());

		events.reserve(SubsController::Live(c).Events.size());
		events.assign(SubsController::Live(c).Events.begin(), SubsController::Live(c).Events.end());

		UpdateActiveLine(c);
		UpdateSelection(c);
		UpdateTextSelection(c);
	}

    // C1-C1c-11: restoration is split so that every allocation happens before
    // the live model or the history stacks change. Install then only swaps
    // containers (no-throw) and announces; if announcing fails, the replaced
    // nodes are parked because unrefreshed observers may still reference them.
    struct Prepared {
        std::unique_ptr<AssFile> next = std::make_unique<AssFile>();
        std::set<int> selection;
        int active = 0;
    };
    Prepared Prepare(agi::Context *c, HistoryRestore const& permission) const {
        if (permission.owner->context != c || !permission.owner->history_action ||
            !permission.authority.Valid() || !c->subtitleEditGate.Authorizes(permission.revision) || SubsController::Live(c).IsNotifying())
            throw std::logic_error("Invalid subtitle history restoration authority");
        Prepared p;
        auto& next = *p.next;
        std::vector<int> sorted(selection);
        sort(begin(sorted), end(sorted));
        next.Info.reserve(script_info.size());
        for (auto const& info : script_info)
            next.Info.emplace_back(info.first, info.second);
        for (auto const& style : styles)
            next.Styles.push_back(*new AssStyle(style));
        next.Attachments = attachments;
        for (auto const& event : events) {
            auto copy = new AssDialogue(event);
            next.Events.push_back(*copy);
            if (copy->Id == active_line_id)
                p.active = copy->Id;
            if (binary_search(begin(sorted), end(sorted), copy->Id))
                p.selection.insert(copy->Id);
        }
        next.Extradata = extradata;
        return p;
    }

    void Install(agi::Context *c, Prepared&& p, std::vector<std::shared_ptr<void>>& parked) const {
        auto& live = SubsController::Live(c);
        std::shared_ptr<AssFile> old(std::move(p.next)); // after the swaps: the replaced content
        parked.reserve(parked.size() + 1);
        // ---- no-throw replacement ----
        live.Info.swap(old->Info);
        live.Styles.swap(old->Styles);
        live.Events.swap(old->Events);
        live.Attachments.swap(old->Attachments);
        live.Extradata.swap(old->Extradata);
        try {
            live.Commit("", AssFile::COMMIT_NEW);
        }
        catch (...) {
            parked.push_back(std::move(old));
            throw;
        }
        c->selectionController->SetSelectionAndActiveIDs(std::move(p.selection), p.active);

        c->textSelectionController->SetInsertionPoint(pos);
        c->textSelectionController->SetSelection(sel_start, sel_end);
        // `old` (Info, Styles and Events of the previous state) is freed here,
        // after every observer was refreshed by the commit above.
    }

    void UpdateActiveLine(const agi::Context *c) {
        if (auto id = c->selectionController->ActiveID()) active_line_id = id;
    }
    void UpdateSelection(const agi::Context *c) {
        auto ids = c->selectionController->SelectedIDs();
        selection.assign(ids.begin(), ids.end());
    }

	void UpdateTextSelection(const agi::Context *c) {
		pos = c->textSelectionController->GetInsertionPoint();
		sel_start = c->textSelectionController->GetSelectionStart();
		sel_end = c->textSelectionController->GetSelectionEnd();
	}
};

struct SubtitleModelInstallation::Pending {
    std::shared_ptr<AssFile> retired;
    SubtitleModelObservers<int,AssDialogue const*>::Prepared commits;
    SubtitleModelObservers<>::Prepared selected_observers, active_observers;
    std::vector<std::string> names;
    size_t selection_offset=0, active_offset=0;
    std::unique_ptr<agi::SubtitleRebindBatch> batch;
    boost::container::list<SubsController::UndoInfo> undo, redo;
    std::set<int> selected;
    int active, old_active;
    agi::fs::path filename;
    agi::Path paths;
    ProjectMetadata saved_metadata;
    std::shared_ptr<int> autosave_lifetime=std::make_shared<int>(0);
    int old_commit, old_saved, old_auto, old_next;
    uint64_t old_metadata, old_auto_metadata;
    bool published=false;
    Pending(SubsController& owner, SubtitleModelCandidate&& candidate, agi::fs::path const& path)
    : retired(std::move(candidate.file)),
      commits(owner.Live().AnnounceCommit.PrepareObservers()),
      selected_observers(owner.context->selectionController->AnnounceSelectedSetChanged.PrepareObservers()),
      active_observers(owner.context->selectionController->AnnounceActiveLineChanged.PrepareObservers()),
      selected(candidate.selected), active(candidate.active), old_active(owner.context->selectionController->ActiveID()),
      filename(path), paths(*owner.context->path), saved_metadata(retired->Properties),
      old_commit(owner.commit_id),old_saved(owner.saved_commit_id),old_auto(owner.autosaved_commit_id),old_next(owner.next_commit_id),
      old_metadata(owner.context->subtitleAccess->MetadataRevision()),old_auto_metadata(owner.autosaved_metadata_revision) {
        candidate.consumed=true;
        paths.SetToken("?script",path.parent_path());
        if (old_next==std::numeric_limits<int>::max() || old_metadata==std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("Document history or metadata identity exhausted");
        undo.emplace_back(*retired,old_next,selected,active);
        names=commits.Names();
        selection_offset=names.size();
        auto selection_names=selected_observers.Names();names.insert(names.end(),selection_names.begin(),selection_names.end());
        active_offset=names.size();
        auto active_names=active_observers.Names();names.insert(names.end(),active_names.begin(),active_names.end());
    }
};
SubtitleModelInstallation::SubtitleModelInstallation(SubsController& value) : owner(value) {}
SubtitleModelInstallation::~SubtitleModelInstallation() = default;
void SubtitleModelInstallation::CheckThread() const {
    if (thread != std::this_thread::get_id()) throw std::logic_error("Model installation requires host thread");
}
bool SubtitleModelInstallation::Busy() const noexcept {
    return state==State::Preparing || state==State::Prepared || state==State::Authorizing ||
        state==State::Publishing || state==State::Rebinding || state==State::Recovering;
}
void SubtitleModelInstallation::Begin() {
    CheckThread();
    if (Busy() || state==State::RecoveryBlocked || owner.context->subtitleEditGate.NeedsRebinding())
        throw std::logic_error("Another model installation is active or recovery is blocked");
    // C1-C1c-13 (audit C4): a queued open or close dispatched from a modal loop
    // inside an observer notification must not start replacing the document
    // that the notification is still describing.
    if (owner.context->subtitleAccess->Read().IsNotifying())
        throw agi::InvalidInputException("Subtitles cannot be opened or closed while the editor is still updating; try again.");
    expected=owner.context->subtitleEditGate.Capture();
    metadata_revision=owner.context->subtitleAccess->MetadataRevision();
    cancelled=false;state=State::Preparing;
}
void SubtitleModelInstallation::Abort() noexcept {
    if (state==State::RecoveryBlocked || state==State::Committed) return;
    owner.Live().installing=false;
    state=State::Aborting;pending.reset();expected.reset();
}
void SubtitleModelInstallation::Install(SubtitleModelCandidate&& candidate, agi::fs::path const& path) {
    CheckThread();
    if (state!=State::Preparing || !candidate.Available()) throw std::logic_error("Invalid or cancelled installation candidate");
    pending=std::make_unique<Pending>(owner,std::move(candidate),path);state=State::Prepared;
    auto c=owner.context;
    state=State::Authorizing;
    if (!c->subtitleEditGate.Authorizes(*expected) || metadata_revision!=c->subtitleAccess->MetadataRevision())
        throw std::logic_error("Stale model installation input");
    auto session=c->subtitleEditGate.TryBeginComparison();
    if (!session) throw std::logic_error("Model installation requires no active writer or comparison");
    SubsController::Live(c).installing=true;
    pending->commits.Prepare();pending->selected_observers.Prepare();pending->active_observers.Prepare();
    if (cancelled || !c->subtitleEditGate.Authorizes(*expected) || metadata_revision!=c->subtitleAccess->MetadataRevision())
        throw std::logic_error("Model installation cancelled or changed during preparation");
    pending->batch=std::make_unique<agi::SubtitleRebindBatch>(std::move(*session),pending->retired,pending->names,true);
    state=State::Publishing;
    SubsController::Live(c).installing=true;
    try {
        pending->batch->PublishDocumentRevision();
        SubsController::Live(c).swap(*pending->retired);
        owner.undo_stack.swap(pending->undo);owner.redo_stack.swap(pending->redo);
        owner.filename.swap(pending->filename);c->path->swap(pending->paths);
        std::swap(c->subtitleAccess->saved_metadata,pending->saved_metadata);
        c->subtitleAccess->metadata_revision=pending->old_metadata+1;
        owner.commit_id=owner.saved_commit_id=owner.autosaved_commit_id=pending->old_next;
        owner.next_commit_id=pending->old_next+1;
        owner.autosaved_metadata_revision=c->subtitleAccess->metadata_revision;
        owner.autosave_lifetime.swap(pending->autosave_lifetime);pending->autosave_lifetime.reset();
        c->selectionController->selection.swap(pending->selected);
        c->selectionController->active_id=pending->active;
        c->selectionController->model_replaced=false;
        pending->published=true;
        // C1-C1c-14 (audit C5): the live model is the new one from here on, so
        // new subscribers (dialogs, tools opened while recovery is blocked)
        // read it directly and need no delivery. Retries still use the
        // prepared list.
        pending->commits.EndSubscriptionFreeze();
        pending->selected_observers.EndSubscriptionFreeze();
        pending->active_observers.EndSubscriptionFreeze();
        Confirm();
    }
    catch (...) {
        SubsController::Live(c).installing=false;state=State::RecoveryBlocked;throw;
    }
    SubsController::Live(c).installing=false;
    if (state!=State::Committed) throw SubtitleModelRecoveryBlocked();
    pending.reset();
}
void SubtitleModelInstallation::Confirm() {
    state=State::Rebinding;
    auto c=owner.context;
    bool previous=SubsController::Live(c).notifying;SubsController::Live(c).notifying=true;
    try {
        pending->commits.Dispatch(*pending->batch,0,AssFile::COMMIT_NEW,nullptr);
        c->selectionController->model_replaced=false;
        pending->selected_observers.Dispatch(*pending->batch,pending->selection_offset);
        pending->active_observers.Dispatch(*pending->batch,pending->active_offset);
    }
    catch (...) {SubsController::Live(c).notifying=previous;state=State::RecoveryBlocked;throw;}
    SubsController::Live(c).notifying=previous;
    state=pending->batch->Complete()?State::Committed:State::RecoveryBlocked;
}
bool SubtitleModelInstallation::Retry() {
    CheckThread();
    if (state!=State::RecoveryBlocked || !pending || !pending->published) throw std::logic_error("No published installation to recover");
    state=State::Recovering;owner.Live().installing=true;
    try {Confirm();} catch (...) {owner.Live().installing=false;state=State::RecoveryBlocked;throw;}
    owner.Live().installing=false;
    if (state!=State::Committed) return false;
    pending.reset();return true;
}
std::vector<std::string> SubtitleModelInstallation::PendingObservers() const {
    CheckThread();return pending && pending->batch?pending->batch->Pending():std::vector<std::string>{};
}

AssFile& SubsController::Live(agi::Context const* c) noexcept { return *c->ass; }
AssFile& SubsController::Live() const noexcept { return Live(context); }

SubsController::SubsController(agi::Context *context)
: context(context)
, undo_connection(Live().PushState.Connect(&SubsController::OnCommit, this))
, text_selection_connection(context->textSelectionController->AddSelectionListener(&SubsController::OnTextSelectionChanged, this))
, autosave_queue(agi::dispatch::Create())
{
    installation.reset(new SubtitleModelInstallation(*this));
	autosave_timer_changed(&autosave_timer);
	BindConnection(OPT_SUB("App/Auto/Save", [this] { autosave_timer_changed(&autosave_timer); }));
	BindConnection(OPT_SUB("App/Auto/Save Every Seconds", [this] { autosave_timer_changed(&autosave_timer); }));
	autosave_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (!IsInstalling() && !this->context->subtitleEditGate.NeedsRebinding()) RequestAutoSave();
    });
}

SubsController::~SubsController() {
	autosave_lifetime.reset();
	// Make sure there are no autosaves in progress
	autosave_queue->Sync([]{ });
}
SubtitleModelInstallation::State SubsController::InstallationStatus() const { installation->CheckThread();return installation->state; }
bool SubsController::IsInstalling() const { return installation->Busy(); }
std::vector<std::string> SubsController::PendingInstallationObservers() const { return installation->PendingObservers(); }
void SubsController::CancelModelInstallation() { installation->CheckThread();installation->cancelled=true; }
bool SubsController::RetryModelInstallation() {
    if (!installation->Retry()) return false;
    RunPostCommitPlan(); // The plan of the recovered installation; consumed exactly once.
    return true;
}
void SubsController::InstallDocument(SubtitleModelCandidate&& candidate, PostCommitPlan plan) {
    post_commit_plan=std::move(plan);
    try {
        installation->Install(std::move(candidate),post_commit_plan->path);
    }
    catch (...) {
        // Only a published-but-unconfirmed installation keeps its follow-up work
        // for an explicit retry; every other failure published nothing.
        if (installation->state!=SubtitleModelInstallation::State::RecoveryBlocked) post_commit_plan.reset();
        throw;
    }
    RunPostCommitPlan();
}
void SubsController::RunPostCommitPlan() {
    peripheral_errors.clear();
    if (!post_commit_plan) return;
    auto plan=std::move(*post_commit_plan);post_commit_plan.reset();
    // The document is already Committed here. A failing step is reported, never
    // rethrown as a load failure, and does not suppress the remaining steps.
    auto step=[this](wxString const& name, auto&& work) {
        std::string error;
        try { work(); return; }
        catch (agi::Exception const& e) { error=e.GetMessage(); }
        catch (std::exception const& e) { error=e.what(); }
        catch (...) { error=from_wx(_("Unknown error")); }
        peripheral_errors.push_back(from_wx(name)+": "+error);
    };
    if (plan.kind==PostCommitPlan::Kind::LoadedFile) {
        step(_("Recent files"),[&] {
            config::mru->Add("Subtitle", plan.path);
            OPT_SET("Path/Last/Subtitles")->SetString(plan.path.parent_path().string());
        });
        step(_("Backup of original file"),[&] {
            if (!CanSave() || !OPT_GET("App/Auto/Backup")->GetBool()) return;
            auto path_str = OPT_GET("Path/Auto/Backup")->GetString();
            agi::fs::path path;
            if (path_str.empty())
                path = plan.path.parent_path();
            else
                path = context->path->Decode(path_str);
            agi::fs::CreateDirectory(path);
            agi::fs::Copy(plan.path, path/(plan.path.stem().string() + ".ORIGINAL" + plan.path.extension().string()));
        });
    }
    // Ordinary Signal semantics: a throwing listener stops later FileOpen listeners.
    step(_("Subtitle open notification"),[&] { FileOpen(plan.path); });
}
bool SubsController::DeferHostClose(wxCloseEvent& event) {
    if (!IsInstalling()) return false;
    CancelModelInstallation();
    if (event.CanVeto()) event.Veto();else event.Skip(false);
    return true;
}

void SubsController::SetSelectionController(SelectionController *selection_controller) {
	active_line_connection = selection_controller->AddActiveLineListener("subs_controller.AddActiveLineListener", [this](SubtitleModelRebindTicket ticket) { OnActiveLineChanged(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime));
	selection_connection = selection_controller->AddSelectionListener("subs_controller.AddSelectionListener", [this](SubtitleModelRebindTicket ticket) { OnSelectionChanged(); if (ticket.Current()) ticket.Acknowledge(ticket.Revision()); }, std::weak_ptr<void const>(model_observer_lifetime));
}

ProjectProperties SubsController::Load(agi::fs::path const& filename, const char *charset) {
    installation->Begin();
    BOOST_SCOPE_EXIT_ALL(this) { installation->Abort(); };
    auto candidate=[&] {
        AssFile temp;
        SubtitleFormat::GetReader(filename, charset)->ReadFile(&temp, filename, context->project->Timecodes(), charset);
        return SubtitleModelCandidate(temp);
    }(); // Release the parser's independent copy before publication/notification.
    auto props=candidate.Read().Properties;
    // Filename, ?script, metadata baseline, history and selection are published
    // by the installation; MRU, backup and FileOpen run after it is Committed.
    InstallDocument(std::move(candidate),{PostCommitPlan::Kind::LoadedFile,filename});
	return props;
}

void SubsController::Save(agi::fs::path const& filename, const char *encoding) {
    if (IsInstalling() || context->subtitleEditGate.NeedsRebinding()) throw std::logic_error("Cannot save an incomplete model installation");
    // C1-C1c-12: saving moves the file name, ?script, relative media paths and
    // the saved baseline; none of that may change during a comparison. Held for
    // the whole save, so a comparison cannot begin part-way through either.
    auto registration = context->subtitleAccess->BeginProjectWrite();
	const SubtitleFormat *writer = SubtitleFormat::GetWriter(filename);
	if (!writer)
		throw agi::InvalidInputException("Unknown file type.");

	auto snapshot = context->subtitleAccess->Snapshot();
    snapshot->Properties = context->project->PropertiesForSave(filename);
    snapshot->CleanExtradata();
    // No live metadata or dirty-state changes precede successful output.
    writer->WriteFile(snapshot.get(), filename, 0, encoding);
	int old_autosaved_commit_id = autosaved_commit_id, old_saved_commit_id = saved_commit_id;
    auto old_filename = this->filename;
    ProjectMetadata old_metadata = context->subtitleAccess->Read().Properties;
    auto old_script_path = context->path->Decode("?script");
    auto old_saved_metadata = context->subtitleAccess->saved_metadata;
    auto old_auto_metadata = autosaved_metadata_revision;
    auto old_autosave_epoch = autosave_epoch;
	try {
		autosaved_commit_id = saved_commit_id = commit_id;
		++autosave_epoch;

		// Have to set this now for the sake of things that want to save paths
		// relative to the script in the header
		this->filename = filename;
		context->project->SetSubtitlesFilename(filename);
        context->subtitleAccess->MarkMetadataSaved(snapshot->Properties);
        autosaved_metadata_revision = context->subtitleAccess->MetadataRevision();

		FileSave();
	}
	catch (...) {
		autosaved_commit_id = old_autosaved_commit_id;
		saved_commit_id = old_saved_commit_id;
        this->filename = old_filename;
        context->path->SetToken("?script", old_script_path);
        context->subtitleAccess->saved_metadata = std::move(old_saved_metadata);
        autosaved_metadata_revision = old_auto_metadata;
        autosave_epoch = old_autosave_epoch;
        // C1-C1c-13 (audit C2): SetSubtitlesFilename published the new relative
        // media paths through SetMetadata (revision + listeners). Undo that the
        // same way instead of assigning Properties behind the listeners' backs.
        context->subtitleAccess->RestoreMetadata(old_metadata);
		throw;
	}

	SetFileName(filename);
}

void SubsController::Close() {
    // Closing replaces the document with a blank one through the same host
    // installation as Load: same authorization, document epoch, observer
    // confirmation and RecoveryBlocked semantics. Begin rejects active
    // installation/recovery exactly as the legacy guard did.
    installation->Begin();
    BOOST_SCOPE_EXIT_ALL(this) { installation->Abort(); };
    auto candidate=[] {
        AssFile blank;
        blank.LoadDefault(true, OPT_GET("Subtitle Format/ASS/Default Style Catalog")->GetString());
        return SubtitleModelCandidate(blank);
    }();
    InstallDocument(std::move(candidate),{PostCommitPlan::Kind::ClosedFile,{}});
}

int SubsController::TryToClose(bool allow_cancel) const {
	if (!IsModified())
		return wxYES;

	int flags = wxYES_NO;
	if (allow_cancel)
		flags |= wxCANCEL;
	int result = wxMessageBox(fmt_tl("Do you want to save changes to %s?", Filename()), _("Unsaved changes"), flags, context->parent);
	if (result == wxYES) {
		cmd::call("subtitle/save", context);
		// If it fails saving, return cancel anyway
		return IsModified() ? wxCANCEL : wxYES;
	}
	return result;
}

bool SubsController::WriteRecoveryCopy(agi::fs::path const& file) const noexcept {
    try {
        auto writer = SubtitleFormat::GetWriter(file);
        if (!writer) return false;
        auto snapshot = context->subtitleAccess->Snapshot();
        try { snapshot->Properties = context->project->PropertiesForSave(file); }
        catch (...) { } // keep the snapshot's own project properties
        snapshot->CleanExtradata();
        writer->WriteFile(snapshot.get(), file, 0);
        return true;
    }
    catch (...) { }
    // Logging may allocate; keep it out of the handlers and contained.
    try { LOG_E("subtitle/recovery") << "Recovery copy failed: " << file; }
    catch (...) { }
    return false;
}

void SubsController::RequestAutoSave() {
    if (IsInstalling() || context->subtitleEditGate.NeedsRebinding()) throw std::logic_error("Cannot autosave an incomplete model installation");
    // C1-C1c-12: deferred (not failed) during a comparison: the baseline is
    // left unchanged, so the next timer tick or commit after it retries.
    auto registration = context->subtitleEditGate.TryBeginProjectWrite();
    if (!registration) return;
	if (commit_id == autosaved_commit_id && context->subtitleAccess->MetadataRevision() == autosaved_metadata_revision)
		return;

	auto directory = context->path->Decode(OPT_GET("Path/Auto/Save")->GetString());
	if (directory.empty())
		directory = filename.parent_path();

	auto name = filename.filename();
	if (name.empty())
		name = from_wx(_("Untitled"));

	auto previous = autosaved_commit_id;
    auto previous_metadata = autosaved_metadata_revision;
    auto metadata_version = context->subtitleAccess->MetadataRevision();
    auto version = commit_id;
    auto subs_copy = std::shared_ptr<AssFile>(context->subtitleAccess->Snapshot());
    subs_copy->CleanExtradata();
    std::weak_ptr<int> lifetime = autosave_lifetime;
    autosaved_commit_id = version;
    autosaved_metadata_revision = metadata_version;
    auto epoch = ++autosave_epoch;
	autosave_queue->Async([this, subs_copy, name, directory, lifetime, previous, version, previous_metadata, metadata_version, epoch] {
		wxString msg;
        bool succeeded = false;

		try {
			agi::fs::CreateDirectory(directory);
			auto path = directory /  agi::format("%s.%s.AUTOSAVE.ass", name.string(),
			                                     agi::util::strftime("%Y-%m-%d-%H-%M-%S"));
			SubtitleFormat::GetWriter(path)->WriteFile(subs_copy.get(), path, 0);
            succeeded = true;
			msg = fmt_tl("File backup saved as \"%s\".", path);
		}
		catch (const agi::Exception& err) {
			msg = _("Exception when attempting to autosave file: ") + to_wx(err.GetMessage());
		}
		catch (...) {
			msg = _("Unhandled exception when attempting to autosave file.");
		}

		agi::dispatch::Main().Async([this, lifetime, previous, previous_metadata, succeeded, msg, epoch] {
            if (lifetime.expired()) return;
            if (!succeeded && autosave_epoch == epoch) {
                autosaved_commit_id = previous;autosaved_metadata_revision = previous_metadata;
            }
            if (context->frame) context->frame->StatusTimeout(msg);
		});
	});
}

bool SubsController::CanSave() const {
    if (IsInstalling() || context->subtitleEditGate.NeedsRebinding()) return false;
	try {
		return SubtitleFormat::GetWriter(filename)->CanSave(&context->subtitleAccess->Read());
	}
	catch (...) {
		return false;
	}
}
bool SubsController::IsModified() const {
    return commit_id != saved_commit_id || context->subtitleAccess->MetadataDirty();
}

void SubsController::SetFileName(agi::fs::path const& path) {
	filename = path;
	context->path->SetToken("?script", path.parent_path());
	config::mru->Add("Subtitle", path);
	OPT_SET("Path/Last/Subtitles")->SetString(filename.parent_path().string());
}

void SubsController::OnCommit(AssFileCommit c) {
    auto edit_scope = context->subtitleAccess->TryWrite();
    if (!edit_scope) throw std::logic_error("Subtitle history commit requires write authorization");
	if (c.message.empty() && !undo_stack.empty()) return;

	commit_id = next_commit_id++;
	// Allow coalescing only if it's the last change and the file has not been
	// saved since the last change
	if (commit_id == *c.commit_id+1 && redo_stack.empty() && saved_commit_id+1 != commit_id) {
		// If only one line changed just modify it instead of copying the file
		if (c.single_line && c.single_line->Group() == AssEntryGroup::DIALOGUE) {
			for (auto& diag : undo_stack.back().events) {
				if (diag.Id == c.single_line->Id) {
					diag = *c.single_line;
					break;
				}
			}
			*c.commit_id = commit_id;
			return;
		}

		undo_stack.pop_back();
	}

	// Make sure the file has at least one style and one dialogue line
	if (Live().Styles.empty())
		Live().Styles.push_back(*new AssStyle);
	if (Live().Events.empty()) {
		Live().Events.push_back(*new AssDialogue);
		Live().Events.back().Row = 0;
	}

	redo_stack.clear();

	undo_stack.emplace_back(context, c.message, commit_id);

	int depth = std::max<int>(OPT_GET("Limits/Undo Levels")->GetInt(), 2);
	while ((int)undo_stack.size() > depth)
		undo_stack.pop_front();

	// C1-C1c-13 (audit C2): the history entry above is already recorded, so a
	// failed save on every change must not abort the commit before it is
	// announced. Save itself rolls back (the document stays modified); the
	// failure is reported after the commit, outside the notification.
	if (undo_stack.size() > 1 && OPT_GET("App/Auto/Save on Every Change")->GetBool() && !filename.empty() && CanSave()) {
		try { Save(filename); }
		catch (...) { ReportSaveOnChangeFailure(); }
	}

	*c.commit_id = commit_id;
}

void SubsController::ReportSaveOnChangeFailure() noexcept {
	try {
		wxString msg;
		try { throw; }
		catch (agi::Exception const& e) { msg = to_wx(e.GetMessage()); }
		catch (std::exception const& e) { msg = to_wx(std::string(e.what())); }
		catch (...) { msg = _("Unknown error"); }
		msg = _("Saving after the change failed: ") + msg;
		LOG_E("subtitle/save") << from_wx(msg);
		std::weak_ptr<int> lifetime = autosave_lifetime;
		agi::dispatch::Main().Async([this, lifetime, msg] {
			if (lifetime.expired()) return;
			if (context->frame) context->frame->StatusTimeout(msg);
		});
	}
	catch (...) { }
}

void SubsController::OnActiveLineChanged() {
	if (!undo_stack.empty())
		undo_stack.back().UpdateActiveLine(context);
}

void SubsController::OnSelectionChanged() {
	if (!undo_stack.empty())
		undo_stack.back().UpdateSelection(context);
}

void SubsController::OnTextSelectionChanged() {
	if (!undo_stack.empty())
		undo_stack.back().UpdateTextSelection(context);
}

void SubsController::Undo() {
    if (undo_stack.size() <= 1 || history_action || Live().IsNotifying()) return;
    auto edit_scope = context->subtitleAccess->TryWrite();
    if (!edit_scope) return;
    history_action = true;
    BOOST_SCOPE_EXIT_ALL(this) { history_action = false; text_selection_connection.Unblock(); };
    HistoryRestore permission{this, edit_scope->Lease().Capture(), context->subtitleEditGate.Capture()};
    // Prepare the target state first: a failure leaves model and stacks unchanged.
    auto prepared = std::prev(undo_stack.end(), 2)->Prepare(context, permission);
	redo_stack.splice(redo_stack.end(), undo_stack, std::prev(undo_stack.end()));

	commit_id = undo_stack.back().commit_id;

	text_selection_connection.Block();
	undo_stack.back().Install(context, std::move(prepared), retired_after_failure);
	text_selection_connection.Unblock();
}

void SubsController::Redo() {
    if (redo_stack.empty() || history_action || Live().IsNotifying()) return;
    auto edit_scope = context->subtitleAccess->TryWrite();
    if (!edit_scope) return;
    history_action = true;
    BOOST_SCOPE_EXIT_ALL(this) { history_action = false; text_selection_connection.Unblock(); };
    HistoryRestore permission{this, edit_scope->Lease().Capture(), context->subtitleEditGate.Capture()};
    auto prepared = redo_stack.back().Prepare(context, permission);
	undo_stack.splice(undo_stack.end(), redo_stack, std::prev(redo_stack.end()));

	commit_id = undo_stack.back().commit_id;

	text_selection_connection.Block();
	undo_stack.back().Install(context, std::move(prepared), retired_after_failure);
	text_selection_connection.Unblock();
}

wxString SubsController::GetUndoDescription() const {
	return IsUndoStackEmpty() ? wxString() : undo_stack.back().undo_description;
}

wxString SubsController::GetRedoDescription() const {
	return IsRedoStackEmpty() ? wxString() : redo_stack.back().undo_description;
}

agi::fs::path SubsController::Filename() const {
	if (!filename.empty()) return filename;

	// Apple HIG says "untitled" should not be capitalised
#ifndef __WXMAC__
	return from_wx(_("Untitled"));
#else
	return from_wx(_("untitled"));
#endif
}

// ---- C1-C1c-9: Lua macro transactions ------------------------------------

LuaSubtitleTransaction::LuaSubtitleTransaction() = default;
LuaSubtitleTransaction::LuaSubtitleTransaction(LuaSubtitleTransaction&&) noexcept = default;
LuaSubtitleTransaction& LuaSubtitleTransaction::operator=(LuaSubtitleTransaction&&) noexcept = default;
LuaSubtitleTransaction::~LuaSubtitleTransaction() = default;

struct LuaSubtitlePublication::Data {
    std::optional<LuaSubtitleTransaction> transaction; // keeps the work model (original values) alive
    LuaSubtitleResult result;           // owns the nodes the script created
    std::optional<agi::SubtitleEditGate::ComparisonSession> session; // exclusive borrow
    bool modifies = false;
    int type = 0;
    std::vector<int> final_ids;

    // Prepared final document; nodes are live originals or created nodes.
    std::vector<AssInfo> info;
    std::vector<AssStyle*> styles;
    std::vector<AssDialogue*> events;
    std::unordered_set<AssEntry const*> adopted;            // pool nodes that become live
    std::vector<std::unique_ptr<AssEntry>> normalisation;   // default style/line nodes

    // Prepared history.
    boost::container::list<SubsController::UndoInfo> history;
    int last_commit_id = 0, next_commit_id = 0;
    bool pushed = false;
    size_t undo_depth = 2;

    // Retired state, owned from the commit point until destruction.
    std::vector<AssStyle*> retired_styles;
    std::vector<AssDialogue*> retired_events;
    bool retired_owned = false;
    boost::container::list<SubsController::UndoInfo> retired_undo, retired_redo;

    ~Data() {
        if (!retired_owned) return;
        for (auto style : retired_styles) delete style;
        for (auto event : retired_events) delete event;
    }
};

LuaSubtitlePublication::LuaSubtitlePublication() = default;
LuaSubtitlePublication::LuaSubtitlePublication(LuaSubtitlePublication&&) noexcept = default;
LuaSubtitlePublication& LuaSubtitlePublication::operator=(LuaSubtitlePublication&&) noexcept = default;
LuaSubtitlePublication::~LuaSubtitlePublication() = default;
std::vector<int> const& LuaSubtitlePublication::FinalEventIds() const { return data->final_ids; }
bool LuaSubtitlePublication::Modifies() const { return data && data->modifies; }

LuaSubtitleTransaction SubsController::BeginLuaTransaction() {
    installation->CheckThread();
    auto& gate = context->subtitleEditGate;
    if (IsInstalling() || gate.NeedsRebinding())
        throw LuaSubtitleRejected("The subtitles are being replaced or are blocked for recovery");
    // Refuse to start while any writer, interaction or comparison is registered:
    // its result could never be published. Nothing is held while the script runs.
    if (!gate.TryBeginComparison())
        throw LuaSubtitleRejected("The subtitles are being edited or compared");
    LuaSubtitleTransaction transaction;
    transaction.owner = this;
    transaction.expected = gate.Capture();
    transaction.work = context->subtitleAccess->Snapshot();
    transaction.selected = context->selectionController->SelectedIDs();
    transaction.active = context->selectionController->ActiveID();
    return transaction;
}

LuaSubtitlePublication SubsController::PrepareLuaPublication(LuaSubtitleTransaction&& transaction, LuaSubtitleResult&& result) {
    installation->CheckThread();
    if (transaction.owner != this || !transaction.work || !transaction.expected)
        throw std::logic_error("Foreign or consumed Lua transaction");
    LuaSubtitlePublication publication;
    publication.data = std::make_unique<LuaSubtitlePublication::Data>();
    auto& d = *publication.data;
    d.transaction.emplace(std::move(transaction));
    d.result = std::move(result);
    auto& gate = context->subtitleEditGate;
    auto& live = Live();
    AssFile const& work = *d.transaction->work;

    auto current = [&] {
        if (IsInstalling() || gate.NeedsRebinding())
            throw LuaSubtitleRejected("The subtitles are being replaced or are blocked for recovery");
        if (!gate.Authorizes(*d.transaction->expected))
            throw LuaSubtitleRejected("The subtitles were changed or replaced while the macro was running");
    };
    current();
    d.modifies = !d.result.steps.empty();
    if (!d.modifies) {
        for (auto const& line : live.Events) d.final_ids.push_back(line.Id);
        return publication;
    }
    d.session = gate.TryBeginComparison();
    if (!d.session) throw LuaSubtitleRejected("The subtitles are being edited or compared");
    current(); // no writer can register while the exclusive borrow is held

    // Original lines map back to the live nodes they were copied from; the token
    // guarantees the live lists are exactly what was snapshotted.
    std::unordered_map<AssEntry const*, AssEntry*> nodes;
    {
        if (std::distance(work.Styles.begin(), work.Styles.end()) != std::distance(live.Styles.begin(), live.Styles.end()) ||
            std::distance(work.Events.begin(), work.Events.end()) != std::distance(live.Events.begin(), live.Events.end()))
            throw std::logic_error("Lua work model does not match its source document");
        auto ls = live.Styles.begin();
        for (auto const& style : work.Styles) nodes.emplace(&style, &*ls++);
        auto le = live.Events.begin();
        for (auto const& event : work.Events) {
            if (event.Id != le->Id) throw std::logic_error("Lua work model does not match its source document");
            nodes.emplace(&event, &*le++);
        }
    }
    std::unordered_set<AssEntry const*> created;
    for (auto const& node : d.result.pool) created.insert(node.get());

    struct StepValues {
        std::vector<AssInfo> info;
        std::vector<AssStyle const*> styles;
        std::vector<AssDialogue const*> events;
    };
    auto values = [&](std::vector<AssEntry const*> const& lines) {
        StepValues v;
        size_t original_info = 0;
        for (auto line : lines) {
            if (!line) {
                // An untouched Script Info entry. Resolved for every step, so an undo
                // point taken before the first Script Info edit keeps the header.
                if (original_info >= work.Info.size()) throw std::logic_error("Lua Script Info placeholder out of range");
                v.info.push_back(work.Info[original_info++]);
                continue;
            }
            if (!nodes.count(line) && !created.count(line))
                throw LuaSubtitleRejected("Lua result references a line it does not own");
            switch (line->Group()) {
                case AssEntryGroup::INFO: v.info.push_back(*static_cast<AssInfo const*>(line)); break;
                case AssEntryGroup::STYLE: v.styles.push_back(static_cast<AssStyle const*>(line)); break;
                case AssEntryGroup::DIALOGUE: v.events.push_back(static_cast<AssDialogue const*>(line)); break;
                default: throw LuaSubtitleRejected("Lua result contains an unsupported line type");
            }
        }
        return v;
    };

    // Extradata only grows during a run: the live entries must be an unchanged prefix.
    if (work.Extradata.size() < live.Extradata.size() || work.next_extradata_id < live.next_extradata_id)
        throw LuaSubtitleRejected("Lua extradata does not extend the document's extradata");
    for (size_t i = 0; i < work.Extradata.size(); ++i) {
        auto const& e = work.Extradata[i];
        if (i < live.Extradata.size()) {
            auto const& l = live.Extradata[i];
            if (e.id != l.id || e.key != l.key || e.value != l.value)
                throw LuaSubtitleRejected("Lua extradata does not extend the document's extradata");
        }
        else if ((i > 0 && e.id <= work.Extradata[i - 1].id) || e.id >= work.next_extradata_id)
            throw LuaSubtitleRejected("Lua extradata identifiers are not unique");
    }
    auto extradata_exists = [&](uint32_t id) {
        return std::binary_search(work.Extradata.begin(), work.Extradata.end(), id,
            [](auto const& a, auto const& b) {
                if constexpr (std::is_same_v<std::decay_t<decltype(a)>, uint32_t>) return a < b.id;
                else return a.id < b;
            });
    };

    // Final document: last step, with the empty-file normalisation every commit applies.
    auto final_values = values(d.result.steps.back().lines);
    if (final_values.styles.empty()) {
        d.normalisation.push_back(std::make_unique<AssStyle>());
        final_values.styles.push_back(static_cast<AssStyle const*>(d.normalisation.back().get()));
    }
    if (final_values.events.empty()) {
        auto line = std::make_unique<AssDialogue>();
        line->Row = 0;
        final_values.events.push_back(line.get());
        d.normalisation.push_back(std::move(line));
    }
    for (auto const& node : d.normalisation) created.insert(node.get());
    d.info = final_values.info;
    std::unordered_map<AssEntry const*, AssEntry*> owned;
    for (auto const& node : d.result.pool) owned.emplace(node.get(), node.get());
    for (auto const& node : d.normalisation) owned.emplace(node.get(), node.get());
    std::unordered_set<AssEntry const*> used;
    std::set<int> ids;
    auto adopt = [&](AssEntry const* line) -> AssEntry* {
        if (!used.insert(line).second) throw LuaSubtitleRejected("Lua result contains the same line twice");
        if (auto it = nodes.find(line); it != nodes.end()) return it->second;
        auto it = owned.find(line);
        if (it == owned.end()) throw LuaSubtitleRejected("Lua result references a line it does not own");
        d.adopted.insert(line);
        return it->second;
    };
    for (auto style : final_values.styles) d.styles.push_back(static_cast<AssStyle*>(adopt(style)));
    for (auto event : final_values.events) {
        auto line = static_cast<AssDialogue*>(adopt(event));
        if (line->Id <= 0 || !ids.insert(line->Id).second) throw LuaSubtitleRejected("Lua result has duplicate line identities");
        if (!nodes.count(event))
            for (auto id : line->ExtradataIds.get())
                if (!extradata_exists(id)) throw LuaSubtitleRejected("Lua result references missing extradata");
        d.events.push_back(line);
        d.final_ids.push_back(line->Id);
    }
    std::unordered_set<AssEntry const*> kept(d.styles.begin(), d.styles.end());
    kept.insert(d.events.begin(), d.events.end());
    for (auto& style : live.Styles) if (!kept.count(&style)) d.retired_styles.push_back(&style);
    for (auto& event : live.Events) if (!kept.count(&event)) d.retired_events.push_back(&event);

    // History: replay the rules of ProcessingComplete + OnCommit as values.
    // Each non-empty undo point is one step; an empty description (with history
    // present) records nothing; amend id -1 never coalesces; every recorded
    // step gets the empty-file normalisation; Extradata is the final set, as the
    // legacy run had already written it into the document before any commit.
    int next = next_commit_id;
    size_t stack = undo_stack.size();
    for (size_t i = 0; i < d.result.steps.size(); ++i) {
        auto const& step = d.result.steps[i];
        d.type |= step.type;
        if (!step.commit || (step.description.empty() && stack > 0)) continue;
        bool last = i + 1 == d.result.steps.size();
        StepValues v = last ? final_values : values(step.lines);
        std::vector<std::unique_ptr<AssEntry>> defaults;
        if (!last && v.styles.empty()) {
            defaults.push_back(std::make_unique<AssStyle>());
            v.styles.push_back(static_cast<AssStyle const*>(defaults.back().get()));
        }
        if (!last && v.events.empty()) {
            auto line = std::make_unique<AssDialogue>();
            line->Row = 0;
            v.events.push_back(line.get());
            defaults.push_back(std::move(line));
        }
        if (next == std::numeric_limits<int>::max()) throw std::overflow_error("Document history exhausted");
        d.history.emplace_back(context, step.description, next, v.info, v.styles, v.events, work.Extradata);
        d.last_commit_id = next++;
        d.pushed = true;
        ++stack;
    }
    d.next_commit_id = next;
    d.undo_depth = static_cast<size_t>(std::max<int64_t>(OPT_GET("Limits/Undo Levels")->GetInt(), 2));
    return publication;
}

void SubsController::PublishLua(LuaSubtitlePublication&& publication, std::set<int> selection, int active) {
    installation->CheckThread();
    // Shared ownership is allocated up front so parking it after a failed
    // announcement below cannot allocate (C1-C1c-11).
    std::shared_ptr<std::remove_reference_t<decltype(*publication.data)>> d(std::move(publication.data));
    if (!d) throw std::logic_error("Consumed Lua publication");
    auto& gate = context->subtitleEditGate;
    if (!gate.Authorizes(*d->transaction->expected) || IsInstalling() || gate.NeedsRebinding())
        throw LuaSubtitleRejected("The subtitles were changed or replaced while the macro was running");
    if (!d->modifies) {
        context->selectionController->SetSelectionAndActiveIDs(std::move(selection), active);
        return;
    }
    if (!d->session || !gate.Authorizes(*d->session))
        throw LuaSubtitleRejected("The subtitles are being edited or compared");

    auto& live = Live();
    auto& work = *d->transaction->work;
    retired_after_failure.reserve(retired_after_failure.size() + 1);
    // Throws before any change if the revision space is exhausted.
    d->session->AdvanceRevision();

    // ---- commit point: no allocation, nothing below throws ----
    live.Info.swap(d->info);
    live.Styles.clear();
    for (auto style : d->styles) live.Styles.push_back(*style);
    live.Events.clear();
    int row = 0;
    for (auto event : d->events) { event->Row = row++; live.Events.push_back(*event); }
    live.Extradata.swap(work.Extradata);
    live.next_extradata_id = work.next_extradata_id;
    d->retired_owned = true;
    for (auto& node : d->result.pool) if (d->adopted.count(node.get())) (void)node.release();
    for (auto& node : d->normalisation) if (d->adopted.count(node.get())) (void)node.release();
    if (d->pushed) {
        redo_stack.swap(d->retired_redo);
        undo_stack.splice(undo_stack.end(), d->history);
        while (undo_stack.size() > d->undo_depth)
            d->retired_undo.splice(d->retired_undo.end(), undo_stack, undo_stack.begin());
        commit_id = d->last_commit_id;
        next_commit_id = d->next_commit_id;
    }
    // ---- end of commit point ----

    // Ordinary commit notification semantics, once for the whole run. If an
    // observer throws, the retired nodes are parked so observers that were not
    // refreshed cannot reach freed memory.
    try {
        bool previous = live.notifying;
        live.notifying = true;
        BOOST_SCOPE_EXIT_ALL(&) { live.notifying = previous; };
        live.AnnounceCommit(d->type, nullptr);
        context->selectionController->SetSelectionAndActiveIDs(std::move(selection), active);
    }
    catch (...) {
        d->session.reset();
        retired_after_failure.push_back(std::move(d)); // capacity reserved above
        throw;
    }
    bool pushed = d->pushed;
    d.reset(); // releases the exclusive borrow and the retired state
    if (pushed && undo_stack.size() > 1 && OPT_GET("App/Auto/Save on Every Change")->GetBool() && !filename.empty() && CanSave())
        Save(filename);
}

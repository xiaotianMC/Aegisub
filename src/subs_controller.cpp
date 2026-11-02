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
#include <libaegisub/path.h>
#include <libaegisub/util.h>

#include <wx/msgdlg.h>
#include <limits>

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

	UndoInfo(const agi::Context *c, wxString const& d, int commit_id)
	: undo_description(d)
	, commit_id(commit_id)
	, attachments(c->ass->Attachments)
	, extradata(c->ass->Extradata)
	{
		script_info.reserve(c->ass->Info.size());
		for (auto const& info : c->ass->Info)
			script_info.emplace_back(info.Key(), info.Value());

		styles.reserve(c->ass->Styles.size());
		styles.assign(c->ass->Styles.begin(), c->ass->Styles.end());

		events.reserve(c->ass->Events.size());
		events.assign(c->ass->Events.begin(), c->ass->Events.end());

		UpdateActiveLine(c);
		UpdateSelection(c);
		UpdateTextSelection(c);
	}

	void Apply(agi::Context *c, HistoryRestore const& permission) const {
        if (permission.owner->context != c || !permission.owner->history_action ||
            !permission.authority.Valid() || !c->subtitleEditGate.Authorizes(permission.revision) || c->ass->IsNotifying())
            throw std::logic_error("Invalid subtitle history restoration authority");
		// Keep old dialogue lines alive until after the commit is complete
		// since a bunch of stuff holds references to them
		AssFile old;
		old.Events.swap(c->ass->Events);
		c->ass->Info.clear();
		c->ass->Attachments.clear();
		c->ass->Styles.clear();
		c->ass->Extradata.clear();

		sort(begin(selection), end(selection));

		int active = 0;
        std::set<int> new_sel;

		for (auto const& info : script_info)
			c->ass->Info.push_back(*new AssInfo(info.first, info.second));
		for (auto const& style : styles)
			c->ass->Styles.push_back(*new AssStyle(style));
		c->ass->Attachments = attachments;
		for (auto const& event : events) {
			auto copy = new AssDialogue(event);
			c->ass->Events.push_back(*copy);
			if (copy->Id == active_line_id)
				active = copy->Id;
			if (binary_search(begin(selection), end(selection), copy->Id))
				new_sel.insert(copy->Id);
		}
		c->ass->Extradata = extradata;

		c->ass->Commit("", AssFile::COMMIT_NEW);
		c->selectionController->SetSelectionAndActiveIDs(std::move(new_sel), active);

		c->textSelectionController->SetInsertionPoint(pos);
		c->textSelectionController->SetSelection(sel_start, sel_end);
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
      commits(owner.context->ass->AnnounceCommit.PrepareObservers()),
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
    owner.context->subtitleAccess->Read();
    expected=owner.context->subtitleEditGate.Capture();
    metadata_revision=owner.context->subtitleAccess->MetadataRevision();
    cancelled=false;state=State::Preparing;
}
void SubtitleModelInstallation::Abort() noexcept {
    if (state==State::RecoveryBlocked || state==State::Committed) return;
    owner.context->ass->installing=false;
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
    c->ass->installing=true;
    pending->commits.Prepare();pending->selected_observers.Prepare();pending->active_observers.Prepare();
    if (cancelled || !c->subtitleEditGate.Authorizes(*expected) || metadata_revision!=c->subtitleAccess->MetadataRevision())
        throw std::logic_error("Model installation cancelled or changed during preparation");
    pending->batch=std::make_unique<agi::SubtitleRebindBatch>(std::move(*session),pending->retired,pending->names,true);
    state=State::Publishing;
    c->ass->installing=true;
    try {
        pending->batch->PublishDocumentRevision();
        c->ass->swap(*pending->retired);
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
        Confirm();
    }
    catch (...) {
        c->ass->installing=false;state=State::RecoveryBlocked;throw;
    }
    c->ass->installing=false;
    if (state!=State::Committed) throw std::runtime_error("Model installation is RecoveryBlocked; inspect pending observers and retry or close host");
    pending.reset();
}
void SubtitleModelInstallation::Confirm() {
    state=State::Rebinding;
    auto c=owner.context;
    bool previous=c->ass->notifying;c->ass->notifying=true;
    try {
        pending->commits.Dispatch(*pending->batch,0,AssFile::COMMIT_NEW,nullptr);
        c->selectionController->model_replaced=false;
        pending->selected_observers.Dispatch(*pending->batch,pending->selection_offset);
        pending->active_observers.Dispatch(*pending->batch,pending->active_offset);
    }
    catch (...) {c->ass->notifying=previous;state=State::RecoveryBlocked;throw;}
    c->ass->notifying=previous;
    state=pending->batch->Complete()?State::Committed:State::RecoveryBlocked;
}
bool SubtitleModelInstallation::Retry() {
    CheckThread();
    if (state!=State::RecoveryBlocked || !pending || !pending->published) throw std::logic_error("No published installation to recover");
    state=State::Recovering;owner.context->ass->installing=true;
    try {Confirm();} catch (...) {owner.context->ass->installing=false;state=State::RecoveryBlocked;throw;}
    owner.context->ass->installing=false;
    if (state!=State::Committed) return false;
    pending.reset();return true;
}
std::vector<std::string> SubtitleModelInstallation::PendingObservers() const {
    CheckThread();return pending && pending->batch?pending->batch->Pending():std::vector<std::string>{};
}

SubsController::SubsController(agi::Context *context)
: context(context)
, undo_connection(context->ass->PushState.Connect(&SubsController::OnCommit, this))
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
    SetFileName(filename);FileOpen(filename);return true;
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
    installation->Install(std::move(candidate),filename);
    SetFileName(filename); // Post-commit peripheral bookkeeping, not publication authority.

	// Save backup of file
	if (CanSave() && OPT_GET("App/Auto/Backup")->GetBool()) {
		auto path_str = OPT_GET("Path/Auto/Backup")->GetString();
		agi::fs::path path;
		if (path_str.empty())
			path = filename.parent_path();
		else
			path = context->path->Decode(path_str);
		agi::fs::CreateDirectory(path);
		agi::fs::Copy(filename, path/(filename.stem().string() + ".ORIGINAL" + filename.extension().string()));
	}

	FileOpen(filename);
	return props;
}

void SubsController::Save(agi::fs::path const& filename, const char *encoding) {
    if (IsInstalling() || context->subtitleEditGate.NeedsRebinding()) throw std::logic_error("Cannot save an incomplete model installation");
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
    auto old_properties = context->subtitleAccess->Read().Properties;
    auto old_script_path = context->path->Decode("?script");
    auto old_saved_metadata = context->subtitleAccess->saved_metadata;
    auto old_auto_metadata = autosaved_metadata_revision;
	try {
		autosaved_commit_id = saved_commit_id = commit_id;

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
        context->ass->Properties = std::move(old_properties);
        context->subtitleAccess->saved_metadata = std::move(old_saved_metadata);
        autosaved_metadata_revision = old_auto_metadata;
		throw;
	}

	SetFileName(filename);
}

void SubsController::Close() {
    if (IsInstalling() || context->subtitleEditGate.NeedsRebinding()) throw std::logic_error("Cannot replace a document during installation recovery");
    autosave_lifetime = std::make_shared<int>(0);
	undo_stack.clear();
	redo_stack.clear();
	autosaved_commit_id = saved_commit_id = commit_id + 1;
	filename.clear();
	AssFile blank;
	blank.swap(*context->ass);
	context->ass->LoadDefault(true, OPT_GET("Subtitle Format/ASS/Default Style Catalog")->GetString());
    ++context->subtitleAccess->metadata_revision;
    context->subtitleAccess->MarkMetadataSaved(context->ass->Properties);
    autosaved_metadata_revision = context->subtitleAccess->MetadataRevision();
	context->ass->Commit("", AssFile::COMMIT_NEW);
	FileOpen(filename);
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

void SubsController::RequestAutoSave() {
    if (IsInstalling() || context->subtitleEditGate.NeedsRebinding()) throw std::logic_error("Cannot autosave an incomplete model installation");
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
	autosave_queue->Async([this, subs_copy, name, directory, lifetime, previous, version, previous_metadata, metadata_version] {
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

		agi::dispatch::Main().Async([this, lifetime, previous, version, previous_metadata, metadata_version, succeeded, msg] {
            if (lifetime.expired()) return;
            if (!succeeded && autosaved_commit_id == version && autosaved_metadata_revision == metadata_version) {
                autosaved_commit_id = previous;autosaved_metadata_revision = previous_metadata;
            }
            if (context->frame) context->frame->StatusTimeout(msg);
		});
	});
}

bool SubsController::CanSave() const {
    if (IsInstalling() || context->subtitleEditGate.NeedsRebinding()) return false;
	try {
		return SubtitleFormat::GetWriter(filename)->CanSave(context->ass.get());
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
	if (context->ass->Styles.empty())
		context->ass->Styles.push_back(*new AssStyle);
	if (context->ass->Events.empty()) {
		context->ass->Events.push_back(*new AssDialogue);
		context->ass->Events.back().Row = 0;
	}

	redo_stack.clear();

	undo_stack.emplace_back(context, c.message, commit_id);

	int depth = std::max<int>(OPT_GET("Limits/Undo Levels")->GetInt(), 2);
	while ((int)undo_stack.size() > depth)
		undo_stack.pop_front();

	if (undo_stack.size() > 1 && OPT_GET("App/Auto/Save on Every Change")->GetBool() && !filename.empty() && CanSave())
		Save(filename);

	*c.commit_id = commit_id;
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
    if (undo_stack.size() <= 1 || history_action || context->ass->IsNotifying()) return;
    auto edit_scope = context->subtitleAccess->TryWrite();
    if (!edit_scope) return;
    history_action = true;
    BOOST_SCOPE_EXIT_ALL(this) { history_action = false; text_selection_connection.Unblock(); };
	redo_stack.splice(redo_stack.end(), undo_stack, std::prev(undo_stack.end()));

	commit_id = undo_stack.back().commit_id;

	text_selection_connection.Block();
    HistoryRestore permission{this, edit_scope->Lease().Capture(), context->subtitleEditGate.Capture()};
	undo_stack.back().Apply(context, permission);
	text_selection_connection.Unblock();
}

void SubsController::Redo() {
    if (redo_stack.empty() || history_action || context->ass->IsNotifying()) return;
    auto edit_scope = context->subtitleAccess->TryWrite();
    if (!edit_scope) return;
    history_action = true;
    BOOST_SCOPE_EXIT_ALL(this) { history_action = false; text_selection_connection.Unblock(); };
	undo_stack.splice(undo_stack.end(), redo_stack, std::prev(redo_stack.end()));

	commit_id = undo_stack.back().commit_id;

	text_selection_connection.Block();
    HistoryRestore permission{this, edit_scope->Lease().Capture(), context->subtitleEditGate.Capture()};
	undo_stack.back().Apply(context, permission);
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

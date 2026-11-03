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

#pragma once
#include <memory>

#include <libaegisub/fs.h>
#include <libaegisub/signal.h>
#include <libaegisub/subtitle_edit_gate.h>
#include "subtitle_model_installation.h"
#include "subtitle_lua_transaction.h"

#include <boost/container/list.hpp>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <wx/timer.h>

class SelectionController;
class wxCloseEvent;
namespace agi {
	namespace dispatch {
		class Queue;
	}
	struct Context;
}
struct AssFileCommit;
struct ProjectProperties;

class SubsController : private agi::signal::ConnectionScope {
protected:
    // Declared first: expires only after all owned bindings/cache members are destroyed.
    std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);
private:
    friend class SubtitleModelInstallation;
    friend class LuaSubtitlePublication;
    std::unique_ptr<SubtitleModelInstallation> installation;
    /// Retired Lua publication state whose commit notification threw: kept so
    /// observers that were not refreshed never see freed nodes; freed on destruction.
    std::vector<std::shared_ptr<void>> lua_retired_after_failure;
	agi::Context *context;
	agi::signal::Connection undo_connection;
	agi::signal::Connection active_line_connection;
	agi::signal::Connection selection_connection;
	agi::signal::Connection text_selection_connection;

	struct UndoInfo;
    // Private authority minted only after Undo/Redo has admitted a history action.
    // No business API returns this capability or a mutable model reference.
    struct HistoryRestore {
        SubsController const* owner;
        agi::SubtitleEditGate::WriteLease::Token authority;
        agi::SubtitleEditGate::RevisionToken revision;
    };
	boost::container::list<UndoInfo> undo_stack;
	boost::container::list<UndoInfo> redo_stack;

	/// Revision counter for undo coalescing and modified state tracking
	int commit_id = 0;
	/// Last saved version of this file
	int saved_commit_id = 0;
	/// Last autosaved version of this file
	int autosaved_commit_id = 0;
    uint64_t autosaved_metadata_revision = 0;
	/// Version to use for the next commit
	/// Needed to handle Save -> Undo -> Edit, which would result in the file
	/// being marked unmodified if we reused commit IDs
	int next_commit_id = 1;
    bool history_action = false;

	/// Timer for triggering autosaves
	wxTimer autosave_timer;

	/// Queue which autosaves are performed on
	std::unique_ptr<agi::dispatch::Queue> autosave_queue;
    // Replaced on document replacement; expired before waiting for workers.
    std::shared_ptr<int> autosave_lifetime = std::make_shared<int>(0);

	/// A new file has been opened (filename)
	agi::signal::Signal<agi::fs::path> FileOpen;
	/// The file has been saved
	agi::signal::Signal<> FileSave;

	/// The filename of the currently open file, if any
	agi::fs::path filename;

	/// Set the filename, updating things like the MRU and last used path
	void SetFileName(agi::fs::path const& file);

    /// Peripheral work that belongs to a document installation but runs only
    /// after it is Committed (never part of publication or its authority).
    /// Kept until the installation commits so an explicit retry runs it once.
    struct PostCommitPlan {
        enum class Kind { LoadedFile, ClosedFile } kind;
        agi::fs::path path;
    };
    std::optional<PostCommitPlan> post_commit_plan;
    std::vector<std::string> peripheral_errors;
    /// Run the stored plan exactly once. Each step is isolated: a failure is
    /// recorded in peripheral_errors and later steps still run.
    void RunPostCommitPlan();
    /// Install a prepared document candidate and run its post-commit plan.
    void InstallDocument(SubtitleModelCandidate&& candidate, PostCommitPlan plan);

	/// Autosave the file if there have been any chances since the last autosave

	void OnCommit(AssFileCommit c);
	void OnActiveLineChanged();
	void OnSelectionChanged();
	void OnTextSelectionChanged();

public:
    SubtitleModelInstallation::State InstallationStatus() const;
    bool IsInstalling() const;
    std::vector<std::string> PendingInstallationObservers() const;
    void CancelModelInstallation();
    bool RetryModelInstallation();
    /// Errors from the post-commit work of the most recent Committed document
    /// installation (Load, Close or a successful retry). Non-empty means the
    /// document WAS installed but MRU/backup/FileOpen follow-up work failed.
    std::vector<std::string> const& LastPeripheralErrors() const { return peripheral_errors; }
    bool DeferHostClose(wxCloseEvent&);
	SubsController(agi::Context *context);
	~SubsController();

	/// Set the selection controller to use
	///
	/// Required due to that the selection controller is the subtitles grid, and
	/// so is created long after the subtitles controller
	void SetSelectionController(SelectionController *selection_controller);

	/// The file's path and filename if any, or platform-appropriate "untitled"
	agi::fs::path Filename() const;

	/// Does the file have unsaved changes?
	bool IsModified() const;

	/// @brief Load from a file
	/// @param file File name
	/// @param charset Character set of file
	ProjectProperties Load(agi::fs::path const& file, const char *charset);

	/// @brief Save to a file
	/// @param file Path to save to
	/// @param encoding Encoding to use, or empty to let the writer decide (which usually means "App/Save Charset")
	void Save(agi::fs::path const& file, const char *encoding="");
    void RequestAutoSave();

	/// Close the currently open file (i.e. open a new blank file)
	void Close();

	/// If there are unsaved changes, asl the user if they want to save them
	/// @param allow_cancel Let the user cancel the closing
	/// @return wxYES, wxNO or wxCANCEL (note: all three are true in a boolean context)
	int TryToClose(bool allow_cancel = true) const;

	/// Can the file be saved in its current format?
	bool CanSave() const;

	/// The file is about to be saved
	/// This signal is intended for adding metadata which is awkward or
	/// expensive to always keep up to date
	agi::signal::Signal<> UpdateProperties;

	DEFINE_SIGNAL_ADDERS(FileOpen, AddFileOpenListener)
	DEFINE_SIGNAL_ADDERS(FileSave, AddFileSaveListener)

    /// C1-C1c-9 Lua macro transaction (host thread only). Begin snapshots the
    /// document into a private work model and rejects a run while an installation,
    /// recovery, comparison or other writer is active. Prepare validates a result
    /// against the start revision and builds the complete model and history
    /// without touching the live document; Publish installs it in one step on the
    /// same document. Rejections throw LuaSubtitleRejected and change nothing.
    LuaSubtitleTransaction BeginLuaTransaction();
    LuaSubtitlePublication PrepareLuaPublication(LuaSubtitleTransaction&&, LuaSubtitleResult&&);
    void PublishLua(LuaSubtitlePublication&&, std::set<int> selection, int active);

	/// @brief Undo the last set of changes to the file
	void Undo();
	/// @brief Redo the last undone changes
	void Redo();
	/// Check if undo stack is empty
	bool IsUndoStackEmpty() const { return undo_stack.size() <= 1; };
	/// Check if redo stack is empty
	bool IsRedoStackEmpty() const { return redo_stack.empty(); };
	/// Get the description of the first undoable change
	wxString GetUndoDescription() const;
	/// Get the description of the first redoable change
	wxString GetRedoDescription() const;
};

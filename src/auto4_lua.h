// Copyright (c) 2006, Niels Martin Hansen
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#pragma once

#include "auto4_base.h"
#include "subtitle_lua_transaction.h"

#include <deque>
#include <vector>
#include <wx/string.h>

class AssEntry;
class wxControl;
class wxWindow;
struct lua_State;

namespace Automation4 {
	/// @class LuaAssFile
	/// @brief Object wrapping an AssFile object for modification through Lua
	class LuaAssFile {
		struct PendingCommit {
			wxString mesage;
			int modification_type;
			std::vector<AssEntry const*> lines;
		};

		/// File the script reads. For macros this is the run's private work
		/// model; for validation/config callbacks it is the live document,
		/// read synchronously on the host thread and never written.
		AssFile const *ass;
		/// File receiving new extradata and, for export filters, the result.
		/// nullptr in read-only contexts.
		AssFile *target;

		/// Lua state the object exists in
		lua_State *L;

		/// Is the feature this object is created for read-only?
		bool can_modify;
		/// Is the feature allowed to set undo points?
		bool can_set_undo;
		/// throws an error if modification is disallowed
		void CheckAllowModify();
		/// throws an error if the line index is out of bounds
		void CheckBounds(int idx);

		/// How ass file been modified by the script since the last commit
		int modification_type = 0;

		/// Reference count used to avoid deleting this until both lua and the
		/// calling C++ code are done with it
		int references = 2;

		/// Set of subtitle lines being modified; initially a shallow copy of ass->Line.
		/// Lines are never modified in place: every write creates a new entry.
		std::vector<AssEntry const*> lines;
		bool script_info_copied = false;

		/// Commits to apply once processing completes successfully
		std::deque<PendingCommit> pending_commits;
		/// Script Info entries created by the script (copies and new entries)
		std::vector<std::unique_ptr<AssEntry>> info_entries;
		/// Styles and dialogue lines allocated here; owned until handed over or cancelled
		std::vector<AssEntry *> allocated_lines;

		/// Mark the object as finished on the C++ side
		void Expire();

		/// Create copies of all of the lines in the script info section if it
		/// hasn't already happened. This is done lazily, since it only needs
		/// to happen when the user modifies the headers in some way, which
		/// most runs of a script will not do.
		void InitScriptInfoIfNeeded();
		/// Add the line at the given index to the list of lines to be deleted
		/// when the script completes, unless it's an AssInfo, since those are
		/// owned by the container.
		void QueueLineForDeletion(size_t idx);
		/// Set the line at the index to the given value
		void AssignLine(size_t idx, std::unique_ptr<AssEntry> e);
		void InsertLine(std::vector<AssEntry const*> &vec, size_t idx, std::unique_ptr<AssEntry> e);
		/// Take ownership of a newly created entry and return the pointer to store in lines
		AssEntry const* Adopt(std::unique_ptr<AssEntry> e);

		int ObjectIndexRead(lua_State *L);
		void ObjectIndexWrite(lua_State *L);
		int ObjectGetLen(lua_State *L);
		void ObjectDelete(lua_State *L);
		void ObjectDeleteRange(lua_State *L);
		void ObjectAppend(lua_State *L);
		void ObjectInsert(lua_State *L);
		void ObjectGarbageCollect(lua_State *L);
		int ObjectIPairs(lua_State *L);
		int IterNext(lua_State *L);

		int LuaParseKaraokeData(lua_State *L);
		int LuaGetScriptResolution(lua_State *L);

		void LuaSetUndoPoint(lua_State *L);

		// LuaAssFile can only be deleted by the reference count hitting zero
		~LuaAssFile();
	public:
		static LuaAssFile *GetObjPointer(lua_State *L, int idx, bool allow_expired);

		/// makes a Lua representation of AssEntry and places on the top of the stack
		void AssEntryToLua(lua_State *L, size_t idx);
		/// assumes a Lua representation of AssEntry on the top of the stack, and creates an AssEntry object of it
		static std::unique_ptr<AssEntry> LuaToAssEntry(lua_State *L, AssFile *ass=nullptr);

		std::unique_ptr<AssEntry> LuaToTrackedAssEntry(lua_State *L);

		/// @brief Signal that a read-only or export filter call is done. For an
		///        export filter the changes are applied to its own detached file.
		void ProcessingComplete();

		/// @brief Signal that a macro is done: hand every undo point, the final
		///        line list and all created entries to the host for validation and
		///        publication. Nothing is written to any document here.
		/// @param undo_description Description for changes after the last undo point
		LuaSubtitleResult TakeMacroResult(wxString const& undo_description);

		/// End processing without applying any changes made
		void Cancel();

		/// Constructor
		/// @param L lua state
		/// @param source File to read
		/// @param target File receiving writes (nullptr for read-only use)
		/// @param can_modify Is modifying the file allowed?
		/// @param can_set_undo Is setting undo points allowed?
		LuaAssFile(lua_State *L, AssFile const& source, AssFile *target = nullptr, bool can_modify = false, bool can_set_undo = false);
	};

	class LuaProgressSink {
		lua_State *L;

		static int LuaSetProgress(lua_State *L);
		static int LuaSetTask(lua_State *L);
		static int LuaSetTitle(lua_State *L);
		static int LuaGetCancelled(lua_State *L);
		static int LuaDebugOut(lua_State *L);
		static int LuaDisplayDialog(lua_State *L);
		static int LuaDisplayOpenDialog(lua_State *L);
		static int LuaDisplaySaveDialog(lua_State *L);

	public:
		LuaProgressSink(lua_State *L, ProgressSink *ps, bool allow_config_dialog = true);
		~LuaProgressSink();

		static ProgressSink* GetObjPointer(lua_State *L, int idx);
	};

	/// Base class for controls in dialogs
	class LuaDialogControl {
	public:
		/// Name of this control in the output table
		std::string name;

		/// Tooltip of this control
		std::string hint;

		int x, y, width, height;

		/// Create the associated wxControl
		virtual wxControl *Create(wxWindow *parent) = 0;

		/// Get the default flags to use when inserting this control into a sizer
		virtual int GetSizerFlags() const { return wxEXPAND; }

		/// Push the current value of the control onto the lua stack. Must not
		/// touch the GUI as this may be called on a background thread.
		virtual void LuaReadBack(lua_State *L) = 0;

		/// Does this control have any user-changeable data that can be serialized?
		virtual bool CanSerialiseValue() const { return false; }

		/// Serialize the control's current value so that it can be stored
		/// in the script
		virtual std::string SerialiseValue() const { return ""; }

		/// Restore the control's value from a saved value in the script
		virtual void UnserialiseValue([[maybe_unused]] std::string_view serialised) { }

		LuaDialogControl(lua_State *L);

		/// Virtual destructor so this can safely be inherited from
		virtual ~LuaDialogControl() = default;
	};

	/// A lua-generated dialog or panel in the export options dialog
	class LuaDialog final : public ScriptDialog {
		/// Controls in this dialog
		std::vector<std::unique_ptr<LuaDialogControl>> controls;
		/// The names and IDs of buttons in this dialog if non-default ones were used
		std::vector<std::pair<int, std::string>> buttons;

		/// Does the dialog contain any buttons
		bool use_buttons;

		/// ID of the button pushed (once a button has been pushed)
		int button_pushed = -1;

		wxWindow *window = nullptr;

	public:
		LuaDialog(lua_State *L, bool include_buttons);

		/// Push the values of the controls in this dialog onto the lua stack
		/// in a single table
		int LuaReadBack(lua_State *L);

		// ScriptDialog implementation
		wxWindow* CreateWindow(wxWindow *parent) override;
		std::string Serialise() override;
		void Unserialise(const std::string &serialised) override;
	};
}

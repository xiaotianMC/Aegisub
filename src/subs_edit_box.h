// Copyright (c) 2005, Rodrigo Braz Monteiro
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
#include <memory>

#include "tooltip_binding.h"

#include <array>
#include <boost/container/map.hpp>
#include <boost/flyweight/flyweight_fwd.hpp>
#include <list>
#include <map>
#include <vector>

#include <wx/combobox.h>
#include <wx/panel.h>
#include <wx/arrstr.h>
#include <wx/timer.h>

#include <libaegisub/signal.h>

namespace agi::vfr { class Framerate; }
namespace agi { struct Context; }
namespace agi { class Time; }
class AssDialogue;
class AssStyle;
class SubsTextEditCtrl;
class TimeEdit;
class FlatButton;
class wxCheckBox;
class TimeUnitToggle;
class wxSizer;
class wxStaticText;
class FlatBadge;
class FlatCheck;
class FlatChoice;
class FlatFrame;
class wxStyledTextCtrl;
class wxStyledTextEvent;
class wxTextCtrl;
struct AssDialogueBase;


/// @brief Main subtitle edit box
///
/// Controls the text edit and all surrounding controls
class SubsEditBox final : public wxPanel {
protected:
    // Declared first: expires only after all owned bindings/cache members are destroyed.
    std::shared_ptr<int> model_observer_lifetime = std::make_shared<int>(0);
private:
	enum TimeField {
		TIME_START = 0,
		TIME_END,
		TIME_DURATION
	};

	std::vector<agi::signal::Connection> connections;
	std::list<ToolTipBinding> tool_tip_bindings;

	/// Currently active dialogue line
	int active_id = 0;
    AssDialogue const* CurrentLine() const;
	/// Are the controls currently enabled?
	bool controls_enabled = true;

	agi::Context *c;

	agi::signal::Connection file_changed_slot;

	// Box controls
	FlatCheck *comment_box;
	FlatChoice *style_box;
	wxTextCtrl *actor_box;
	FlatFrame *actor_frame;
	TimeEdit *start_time;
	TimeEdit *end_time;
	TimeEdit *duration;
	wxTextCtrl *layer;
	FlatFrame *layer_frame;
	wxTextCtrl *effect_box;
	FlatFrame *effect_frame;
	TimeUnitToggle *time_unit;
	FlatBadge *char_count;
	FlatCheck *split_box;
	wxStaticText *original_caption;

	/// The actors and effects in the file, to choose from
	wxArrayString actor_values, effect_values;
	/// The selected lines' layers when a drag of the layer label began
	std::map<int, int> layer_base;
	bool scrubbing_layer = false;
	/// When the wheel or a key last stepped the style or layer: steps close
	/// together are one undo step
	wxLongLong last_step = 0;

	wxSizer *fields_sizer; ///< The two rows of fields, above or below the text
	wxSizer *top_sizer;
	wxSizer *middle_left_sizer;
	wxSizer *original_sizer;
	wxSizer *bottom_sizer;

	void SetControlsState(bool state);
	/// @brief Update times of selected lines
	/// @param field Field which changed
	void CommitTimes(TimeField field);
	/// @brief Commits the current edit box contents
	/// @param desc Undo description to use
	void CommitText(wxString const& desc);
	void Commit(wxString const& desc, int type, bool amend, AssDialogue const* line);

	/// Last commit ID for undo coalescing
	int commit_id = -1;

	/// Last used commit message to avoid coalescing different types of changes
	wxString last_commit_type;

	/// Last field to get a time commit, as they all have the same commit message
	int last_time_commit_type;

	/// Timer to stop coalescing changes after a break with no edits
	wxTimer undo_timer;

	/// The start and end times of the selected lines without changes made to
	/// avoid negative durations, so that they can be restored if future changes
	/// eliminate the negative durations
	boost::container::map<int, std::pair<agi::Time, agi::Time>> initial_times;

	// Constructor helpers
	TimeEdit *MakeTimeCtrl(wxString const& label, wxString const& tooltip, TimeField field);
	/// A text field in a flat frame whose chevron lists @p values
	/// @p scroll_effects: the list also offers the Banner / Scroll form
	wxTextCtrl *MakeTextField(FlatFrame *&frame, wxString const& hint, wxString const& tooltip, int width, wxArrayString &values, bool scroll_effects = false);
	FlatButton *MakeBottomButton(const char *cmd_name);

	void OnChange(wxStyledTextEvent &event);
	void OnKeyDown(wxKeyEvent &event);

	void OnActiveLineChanged();
	void OnSelectedSetChanged();
	void OnLineInitialTextChanged(std::string const& new_text);

	void OnTimeUnitChange(bool by_frame);
	/// Put the fields above or below the text, as the option says
	void ArrangeRows();
	agi::signal::Connection rows_option;
	void UpdateStyleBox();
	void OnStyleChange();
	void OnActorChange(wxCommandEvent &event);
	void OnCommentChange(wxCommandEvent &);
	void OnEffectChange(wxCommandEvent &);

	/// Show the selected lines' layer, or "—" when they differ
	void UpdateLayerField();
	/// Give the selected lines the typed layer
	void CommitLayerText();
	/// Move each selected line @p delta layers from its own (or from where
	/// it was when a drag began)
	void ShiftLayers(int delta, bool amend);
	void StepLayer(int delta);
	void ScrubLayer(int steps, bool final);
	/// Keep the character count in the text box's corner
	void PlaceCharacterCount();
	/// Leave out the labels of the times when the row is too narrow for them
	void FitTimeLabels();
	void OnSplit(wxCommandEvent&);
	void DoOnSplit(bool show_original);

	/// @brief Set a field in each selected line to a specified value
	/// @param set   Callable which updates a passed line
	/// @param desc  Undo description to use
	/// @param type  Commit type to use
	/// @param amend Coalesce sequences of commits of the same type
	template<class setter>
	void SetSelectedRows(setter set, wxString const& desc, int type, bool amend = false);

	/// @brief Set a field in each selected line to a specified value
	/// @param field Field to set
	/// @param value Value to set the field to
	/// @param desc  Undo description to use
	/// @param type  Commit type to use
	/// @param amend Coalesce sequences of commits of the same type
	template<class T>
	void SetSelectedRows(T AssDialogueBase::*field, T value, wxString const& desc, int type, bool amend = false);

	template<class T>
	void SetSelectedRows(T AssDialogueBase::*field, wxString const& value, wxString const& desc, int type, bool amend = false);

	/// @brief Reload the current line from the file
	/// @param type AssFile::COMMITType
	void OnCommit(int type);

	void UpdateFields(int type, bool repopulate_lists);

	/// Regenerate a dropdown list with the unique values of a dialogue field
	void PopulateList(wxTextCtrl *ctrl, wxArrayString &values, boost::flyweight<std::string> AssDialogue::*field);

	/// @brief Enable or disable frame timing mode
	void UpdateFrameTiming(agi::vfr::Framerate const& fps);

	/// Update the character count box for the given text
	void UpdateCharacterCount(std::string const& text);

	/// Call a command the restore focus to the edit box
	void CallCommand(const char *cmd_name);

	void SetDurationField();

	SubsTextEditCtrl *edit_ctrl;
	wxTextCtrl *secondary_editor;

public:
	/// @brief Constructor
	/// @param parent Parent window
	SubsEditBox(wxWindow *parent, agi::Context *context);
	~SubsEditBox();

	/// Put the keyboard in the style box, where typing a style's first
	/// letters chooses it for the selected lines
	void FocusStyle();
	/// Ask for the active line's Banner or Scroll effect, as the Effect
	/// field's list offers, and set it for the selected lines
	void EditScrollEffect();
};

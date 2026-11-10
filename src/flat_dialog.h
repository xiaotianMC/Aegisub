// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// The foot of a dialog drawn as the flat controls are (docs/dialogs.md):
// FlatButtons in place of the system's, the one the dialog is for filled
// with the accent colour, and the keys the system's buttons answer to.
#pragma once

#include <wx/sizer.h>

#include <functional>
#include <optional>
#include <vector>

class FlatButton;
class FlatFrame;
class wxDialog;
class wxTextCtrl;

namespace flat {

/// The buttons at the foot of a dialog: Help and any others at the left,
/// the standard ones at the right in the system's order. They have the
/// standard ids, so a dialog's handlers and wxDialog's own (OK accepts,
/// Apply transfers, Cancel ends) work as with the system's buttons.
///
/// The dialog's keys are kept as the system's buttons have them: Escape
/// and the title bar's close button press Cancel (or the escape id's
/// button), Enter in a one-line field presses the affirmative button,
/// and Alt with a button's underlined letter presses it.
class DialogButtons final : public wxBoxSizer {
	wxDialog *dialog;
	wxBoxSizer *left, *right;
	std::vector<FlatButton*> buttons;

	FlatButton *Make(int id, wxString const& label, bool accent);

public:
	/// @p flags is wxOK, wxCANCEL, wxAPPLY, wxCLOSE, wxYES, wxNO and wxHELP
	/// as for CreateStdDialogButtonSizer; with @p help_page, Help opens it
	DialogButtons(wxDialog *dialog, long flags, const char *help_page = nullptr);

	/// The button with @p id, if there is one
	FlatButton *Get(int id) const;
	/// Another button, after Help at the left
	FlatButton *AddLeft(wxString const& label, int id = wxID_ANY);
	/// Another button, before the standard ones at the right
	FlatButton *AddRight(wxString const& label, int id = wxID_ANY, bool accent = false);
	std::vector<FlatButton*> const& Buttons() const { return buttons; }
};

/// A number in a FlatFrame, its name inside, in place of a spin control:
/// typed (a value out of range is not taken, and leaving the field shows
/// the value taken), stepped with the wheel, or dragged by its name. It
/// lives as long as the handlers bound to its windows.
class NumberField final {
	FlatFrame *frame;
	wxTextCtrl *text;
	double value, min, max, step;
	int precision;
	std::optional<double> scrub_start;

	NumberField(wxWindow *parent, wxString const& label, double value, double min, double max, double step, int precision, int width);
	void Take(double v, bool show);
	void ShowTaken();

public:
	/// Made for @p parent, going with its windows; @p precision is the decimals kept
	static NumberField *Create(wxWindow *parent, wxString const& label, double value, double min, double max,
	                           double step = 1, int precision = 0, int width = 44);

	/// Called with each value taken from the user
	std::function<void(double)> on_change;

	FlatFrame *Frame() const { return frame; }
	wxTextCtrl *Text() const { return text; }
	double GetValue() const { return value; }
	int GetInt() const { return static_cast<int>(value); }
	/// Show @p v, without calling on_change
	void SetValue(double v);
	void SetRange(double new_min, double new_max);
	void SetToolTip(wxString const& tip);
	bool Enable(bool enable = true);
};

/// @p v with up to @p precision decimals, no trailing zeros and no "-0"
wxString FormatNumber(double v, int precision);

/// Answer @p dialog's keys for the FlatButtons in it, as DialogButtons does;
/// for a dialog laying its buttons out itself
void UseFlatButtonKeys(wxDialog *dialog);

/// Alt with @p key in @p window, a page laying out flat controls rather than
/// a dialog: press the button, tick the check box or choose the switch's
/// option underlining it. False if none does.
bool PressMnemonic(wxWindow *window, int key);

}

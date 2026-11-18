// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Toolbars the keyboard goes into (docs/keyboard-regions.md): each is one
// Tab stop in its region, the arrows go along its buttons, Space and Enter
// press the one the keyboard is on, Alt+Down or F4 open its menu, and the
// keys it has no use for are the region's own. A ring in the accent colour
// goes round that button while the bar has the keyboard.
#pragma once

#include <functional>
#include <map>
#include <vector>

#include <wx/toolbar.h>

class KeyToolBar : public wxToolBar {
public:
	/// @param keyboard Whether Tab stops at it: not the main window's own
	///                 toolbar, which is in no region
	KeyToolBar(wxWindow *parent, long style, bool keyboard = true);

	/// The button the keyboard is on: the one it was last put on, else
	/// the chosen one of a group (the visual tool in use), else the first;
	/// wxID_NONE when none can be pressed
	int KeyTool() const;
	/// Put the keyboard on button @p id
	void SetKeyTool(int id);
	/// The buttons the keyboard can be on, in order: those enabled, not
	/// the separators
	std::vector<int> KeyTools() const;
	/// Press button @p id as a click does: a check button flips, a radio
	/// button is chosen. @return Whether it was pressed
	bool PressTool(int id);
	/// Whether the button being pressed was pressed by a key, for the
	/// regions: a click gives the keyboard to the video, a key leaves it
	bool PressingByKeys() const { return pressing_by_keys; }
	/// Open the menu of button @p id: its drop-down one, or one given by
	/// SetMenuOpener. @return Whether it had one
	bool OpenMenu(int id);
	/// A button whose menu is not a drop-down one (the drawing shapes')
	void SetMenuOpener(int id, std::function<void()> open);
	void ClearMenuOpeners() { menu_openers.clear(); }
	/// A key pressed with the keyboard on the bar. @return Whether it was one of its own
	bool Key(int key, int modifiers);
	/// Where button @p id is in the bar, empty when it can't be told
	wxRect ToolRect(int id) const;
	/// Where the ring is drawn, empty while the bar hasn't the keyboard
	wxRect RingRect() const;

	/// A click doesn't give it the keyboard; Tab does, when it has a
	/// button to press
	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override;

#ifdef __WXMSW__
	void SetFocus() override;
	WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
	bool keyboard;
	int key_tool = wxID_NONE;
	bool pressing_by_keys = false;
	std::map<int, std::function<void()>> menu_openers;

	void OnKey(wxKeyEvent &event);
	void OnFocus(wxFocusEvent &event);
};

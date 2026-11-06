// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
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

/// @file toolbar.cpp
/// @brief Dynamic menu toolbar generator.
/// @ingroup toolbar menu

#include "include/aegisub/toolbar.h"

#include "command/command.h"
#include "compat.h"
#include "include/aegisub/hotkey.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "theme.h"
#include "include/aegisub/context.h"
#include "video_display.h"
#include "visual_tool_rectangle.h"

#include <libaegisub/hotkey.h>
#include <libaegisub/json.h>
#include <libaegisub/log.h>
#include <libaegisub/signal.h>
#include <libaegisub/string.h>

#include <boost/interprocess/streams/bufferstream.hpp>
#include <vector>

#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/timer.h>
#include <wx/toolbar.h>
#include <wx/utils.h>

namespace {
	json::Object const& get_root() {
		static json::Object root;
		if (root.empty()) {
			boost::interprocess::ibufferstream stream((const char *)default_toolbar, sizeof(default_toolbar));
			root = std::move(static_cast<json::Object&>(agi::json_util::parse(stream)));
		}
		return root;
	}

	class Toolbar final : public wxToolBar {
		/// Window ID of first toolbar control
		static const int TOOL_ID_BASE = 5000;

		/// Toolbar name in config file
		std::string name;
		/// Project context
		agi::Context *context;
		/// Commands for each of the buttons, and for the entries of their
		/// dropdown menus (ids from TOOL_ID_BASE in this order)
		std::vector<cmd::Command *> commands;
		/// Whether commands[i] is a button (otherwise only in a dropdown menu)
		std::vector<bool> on_bar;
		/// IsActive when a dynamic icon was last drawn, per command
		std::vector<bool> icon_state;
		/// Hotkey context
		std::string ht_context;

		/// Current icon size
		int icon_size;
		int shape_tool_id = wxID_NONE;
		DrawingShape drawing_shape = DrawingShape::Rectangle;
		wxTimer shape_timer{this};
		bool shape_pressed = false;
		wxPoint shape_menu_position;

		void SelectShape() {
			auto display = context->videoDisplay;
			if (!display || !cmd::get("video/tool/rectangle")->Validate(context)) return;
			if (!display->ToolIsType(typeid(VisualToolRectangle)))
				(*cmd::get("video/tool/rectangle"))(context);
			display->SetSubTool(static_cast<int>(drawing_shape));
			UpdateShapeButton();
		}

		void UpdateShapeButton() {
			if (shape_tool_id == wxID_NONE) return;
			SetToolNormalBitmap(shape_tool_id, DrawingShapeIcon(drawing_shape, icon_size));
			SetToolShortHelp(shape_tool_id, DrawingShapeLabel(drawing_shape) + " — " +
				_("Hold left mouse button for shapes; Shift constrains proportions"));
		}

		void ShowShapeMenu() {
			shape_timer.Stop();
			shape_pressed = false;
			if (HasCapture()) ReleaseMouse();
			wxMenu menu;
			for (int i = 0; i <= static_cast<int>(DrawingShape::Polygon); ++i) {
				auto shape = static_cast<DrawingShape>(i);
				auto item = menu.AppendCheckItem(wxID_HIGHEST + 200 + i, DrawingShapeLabel(shape));
				item->Check(shape == drawing_shape);
				item->SetBitmap(DrawingShapeIcon(shape, icon_size));
			}
			menu.Bind(wxEVT_MENU, [this](wxCommandEvent& event) {
				drawing_shape = static_cast<DrawingShape>(event.GetId() - wxID_HIGHEST - 200);
				SelectShape();
			});
			PopupMenu(&menu, shape_menu_position);
		}

		void BindShapeEvents() {
			Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
				auto tool = FindToolForPosition(event.GetX(), event.GetY());
				if (!tool || tool->GetId() != shape_tool_id || !tool->IsEnabled()) {
					event.Skip();
					return;
				}
				shape_pressed = true;
				shape_menu_position = wxPoint(GetClientSize().x, event.GetY());
				CaptureMouse();
				shape_timer.StartOnce(400);
			});
			Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
				if (!shape_pressed) { event.Skip(); return; }
				shape_pressed = false;
				bool short_click = shape_timer.IsRunning();
				shape_timer.Stop();
				if (HasCapture()) ReleaseMouse();
				auto tool = FindToolForPosition(event.GetX(), event.GetY());
				if (short_click && tool && tool->GetId() == shape_tool_id) SelectShape();
			});
			Bind(wxEVT_TIMER, [this](wxTimerEvent&) { ShowShapeMenu(); }, shape_timer.GetId());
			Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
				shape_timer.Stop();
				shape_pressed = false;
			});
			// Also expose the flyout without a timed gesture.
			Bind(wxEVT_TOOL_RCLICKED, [this](wxCommandEvent& event) {
				if (event.GetId() != shape_tool_id) { event.Skip(); return; }
				shape_menu_position = wxPoint(GetClientSize().x, ScreenToClient(wxGetMousePosition()).y);
				ShowShapeMenu();
			});
		}

		/// Listener for icon size change signal
		agi::signal::Connection icon_size_slot;

		/// Listener for hotkey change signal
		agi::signal::Connection hotkeys_changed_slot;

		/// The main toolbar's layout choice, and the appearance (icons follow it)
		agi::signal::Connection classic_slot;
		agi::signal::Connection theme_slot;

		/// Layout in default_toolbar.json: the main toolbar has a classic one
		/// with every button.
		std::string LayoutName() const {
			if (name == "main" && OPT_GET("App/Classic Toolbar")->GetBool()) return "main/classic";
			return name;
		}

		cmd::Command *Find(std::string const& command_name) {
			try {
				return cmd::get(command_name);
			}
			catch (cmd::CommandNotFound const&) {
				LOG_W("toolbar/command/not_found") << "Command '" << command_name << "' not found; skipping";
				return nullptr;
			}
		}

		/// Before a dropdown menu opens: enable its entries as menus do.
		void OnDropdown(wxCommandEvent &evt) {
			evt.Skip();
			auto tool = FindById(evt.GetId());
			if (!tool || !tool->GetDropdownMenu()) return;
			for (auto item : tool->GetDropdownMenu()->GetMenuItems()) {
				size_t index = item->GetId() - TOOL_ID_BASE;
				if (index < commands.size()) item->Enable(commands[index]->Validate(context));
			}
		}

		/// Enable/disable the toolbar buttons
		void OnIdle(wxIdleEvent &) {
			if (shape_tool_id != wxID_NONE && context->videoDisplay &&
				context->videoDisplay->ToolIsType(typeid(VisualToolRectangle))) {
				auto shape = static_cast<DrawingShape>(context->videoDisplay->GetSubTool());
				if (shape != drawing_shape) { drawing_shape = shape; UpdateShapeButton(); }
			}
			for (size_t i = 0; i < commands.size(); ++i) {
				if (!on_bar[i]) continue;
				if (commands[i]->Type() & cmd::COMMAND_VALIDATE)
					EnableTool(TOOL_ID_BASE + i, commands[i]->Validate(context));
				if (commands[i]->Type() & cmd::COMMAND_TOGGLE || commands[i]->Type() & cmd::COMMAND_RADIO)
					ToggleTool(TOOL_ID_BASE + i, commands[i]->IsActive(context));
				if (commands[i]->Type() & cmd::COMMAND_DYNAMIC_ICON) {
					bool state = commands[i]->IsActive(context);
					if (state != icon_state[i]) {
						icon_state[i] = state;
						SetToolNormalBitmap(TOOL_ID_BASE + i, commands[i]->IconFor(context, icon_size, GetLayoutDirection()));
					}
				}
			}
		}

		/// Toolbar button click handler
		void OnClick(wxCommandEvent &evt) {
			if (evt.GetId() == shape_tool_id) { SelectShape(); return; }
			(*commands[evt.GetId() - TOOL_ID_BASE])(context);
		}

		/// Regenerate the toolbar when the icon size changes
		void OnIconSizeChange(agi::OptionValue const& opt) {
			icon_size = opt.GetInt();
			RegenerateToolbar();
		}

		/// Clear the toolbar and recreate it
		void RegenerateToolbar() {
			shape_timer.Stop();
			shape_pressed = false;
			if (HasCapture()) ReleaseMouse();
			shape_tool_id = wxID_NONE;
			Unbind(wxEVT_IDLE, &Toolbar::OnIdle, this);
			ClearTools();
			commands.clear();
			on_bar.clear();
			icon_state.clear();
			Populate();
		}

		/// Add a button for @p command; with @p more, a dropdown button whose
		/// menu holds @p command and @p more (a group, e.g. the tool dialogs).
		/// The button itself runs @p command.
		bool AddCommand(cmd::Command *command, std::vector<cmd::Command *> const& more = {}) {
			int flags = command->Type();
			wxItemKind kind =
				!more.empty() ? wxITEM_DROPDOWN :
				flags & cmd::COMMAND_RADIO ? wxITEM_RADIO :
				flags & cmd::COMMAND_TOGGLE ? wxITEM_CHECK :
				wxITEM_NORMAL;

			int id = TOOL_ID_BASE + commands.size();
			AddTool(id, command->StrDisplay(context), command->IconFor(context, icon_size, GetLayoutDirection()), command->GetTooltip(ht_context), kind);
			if (std::string_view(command->name()) == "video/tool/rectangle") {
				shape_tool_id = id;
				UpdateShapeButton();
			}
			commands.push_back(command);
			on_bar.push_back(true);
			icon_state.push_back(flags & cmd::COMMAND_DYNAMIC_ICON ? command->IsActive(context) : false);
			bool dynamic = flags != cmd::COMMAND_NORMAL;

			if (!more.empty()) {
				auto menu = new wxMenu;
				auto add = [&](cmd::Command *entry, int entry_id) {
					auto item = new wxMenuItem(menu, entry_id, entry->StrDisplay(context), entry->StrHelp());
					item->SetBitmap(entry->Icon(16, GetLayoutDirection()));
					menu->Append(item);
				};
				add(command, id);
				for (auto entry : more) {
					add(entry, TOOL_ID_BASE + commands.size());
					commands.push_back(entry);
					on_bar.push_back(false);
					icon_state.push_back(false);
				}
				SetDropdownMenu(id, menu);
			}
			return dynamic;
		}

		/// Populate the toolbar with buttons
		void Populate() {
			json::Object const& root = get_root();
			auto root_it = root.find(LayoutName());
			if (root_it == root.end()) {
				// Toolbar names are all hardcoded so this should never happen
				throw agi::InternalError("Toolbar named " + LayoutName() + " not found.");
			}

			json::Array const& arr = root_it->second;
			commands.reserve(arr.size());
			bool needs_onidle = false;
			bool last_was_sep = false;

			for (json::UnknownElement const& entry : arr) {
				// A string names a command ("" a separator); a list is a
				// dropdown button for its first command with all of them in its menu.
				std::vector<cmd::Command *> group;
				try {
					json::String const& command_name = entry;
					if (command_name.empty()) {
						if (!last_was_sep)
							AddSeparator();
						last_was_sep = true;
						continue;
					}
					if (auto command = Find(command_name)) group.push_back(command);
				}
				catch (json::Exception const&) {
					for (json::String const& command_name : static_cast<json::Array const&>(entry))
						if (auto command = Find(command_name)) group.push_back(command);
				}
				if (group.empty()) continue;

				last_was_sep = false;
				auto first = group.front();
				group.erase(group.begin());
				needs_onidle = AddCommand(first, group) || needs_onidle;
			}

			// Only bind the update function if there are actually any dynamic tools
			if (needs_onidle) {
				Bind(wxEVT_IDLE, &Toolbar::OnIdle, this);
			}

			Realize();
		}

		void Connect() {
			classic_slot = OPT_SUB("App/Classic Toolbar", [this] { if (name == "main") RegenerateToolbar(); });
			theme_slot = theme::AddChangeListener([this] { RegenerateToolbar(); });
			Bind(wxEVT_TOOL_DROPDOWN, &Toolbar::OnDropdown, this);
		}

	public:
		Toolbar(wxWindow *parent, std::string name, agi::Context *c, std::string ht_context, bool vertical)
		: wxToolBar(parent, -1, wxDefaultPosition, wxDefaultSize, wxTB_NODIVIDER | wxTB_FLAT | (vertical ? wxTB_VERTICAL : wxTB_HORIZONTAL))
		, name(std::move(name))
		, context(c)
		, ht_context(std::move(ht_context))
		, icon_size(OPT_GET("App/Toolbar Icon Size")->GetInt())
		, icon_size_slot(OPT_SUB("App/Toolbar Icon Size", &Toolbar::OnIconSizeChange, this))
		, hotkeys_changed_slot(hotkey::inst->AddHotkeyChangeListener(&Toolbar::RegenerateToolbar, this))
		{
			Populate();
			Connect();
			BindShapeEvents();
			Bind(wxEVT_TOOL, &Toolbar::OnClick, this);
			Bind(wxEVT_DPI_CHANGED, [this] (wxDPIChangedEvent &e) { RegenerateToolbar(); e.Skip(); });
		}

		Toolbar(wxFrame *parent, std::string name, agi::Context *c, std::string ht_context)
		: wxToolBar(parent, -1, wxDefaultPosition, wxDefaultSize, wxTB_FLAT | wxTB_HORIZONTAL)
		, name(std::move(name))
		, context(c)
		, ht_context(std::move(ht_context))
#ifndef __WXMAC__
		, icon_size(OPT_GET("App/Toolbar Icon Size")->GetInt())
		, icon_size_slot(OPT_SUB("App/Toolbar Icon Size", &Toolbar::OnIconSizeChange, this))
#else
		, icon_size(32)
#endif
		, hotkeys_changed_slot(hotkey::inst->AddHotkeyChangeListener(&Toolbar::RegenerateToolbar, this))
		{
			parent->SetToolBar(this);
			Populate();
			Connect();
			BindShapeEvents();
			Bind(wxEVT_TOOL, &Toolbar::OnClick, this);
			Bind(wxEVT_DPI_CHANGED, [this] (wxDPIChangedEvent &e) { RegenerateToolbar(); e.Skip(); });
		}
	};
}

namespace toolbar {
	void AttachToolbar(wxFrame *frame, std::string const& name, agi::Context *c, std::string const& hotkey) {
		new Toolbar(frame, name, c, hotkey);
	}

	wxToolBar *GetToolbar(wxWindow *parent, std::string const& name, agi::Context *c, std::string const& hotkey, bool vertical) {
		return new Toolbar(parent, name, c, hotkey, vertical);
	}
}

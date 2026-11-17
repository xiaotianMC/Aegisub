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
//
// Aegisub Project http://www.aegisub.org/

#include "command.h"

#include "../include/aegisub/context.h"
#include "../libresrc/libresrc.h"
#include "../project.h"
#include "../dialog_rectangle.h"
#include "../video_display.h"
#include "../visual_tool_clip.h"
#include "../visual_tool_rectangle.h"
#include "../visual_tool_cross.h"
#include "../visual_tool_drag.h"
#include "../visual_tool_rotatexy.h"
#include "../visual_tool_rotatez.h"
#include "../visual_tool_scale.h"
#include "../visual_tool_scroll.h"
#include "../visual_tool_vector_clip.h"


namespace {
	using cmd::Command;

	template<class T>
	struct visual_tool_command : public Command {
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool Validate(const agi::Context *c) override {
			return !!c->project->VideoProvider();
		}

		bool IsActive(const agi::Context *c) override {
			// The preview pan mode takes the tool's place while it is on:
			// one button down, not two (docs/rectangle-drawing.md)
			return c->videoDisplay->ToolIsType(typeid(T)) && !c->videoDisplay->IsPanMode();
		}

		void operator()(agi::Context *c) override {
			c->videoDisplay->SetTool(std::make_unique<T>(c->videoDisplay, c));
		}
	};

	template<VisualToolVectorClipMode M>
	struct visual_tool_vclip_command : public Command {
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool Validate(const agi::Context *c) override {
			return !!c->project->VideoProvider();
		}

		bool IsActive(const agi::Context *c) override {
			return c->videoDisplay->ToolIsType(typeid(VisualToolVectorClip)) && c->videoDisplay->GetSubTool() == M;
		}

		void operator()(agi::Context *c) override {
			c->videoDisplay->SetTool(std::make_unique<VisualToolVectorClip>(c->videoDisplay, c));
			c->videoDisplay->SetSubTool(M);
		}
	};

	struct visual_mode_cross final : public visual_tool_command<VisualToolCross> {
		CMD_NAME("video/tool/cross")
		CMD_ICON(visual_standard)
		STR_MENU("Standard")
		STR_DISP("Standard")
		STR_HELP("Standard mode, double click sets position")
	};

	struct visual_mode_drag final : public visual_tool_command<VisualToolDrag> {
		CMD_NAME("video/tool/drag")
		CMD_ICON(visual_move)
		STR_MENU("Drag")
		STR_DISP("Drag")
		STR_HELP("Drag subtitles")
	};

	struct visual_mode_rotate_z final : public visual_tool_command<VisualToolRotateZ> {
		CMD_NAME("video/tool/rotate/z")
		CMD_ICON(visual_rotatez)
		STR_MENU("Rotate Z")
		STR_DISP("Rotate Z")
		STR_HELP("Rotate subtitles on their Z axis")
	};

	struct visual_mode_rotate_xy final : public visual_tool_command<VisualToolRotateXY> {
		CMD_NAME("video/tool/rotate/xy")
		CMD_ICON(visual_rotatexy)
		STR_MENU("Rotate XY")
		STR_DISP("Rotate XY")
		STR_HELP("Rotate subtitles on their X and Y axes")
	};

	struct visual_mode_scale final : public visual_tool_command<VisualToolScale> {
		CMD_NAME("video/tool/scale")
		CMD_ICON(visual_scale)
		STR_MENU("Scale")
		STR_DISP("Scale")
		STR_HELP("Scale subtitles on X and Y axes")
	};

	struct visual_mode_rectangle final : public visual_tool_command<VisualToolRectangle> {
		CMD_NAME("video/tool/rectangle")
		CMD_ICON(visual_rectangle)
		STR_MENU("Draw Rectangle")
		STR_DISP("Draw Rectangle")
		STR_HELP("Draw shapes behind a visible subtitle; hold the toolbar button to choose a shape, Shift constrains proportions, Escape cancels")
	};

	struct edit_shape final : public Command {
		CMD_NAME("edit/line/rectangle")
		CMD_ICON(visual_rectangle)
		CMD_TYPE(COMMAND_VALIDATE)
		STR_MENU("Edit Shape")
		STR_DISP("Edit Shape")
		STR_HELP("Show the shape in the style panel: its size, and its position, fill and opacity in the sections below")
		bool Validate(const agi::Context *c) override { return CanEditDrawing(c); }
		void operator()(agi::Context *c) override { EditDrawing(c); }
	};

	struct video_pan_mode final : public Command {
		CMD_NAME("video/tool/pan")
		CMD_ICON(visual_pan)
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_TOGGLE)
		STR_MENU("Pan Preview")
		STR_DISP("Pan Preview")
		STR_HELP("Drag the enlarged preview to view other parts; turning it off restores the original size")
		bool Validate(const agi::Context *c) override { return !!c->project->VideoProvider(); }
		bool IsActive(const agi::Context *c) override { return c->videoDisplay->IsPanMode(); }
		void operator()(agi::Context *c) override { c->videoDisplay->TogglePanMode(); }
	};

	struct visual_mode_scroll final : public visual_tool_command<VisualToolScroll> {
		CMD_NAME("video/tool/scroll")
		CMD_ICON(visual_scroll)
		STR_MENU("Scroll Effect")
		STR_DISP("Scroll Effect")
		STR_HELP("Set where a line with a Banner or Scroll effect moves, and how fast: drag out an area, drag its edges or the dashed end, double-click to turn it round")
	};

	struct visual_mode_clip final : public visual_tool_command<VisualToolClip> {
		CMD_NAME("video/tool/clip")
		CMD_ICON(visual_clip)
		STR_MENU("Clip")
		STR_DISP("Clip")
		STR_HELP("Clip subtitles to a rectangle")
	};

	struct visual_mode_vector_clip final : public visual_tool_command<VisualToolVectorClip> {
		CMD_NAME("video/tool/vector_clip")
		CMD_ICON(visual_vector_clip)
		STR_MENU("Vector Clip")
		STR_DISP("Vector Clip")
		STR_HELP("Clip subtitles to a vectorial area")
	};

	// Vector clip tools

	struct visual_mode_vclip_drag final : public visual_tool_vclip_command<VCLIP_DRAG> {
		CMD_NAME("video/tool/vclip/drag")
		CMD_ICON(visual_vector_clip_drag)
		STR_MENU("Drag")
		STR_DISP("Drag")
		STR_HELP("Drag control points")
	};

	struct visual_mode_vclip_line final : public visual_tool_vclip_command<VCLIP_LINE> {
		CMD_NAME("video/tool/vclip/line")
		CMD_ICON(visual_vector_clip_line)
		STR_MENU("Line")
		STR_DISP("Line")
		STR_HELP("Append a line")
	};
	struct visual_mode_vclip_bicubic final : public visual_tool_vclip_command<VCLIP_BICUBIC> {
		CMD_NAME("video/tool/vclip/bicubic")
		CMD_ICON(visual_vector_clip_bicubic)
		STR_MENU("Bicubic")
		STR_DISP("Bicubic")
		STR_HELP("Append a bezier bicubic curve")
	};
	struct visual_mode_vclip_convert final : public visual_tool_vclip_command<VCLIP_CONVERT> {
		CMD_NAME("video/tool/vclip/convert")
		CMD_ICON(visual_vector_clip_convert)
		STR_MENU("Convert")
		STR_DISP("Convert")
		STR_HELP("Convert a segment between line and bicubic")
	};
	struct visual_mode_vclip_insert final : public visual_tool_vclip_command<VCLIP_INSERT> {
		CMD_NAME("video/tool/vclip/insert")
		CMD_ICON(visual_vector_clip_insert)
		STR_MENU("Insert")
		STR_DISP("Insert")
		STR_HELP("Insert a control point")
	};
	struct visual_mode_vclip_remove final : public visual_tool_vclip_command<VCLIP_REMOVE> {
		CMD_NAME("video/tool/vclip/remove")
		CMD_ICON(visual_vector_clip_remove)
		STR_MENU("Remove")
		STR_DISP("Remove")
		STR_HELP("Remove a control point")
	};
	struct visual_mode_vclip_freehand final : public visual_tool_vclip_command<VCLIP_FREEHAND> {
		CMD_NAME("video/tool/vclip/freehand")
		CMD_ICON(visual_vector_clip_freehand)
		STR_MENU("Freehand")
		STR_DISP("Freehand")
		STR_HELP("Draw a freehand shape")
	};
	struct visual_mode_vclip_freehand_smooth final : public visual_tool_vclip_command<VCLIP_FREEHAND_SMOOTH> {
		CMD_NAME("video/tool/vclip/freehand_smooth")
		CMD_ICON(visual_vector_clip_freehand_smooth)
		STR_MENU("Freehand smooth")
		STR_DISP("Freehand smooth")
		STR_HELP("Draw a smoothed freehand shape")
	};

	/// The arrows with Ctrl+Shift (a pixel) or Alt+Shift (ten) move what
	/// the visual tool has selected (docs/keyboard-regions.md)
	template<int DX, int DY>
	struct visual_nudge_command : public Command {
		CMD_TYPE(COMMAND_VALIDATE)

		bool Validate(const agi::Context *c) override {
			return c->project->VideoProvider() && c->videoDisplay;
		}

		void operator()(agi::Context *c) override {
			if (Validate(c)) c->videoDisplay->NudgeTool(Vector2D(DX, DY));
		}
	};

	struct visual_nudge_left final : public visual_nudge_command<-1, 0> {
		CMD_NAME("video/tool/nudge/left")
		STR_MENU("Nudge left")
		STR_DISP("Nudge left")
		STR_HELP("Move what the visual tool has selected a pixel of the script to the left: the selected lines in Standard, the selected points in the others")
	};
	struct visual_nudge_right final : public visual_nudge_command<1, 0> {
		CMD_NAME("video/tool/nudge/right")
		STR_MENU("Nudge right")
		STR_DISP("Nudge right")
		STR_HELP("Move what the visual tool has selected a pixel of the script to the right: the selected lines in Standard, the selected points in the others")
	};
	struct visual_nudge_up final : public visual_nudge_command<0, -1> {
		CMD_NAME("video/tool/nudge/up")
		STR_MENU("Nudge up")
		STR_DISP("Nudge up")
		STR_HELP("Move what the visual tool has selected a pixel of the script up: the selected lines in Standard, the selected points in the others")
	};
	struct visual_nudge_down final : public visual_nudge_command<0, 1> {
		CMD_NAME("video/tool/nudge/down")
		STR_MENU("Nudge down")
		STR_DISP("Nudge down")
		STR_HELP("Move what the visual tool has selected a pixel of the script down: the selected lines in Standard, the selected points in the others")
	};
	struct visual_nudge_left_large final : public visual_nudge_command<-10, 0> {
		CMD_NAME("video/tool/nudge/left/large")
		STR_MENU("Nudge left by 10")
		STR_DISP("Nudge left by 10")
		STR_HELP("Move what the visual tool has selected ten pixels of the script to the left")
	};
	struct visual_nudge_right_large final : public visual_nudge_command<10, 0> {
		CMD_NAME("video/tool/nudge/right/large")
		STR_MENU("Nudge right by 10")
		STR_DISP("Nudge right by 10")
		STR_HELP("Move what the visual tool has selected ten pixels of the script to the right")
	};
	struct visual_nudge_up_large final : public visual_nudge_command<0, -10> {
		CMD_NAME("video/tool/nudge/up/large")
		STR_MENU("Nudge up by 10")
		STR_DISP("Nudge up by 10")
		STR_HELP("Move what the visual tool has selected ten pixels of the script up")
	};
	struct visual_nudge_down_large final : public visual_nudge_command<0, 10> {
		CMD_NAME("video/tool/nudge/down/large")
		STR_MENU("Nudge down by 10")
		STR_DISP("Nudge down by 10")
		STR_HELP("Move what the visual tool has selected ten pixels of the script down")
	};
}

namespace cmd {
	void init_visual_tools() {
		reg(std::make_unique<visual_mode_cross>());
		reg(std::make_unique<visual_mode_drag>());
		reg(std::make_unique<visual_mode_rotate_z>());
		reg(std::make_unique<visual_mode_rotate_xy>());
		reg(std::make_unique<visual_mode_scale>());
		reg(std::make_unique<visual_mode_rectangle>());
		reg(std::make_unique<edit_shape>());
		reg(std::make_unique<video_pan_mode>());
		reg(std::make_unique<visual_mode_scroll>());
		reg(std::make_unique<visual_mode_clip>());
		reg(std::make_unique<visual_mode_vector_clip>());

		reg(std::make_unique<visual_mode_vclip_drag>());
		reg(std::make_unique<visual_mode_vclip_line>());
		reg(std::make_unique<visual_mode_vclip_bicubic>());
		reg(std::make_unique<visual_mode_vclip_convert>());
		reg(std::make_unique<visual_mode_vclip_insert>());
		reg(std::make_unique<visual_mode_vclip_remove>());
		reg(std::make_unique<visual_mode_vclip_freehand>());
		reg(std::make_unique<visual_mode_vclip_freehand_smooth>());

		reg(std::make_unique<visual_nudge_left>());
		reg(std::make_unique<visual_nudge_right>());
		reg(std::make_unique<visual_nudge_up>());
		reg(std::make_unique<visual_nudge_down>());
		reg(std::make_unique<visual_nudge_left_large>());
		reg(std::make_unique<visual_nudge_right_large>());
		reg(std::make_unique<visual_nudge_up_large>());
		reg(std::make_unique<visual_nudge_down_large>());
	}
}

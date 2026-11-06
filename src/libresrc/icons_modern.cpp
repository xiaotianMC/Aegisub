// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
// Monochrome toolbar icons (docs/icons.md): SVG bodies on a 16×16 grid,
// stroked 1.5 wide with round ends; '@' is replaced by the icon colour.
// Parts drawn solid say fill='@' themselves.
#include "libresrc.h"

#include <algorithm>
#include <iterator>
#include <string_view>

namespace {
struct ModernIcon { std::string_view name; const char *body; };

// Sorted by name for the binary search below
const ModernIcon icons[] = {
	{"attach_button", "<path d='M11 5V11A3 3 0 0 1 5 11V4A2 2 0 0 1 9 4V10.5A1 1 0 0 1 7 10.5V5'/>"},
	{"automation_toolbutton", "<path d='M5 4.5L1.5 8L5 11.5M11 4.5L14.5 8L11 11.5M9.5 2.5L6.5 13.5'/>"},
	{"button_audio_commit", "<path d='M3 8.5L6.5 12L13 4.5'/>"},
	{"button_audio_goto", "<circle cx='8' cy='8' r='4.5'/><circle cx='8' cy='8' r='1.3' fill='@' stroke='none'/><path d='M8 1V3.5M8 12.5V15M1 8H3.5M12.5 8H15'/>"},
	{"button_leadin", "<path d='M9.5 8H1.5'/><path d='M4.0 5.5L1.5 8L4.0 10.5'/><path d='M14.0 3H11.5V13H14.0'/>"},
	{"button_leadout", "<path d='M2.0 3H4.5V13H2.0'/><path d='M6.5 8H14.5'/><path d='M12.0 5.5L14.5 8L12.0 10.5'/>"},
	{"button_next", "<path d='M12.5 3.5V12.5'/><path d='M3.5 3.5L10.5 8L3.5 12.5Z' fill='@'/>"},
	{"button_pause", "<rect x='3.5' y='2.5' width='3' height='11' rx='1' fill='@' stroke='none'/><rect x='9.5' y='2.5' width='3' height='11' rx='1' fill='@' stroke='none'/>"},
	{"button_play", "<path d='M4.5 2.5L13 8L4.5 13.5Z' fill='@'/>"},
	{"button_playfirstfiveh", "<path d='M3.8 3H2V13H3.8'/><path d='M5 5L9 8L5 11Z' fill='@'/><path d='M10 8H14'/><path d='M14 5.5V10.5'/>"},
	{"button_playfivehafter", "<path d='M1.7 3H3.5V13H1.7'/><path d='M5.5 5L9.5 8L5.5 11Z' fill='@'/><circle cx='11.7' cy='8' r='0.8' fill='@' stroke='none'/><circle cx='14.2' cy='8' r='0.8' fill='@' stroke='none'/>"},
	{"button_playfivehbefore", "<circle cx='1.8' cy='8' r='0.8' fill='@' stroke='none'/><circle cx='4.3' cy='8' r='0.8' fill='@' stroke='none'/><path d='M6.5 5L10.5 8L6.5 11Z' fill='@'/><path d='M14.3 3H12.5V13H14.3'/>"},
	{"button_playlastfiveh", "<path d='M2 5.5V10.5'/><path d='M2 8H6'/><path d='M7 5L11 8L7 11Z' fill='@'/><path d='M12.2 3H14V13H12.2'/>"},
	{"button_playline", "<path d='M5 3L11.5 7L5 11Z' fill='@'/><path d='M2.5 14H13.5'/>"},
	{"button_playsel", "<path d='M3.8 3H2V13H3.8'/><path d='M12.2 3H14V13H12.2'/><path d='M6 4.5L10.5 8L6 11.5Z' fill='@'/>"},
	{"button_playtoend", "<path d='M2 4.5L6.5 8L2 11.5Z' fill='@'/><path d='M8.5 8H14'/><path d='M11.5 5.5L14 8L11.5 10.5'/>"},
	{"button_prev", "<path d='M3.5 3.5V12.5'/><path d='M12.5 3.5L5.5 8L12.5 12.5Z' fill='@'/>"},
	{"button_stop", "<rect x='3.5' y='3.5' width='9' height='9' rx='1.5' fill='@'/>"},
	{"font_collector_button", "<path d='M1.5 3H9.5M5.5 3V13'/><path d='M12.5 4V11.5'/><path d='M10.5 9.5L12.5 11.5L14.5 9.5'/><path d='M10 14H15'/>"},
	{"jumpto_button", "<path d='M2 8H11'/><path d='M8.5 5.5L11 8L8.5 10.5'/><path d='M13.5 3V13'/>"},
	{"kara_mode", "<rect x='5.75' y='1.5' width='4.5' height='8' rx='2.25'/><path d='M3.5 7.5A4.5 4.5 0 0 0 12.5 7.5'/><path d='M8 12V14.5M5.5 14.5H10.5'/>"},
	{"kara_timing_copier", "<rect x='1.5' y='1.5' width='3.4' height='3.4' rx='.6'/><rect x='6.3' y='1.5' width='3.4' height='3.4' rx='.6'/><rect x='11.1' y='1.5' width='3.4' height='3.4' rx='.6'/><rect x='1.5' y='11.1' width='3.4' height='3.4' rx='.6'/><rect x='6.3' y='11.1' width='3.4' height='3.4' rx='.6'/><rect x='11.1' y='11.1' width='3.4' height='3.4' rx='.6'/><path d='M8 6.6V9.4M6.6 8.2L8 9.6L9.4 8.2'/>"},
	{"new_toolbutton", "<path d='M3.5 1.5H9.5L12.5 4.5V14.5H3.5Z'/><path d='M9.5 1.5V4.5H12.5'/>"},
	{"open_toolbutton", "<path d='M1.5 3H6L7.5 4.5H14.5V13H1.5Z'/><path d='M1.5 6.5H14.5'/>"},
	{"options_button", "<path d='M8.00 3.10 L9.29 1.53 L10.53 1.90 L10.72 3.93 L11.46 4.54 L13.49 4.33 L14.10 5.47 L12.81 7.04 L12.90 8.00 L14.47 9.29 L14.10 10.53 L12.07 10.72 L11.46 11.46 L11.67 13.49 L10.53 14.10 L8.96 12.81 L8.00 12.90 L6.71 14.47 L5.47 14.10 L5.28 12.07 L4.54 11.46 L2.51 11.67 L1.90 10.53 L3.19 8.96 L3.10 8.00 L1.53 6.71 L1.90 5.47 L3.93 5.28 L4.54 4.54 L4.33 2.51 L5.47 1.90 L7.04 3.19Z'/><circle cx='8' cy='8' r='2'/>"},
	{"properties_toolbutton", "<path d='M1.5 4.5H3.5M7.5 4.5H14.5M1.5 11.5H8.5M12.5 11.5H14.5'/><circle cx='5.5' cy='4.5' r='2'/><circle cx='10.5' cy='11.5' r='2'/>"},
	{"resample_toolbutton", "<path d='M1.5 5.5V1.5H5.5M10.5 1.5H14.5V5.5M14.5 10.5V14.5H10.5M5.5 14.5H1.5V10.5'/><rect x='5' y='5' width='6' height='6' rx='1'/>"},
	{"save_toolbutton", "<path d='M2.5 2.5H11L13.5 5V13.5H2.5Z'/><path d='M5 2.5V5.5H10V2.5'/><path d='M5 13.5V9.5H11V13.5'/>"},
	{"select_visible_button", "<path d='M1 8C3 4.2 13 4.2 15 8C13 11.8 3 11.8 1 8Z'/><circle cx='8' cy='8' r='2'/>"},
	{"shape_ellipse", "<ellipse cx='7.5' cy='7.5' rx='6' ry='5'/>"},
	{"shape_polygon", "<path d='M1.5 2.5H13.5L8.5 7.5L13.5 12.5H1.5Z'/>"},
	{"shape_rectangle", "<rect x='1.5' y='2.5' width='11.5' height='9.5' rx='1'/>"},
	{"shape_triangle", "<path d='M7.5 2L13.5 12.5H1.5Z'/>"},
	{"shift_times_toolbutton", "<rect x='4.5' y='2.5' width='7' height='4' rx='1'/><path d='M1.5 11H14.5M4 8.5L1.5 11L4 13.5M12 8.5L14.5 11L12 13.5'/>"},
	{"shift_to_frame", "<path d='M2.5 5V14.5'/><path d='M0.0 1H5.0V3.3L2.5 5.6L0.0 3.3Z' fill='@'/><rect x='5' y='6' width='9.5' height='5.5' rx='1'/><path d='M11.5 8.75H8M9.5 7.25L8 8.75L9.5 10.25'/>"},
	{"snap_subs_to_scene", "<path d='M1.5 2V14M14.5 2V14'/><rect x='4' y='5.5' width='8' height='5' rx='1'/><path d='M3 8H1.5M13 8H14.5'/>"},
	{"spellcheck_toolbutton", "<path d='M1.5 3.5H10.5M1.5 7H7'/><path d='M7 11L9.5 13.5L14.5 7.5'/>"},
	{"style_toolbutton", "<path d='M1.5 13.5L5 3L8.5 13.5M2.8 10H7.2'/><circle cx='12' cy='11' r='2.5'/><path d='M14.5 8V13.5'/>"},
	{"styling_toolbutton", "<path d='M14.5 1.5L8.5 7.5'/><path d='M8 6.5L9.5 8'/><path d='M7.5 8.5C5.5 8.3 4.3 9.8 4.3 11.4C4.3 12.8 3.4 13.6 1.8 13.8C4.2 15 8.6 14.6 9.5 10.5Z'/>"},
	{"subend_to_video", "<path d='M12 5V14.5'/><path d='M9.5 1H14.5V3.3L12 5.6L9.5 3.3Z' fill='@'/><path d='M12 7H6.5M12 14.5H6.5'/>"},
	{"substart_to_video", "<path d='M4 5V14.5'/><path d='M1.5 1H6.5V3.3L4 5.6L1.5 3.3Z' fill='@'/><path d='M4 7H9.5M4 14.5H9.5'/>"},
	{"timing_processor_toolbutton", "<circle cx='8' cy='9.5' r='5.25'/><path d='M8 9.5V6.5M6 1.5H10M8 1.5V4.2M12.3 4.3L13.5 3.1'/>"},
	{"toggle_audio_autocommit", "<path d='M13.5 8A5.5 5.5 0 1 1 11.9 4.1'/><path d='M12.2 1.5V4.4H9.3'/><path d='M5.5 8.2L7.3 10L10.5 6.3'/>"},
	{"toggle_audio_autoscroll", "<path d='M3.3 3H1.5V13H3.3'/><path d='M12.7 3H14.5V13H12.7'/><path d='M4.5 8H11.5'/><path d='M9.3 5.8L11.5 8L9.3 10.2'/>"},
	{"toggle_audio_medusa", "<rect x='1' y='3.5' width='14' height='9' rx='1.5'/><circle cx='4' cy='6.5' r='0.7' fill='@' stroke='none'/><circle cx='6.7' cy='6.5' r='0.7' fill='@' stroke='none'/><circle cx='9.3' cy='6.5' r='0.7' fill='@' stroke='none'/><circle cx='12' cy='6.5' r='0.7' fill='@' stroke='none'/><path d='M5 9.8H11'/>"},
	{"toggle_audio_nextcommit", "<path d='M1.5 8L4.5 11L9 5'/><path d='M12.5 3V13'/><path d='M10.3 10.8L12.5 13L14.7 10.8'/>"},
	{"toggle_audio_spectrum", "<path d='M2.5 9V13M5.5 4V13M8.5 7V13M11.5 2.5V13M14 10V13'/>"},
	{"toggle_tag_hiding", "<path d='M5 2.5C3.5 2.5 3.5 3.5 3.5 5V6.5C3.5 7.5 2.8 8 1.8 8C2.8 8 3.5 8.5 3.5 9.5V11C3.5 12.5 3.5 13.5 5 13.5'/><path d='M11 2.5C12.5 2.5 12.5 3.5 12.5 5V6.5C12.5 7.5 13.2 8 14.2 8C13.2 8 12.5 8.5 12.5 9.5V11C12.5 12.5 12.5 13.5 11 13.5'/><circle cx='6.2' cy='8' r='0.8' fill='@' stroke='none'/><circle cx='8' cy='8' r='0.8' fill='@' stroke='none'/><circle cx='9.8' cy='8' r='0.8' fill='@' stroke='none'/>"},
	{"toggle_video_autoscroll", "<rect x='1.5' y='1.5' width='13' height='8' rx='1.5'/><path d='M6.8 3.6L10 5.5L6.8 7.4Z' fill='@'/><path d='M2 13.5H14'/><path d='M8 9.5V11.5'/>"},
	{"translation_toolbutton", "<path d='M1.5 2.5H9.5V8.5H5.5L3 10.5V8.5H1.5Z'/><path d='M11.5 6.5H14.5V12.5H13V14.5L10.5 12.5H6.5V10.5'/>"},
	{"video_to_subend", "<path d='M1 8H8.5'/><path d='M6.3 5.8L8.5 8L6.3 10.2'/><path d='M11.5 3H14V13H11.5'/>"},
	{"video_to_substart", "<path d='M1 8H9'/><path d='M6.8 5.8L9 8L6.8 10.2'/><path d='M14.0 3H11.5V13H14.0'/>"},
	{"visual_clip", "<path d='M4.5 1V11.5H15M1 4.5H11.5V15'/>"},
	{"visual_help", "<circle cx='8' cy='8' r='6.5'/><path d='M6 6.2C6 5 6.9 4.2 8 4.2S10 5 10 6.1C10 7.5 8 7.6 8 9.4'/><circle cx='8' cy='11.6' r='0.95' fill='@' stroke='none'/>"},
	{"visual_move", "<path d='M8 1.5V14.5M1.5 8H14.5M6 3.5L8 1.5L10 3.5M6 12.5L8 14.5L10 12.5M3.5 6L1.5 8L3.5 10M12.5 6L14.5 8L12.5 10'/>"},
	{"visual_move_conv_move", "<circle cx='3.5' cy='8' r='2'/><path d='M6.5 8H14.5M11.5 5L14.5 8L11.5 11'/>"},
	{"visual_move_conv_pos", "<path d='M8 14.5C8 14.5 3 9.6 3 6.2A5 5 0 0 1 13 6.2C13 9.6 8 14.5 8 14.5Z'/><circle cx='8' cy='6.2' r='1.8'/>"},
	{"visual_pan", "<path d='M5 8.5V4.5A1.1 1.1 0 0 1 7.2 4.5V7.5M7.2 7V3A1.1 1.1 0 0 1 9.4 3V7M9.4 7V4A1.1 1.1 0 0 1 11.6 4V8M11.6 7.5V6A1.1 1.1 0 0 1 13.8 6V10C13.8 12.8 12 14.5 9.4 14.5H8.5C6.6 14.5 5.6 13.8 4.6 12.5L2.4 9.4C1.8 8.6 2.9 7.4 3.9 8.2L5 9.3'/>"},
	{"visual_rotatexy", "<ellipse cx='8' cy='9' rx='6.5' ry='2.8'/><path d='M8 1.5V14.5'/><path d='M12.5 4.7L15.3 6.4L12.3 7.8Z' fill='@'/>"},
	{"visual_rotatez", "<path d='M13 9.5A5.2 5.2 0 1 1 10.6 3.4'/><path d='M10.5 0.9L10.9 3.6L8.2 4.2'/><circle cx='8' cy='8.3' r='1.3' fill='@' stroke='none'/>"},
	{"visual_scale", "<rect x='1.5' y='8.5' width='6' height='6' rx='1'/><path d='M8 8L14.5 1.5M10 1.5H14.5V6'/>"},
	{"visual_standard", "<circle cx='8' cy='8' r='3.5'/><path d='M8 1V4.5M8 11.5V15M1 8H4.5M11.5 8H15'/>"},
	{"visual_vector_clip", "<path d='M3 12.5C4 4.5 12 11.5 13 3.5'/><rect x='1' y='11' width='3.2' height='3.2' rx='.5'/><rect x='11.8' y='1.8' width='3.2' height='3.2' rx='.5'/>"},
	{"visual_vector_clip_bicubic", "<rect x='1.4' y='11.4' width='3.2' height='3.2' rx='.5'/><rect x='11.4' y='1.4' width='3.2' height='3.2' rx='.5'/><path d='M4.6 13C11 13 5 3 11.4 3'/><path d='M4.6 13H8'/><path d='M11.4 3H8'/><circle cx='8.6' cy='13' r='1' fill='@' stroke='none'/><circle cx='7.4' cy='3' r='1' fill='@' stroke='none'/>"},
	{"visual_vector_clip_convert", "<path d='M2.5 2.5V13.5'/><path d='M13.5 2.5C11 6 16 10 13.5 13.5'/><path d='M5.5 8H10.5M7 6.5L5.5 8L7 9.5M9 6.5L10.5 8L9 9.5'/>"},
	{"visual_vector_clip_drag", "<rect x='6.4' y='6.4' width='3.2' height='3.2' rx='.5'/><path d='M8 1.5V5M8 11V14.5M1.5 8H5M11 8H14.5'/><path d='M6.3 3.2L8 1.5L9.7 3.2M6.3 12.8L8 14.5L9.7 12.8M3.2 6.3L1.5 8L3.2 9.7M12.8 6.3L14.5 8L12.8 9.7'/>"},
	{"visual_vector_clip_freehand", "<path d='M14.5 3.5L9 9L6.8 9.6L7.4 7.4L12.9 1.9Z'/><path d='M1.5 13.5L3.5 11L5.5 14L7.5 11.5'/>"},
	{"visual_vector_clip_freehand_smooth", "<path d='M14.5 3.5L9 9L6.8 9.6L7.4 7.4L12.9 1.9Z'/><path d='M1.5 13C2.5 10 4 10 4.8 12.2S7 14.5 8 11.5'/>"},
	{"visual_vector_clip_insert", "<path d='M1.5 14.5C3 8 13 8 14.5 14.5'/><rect x='6.4' y='8.0' width='3.2' height='3.2' rx='.5'/><path d='M8 1.5V5.5M6 3.5H10'/>"},
	{"visual_vector_clip_line", "<rect x='1.4' y='11.4' width='3.2' height='3.2' rx='.5'/><rect x='11.4' y='1.4' width='3.2' height='3.2' rx='.5'/><path d='M4.6 11.4L11.4 4.6'/>"},
	{"visual_vector_clip_remove", "<path d='M1.5 14.5C3 8 13 8 14.5 14.5'/><rect x='6.4' y='8.0' width='3.2' height='3.2' rx='.5'/><path d='M6 3.5H10'/>"},
	{"zoom_in_button", "<circle cx='6.5' cy='6.5' r='4.5'/><path d='M10 10L14.5 14.5'/><path d='M4.5 6.5H8.5M6.5 4.5V8.5'/>"},
	{"zoom_out_button", "<circle cx='6.5' cy='6.5' r='4.5'/><path d='M10 10L14.5 14.5'/><path d='M4.5 6.5H8.5'/>"},
};
}

const char *libresrc_modern_icon(std::string_view name) {
	auto it = std::lower_bound(std::begin(icons), std::end(icons), name,
		[](ModernIcon const& icon, std::string_view n) { return icon.name < n; });
	return it != std::end(icons) && it->name == name ? it->body : nullptr;
}

// Copyright (c) 2010, Amar Takhar <verm@aegisub.org>
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

/// @file preferences.cpp
/// @brief Preferences dialogue
/// @ingroup configuration_ui

#include "preferences.h"
#include "utils.h"

#include "ass_style_storage.h"
#include "audio_provider_factory.h"
#include "command_palette.h"
#include "audio_renderer_waveform.h"
#include "command/command.h"
#include "compat.h"
#include "format.h"
#include "flat_controls.h"
#include "flat_dialog.h"
#include "help_button.h"
#include "hotkey_data_view_model.h"
#include "include/aegisub/audio_player.h"
#include "include/aegisub/hotkey.h"
#include "include/aegisub/subtitles_provider.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "preferences_base.h"
#include "secret.h"
#include "speech_transcribe.h"
#include "theme.h"
#include "video_provider_manager.h"

#ifdef WITH_PORTAUDIO
#include "audio_player_portaudio.h"
#endif

#ifdef WITH_FFMS2
#include <ffms.h>
#endif

#include <libaegisub/hotkey.h>

#include <cstdlib>
#include <unordered_set>

#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/dc.h>
#include <wx/event.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>

namespace {
wxArrayString get_registered_commands() {
	wxArrayString commands = to_wx(cmd::get_registered_commands());
	commands.Sort();
	return commands;
}

#ifdef __APPLE__
void add_current_hotkey_commands(wxArrayString& commands, HotkeyDataViewModel *model, wxDataViewItem const& parent) {
	wxDataViewItemArray children;
	model->GetChildren(parent, children);

	for (auto const& child : children) {
		wxVariant value;
		model->GetValue(value, child, 1);
		wxString command = value.GetString();
		if (commands.Index(command) == wxNOT_FOUND)
			commands.Add(command);

		if (model->IsContainer(child))
			add_current_hotkey_commands(commands, model, child);
	}
}

wxArrayString get_hotkey_command_choices(HotkeyDataViewModel *model) {
	wxArrayString commands = get_registered_commands();
	if (commands.Index("") == wxNOT_FOUND)
		commands.Add("");

	add_current_hotkey_commands(commands, model, wxDataViewItem(nullptr));
	commands.Sort();
	return commands;
}
#endif

/// General preferences page
void General(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("General"));

	auto startup = p->PageSizer(_("Startup"));
	p->OptionAdd(startup, _("Check for updates on startup"), "App/Auto/Check For Updates");
	wxString autoload_modes[] = { _("Never"), _("Always"), _("Ask") };
	wxArrayString autoload_modes_arr(3, autoload_modes);
	p->OptionChoice(startup, _("Automatically load linked files"), autoload_modes_arr, "App/Auto/Load Linked Files", false,
		_("The video, audio and keyframes a subtitle file names, when it is opened"));

	auto files = p->PageSizer(_("Files"));
	p->OptionAdd(files, _("Save UI state in subtitles files"), "App/Save UI State",
		{.help = _("Where the video and the grid were, saved with the file and restored when it is opened again")});
	p->OptionAdd(files, _("Undo Levels"), "Limits/Undo Levels", {.min = 2, .max = 10000});

	auto recent = p->PageSizer(_("Recently Used Lists"));
	p->OptionAdd(recent, _("Files"), "Limits/MRU", {.min = 0, .max = 16});
	p->OptionAdd(recent, _("Find/Replace"), "Limits/Find Replace");

	p->SetSizerAndFit(p->sizer);
}

void General_DefaultStyles(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Default styles"), OptionPage::PAGE_SUB);

	auto section = p->PageSizer(_("Default style catalogs"));
	p->Note(section, _("The chosen style catalogs will be loaded when you start a new file or import files in the various formats.\n\nYou can set up style catalogs in the Style Manager."));

	// Build a list of available style catalogs, and wished-available ones
	auto const& avail_catalogs = AssStyleStorage::GetCatalogs();
	std::unordered_set<std::string> catalogs_set(begin(avail_catalogs), end(avail_catalogs));
	// Always include one named "Default" even if it doesn't exist (ensure there is at least one on the list)
	catalogs_set.insert("Default");
	// Include all catalogs named in the existing configuration
	static const char *formats[] = { "ASS", "MicroDVD", "SRT", "TTXT", "TXT" };
	for (auto formatname : formats)
		catalogs_set.insert(OPT_GET("Subtitle Format/" + std::string(formatname) + "/Default Style Catalog")->GetString());
	// Sorted version
	wxArrayString catalogs;
	for (auto const& cn : catalogs_set)
		catalogs.Add(to_wx(cn));
	catalogs.Sort();

	p->OptionChoice(section, _("New files"), catalogs, "Subtitle Format/ASS/Default Style Catalog");
	p->OptionChoice(section, _("MicroDVD import"), catalogs, "Subtitle Format/MicroDVD/Default Style Catalog");
	p->OptionChoice(section, _("SRT import"), catalogs, "Subtitle Format/SRT/Default Style Catalog");
	p->OptionChoice(section, _("TTXT import"), catalogs, "Subtitle Format/TTXT/Default Style Catalog");
	p->OptionChoice(section, _("Plain text import"), catalogs, "Subtitle Format/TXT/Default Style Catalog");

	p->SetSizerAndFit(p->sizer);
}

/// Appearance preferences page: the theme and the toolbar
void Appearance(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Appearance"));

	auto theme_section = p->PageSizer(_("Theme"));
	const wxString themes[] = { _("Follow system"), _("Light"), _("Dark") };
	p->OptionChoice(theme_section, _("Theme"), wxArrayString(3, themes), "App/Theme");
	// Custom-drawn parts switch at once; toolkit controls fully only after a restart.
	p->Note(theme_section, _("The subtitle grid and edit box switch at once; some window controls switch fully only after a restart."));

	auto toolbar = p->PageSizer(_("Toolbar"));
	p->OptionAdd(toolbar, _("Show main toolbar"), "App/Show Toolbar");
	p->OptionAdd(toolbar, _("Classic main toolbar (every button)"), "App/Classic Toolbar");
	p->OptionAdd(toolbar, _("Classic coloured icons"), "App/Classic Icons");
	p->OptionAdd(toolbar, _("Toolbar Icon Size"), "App/Toolbar Icon Size");

	p->SetSizerAndFit(p->sizer);
}

/// Editing preferences page: the edit box and what helps typing in it
void Editing(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Editing"));

	auto edit_box = p->PageSizer(_("Edit Box"));
	p->OptionAdd(edit_box, _("Enable call tips"), "App/Call Tips",
		{.help = _("Show the parameters of the tag being typed, above the text")});
	p->OptionAdd(edit_box, _("Enable ASS autocompletion"), "Subtitle/Edit Box/Auto Completion");
	p->OptionAdd(edit_box, _("Enable syntax highlighting"), "Subtitle/Highlight/Syntax");
	p->OptionAdd(edit_box, _("Shift+Enter adds \\n"), "Subtitle/Edit Box/Soft Line Break");
	p->OptionAdd(edit_box, _("Overwrite in time boxes"), "Subtitle/Time Edit/Insert Mode",
		{.help = _("Typing in a time replaces the digits after the cursor, rather than pushing them along")});
	p->OptionAdd(edit_box, _("Line fields below the text, next to the grid"), "Subtitle/Edit Box/Fields Below Text");
	p->OptionFont(edit_box, "Subtitle/Edit Box/");
	p->OptionBrowse(edit_box, _("Dictionaries path"), "Path/Dictionary");

	auto character_count = p->PageSizer(_("Character Counter"));
	p->OptionAdd(character_count, _("Maximum characters per line"), "Subtitle/Character Limit", {.min = 0, .max = 1000, .help = _("0 doesn't check")});
	p->OptionAdd(character_count, _("Characters Per Second Warning Threshold"), "Subtitle/Character Counter/CPS Warning Threshold", {.min = 0, .max = 1000});
	p->OptionAdd(character_count, _("Characters Per Second Error Threshold"), "Subtitle/Character Counter/CPS Error Threshold", {.min = 0, .max = 1000});
	p->OptionAdd(character_count, _("Ignore whitespace"), "Subtitle/Character Counter/Ignore Whitespace");
	p->OptionAdd(character_count, _("Ignore punctuation"), "Subtitle/Character Counter/Ignore Punctuation");

	auto tl_assistant = p->PageSizer(_("Translation Assistant"));
	p->OptionAdd(tl_assistant, _("Skip over whitespace"), "Tool/Translation Assistant/Skip Whitespace");

	p->SetSizerAndFit(p->sizer);
}

/// Subtitle grid preferences page
void Grid(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Subtitle Grid"));

	auto grid = p->PageSizer(_("Options"));
	p->OptionAdd(grid, _("Focus grid on click"), "Subtitle/Grid/Focus Allow",
		{.help = _("Clicking a line gives the keyboard to the grid, rather than leaving it in the text")});
	p->OptionAdd(grid, _("Highlight visible subtitles"), "Subtitle/Grid/Highlight Subtitles in Frame",
		{.help = _("Lines showing on the video's current frame get their own background")});
	p->OptionAdd(grid, _("Shade every other row"), "Subtitle/Grid/Alternate Rows");
	p->OptionFont(grid, "Subtitle/Grid/");

	p->SetSizerAndFit(p->sizer);
}

/// Audio preferences page
void Audio(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Audio"));

	auto interaction = p->PageSizer(_("Interaction"));
	p->OptionAdd(interaction, _("Default mouse wheel to zoom"), "Audio/Wheel Default to Zoom");
	p->OptionAdd(interaction, _("Lock scroll on cursor"), "Audio/Lock Scroll on Cursor",
		{.help = _("Keep the playback cursor in view while playing")});
	p->OptionAdd(interaction, _("Auto-focus on mouse over"), "Audio/Auto/Focus",
		{.help = _("The audio display takes the keyboard when the pointer is over it")});
	p->OptionAdd(interaction, _("Play audio when stepping in video"), "Audio/Plays When Stepping Video");
	p->OptionAdd(interaction, _("Left-click-drag moves end marker"), "Audio/Drag Timing",
		{.help = _("Dragging with the left button sets the end; otherwise it sets the start")});
	p->OptionAdd(interaction, _("Marker drag-start sensitivity (px)"), "Audio/Start Drag Sensitivity", {.min = 1, .max = 15});

	auto timing = p->PageSizer(_("Timing"));
	p->OptionAdd(timing, _("Default timing length (ms)"), "Timing/Default Duration", {.min = 0, .max = 36000});
	p->OptionAdd(timing, _("Default lead-in length (ms)"), "Audio/Lead/IN", {.min = 0, .max = 36000});
	p->OptionAdd(timing, _("Default lead-out length (ms)"), "Audio/Lead/OUT", {.min = 0, .max = 36000});
	p->OptionAdd(timing, _("Automatically link nearby subtitles"), "Timing/Link Adjacent Lines",
		{.help = _("Dragging the boundary between two lines that touch moves both")});
	p->OptionAdd(timing, _("Link subtitle gaps under (s)"), "Timing/Adjacent Line Gap", {.min = 0, .max = 10, .inc = 0.1});
	// Out, a line shows a little longer; in, it would cover a word or cut one
	// off, so those reach only a few frames
	p->OptionAdd(timing, _("Snap a start earlier to a scene change within (ms)"), "Timing/Scene Snap/Start Earlier", {.min = 0, .max = 10000, .inc = 50,
		.help = _("How far Snap to Nearby Scene Changes moves a line's start back to land on a scene change")});
	p->OptionAdd(timing, _("Snap a start later to a scene change within (ms)"), "Timing/Scene Snap/Start Later", {.min = 0, .max = 10000, .inc = 10,
		.help = _("How far Snap to Nearby Scene Changes moves a line's start on to land on a scene change: further would hide the line's first word")});
	p->OptionAdd(timing, _("Snap an end later to a scene change within (ms)"), "Timing/Scene Snap/End Later", {.min = 0, .max = 10000, .inc = 50,
		.help = _("How far Snap to Nearby Scene Changes moves a line's end on to land just before a scene change")});
	p->OptionAdd(timing, _("Snap an end earlier to a scene change within (ms)"), "Timing/Scene Snap/End Earlier", {.min = 0, .max = 10000, .inc = 10,
		.help = _("How far Snap to Nearby Scene Changes moves a line's end back to land just before a scene change: further would take the line away before it is said")});

	auto speech = p->PageSizer(_("Speech Recognition"));
	p->OptionAdd(speech, _("Pair each line with an empty translation line"), "Tool/Speech Import/Bilingual",
		{.help = _("Import Speech Recognition gives each line the JP style and a CN line at the same times for its translation")});
	p->OptionAdd(speech, _("Split sentences longer than (ms)"), "Tool/Speech Import/Max Duration", {.min = 2000, .max = 30000, .inc = 500,
		.help = _("A sentence heard as longer is split where a clause ends or the speaker pauses; the lines share a #group mark in the Effect field")});
	p->OptionAdd(speech, _("Show each line at least (ms)"), "Tool/Speech Import/Min Duration", {.min = 0, .max = 5000, .inc = 100,
		.help = _("A short line is shown longer, up to the next line or scene change, to give time to read its translation")});
	p->OptionAdd(speech, _("Mark words heard with less confidence than"), "Tool/Speech Import/Unsure Below", {.min = 0, .max = 1, .inc = 0.05,
		.help = _("Such words are written in the Effect field as ?word, for the translator to check")});

	auto transcribe = p->PageSizer(_("Transcription"));
	const wxString backend_arr[] = { _("ElevenLabs Scribe, over the net"), _("A program of your own, run here") };
	wxArrayString choice_backend(2, backend_arr);
	p->OptionChoice(transcribe, _("Heard by"), choice_backend, "Tool/Transcribe/Backend", false,
		_("Transcribe the Audio sends the audio to ElevenLabs, which charges by its length, or runs the command below and reads the JSON it writes"));
	p->OptionAdd(transcribe, _("Language"), "Tool/Transcribe/Language",
		{.help = _("The code of what is spoken: ja, zh, en...; empty to have it detected")});
	p->OptionAdd(transcribe, _("Other languages among it"), "Tool/Transcribe/Secondary Languages",
		{.help = _("Codes, separated by commas, of languages mixed into the speech, such as English words in Japanese (ElevenLabs)")});
	p->OptionAdd(transcribe, _("Tell the speakers apart"), "Tool/Transcribe/Diarize",
		{.help = _("Each line's speaker goes in its Actor field (ElevenLabs)")});
	p->OptionAdd(transcribe, _("Give the names in the Actor fields as terms to recognise"), "Tool/Transcribe/Keyterms From Actors",
		{.help = _("Names the recogniser is told of are heard right more often (ElevenLabs)")});
	p->OptionAdd(transcribe, _("Other terms to recognise"), "Tool/Transcribe/Keyterms",
		{.help = _("Names and words easily misheard, separated by commas; 100 terms at most in all (ElevenLabs)")});
	p->OptionAdd(transcribe, _("ElevenLabs model"), "Tool/Transcribe/ElevenLabs/Model",
		{.help = _("scribe_v3; or scribe_v2, the one before")});
	{
		// The key: where it is, and setting or forgetting it. Not a setting
		// of the file's: the keychain keeps it where there is one.
		auto status = new wxStaticText(transcribe.box, -1, "");
		auto set = new FlatButton(transcribe.box, _("Set..."));
		auto forget = new FlatButton(transcribe.box, _("Forget"));
		auto const service = transcribe::key_service;
		auto refresh = [=] {
			auto env = std::getenv("ELEVENLABS_API_KEY");
			bool const in_setting = !OPT_GET("Tool/Transcribe/ElevenLabs/Key")->GetString().empty();
			bool const held = secret::Held(service).has_value();
			wxString where;
			if (env && *env) where = _("From the ELEVENLABS_API_KEY environment variable");
			else if (in_setting) where = _("Kept in the settings file, as plain text");
			else if (held) where = _("Kept until Aegisub is closed: this machine has no keychain");
			else if (secret::Available()) where = _("Kept in the system's keychain, once set");
			else where = _("Asked for each time: this machine has no keychain");
			status->SetLabel(where);
			forget->Enable(in_setting || held || secret::Available());
		};
		set->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
			bool const keychain = secret::Available();
			wxString key = wxGetPasswordFromUser(_("Enter your ElevenLabs API key, from the API keys page of elevenlabs.io.") + "\n\n"
				+ (keychain ? _("It is kept in the system's keychain.") : _("This machine has no keychain Aegisub can use, so it is kept only until Aegisub is closed.")),
				_("ElevenLabs API key"), "", p);
			auto found = transcribe::FindKey({nullptr, std::nullopt, std::nullopt, from_wx(key)});
			if (found.key.empty()) return;
			OPT_SET("Tool/Transcribe/ElevenLabs/Key")->SetString("");
			if (!keychain || !secret::Save(service, found.key)) secret::Hold(service, found.key);
			refresh();
		});
		forget->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
			secret::Forget(service);
			secret::Drop(service);
			OPT_SET("Tool/Transcribe/ElevenLabs/Key")->SetString("");
			refresh();
		});
		auto row = new wxBoxSizer(wxHORIZONTAL);
		row->Add(status, wxSizerFlags(1).CentreVertical());
		row->Add(set, wxSizerFlags().Border(wxLEFT, p->FromDIP(4)));
		row->Add(forget, wxSizerFlags().Border(wxLEFT, p->FromDIP(4)));
		p->Row(transcribe, _("ElevenLabs API key"), row,
			_("Asked for when the audio is first sent, if not set here. The ELEVENLABS_API_KEY environment variable, when set, is used over anything kept"));
		refresh();
	}
	p->OptionAdd(transcribe, _("Ask before sending audio"), "Tool/Transcribe/Ask",
		{.help = _("Say how long the audio is and that it leaves this computer, and ask, each time before sending it to ElevenLabs")});
	p->OptionAdd(transcribe, _("Command"), "Tool/Transcribe/Command",
		{.help = _("Run with {media} the audio or video file, {out} an empty folder to write a JSON file with each word's times to, and {language} the language above; a part in [brackets] is left out when its value is empty")});

	auto snapping = p->PageSizer(_("Snapping"));
	p->OptionAdd(snapping, _("Snap markers by default"), "Audio/Snap/Enable");
	p->OptionAdd(snapping, _("Maximum snap distance (px)"), "Audio/Snap/Distance", {.min = 0, .max = 25});

	auto display = p->PageSizer(_("Display Visual Options"));
	const wxString dtl_arr[] = { _("Don't show"), _("Show previous"), _("Show previous and next"), _("Show all") };
	wxArrayString choice_dtl(4, dtl_arr);
	p->OptionChoice(display, _("Show inactive lines"), choice_dtl, "Audio/Inactive Lines Display Mode");
	p->OptionAdd(display, _("Include commented inactive lines"), "Audio/Display/Draw/Inactive Comments");
	p->OptionAdd(display, _("Keyframes in dialogue mode"), "Audio/Display/Draw/Keyframes in Dialogue Mode");
	p->OptionAdd(display, _("Keyframes in karaoke mode"), "Audio/Display/Draw/Keyframes in Karaoke Mode");
	p->OptionAdd(display, _("Cursor time"), "Audio/Display/Draw/Cursor Time");
	p->OptionAdd(display, _("Video position"), "Audio/Display/Draw/Video Position");
	p->OptionAdd(display, _("Seconds boundaries"), "Audio/Display/Draw/Seconds");
	p->OptionAdd(display, _("dB scale on the waveform"), "Audio/Display/Draw/dB Scale");
	p->OptionChoice(display, _("Waveform Style"), AudioWaveformRenderer::GetWaveformStyles(), "Audio/Display/Waveform Style");
	p->OptionAdd(display, _("Line boundary thickness (px)"), "Audio/Line Boundaries Thickness", {.min = 1, .max = 5});

	auto label = p->PageSizer(_("Audio labels"));
	p->OptionFont(label, "Audio/Karaoke/");

	p->SetSizerAndFit(p->sizer);
}

/// Video preferences page
void Video(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Video"));

	auto playback = p->PageSizer(_("Playback"));
	p->OptionAdd(playback, _("Seek video to line start on selection change"), "Video/Subtitle Sync");
	p->OptionAdd(playback, _("Automatically open audio when opening video"), "Video/Open Audio");
	p->OptionAdd(playback, _("Show keyframes in slider"), "Video/Slider/Show Keyframes");
	p->OptionAdd(playback, _("Fast jump step in frames"), "Video/Slider/Fast Jump Step");
	const wxString csens_arr[] = {_("Low: only clear cuts"), wxGETTEXT_IN_CONTEXT("scene detection", "Medium"), _("High: also cuts between similar shots")};
	wxArrayString choice_sens(3, csens_arr);
	p->OptionChoice(playback, _("Scene change detection"), choice_sens, "Tool/Scene Detection/Sensitivity", false,
		_("How readily Detect Scene Changes takes a change of picture for a cut. Higher finds more cuts, and more that are not."));
	p->OptionAdd(playback, _("Also find dissolves and fades"), "Tool/Scene Detection/Gradual",
		{.help = _("A change of shot that takes a few frames is placed at its middle")});
	p->OptionAdd(playback, _("Leave out the bottom of the picture (%)"), "Tool/Scene Detection/Subtitle Band",
		{.min = 0, .max = 50, .help = _("Where subtitles are burnt in, so that their coming and going is not taken for a cut; black bars are left out by themselves")});

	auto magia = p->PageSizer(_("MagiaTimeline"));
	p->OptionBrowse(magia, _("Installed in"), "Tool/MagiaTimeline/Path");

	auto video_display = p->PageSizer(_("Video Display"));
	p->OptionAdd(video_display, _("Only show visual tools when mouse is over video"), "Tool/Visual/Autohide");
	const wxString czoom_arr[24] = { "12.5%", "25%", "37.5%", "50%", "62.5%", "75%", "87.5%", "100%", "112.5%", "125%", "137.5%", "150%", "162.5%", "175%", "187.5%", "200%", "212.5%", "225%", "237.5%", "250%", "262.5%", "275%", "287.5%", "300%" };
	wxArrayString choice_zoom(24, czoom_arr);
	p->OptionChoice(video_display, _("Default Zoom"), choice_zoom, "Video/Default Zoom");
	const wxString cscroll_arr[] = {_("Resizes the video box"), _("Resizes the video box (reversed)"), _("Zooms the video"), _("Zooms the video (reversed)"), _("Pans the video"), _("Pans the video (X/Y swapped)"), _("Does nothing")};
	wxArrayString choice_scroll(7, cscroll_arr);
	p->OptionChoice(video_display, _("Scrolling on the video display"), choice_scroll, "Video/Scroll Action");
	p->OptionChoice(video_display, _("Ctrl+Scrolling on the video display"), choice_scroll, "Video/Ctrl Scroll Action");
	p->OptionChoice(video_display, _("Shift+Scrolling on the video display"), choice_scroll, "Video/Shift Scroll Action");
	const wxString cscr_arr[3] = { "?video", "?script", "." };
	wxArrayString scr_res(3, cscr_arr);
	p->OptionChoice(video_display, _("Screenshot save path"), scr_res, "Path/Screenshot", false,
		_("?video: next to the video; ?script: next to the subtitles; .: the working folder"));

	auto resolution = p->PageSizer(_("Script Resolution"));
	wxWindow *autocb = p->OptionAdd(resolution, _("Use resolution of first video opened"), "Subtitle/Default Resolution/Auto");
	p->DisableIfChecked(autocb,
		p->OptionAdd(resolution, _("Default width"), "Subtitle/Default Resolution/Width"));
	p->DisableIfChecked(autocb,
		p->OptionAdd(resolution, _("Default height"), "Subtitle/Default Resolution/Height"));

	const wxString cres_arr[] = {_("Never"), _("Ask"), _("Always resample")};
	wxArrayString choice_res(3, cres_arr);
	p->OptionChoice(resolution, _("Match video resolution on open"), choice_res, "Video/PlayRes Mismatch");

	auto layoutres = p->PageSizer(_("Layout Resolution"));

	const wxString cnolayoutres_arr[] = {_("Never"), _("Ask"), _("Always set"), _("Always set from script resolution")};
	wxArrayString choice_nolayoutres(4, cnolayoutres_arr);
	p->OptionChoice(layoutres, _("Set layout resolution from video on open"), choice_nolayoutres, "Video/No LayoutRes in Script");

	const wxString clayoutresmismatch_arr[] = {_("Never"), _("When aspect ratio changes"), _("Always")};
	wxArrayString choice_layoutresmismatch(3, clayoutresmismatch_arr);
	p->OptionChoice(layoutres, _("Prompt on layout resolution mismatch"), choice_layoutresmismatch, "Video/LayoutRes Mismatch");

	auto ycbcr = p->PageSizer(_("YCbCr Matrix"));
	p->OptionAdd(ycbcr, _("Warn on untagged video color matrix"), "Video/Untagged Matrix Warning");
	p->OptionAdd(ycbcr, _("Warn on HDR/WCG video"), "Video/HDR Video Warning");

	const wxString cnoycbcr_arr[] = {_("Never"), _("Ask"), _("Always set")};
	wxArrayString choice_noycbcr(3, cnoycbcr_arr);
	p->OptionChoice(ycbcr, _("Use video's YCbCr Matrix when script has no matrix set"), choice_noycbcr, "Video/No YCbCr Matrix in Script");

	const wxString cmisycbcr_arr[] = {_("Never"), _("Ask"), _("Always set")};
	wxArrayString choice_misycbcr(3, cmisycbcr_arr);
	p->OptionChoice(ycbcr, _("Match video's YCbCr Matrix on open"), choice_misycbcr, "Video/YCbCr Matrix Mismatch");

	p->SetSizerAndFit(p->sizer);
}

/// Interface Colours preferences subpage
void Interface_Colours(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Colors"), OptionPage::PAGE_SCROLL|OptionPage::PAGE_SUB);

	delete p->sizer;
	wxSizer *main_sizer = new wxBoxSizer(wxHORIZONTAL);

	p->sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(p->sizer, 1);

	auto audio = p->PageSizer(_("Audio Display"));
	p->OptionAdd(audio, _("Play cursor"), "Colour/Audio Display/Play Cursor");
	p->OptionAdd(audio, _("Current frame range"), "Colour/Audio Display/Current Frame Range", {.alpha = true});
	p->OptionAdd(audio, _("Previous frame range"), "Colour/Audio Display/Previous Frame Range", {.alpha = true});
	p->OptionAdd(audio, _("Line boundary start"), "Colour/Audio Display/Line boundary Start");
	p->OptionAdd(audio, _("Line boundary end"), "Colour/Audio Display/Line boundary End");
	p->OptionAdd(audio, _("Line boundary inactive line"), "Colour/Audio Display/Line Boundary Inactive Line");
	p->OptionAdd(audio, _("Syllable boundaries"), "Colour/Audio Display/Syllable Boundaries");
	p->OptionAdd(audio, _("Seconds boundaries"), "Colour/Audio Display/Seconds Line");

	auto syntax = p->PageSizer(_("Syntax Highlighting"));
	p->OptionAdd(syntax, _("Background"), "Colour/Subtitle/Background");
	p->OptionAdd(syntax, _("Normal"), "Colour/Subtitle/Syntax/Normal");
	p->OptionAdd(syntax, _("Comments"), "Colour/Subtitle/Syntax/Comment");
	p->OptionAdd(syntax, _("Drawing Commands"), "Colour/Subtitle/Syntax/Drawing Command");
	p->OptionAdd(syntax, _("Drawing X Coords"), "Colour/Subtitle/Syntax/Drawing X");
	p->OptionAdd(syntax, _("Drawing Y Coords"), "Colour/Subtitle/Syntax/Drawing Y");
	p->OptionAdd(syntax, _("Underline Spline Endpoints"), "Colour/Subtitle/Syntax/Underline/Drawing Endpoint");
	p->CellSkip(syntax);
	p->OptionAdd(syntax, _("Brackets"), "Colour/Subtitle/Syntax/Brackets");
	p->OptionAdd(syntax, _("Slashes and Parentheses"), "Colour/Subtitle/Syntax/Slashes");
	p->OptionAdd(syntax, _("Tags"), "Colour/Subtitle/Syntax/Tags");
	p->OptionAdd(syntax, _("Parameters"), "Colour/Subtitle/Syntax/Parameters");
	p->OptionAdd(syntax, _("Error"), "Colour/Subtitle/Syntax/Error");
	p->OptionAdd(syntax, _("Error Background"), "Colour/Subtitle/Syntax/Background/Error");
	p->OptionAdd(syntax, _("Line Break"), "Colour/Subtitle/Syntax/Line Break");
	p->OptionAdd(syntax, _("Karaoke templates"), "Colour/Subtitle/Syntax/Karaoke Template");
	p->OptionAdd(syntax, _("Karaoke variables"), "Colour/Subtitle/Syntax/Karaoke Variable");

	p->sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->AddSpacer(5);
	main_sizer->Add(p->sizer, 1);

	auto color_schemes = p->PageSizer(_("Audio Color Schemes"));
	wxArrayString schemes = to_wx(OPT_GET("Audio/Colour Schemes")->GetListString());
	p->OptionChoice(color_schemes, _("Spectrum"), schemes, "Colour/Audio Display/Spectrum");
	p->OptionChoice(color_schemes, _("Waveform"), schemes, "Colour/Audio Display/Waveform");

	auto grid = p->PageSizer(_("Subtitle Grid"));
	p->OptionAdd(grid, _("Standard foreground"), "Colour/Subtitle Grid/Standard");
	p->OptionAdd(grid, _("Standard background"), "Colour/Subtitle Grid/Background/Background");
	p->OptionAdd(grid, _("Selection foreground"), "Colour/Subtitle Grid/Selection");
	p->OptionAdd(grid, _("Selection background"), "Colour/Subtitle Grid/Background/Selection");
	p->OptionAdd(grid, _("Collision foreground"), "Colour/Subtitle Grid/Collision");
	p->OptionAdd(grid, _("In frame background"), "Colour/Subtitle Grid/Background/Inframe");
	p->OptionAdd(grid, _("Comment background"), "Colour/Subtitle Grid/Background/Comment");
	p->OptionAdd(grid, _("Selected comment background"), "Colour/Subtitle Grid/Background/Selected Comment");
	p->OptionAdd(grid, _("Header background"), "Colour/Subtitle Grid/Header");
	p->OptionAdd(grid, _("Left Column"), "Colour/Subtitle Grid/Left Column");
	p->OptionAdd(grid, _("Active Line Border"), "Colour/Subtitle Grid/Active Border");
	p->OptionAdd(grid, _("Lines"), "Colour/Subtitle Grid/Lines");
	p->OptionAdd(grid, _("CPS Error"), "Colour/Subtitle Grid/CPS Error");

	auto visual_tools = p->PageSizer(_("Visual Typesetting Tools"));
	p->OptionAdd(visual_tools, _("Primary Lines"), "Colour/Visual Tools/Lines Primary");
	p->OptionAdd(visual_tools, _("Secondary Lines"), "Colour/Visual Tools/Lines Secondary");
	p->OptionAdd(visual_tools, _("Primary Highlight"), "Colour/Visual Tools/Highlight Primary");
	p->OptionAdd(visual_tools, _("Secondary Highlight"), "Colour/Visual Tools/Highlight Secondary");

	// Separate sizer to prevent the colors in the visual tools section from getting resized
	auto visual_tools_alpha = p->PageSizer(_("Visual Typesetting Tools Alpha"));
	p->OptionAdd(visual_tools_alpha, _("Shaded Area"), "Colour/Visual Tools/Shaded Area Alpha", {.min = 0, .max = 1, .inc = 0.1});

	p->sizer = main_sizer;

	p->SetSizerAndFit(p->sizer);
}

/// Backup preferences page
void Backup(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Backup"));

	auto save = p->PageSizer(_("Automatic Save"));
	wxWindow *cb = p->OptionAdd(save, _("Enable"), "App/Auto/Save");
	p->CellSkip(save);
	p->EnableIfChecked(cb,
		p->OptionAdd(save, _("Interval in seconds"), "App/Auto/Save Every Seconds", {.min = 1}));
	p->OptionBrowse(save, _("Path"), "Path/Auto/Save", cb, true);
	p->OptionAdd(save, _("Autosave after every change"), "App/Auto/Save on Every Change");

	auto backup = p->PageSizer(_("Automatic Backup"));
	cb = p->OptionAdd(backup, _("Enable"), "App/Auto/Backup");
	p->CellSkip(backup);
	p->OptionBrowse(backup, _("Path"), "Path/Auto/Backup", cb, true);

	p->SetSizerAndFit(p->sizer);
}

/// Automation preferences page
void Automation(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Automation"));

	auto general = p->PageSizer(_("General"));

	p->OptionAdd(general, _("Base path"), "Path/Automation/Base");
	p->OptionAdd(general, _("Include path"), "Path/Automation/Include");
	p->OptionAdd(general, _("Auto-load path"), "Path/Automation/Autoload");

	const wxString tl_arr[6] = { _("0: Fatal"), _("1: Error"), _("2: Warning"), _("3: Hint"), _("4: Debug"), _("5: Trace") };
	wxArrayString tl_choice(6, tl_arr);
	p->OptionChoice(general, _("Trace level"), tl_choice, "Automation/Trace Level");

	const wxString ar_arr[4] = { _("No scripts"), _("Subtitle-local scripts"), _("Global autoload scripts"), _("All scripts") };
	wxArrayString ar_choice(4, ar_arr);
	p->OptionChoice(general, _("Autoreload on Export"), ar_choice, "Automation/Autoreload Mode");

	p->SetSizerAndFit(p->sizer);
}

/// Advanced preferences page
void Advanced(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Advanced"));

	auto general = p->PageSizer(_("General"));

	auto warning = new wxStaticText(general.box, wxID_ANY ,_("Changing these settings might result in bugs and/or crashes. Do not touch these unless you know what you're doing."));
	auto font = parent->GetFont().MakeBold();
	font.SetPointSize(12);
	warning->SetFont(font);
	p->sizer->Fit(p);
	warning->Wrap(400);
	general.sizer->Add(warning, 0, wxALL, 5);

	p->SetSizerAndFit(p->sizer);
}

/// Advanced Audio preferences subpage
void Advanced_Audio(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Audio"), OptionPage::PAGE_SUB);

	auto expert = p->PageSizer(_("Expert"));

	wxArrayString ap_choice = to_wx(GetAudioProviderNames());
	p->OptionChoice(expert, _("Audio provider"), ap_choice, "Audio/Provider");

	wxArrayString apl_choice = to_wx(AudioPlayerFactory::GetClasses());
	p->OptionChoice(expert, _("Audio player"), apl_choice, "Audio/Player");

	auto cache = p->PageSizer(_("Cache"));
	const wxString ct_arr[3] = { _("None (NOT RECOMMENDED)"), _("RAM"), _("Hard Disk") };
	wxArrayString ct_choice(3, ct_arr);
	p->OptionChoice(cache, _("Cache type"), ct_choice, "Audio/Cache/Type");
	p->OptionBrowse(cache, _("Path"), "Audio/Cache/HD/Location");

	auto spectrum = p->PageSizer(_("Spectrum"));

	const wxString sq_arr[4] = { _("Regular quality"), _("Better quality"), _("High quality"), _("Insane quality") };
	wxArrayString sq_choice(4, sq_arr);
	p->OptionChoice(spectrum, _("Quality"), sq_choice, "Audio/Renderer/Spectrum/Quality");

	const wxString sc_arr[5] = { _("Linear"), _("Extended"), _("Medium"), _("Compressed"), _("Logarithmic") };
	wxArrayString sc_choice(5, sc_arr);
	p->OptionChoice(spectrum, _("Frequency mapping"), sc_choice, "Audio/Renderer/Spectrum/FreqCurve");

	p->OptionAdd(spectrum, _("Cache memory max (MB)"), "Audio/Renderer/Spectrum/Memory Max", {.min = 2, .max = 1024});

#ifdef WITH_AVISYNTH
	auto avisynth = p->PageSizer("Avisynth");
	const wxString adm_arr[3] = { "ConvertToMono", "GetLeftChannel", "GetRightChannel" };
	wxArrayString adm_choice(3, adm_arr);
	p->OptionChoice(avisynth, _("Avisynth down-mixer"), adm_choice, "Audio/Downmixer");
	p->OptionAdd(avisynth, _("Force sample rate"), "Provider/Audio/AVS/Sample Rate");
#endif

#ifdef WITH_FFMS2
	auto ffms = p->PageSizer("FFmpegSource");

	const wxString error_modes[] = { _("Ignore"), _("Clear"), _("Stop"), _("Abort") };
	wxArrayString error_modes_choice(4, error_modes);
	p->OptionChoice(ffms, _("Audio indexing error handling mode"), error_modes_choice, "Provider/Audio/FFmpegSource/Decode Error Handling");

	p->OptionAdd(ffms, _("Always index all audio tracks"), "Provider/FFmpegSource/Index All Tracks");
#endif

#ifdef WITH_PORTAUDIO
	auto portaudio = p->PageSizer("Portaudio");
	p->OptionChoice(portaudio, _("Portaudio device"), PortAudioPlayer::GetOutputDevices(), "Player/Audio/PortAudio/Device Name");
#endif

#ifdef WITH_OSS
	auto oss = p->PageSizer("OSS");
	p->OptionBrowse(oss, _("OSS Device"), "Player/Audio/OSS/Device");
#endif

#ifdef WITH_DIRECTSOUND
	auto dsound = p->PageSizer("DirectSound");
	p->OptionAdd(dsound, _("Buffer latency"), "Player/Audio/DirectSound/Buffer Latency", {.min = 1, .max = 1000});
	p->OptionAdd(dsound, _("Buffer length"), "Player/Audio/DirectSound/Buffer Length", {.min = 1, .max = 100});
#endif

	p->SetSizerAndFit(p->sizer);
}

/// Advanced Video preferences subpage
void Advanced_Video(PreferencesBook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Video"), OptionPage::PAGE_SUB);

	auto expert = p->PageSizer(_("Expert"));

	wxArrayString vp_choice = to_wx(VideoProviderFactory::GetClasses());
	p->OptionChoice(expert, _("Video provider"), vp_choice, "Video/Provider");

	wxArrayString sp_choice = to_wx(SubtitlesProviderFactory::GetClasses());
	p->OptionChoice(expert, _("Subtitles provider"), sp_choice, "Subtitle/Provider");

#ifdef WITH_AVISYNTH
	auto avisynth = p->PageSizer("Avisynth");
	p->OptionAdd(avisynth, _("Allow pre-2.56a Avisynth"), "Provider/Avisynth/Allow Ancient");
	p->CellSkip(avisynth);
	p->OptionAdd(avisynth, _("Avisynth memory limit"), "Provider/Avisynth/Memory Max");
#endif

#ifdef WITH_FFMS2
	auto ffms = p->PageSizer("FFmpegSource");

	const wxString log_levels[] = { wxTRANSLATE("Quiet"), wxTRANSLATE("Panic"), wxTRANSLATE("Fatal"), wxTRANSLATE("Error"), wxTRANSLATE("Warning"), wxTRANSLATE("Info"), wxTRANSLATE("Verbose"), wxTRANSLATE("Debug") };
	wxArrayString log_levels_choice(8, log_levels);
	p->OptionChoice(ffms, _("Debug log verbosity"), log_levels_choice, "Provider/FFmpegSource/Log Level", true);

	p->OptionAdd(ffms, _("Decoding threads"), "Provider/Video/FFmpegSource/Decoding Threads", {.min = -1});
	p->OptionAdd(ffms, _("Enable unsafe seeking"), "Provider/Video/FFmpegSource/Unsafe Seeking");
#endif

	p->SetSizerAndFit(p->sizer);
}

/// wxDataViewIconTextRenderer with command name autocompletion
class CommandRenderer final : public wxDataViewCustomRenderer {
	wxArrayString autocomplete;
	wxDataViewIconText value;
	static const int icon_width = 20;

	wxDataViewIconText MakeValue(wxString const& text) const {
		wxBitmapBundle icon;
		try {
			icon = cmd::get(from_wx(text))->Icon();
		}
		catch (agi::Exception const&) {
			// Invalid command names are reported in the description column.
		}
		return wxDataViewIconText(text, icon);
	}

public:
	CommandRenderer()
	: wxDataViewCustomRenderer("string", wxDATAVIEW_CELL_EDITABLE)
	, autocomplete(get_registered_commands())
	{
	}

	wxWindow *CreateEditorCtrl(wxWindow *parent, wxRect label_rect, wxVariant const& value) override {
		wxString text;
		if (value.GetType() == "wxDataViewIconText") {
			wxDataViewIconText iconText;
			iconText << value;
			text = iconText.GetText();
		}
		else {
			text = value.GetString();
		}

		// adjust the label rect to take the width of the icon into account
		label_rect.x += parent->FromDIP(icon_width);
		label_rect.width -= parent->FromDIP(icon_width);

		wxTextCtrl* ctrl = new wxTextCtrl(parent, -1, text, label_rect.GetPosition(), label_rect.GetSize(), wxTE_PROCESS_ENTER);
		ctrl->SetInsertionPointEnd();
		ctrl->SelectAll();
		ctrl->AutoComplete(autocomplete);
		return ctrl;
	}

	bool SetValue(wxVariant const& var) override {
		if (var.GetType() == "wxDataViewIconText") {
			value << var;
			return true;
		}
		if (var.GetType() == "string") {
			value = MakeValue(var.GetString());
			return true;
		}
		return false;
	}

	bool Render(wxRect rect, wxDC *dc, int state) override {
		wxIcon const& icon = value.GetBitmapBundle().GetIconFor(dc->GetWindow());
		if (icon.IsOk())
			dc->DrawIcon(icon, rect.x, rect.y + (rect.height - icon.GetLogicalHeight()) / 2);

		// Using FromDIP here creates slightly awkward spacing when there is no bitmap available for the exact
		// requested size (e.g. on 125% DPI scaling), but we cannot use the actual icon's size because our value
		// may not have an icon.
		RenderText(value.GetText(), dc->GetWindow()->FromDIP(icon_width), rect, dc, state);

		return true;
	}

	wxSize GetSize() const override {
		if (!value.GetText().empty()) {
			wxSize size = GetTextExtent(value.GetText());
			size.x += GetView()->FromDIP(icon_width);
			return size;
		}
		return DipSize(80,20);
	}

	bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant &var) override {
		wxTextCtrl *text = static_cast<wxTextCtrl*>(editor);
		var = text->GetValue();
		return true;
	}

	bool GetValue(wxVariant &) const override { return false; }
	bool HasEditorCtrl() const override { return true; }
};

class HotkeyRenderer final : public wxDataViewCustomRenderer {
	wxString value;
	wxTextCtrl *ctrl = nullptr;

public:
	HotkeyRenderer()
	: wxDataViewCustomRenderer("string", wxDATAVIEW_CELL_EDITABLE)
	{ }

	wxWindow *CreateEditorCtrl(wxWindow *parent, wxRect label_rect, wxVariant const& var) override {
		ctrl = new wxTextCtrl(parent, -1, var.GetString(), label_rect.GetPosition(), label_rect.GetSize(), wxTE_PROCESS_ENTER);
		ctrl->SetInsertionPointEnd();
		ctrl->SelectAll();
		ctrl->Bind(wxEVT_CHAR_HOOK, &HotkeyRenderer::OnKeyDown, this);
		return ctrl;
	}

	void OnKeyDown(wxKeyEvent &evt) {
		ctrl->ChangeValue(to_wx(hotkey::keypress_to_str(evt.GetKeyCode(), evt.GetModifiers())));
	}

	bool SetValue(wxVariant const& var) override {
		value = var.GetString();
		return true;
	}

	bool Render(wxRect rect, wxDC *dc, int state) override {
		RenderText(value, 0, rect, dc, state);
		return true;
	}

	bool GetValueFromEditorCtrl(wxWindow*, wxVariant &var) override {
		var = ctrl->GetValue();
		return true;
	}

	bool GetValue(wxVariant &) const override { return false; }
	wxSize GetSize() const override { return !value ? DipSize(80, 20) : GetTextExtent(value); }
	bool HasEditorCtrl() const override { return true; }
};

static void edit_item(wxDataViewCtrl *dvc, wxDataViewItem item) {
	dvc->EditItem(item, dvc->GetColumn(0));
}

class Interface_Hotkeys final : public OptionPage {
	wxDataViewCtrl *dvc;
	wxObjectDataPtr<HotkeyDataViewModel> model;
	wxTextCtrl *quick_search;

	void OnNewButton(wxCommandEvent&);
	void OnUpdateFilter(wxCommandEvent&);
public:
	Interface_Hotkeys(PreferencesBook *book, Preferences *parent);
};

/// Interface Hotkeys preferences subpage
Interface_Hotkeys::Interface_Hotkeys(PreferencesBook *book, Preferences *parent)
: OptionPage(book, parent, _("Hotkeys"))
, model(new HotkeyDataViewModel(parent))
{
	auto quick_search_frame = flat::SearchField(this, _("Search"), quick_search);

	auto new_button = new FlatButton(this, _("&New"));
	auto edit_button = new FlatButton(this, _("&Edit"));
	auto delete_button = new FlatButton(this, _("&Delete"));

	new_button->Bind(wxEVT_BUTTON, &Interface_Hotkeys::OnNewButton, this);
	edit_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { edit_item(dvc, dvc->GetSelection()); });
	delete_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { model->Delete(dvc->GetSelection()); });

	quick_search->Bind(wxEVT_TEXT, &Interface_Hotkeys::OnUpdateFilter, this);

	dvc = new wxDataViewCtrl(this, -1);
	dvc->AssociateModel(model.get());
#ifndef __APPLE__
	dvc->AppendColumn(new wxDataViewColumn(_("Hotkey"), new HotkeyRenderer, 0, 125, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE));
	dvc->AppendColumn(new wxDataViewColumn(_("Command"), new CommandRenderer, 1, 250, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE));
#else
	auto col = new wxDataViewColumn(_("Hotkey"), new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_EDITABLE), 0, 150, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE);
	col->SetMinWidth(150);
	dvc->AppendColumn(col);
	dvc->AppendColumn(new wxDataViewColumn(_("Command"), new wxDataViewChoiceRenderer(get_hotkey_command_choices(model.get()), wxDATAVIEW_CELL_EDITABLE), 1, 250, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE));
#endif
	dvc->AppendTextColumn(_("Description"), 2, wxDATAVIEW_CELL_INERT, 300, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE);

	wxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
	buttons->Add(quick_search_frame, wxSizerFlags(1).CenterVertical().Border());
	buttons->Add(new_button, wxSizerFlags().Border());
	buttons->Add(edit_button, wxSizerFlags().Border());
	buttons->Add(delete_button, wxSizerFlags().Border());

	sizer->Add(buttons, wxSizerFlags().Expand());
	sizer->Add(dvc, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT));

	SetSizerAndFit(sizer);
}

void Interface_Hotkeys::OnNewButton(wxCommandEvent&) {
	wxDataViewItem sel = dvc->GetSelection();
	dvc->ExpandAncestors(sel);
	dvc->Expand(sel);

	wxDataViewItem new_item = model->New(sel);
	if (new_item.IsOk()) {
		dvc->Select(new_item);
		dvc->EnsureVisible(new_item);
		edit_item(dvc, new_item);
	}
}

void Interface_Hotkeys::OnUpdateFilter(wxCommandEvent&) {
	model->SetFilter(quick_search->GetValue());

	if (!quick_search->GetValue().empty()) {
		wxDataViewItemArray contexts;
		model->GetChildren(wxDataViewItem(nullptr), contexts);
		for (auto const& context : contexts)
			dvc->Expand(context);
	}
}
}

void Preferences::SetOption(std::unique_ptr<agi::OptionValue> new_value) {
	pending_changes[new_value->GetName()] = std::move(new_value);
	applyButton->Enable(true);
	// The marks of changed settings follow
	for (size_t page = 0; book && page < book->GetPageCount(); ++page)
		if (auto options = dynamic_cast<OptionPage*>(book->GetPage(page))) options->RefreshMarks();
}

void Preferences::AddPendingChange(Thunk const& callback) {
	pending_callbacks.push_back(callback);
	applyButton->Enable(true);
}

void Preferences::AddChangeableOption(std::string const& name) {
	option_names.push_back(name);
}

void Preferences::OnOK(wxCommandEvent &event) {
	OnApply(event);
	EndModal(0);
}

void Preferences::OnApply(wxCommandEvent &) {
	for (auto const& change : pending_changes)
		OPT_SET(change.first)->Set(change.second.get());
	pending_changes.clear();

	for (auto const& thunk : pending_callbacks)
		thunk();
	pending_callbacks.clear();

	applyButton->Enable(false);
	config::opt->Flush();
}

void Preferences::RestorePageDefaults() {
	auto page = dynamic_cast<OptionPage*>(book->GetPage(CurrentPage()));
	if (page && !page->Options().empty()) {
		// Like any change in the dialog: OK keeps it, Cancel forgets it
		page->ResetAll();
		return;
	}
	if (!dynamic_cast<Interface_Hotkeys*>(book->GetPage(CurrentPage()))) return;
	if (wxYES != wxMessageBox(_("Restore the default hotkeys? Every hotkey you have set will be lost."), _("Restore defaults?"), wxYES_NO))
		return;
	agi::hotkey::Hotkey def_hotkeys("", GET_DEFAULT_CONFIG(default_hotkey));
	hotkey::inst->SetHotkeyMap(def_hotkeys.GetHotkeyMap());

	// Close and reopen the dialog to show the hotkeys again
	OPT_SET("Tool/Preferences/Page")->SetInt(book->GetSelection());
	EndModal(-1);
}

Preferences::Preferences(wxWindow *parent): wxDialog(parent, -1, _("Preferences"), wxDefaultPosition, wxSize(-1, -1), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	SetIcons(GETICONS(options_button));
	theme::UseSurface(this);

	// The list of pages on the left, the search box above it
	book = new PreferencesBook(this);
	search_frame = flat::SearchField(book, _("Search settings (name or pinyin)"), search, 160);
	search->SetName("preferences_search");
	// What the search found: shown only while searching, so the list
	// starts right under the box
	search_status = new wxStaticText(book, -1, wxEmptyString);
	search_status->SetForegroundColour(flat::Colours(this).dim);
	search_status->Hide();
	book->AddAboveList(search_frame);
	book->AddAboveList(search_status);

	General(book, this);
	General_DefaultStyles(book, this);
	Appearance(book, this);
	Interface_Colours(book, this);
	Editing(book, this);
	Grid(book, this);
	Audio(book, this);
	Video(book, this);
	new Interface_Hotkeys(book, this);
	Backup(book, this);
	Automation(book, this);
	Advanced(book, this);
	Advanced_Audio(book, this);
	Advanced_Video(book, this);

	IndexPages();
	search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Search(search->GetValue()); });
	search->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { NextMatchingPage(); });

	book->ChangeSelection(OPT_GET("Tool/Preferences/Page")->GetInt());
	book->on_change = [this](size_t page) {
		OPT_SET("Tool/Preferences/Page")->SetInt(page);
		ShowFirstMatch(page);
	};

	// Bottom Buttons
	auto buttonSizer = new flat::DialogButtons(this, wxOK | wxCANCEL | wxAPPLY | wxHELP);
	applyButton = buttonSizer->Get(wxID_APPLY);
	auto defaultButton = buttonSizer->AddLeft(_("&Restore Page Defaults"));
	defaultButton->SetToolTip(_("Give every setting of the page shown its default; OK keeps it, Cancel forgets it. On the hotkeys page, restore the default hotkeys."));

	// Main Sizer
	wxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(book, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(8)));
	mainSizer->Add(buttonSizer, wxSizerFlags(0).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));

	SetSizerAndFit(mainSizer);
	// Not as tall as the tallest page: the pages scroll
	// Fitting made the tallest page the least height; the pages scroll, so
	// the window can be smaller than that
	SetMinSize(FromDIP(wxSize(640, 420)));
	SetSize(wxSize(std::max(GetSize().x, FromDIP(760)), std::min(GetSize().y, FromDIP(600))));
	CenterOnParent();

	applyButton->Enable(false);

	Bind(wxEVT_BUTTON, &Preferences::OnOK, this, wxID_OK);
	Bind(wxEVT_BUTTON, &Preferences::OnApply, this, wxID_APPLY);
	Bind(wxEVT_BUTTON, std::bind(&HelpButton::OpenPage, "Options"), wxID_HELP);
	defaultButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RestorePageDefaults(); });
	// The keyboard starts in the search box, not on a page's first setting:
	// set again once shown, as Windows gives a dialog's first control the
	// keyboard when it opens
	search->SetFocus();
	Bind(wxEVT_INIT_DIALOG, [this](wxInitDialogEvent &event) {
		event.Skip();
		CallAfter([this] { search->SetFocus(); });
	});
}

namespace {
std::wstring WithoutSpaces(std::wstring text) {
	std::erase_if(text, [](wchar_t ch) { return std::iswspace(ch) || ch == L':' || ch == L'：'; });
	return text;
}

/// The label of a control the search looks at, empty for other controls.
wxString SearchLabel(wxWindow *window) {
	if (dynamic_cast<wxStaticText*>(window) || dynamic_cast<wxCheckBox*>(window) || dynamic_cast<FlatCheck*>(window) || dynamic_cast<wxStaticBox*>(window))
		return wxControl::RemoveMnemonics(window->GetLabel());
	return {};
}

void CollectLabels(wxWindow *window, std::vector<wxWindow*>& out) {
	for (auto child : window->GetChildren()) {
		if (!SearchLabel(child).empty()) out.push_back(child);
		CollectLabels(child, out);
	}
}
}

void Preferences::IndexPages() {
	auto texts = [](wxString const& label) {
		std::vector<std::wstring> out;
		for (auto const& key : palette::MakeKeys(label.ToStdWstring(), ""))
			if (!key.text.empty()) out.push_back(WithoutSpaces(key.text));
		return out;
	};
	for (size_t page = 0; page < book->GetPageCount(); ++page) {
		search_entries.push_back({nullptr, page, texts(book->GetPageText(page)), {}, {}});
		std::vector<wxWindow*> labels;
		CollectLabels(book->GetPage(page), labels);
		for (auto window : labels)
			search_entries.push_back({window, page, texts(SearchLabel(window)), window->GetFont(),
				window->UseForegroundColour() ? window->GetForegroundColour() : wxColour()});
	}
}

void Preferences::ShowSearch(wxString const& query) {
	search->ChangeValue(query);
	Search(query);
}

void Preferences::Search(wxString const& query) {
	auto wanted = WithoutSpaces(palette::NormaliseQuery(query.ToStdWstring()));
	search_query = wanted;
	matching_pages.clear();
	for (auto& entry : search_entries) {
		entry.matched = !wanted.empty() && std::any_of(entry.texts.begin(), entry.texts.end(),
			[&](std::wstring const& text) { return text.find(wanted) != text.npos; });
		if (entry.matched && (matching_pages.empty() || matching_pages.back() != entry.page))
			matching_pages.push_back(entry.page);
	}
	ShowMatches();
	if (!matching_pages.empty() && std::find(matching_pages.begin(), matching_pages.end(), CurrentPage()) == matching_pages.end())
		book->ChangeSelection(matching_pages.front());
	ShowFirstMatch(CurrentPage());
}

void Preferences::ShowFirstMatch(size_t page) {
	auto controls = MatchingControls(page);
	if (auto options = dynamic_cast<OptionPage*>(book->GetPage(page)); options && !controls.empty())
		options->ScrollTo(controls.front());
}

void Preferences::ShowMatches() {
	auto accent = theme::UiColours().accent;
	bool searching = !search_query.empty();
	for (auto& entry : search_entries) {
		// A section box passes its font and colour on to the controls in it:
		// it counts for the page but is not restyled.
		if (!entry.window || dynamic_cast<wxStaticBox*>(entry.window)) continue;
		if (entry.matched) {
			entry.window->SetFont(entry.font.Bold());
			entry.window->SetForegroundColour(accent);
		}
		else {
			entry.window->SetFont(entry.font);
			entry.window->SetForegroundColour(entry.colour);
		}
		entry.window->Refresh();
	}

	// The number of matches on each page, beside its name
	std::vector<size_t> counts;
	if (searching) {
		counts.assign(book->GetPageCount(), 0);
		for (auto const& entry : search_entries)
			if (entry.matched && entry.page < counts.size()) ++counts[entry.page];
	}
	book->SetMatches(counts);

	search_status->SetLabel(SearchStatus());
	search_status->Wrap(search_frame->GetSize().x);
	search_status->Show(!search_status->GetLabel().empty());
	book->Layout();
	Layout();
}

void Preferences::NextMatchingPage() {
	if (matching_pages.empty()) return;
	auto next = std::upper_bound(matching_pages.begin(), matching_pages.end(), CurrentPage());
	book->SetSelection(next == matching_pages.end() ? matching_pages.front() : *next);
	ShowFirstMatch(CurrentPage());
}

std::vector<wxWindow*> Preferences::MatchingControls(size_t page) const {
	std::vector<wxWindow*> out;
	for (auto const& entry : search_entries)
		if (entry.matched && entry.window && entry.page == page) out.push_back(entry.window);
	return out;
}

size_t Preferences::CurrentPage() const {
	return static_cast<size_t>(book->GetSelection());
}

wxString Preferences::SearchStatus() const {
	if (search_query.empty()) return {};
	if (matching_pages.empty()) return _("No matching settings");
	size_t count = std::count_if(search_entries.begin(), search_entries.end(), [](SearchEntry const& entry) { return entry.matched; });
	return fmt_tl("%d matches on %d pages; Enter shows the next", count, matching_pages.size());
}

std::vector<Preferences::Setting> Preferences::Settings() const {
	std::vector<Setting> settings;
	for (auto const& entry : search_entries) {
		auto page = book->GetPageText(entry.page);
		if (!entry.window) settings.push_back({page, page, true});
		else if (auto label = SearchLabel(entry.window); !label.empty())
			settings.push_back({label, page, false});
	}
	return settings;
}

std::vector<Preferences::Setting> const& SettingLabels(wxWindow *parent) {
	static std::vector<Preferences::Setting> settings = [&] {
		Preferences dialog(parent);
		return dialog.Settings();
	}();
	return settings;
}

void ShowPreferences(wxWindow *parent, wxString const& search) {
	while (true) {
		Preferences dialog(parent);
		if (!search.empty()) dialog.ShowSearch(search);
		if (dialog.ShowModal() >= 0) break;
	}
}

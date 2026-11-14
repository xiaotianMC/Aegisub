// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <libaegisub/exception.h>
#include <libaegisub/fs.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class AssFile;
namespace agi { class ProgressSink; }

/// Speech recognition run from the editor on the audio it has open: by
/// ElevenLabs Scribe over the net, or by a program of the user's own
/// (WhisperX, faster-whisper...) run here, its JSON read back. Either way
/// what is heard goes through speech::ReadJson and speech::Import, the
/// same as a file imported by hand.
namespace transcribe {

/// What to hear and how
struct Request {
	/// The audio or video file
	agi::fs::path media;
	/// ISO 639-1 or 639-3 code of what is spoken; empty to have it detected
	std::string language;
	/// Other languages expected among it, English words in Japanese
	std::vector<std::string> secondary_languages;
	/// Tell the speakers apart, each word given its speaker
	bool diarize = true;
	/// Names and terms easily misheard, to recognise; 100 at most
	std::vector<std::string> keyterms;
};

/// The ElevenLabs settings
struct ElevenLabs {
	std::string model = "scribe_v3";
	std::string key;
};

/// The keychain entry the ElevenLabs key is kept under (secret.h)
inline constexpr const char *key_service = "Aegisub: ElevenLabs API key";

/// Where the key to send may be, in the order looked
struct KeySources {
	/// The ELEVENLABS_API_KEY environment variable; null when not set
	const char *environment = nullptr;
	/// What the system's keychain holds
	std::optional<std::string> keychain;
	/// What is held for this run, on a machine without a keychain
	std::optional<std::string> session;
	/// What the settings file keeps, as plain text, from before the keychain
	std::string setting;
};
enum class KeyPlace { None, Environment, Keychain, Session, Setting };
struct FoundKey {
	std::string key;
	KeyPlace place = KeyPlace::None;
};
/// The key to send, trimmed, from the first of @p sources that has one
FoundKey FindKey(KeySources const& sources);

/// The codes in a setting such as "en, zh" or "en zh": split at commas and
/// spaces, each trimmed, none empty
std::vector<std::string> SplitCodes(std::string const& setting);
/// The terms in a setting such as "Tokyo Tower, Madoka": split at commas
/// and line ends only, so a term keeps its spaces
std::vector<std::string> SplitList(std::string const& setting);

/// The names in the subtitles' Actor fields, after @p extra, each once in
/// the order first met, no more than @p limit in all. Speakers a
/// recogniser named (SPEAKER_00, speaker_1) are not names and left out.
std::vector<std::string> Keyterms(AssFile const& file, std::vector<std::string> extra, size_t limit = 100);

/// The text fields of the form sent to ElevenLabs, in order; a name
/// repeated is a list. The file itself is not among them.
std::vector<std::pair<std::string, std::string>> Form(Request const& request, ElevenLabs const& settings);

/// Whether ElevenLabs takes @p media as it is, by its extension; else the
/// audio is written out as WAV and that is sent
bool Accepted(agi::fs::path const& media);

/// What ElevenLabs' answer says went wrong, for the user: by the status,
/// with the message in the JSON body if it has one. Empty when it is fine.
std::string Explain(long status, std::string const& body);

/// @p ms as m:ss or h:mm:ss
std::string Duration(int ms);

/// Whether anything can be sent: not when built without libcurl
bool CanUpload();

/// ElevenLabs did not accept the key: it is asked for again
DEFINE_EXCEPTION(KeyRefused, agi::Exception);

/// Send @p request to ElevenLabs, how far the upload has got shown on @p
/// ps. @return The JSON it answered, for speech::ReadJson.
/// @throws KeyRefused, agi::EnvironmentError with what went wrong, and
/// agi::UserCancelException when cancelled on @p ps
std::string Upload(Request const& request, ElevenLabs const& settings, agi::ProgressSink *ps);

/// A command line of the user's made ready to run: @p templ with {media},
/// {out} and {language} put in and split into its words, quotes (" or ')
/// grouping and taken off. A part in [brackets] is left out when a
/// placeholder in it has nothing to put, so "[--language {language}]" is
/// there only with a language.
std::vector<std::string> CommandLine(std::string const& templ, agi::fs::path const& media,
                                     agi::fs::path const& out, std::string const& language);

/// The JSON the command wrote in @p folder: the one named after @p media
/// if there, else the only one; nothing if there are none or several others
std::optional<agi::fs::path> OutputJson(agi::fs::path const& folder, agi::fs::path const& media);

/// A program of the user's, started on the main thread and waited for
/// from another
class Program {
	struct Impl;
	std::unique_ptr<Impl> impl;
public:
	/// Start @p argv in @p folder. @throws agi::EnvironmentError if it does not start
	Program(std::vector<std::string> const& argv, agi::fs::path const& folder);
	~Program();

	/// Wait for it to end, its latest line of output shown on @p ps; killed
	/// with what it started if cancelled there. @throws agi::UserCancelException
	void Wait(agi::ProgressSink *ps);
	/// What it printed, stdout and stderr together, as UTF-8
	std::string Output() const;
	/// Its exit code once ended; -1 before
	int ExitCode() const;
};

}

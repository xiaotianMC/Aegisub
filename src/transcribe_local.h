// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include "speech_lines.h"
#include "speech_transcribe.h"

#include <libaegisub/fs.h>

#include <string>
#include <vector>

namespace agi { class ProgressSink; }

/// Speech recognition here, by whisper.cpp built in: no program to install
/// and nothing sent anywhere, only a model file to have
namespace transcribe {

/// A token whisper.cpp gave, its times in milliseconds on the audio's own
/// timeline, -1 where it gave none
struct Token {
	/// Its bytes, which may be only part of a character: a kanji's three
	/// bytes can be split between two tokens
	std::string text;
	int start = -1, end = -1;
	/// How sure it is, 0 to 1; -1 if not said
	double p = -1;
	/// <|endoftext|>, timestamps and the like: not text
	bool special = false;
};

/// A segment whisper.cpp gave, in milliseconds
struct WhisperSegment {
	int start = 0, end = 0;
	std::vector<Token> tokens;
};

/// What whisper.cpp heard made a transcript of words with their times: a
/// token's bytes joined to the next until they are whole characters; a
/// piece run on to the word before unless it begins with a space or either
/// is written without spaces (kanji, kana, Thai...), where each piece is a
/// word and the lines are split by ICU later. Punctuation goes on the word
/// before, its end left where the word's was. A word ends no later than the
/// next one starts.
speech::Transcript FromWhisper(std::vector<WhisperSegment> const& segments, std::string language);

/// How to run whisper.cpp
struct Local {
	/// The ggml model file
	agi::fs::path model;
	/// The VAD model, to leave silence out and so not have words made up
	/// in it; empty for none
	agi::fs::path vad_model;
	bool use_gpu = true;
	/// 0 for as many as the processor has, up to 8
	int threads = 0;
};

/// Whether this build has whisper.cpp
bool CanRunLocal();

/// Hear @p samples, 16 kHz mono from -1 to 1, as @p request says, how far
/// it has got shown on @p ps
/// @throws agi::EnvironmentError with what went wrong, and
/// agi::UserCancelException when cancelled on @p ps
speech::Transcript RunLocal(Request const& request, Local const& settings, std::vector<float> const& samples, agi::ProgressSink *ps);
}

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC

#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace agi { struct Context; }

/// Speech recognition's output made into subtitle lines: a line a sentence,
/// each one the translator can take on its own
namespace speech {
/// A word heard, in milliseconds; -1 for a time the aligner gave none of
struct Word {
	std::string text;
	int start = -1, end = -1;
	/// How sure the recogniser or aligner is of it, 0 to 1; -1 if not said
	double score = -1;
	std::string speaker;
};

/// What the recogniser gave as one piece, usually a sentence or two
struct Segment {
	int start = 0, end = 0;
	std::string text, speaker;
	std::vector<Word> words;
};

struct Transcript {
	/// "ja", "en"...; empty if not said
	std::string language;
	std::vector<Segment> segments;
};

/// The JSON WhisperX writes, or faster-whisper's of the same shape: its
/// segments, with their words' times and scores ("score" or "probability")
/// and speakers when diarized; or ElevenLabs Scribe's, its "words" with
/// their types and speaker_id, its ISO 639-3 language made 639-1.
/// @throws agi::InvalidInputException
Transcript ReadJson(std::istream &stream);

struct Line {
	int start = 0, end = 0;
	std::string text, speaker;
	/// Lines split from one sentence share a group number, above 0
	int group = 0;
	/// Words heard but not surely, each run of them as one
	std::vector<std::string> unsure;
	/// Not said: "（笑い）", "♪"... kept as a comment, out of the dialogue
	bool note = false;
};

struct Rules {
	/// A sentence longer than this is split, and no piece shorter than
	/// min_piece is made
	int max_duration = 7000, min_piece = 1500;
	/// Pieces with no sentence end between them, nearer than this, are one
	/// sentence; a longer pause within one ends it. With no punctuation, as
	/// Whisper often writes Japanese, a pause of sentence_pause after a
	/// sentence's last word (です, ました, ね...) ends it too.
	int join_gap = 300, pause_break = 600, sentence_pause = 250;
	/// Every line shown at least this long, if the next line or cut allows
	int min_duration = 1000;
	/// As the timing post-processor's
	int lead_in = 100, lead_out = 350, adjacent_gap = 300;
	/// Words scored below this are marked unsure
	double unsure_below = 0.3;
	/// A cut with less than this of a line on one side takes that end; with
	/// more on both, the line is split at a pause of at least min_pause
	/// within cut_pause of it
	int cut_side = 300, cut_pause = 200, min_pause = 100;
};

/// @p cuts, the scene changes' times, sorted; empty if none are known.
/// Each word keeps the time the recogniser gave it.
/// Lines are in time order, their ends not yet snapped to the frames.
std::vector<Line> MakeLines(Transcript const& transcript, Rules const& rules, std::vector<int> const& cuts);

/// The scene changes among a video's @p keyframes (frame numbers): those
/// the encoder put every so many frames, whatever was shown, left out. A
/// video cut for the web often has nothing but those.
std::vector<int> SceneCuts(std::vector<int> const& keyframes);

/// @p transcript's lines put in the subtitles, in one undo step: in place of
/// them if nothing is written yet, else among them by time. Snapped to the
/// scene changes if they are known; paired with translation lines if
/// Tool/Speech Import/Bilingual is set. @return How many lines were made
size_t Import(agi::Context *c, Transcript const& transcript);
}

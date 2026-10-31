// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
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

#include "search_replace_engine.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "subtitle_project_access.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include "text_selection_controller.h"

#include <libaegisub/exception.h>
#include <libaegisub/util.h>

#include <boost/locale/conversion.hpp>
#include <unicode/normalizer2.h>
#include <unicode/bytestream.h>
#include <unicode/edits.h>
#include <limits>

#include <wx/msgdlg.h>

namespace {
static const size_t bad_pos = -1;
static const MatchState bad_match{nullptr, 0, bad_pos};

auto get_dialogue_field(SearchReplaceSettings::Field field) -> decltype(&AssDialogueBase::Text) {
	switch (field) {
		case SearchReplaceSettings::Field::TEXT: return &AssDialogueBase::Text;
		case SearchReplaceSettings::Field::STYLE: return &AssDialogueBase::Style;
		case SearchReplaceSettings::Field::ACTOR: return &AssDialogueBase::Actor;
		case SearchReplaceSettings::Field::EFFECT: return &AssDialogueBase::Effect;
	}
	throw agi::InternalError("Bad field for search");
}

std::string get_normalized(const AssDialogue *diag, decltype(&AssDialogueBase::Text) field, icu::Edits& edits) {
    auto const& value = (diag->*field).get();
    if (value.size() > std::numeric_limits<int32_t>::max()) throw std::length_error("Subtitle text too long to normalize");
    std::string normalized;
    icu::StringByteSink<std::string> sink(&normalized);
    UErrorCode status = U_ZERO_ERROR;
    auto normalizer = icu::Normalizer2::getNFCInstance(status);
    if (U_FAILURE(status)) throw agi::InternalError("Unicode normalizer unavailable");
    normalizer->normalizeUTF8(0, icu::StringPiece(value.data(), static_cast<int32_t>(value.size())), sink, &edits, status);
    if (U_FAILURE(status)) throw agi::InternalError("Subtitle normalization failed");
    return normalized;
}
size_t map_normalized_index(icu::Edits const& edits, size_t index, bool to_source) {
    if (index > std::numeric_limits<int32_t>::max()) throw std::out_of_range("Subtitle match offset too large");
    UErrorCode status = U_ZERO_ERROR;
    auto iterator = edits.getFineIterator();
    auto mapped = to_source ? iterator.sourceIndexFromDestinationIndex(static_cast<int32_t>(index), status)
                            : iterator.destinationIndexFromSourceIndex(static_cast<int32_t>(index), status);
    if (U_FAILURE(status)) throw agi::InternalError("Subtitle match offset mapping failed");
    return mapped;
}

typedef std::function<MatchState (const AssDialogue*, size_t)> matcher;

class noop_accessor {
	boost::flyweight<std::string> AssDialogueBase::*field;
	size_t start = 0;
    icu::Edits edits;

public:
	noop_accessor(SearchReplaceSettings::Field f) : field(get_dialogue_field(f)) { }

	std::string get(const AssDialogue *d, size_t s) {
        auto normalized = get_normalized(d, field, edits);
        start = map_normalized_index(edits, s, false);
        return normalized.substr(start);
	}

	MatchState make_match_state(size_t s, size_t e, boost::u32regex *r = nullptr) {
        return {r, map_normalized_index(edits, s + start, true), map_normalized_index(edits, e + start, true)};
	}
};

class skip_tags_accessor {
	boost::flyweight<std::string> AssDialogueBase::*field;
	agi::util::tagless_find_helper helper;
    icu::Edits edits;

public:
	skip_tags_accessor(SearchReplaceSettings::Field f) : field(get_dialogue_field(f)) { }

	std::string get(const AssDialogue *d, size_t s) {
        auto normalized = get_normalized(d, field, edits);
        return helper.strip_tags(normalized, map_normalized_index(edits, s, false));
	}

	MatchState make_match_state(size_t s, size_t e, boost::u32regex *r = nullptr) {
		helper.map_range(s, e);
        return {r, map_normalized_index(edits, s, true), map_normalized_index(edits, e, true)};
	}
};

template<typename Accessor>
matcher get_matcher(SearchReplaceSettings const& settings, Accessor&& a) {
	if (settings.use_regex) {
		int flags = boost::u32regex::perl;
		if (!settings.match_case)
			flags |= boost::u32regex::icase;

		auto regex = boost::make_u32regex(settings.find, flags);

		return [=](const AssDialogue *diag, size_t start) mutable -> MatchState {
			boost::smatch result;
			auto const& str = a.get(diag, start);
			if (!u32regex_search(str, result, regex, start > 0 ? boost::match_not_bol : boost::match_default))
				return bad_match;
			return a.make_match_state(result.position(), result.position() + result.length(), &regex);
		};
	}

	bool full_match_only = settings.exact_match;
	bool match_case = settings.match_case;
	std::string look_for = settings.find;

	if (!settings.match_case)
		look_for = boost::locale::fold_case(look_for);

	return [=](const AssDialogue *diag, size_t start) mutable -> MatchState {
		const auto str = a.get(diag, start);
		if (full_match_only && str.size() != look_for.size())
			return bad_match;

		if (match_case) {
			const auto pos = str.find(look_for);
			return pos == std::string::npos ? bad_match : a.make_match_state(pos, pos + look_for.size());
		}

		const auto pos = agi::util::ifind(str, look_for);
		return pos.first == bad_pos ? bad_match : a.make_match_state(pos.first, pos.second);
	};
}

template<typename Iterator, typename Container>
Iterator circular_next(Iterator it, Container& c) {
	++it;
	if (it == c.end())
		it = c.begin();
	return it;
}

}

std::function<MatchState (const AssDialogue*, size_t)> SearchReplaceEngine::GetMatcher(SearchReplaceSettings const& settings) {
	if (settings.skip_tags)
		return get_matcher(settings, skip_tags_accessor(settings.field));
	return get_matcher(settings, noop_accessor(settings.field));
}

SearchReplaceEngine::SearchReplaceEngine(agi::Context *c)
: context(c)
{
}

void SearchReplaceEngine::Replace(AssDialogue *diag, MatchState &ms) {
	auto& diag_field = diag->*get_dialogue_field(settings.field);
	auto text = diag_field.get();

	std::string replacement = settings.replace_with;
	if (ms.re) {
		auto to_replace = boost::locale::normalize(text.substr(ms.start, ms.end - ms.start));
		replacement = u32regex_replace(to_replace, *ms.re, replacement, boost::format_first_only);
	}

	diag_field = text.substr(0, ms.start) + replacement + text.substr(ms.end);
	ms.end = ms.start + replacement.size();
}

bool SearchReplaceEngine::FindReplace(bool replace) {
    auto edit_scope = replace ? context->subtitleAccess->TryWrite() : std::optional<SubtitleProjectAccess::WriteScope>{};
    if (replace && !edit_scope) return false;
	if (!initialized)
		return false;

	auto matches = GetMatcher(settings);

	AssDialogue *line = context->selectionController->GetActiveLine();
	auto it = context->ass->iterator_to(*line);
	size_t pos = 0;

	auto replace_ms = bad_match;
	if (replace) {
		if (settings.field == SearchReplaceSettings::Field::TEXT)
			pos = context->textSelectionController->GetSelectionStart();

		if ((replace_ms = matches(line, pos))) {
			size_t end = bad_pos;
			if (settings.field == SearchReplaceSettings::Field::TEXT)
				end = context->textSelectionController->GetSelectionEnd();

			if (end == bad_pos || (pos == replace_ms.start && end == replace_ms.end)) {
				Replace(line, replace_ms);
				pos = replace_ms.end;
				context->ass->Commit(_("replace"), AssFile::COMMIT_DIAG_TEXT);
			}
			else {
				// The current line matches, but it wasn't already selected,
				// so the match hasn't been "found" and displayed to the user
				// yet, so do that rather than replacing
				context->textSelectionController->SetSelection(replace_ms.start, replace_ms.end);
				return true;
			}
		}
	}
	// Search from the end of the selection to avoid endless matching the same thing
	else if (settings.field == SearchReplaceSettings::Field::TEXT)
		pos = context->textSelectionController->GetSelectionEnd();
	// For non-text fields we just look for matching lines rather than each
	// match within the line, so move to the next line
	else if (settings.field != SearchReplaceSettings::Field::TEXT)
		it = circular_next(it, context->ass->Events);

	auto const& sel = context->selectionController->GetSelectedSet();
	bool selection_only = sel.size() > 1 && settings.limit_to == SearchReplaceSettings::Limit::SELECTED;

	do {
		if (selection_only && !sel.count(&*it)) continue;
		if (settings.ignore_comments && it->Comment) continue;

		if (MatchState ms = matches(&*it, pos)) {
			if (selection_only)
				// We're cycling through the selection, so don't muck with it
				context->selectionController->SetActiveLine(&*it);
			else
				context->selectionController->SetSelectionAndActive({ &*it }, &*it);

			if (settings.field == SearchReplaceSettings::Field::TEXT)
				context->textSelectionController->SetSelection(ms.start, ms.end);

			return true;
		}
	} while (pos = 0, &*(it = circular_next(it, context->ass->Events)) != line);

	// Replaced something and didn't find another match, so select the newly
	// inserted text
	if (replace_ms && settings.field == SearchReplaceSettings::Field::TEXT)
		context->textSelectionController->SetSelection(replace_ms.start, replace_ms.end);

	return true;
}

bool SearchReplaceEngine::ReplaceAll() {
    auto edit_scope = context->subtitleAccess->TryWrite();
    if (!edit_scope) return false;
	if (!initialized)
		return false;

	size_t count = 0;

	auto matches = GetMatcher(settings);

	auto const& sel = context->selectionController->GetSelectedSet();
	bool selection_only = settings.limit_to == SearchReplaceSettings::Limit::SELECTED;

	for (auto& diag : context->ass->Events) {
		if (selection_only && !sel.count(&diag)) continue;
		if (settings.ignore_comments && diag.Comment) continue;

		if (settings.use_regex) {
			if (MatchState ms = matches(&diag, 0)) {
				auto& diag_field = diag.*get_dialogue_field(settings.field);
                auto text = boost::locale::normalize(diag_field.get());
				count += std::distance(
					boost::u32regex_iterator<std::string::const_iterator>(begin(text), end(text), *ms.re),
					boost::u32regex_iterator<std::string::const_iterator>());
				diag_field = u32regex_replace(text, *ms.re, settings.replace_with);
			}
			continue;
		}

		size_t pos = 0;
		while (MatchState ms = matches(&diag, pos)) {
			++count;
			Replace(&diag, ms);
			pos = ms.end;
		}
	}

	if (count > 0) {
		context->ass->Commit(_("replace"), AssFile::COMMIT_DIAG_TEXT);
		wxMessageBox(fmt_plural(count, "One match was replaced.", "%d matches were replaced.", (int)count));
	}
	else {
		wxMessageBox(_("No matches found."));
	}

	return true;
}

void SearchReplaceEngine::Configure(SearchReplaceSettings const& new_settings) {
	settings = new_settings;
	initialized = true;
}

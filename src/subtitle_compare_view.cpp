// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "subtitle_compare_view.h"

#include "ass_dialogue.h"
#include "ass_file.h"

#include <libaegisub/ass/time.h>

#include <algorithm>
#include <stdexcept>

namespace subtitle_compare_view {
namespace {
// Milliseconds are significant in comparisons (a 1 ms change is a change).
std::string time(int ms) { return agi::Time(ms).GetAssFormatted(true); }

size_t bit(model::Field field) {
    size_t i = 0;
    for (model::Fields f = field; f > 1; f >>= 1) ++i;
    return i;
}

std::string extras(AssFile const& file, AssDialogue const& line) {
    std::string out;
    for (auto const& e : file.GetExtradata(line.ExtradataIds.get())) {
        if (!out.empty()) out += "; ";
        out += e.key + "=" + e.value;
    }
    return out;
}

std::string value(AssFile const& file, AssDialogue const& line, model::Field field) {
    switch (field) {
        case model::Text: return line.Text.get();
        case model::Start: return time(line.Start.GetMilliseconds());
        case model::End: return time(line.End.GetMilliseconds());
        case model::Layer: return std::to_string(line.Layer);
        case model::Style: return line.Style.get();
        case model::Actor: return line.Actor.get();
        case model::MarginL: return std::to_string(line.Margin[0]);
        case model::MarginR: return std::to_string(line.Margin[1]);
        case model::MarginV: return std::to_string(line.Margin[2]);
        case model::Effect: return line.Effect.get();
        case model::Comment: return line.Comment ? "yes" : "no";
        case model::Extra: return extras(file, line);
        default: return {};
    }
}

Line make_line(AssFile const& file, AssDialogue const& line, size_t index) {
    Line out;
    out.number = index + 1;
    for (auto field : FieldOrder()) out.values[bit(field)] = value(file, line, field);
    out.comment = line.Comment;
    return out;
}

// ---- text diff ----
struct Token { size_t begin, end; };

size_t utf8_length(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1; // stray continuation byte: one byte at a time
}

bool word(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool space(unsigned char c) { return c == ' ' || c == '\t'; }

std::vector<Token> tokenize(std::string const& s) {
    std::vector<Token> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        size_t j = i;
        unsigned char c = s[i];
        if (c == '{') {
            j = s.find('}', i);
            j = j == std::string::npos ? n : j + 1;
        }
        else if (c == '\\' && i + 1 < n && (s[i + 1] == 'N' || s[i + 1] == 'n' || s[i + 1] == 'h')) j = i + 2;
        else if (word(c)) while (j < n && word(s[j])) ++j;
        else if (space(c)) while (j < n && space(s[j])) ++j;
        else j = std::min(n, i + utf8_length(c));
        out.push_back({i, j});
        i = j;
    }
    return out;
}

void add_span(std::vector<Span>& spans, size_t begin, size_t end) {
    if (begin >= end) return;
    if (!spans.empty() && spans.back().end == begin) spans.back().end = end;
    else spans.push_back({begin, end});
}
}

std::vector<model::Field> const& FieldOrder() {
    static std::vector<model::Field> const order{model::Text, model::Start, model::End, model::Style,
        model::Actor, model::Effect, model::Layer, model::MarginL, model::MarginR, model::MarginV,
        model::Comment, model::Extra};
    return order;
}

char const* FieldName(model::Field field) {
    switch (field) {
        case model::Text: return "Text";
        case model::Start: return "Start";
        case model::End: return "End";
        case model::Layer: return "Layer";
        case model::Style: return "Style";
        case model::Actor: return "Actor";
        case model::MarginL: return "Left margin";
        case model::MarginR: return "Right margin";
        case model::MarginV: return "Vertical margin";
        case model::Effect: return "Effect";
        case model::Comment: return "Comment";
        case model::Extra: return "Extra data";
        default: return "";
    }
}

std::string const& Line::Value(model::Field field) const { return values.at(bit(field)); }

TextDiff DiffText(std::string const& left, std::string const& right) {
    TextDiff out;
    auto a = tokenize(left), b = tokenize(right);
    auto same = [&](size_t i, size_t j) {
        return a[i].end - a[i].begin == b[j].end - b[j].begin
            && left.compare(a[i].begin, a[i].end - a[i].begin, right, b[j].begin, b[j].end - b[j].begin) == 0;
    };
    size_t pre = 0;
    while (pre < a.size() && pre < b.size() && same(pre, pre)) ++pre;
    size_t suf = 0;
    while (suf < a.size() - pre && suf < b.size() - pre && same(a.size() - 1 - suf, b.size() - 1 - suf)) ++suf;
    size_t n = a.size() - pre - suf, m = b.size() - pre - suf;
    auto whole = [&] {
        if (n) add_span(out.left, a[pre].begin, a[pre + n - 1].end);
        if (m) add_span(out.right, b[pre].begin, b[pre + m - 1].end);
    };
    if (!n || !m || n * m > 1000000) { whole(); return out; }

    // Longest common subsequence of the middle tokens.
    std::vector<uint32_t> lcs((n + 1) * (m + 1), 0);
    auto at = [&](size_t i, size_t j) -> uint32_t& { return lcs[i * (m + 1) + j]; };
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            at(i, j) = same(pre + i, pre + j) ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
    size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (same(pre + i, pre + j)) { ++i; ++j; }
        else if (at(i + 1, j) >= at(i, j + 1)) { add_span(out.left, a[pre + i].begin, a[pre + i].end); ++i; }
        else { add_span(out.right, b[pre + j].begin, b[pre + j].end); ++j; }
    }
    for (; i < n; ++i) add_span(out.left, a[pre + i].begin, a[pre + i].end);
    for (; j < m; ++j) add_span(out.right, b[pre + j].begin, b[pre + j].end);
    return out;
}

State::State(AssFile const& current, AssFile const& baseline, model::Fields comparison_fields,
        std::vector<model::Item> const& items, Side project_side) {
    std::vector<AssDialogue const*> cur, base;
    for (auto const& line : current.Events) cur.push_back(&line);
    for (auto const& line : baseline.Events) base.push_back(&line);
    for (size_t i = 0; i < items.size(); ++i) {
        auto const& item = items[i];
        if ((item.current && *item.current >= cur.size()) || (item.baseline && *item.baseline >= base.size()))
            throw std::invalid_argument("Comparison items do not describe these documents");
        // Built with the project on the right: the baseline file is the left side.
        Entry e;
        e.item = i;
        e.ambiguity = item.ambiguity;
        if (item.baseline) e.left = make_line(baseline, *base[*item.baseline], *item.baseline);
        if (item.current) e.right = make_line(current, *cur[*item.current], *item.current);
        switch (item.kind) {
            case model::Kind::Added: e.change = Change::Removed; break;   // only the baseline has it
            case model::Kind::Deleted: e.change = Change::Inserted; break; // only the project has it
            case model::Kind::Modified: e.change = Change::Modified; break;
            default: e.change = Change::Unchanged; break;
        }
        if (e.change == Change::Modified) {
            e.changed = item.changed & comparison_fields;
            if (!e.changed) e.change = Change::Unchanged; // nothing the comparison can apply
            else if (e.changed & model::Text) e.text = DiffText(e.left->Value(model::Text), e.right->Value(model::Text));
        }
        if (e.IsDiff()) diffs.push_back(entries.size());
        entries.push_back(std::move(e));
    }
    choices.resize(entries.size());
    checked.resize(entries.size(), false);
    SetProjectSide(project_side);
}

std::optional<size_t> State::DiffIndex(size_t entry) const {
    auto it = std::lower_bound(diffs.begin(), diffs.end(), entry);
    if (it == diffs.end() || *it != entry) return std::nullopt;
    return size_t(it - diffs.begin());
}

void State::SetProjectSide(Side side) {
    if (side == project) return;
    project = side;
    for (auto& e : entries) {
        std::swap(e.left, e.right);
        std::swap(e.text.left, e.text.right);
        if (e.change == Change::Inserted) e.change = Change::Removed;
        else if (e.change == Change::Removed) e.change = Change::Inserted;
    }
}

model::Fields State::Whole(size_t entry) const {
    auto const& e = entries.at(entry);
    return e.change == Change::Modified ? e.changed : model::All;
}

Decision State::GetDecision(size_t entry) const {
    auto const& c = choices.at(entry);
    if (!entries[entry].IsDiff() || !c.decided) return Decision::Pending;
    Side winner;
    if (c.take == 0) winner = project;
    else if (c.take == Whole(entry)) winner = FileSide();
    else return Decision::Partial;
    return winner == Side::Right ? Decision::Accepted : Decision::Rejected;
}

std::optional<Side> State::Winner(size_t entry, model::Field field) const {
    auto const& c = choices.at(entry);
    if (!entries[entry].IsDiff() || !c.decided) return std::nullopt;
    if (entries[entry].change != Change::Modified) return c.take ? FileSide() : project;
    return (c.take & field) ? FileSide() : project;
}

size_t State::Count(Decision decision) const {
    size_t n = 0;
    for (auto e : diffs) n += GetDecision(e) == decision;
    return n;
}

State::Choice State::Target(size_t entry, Decision decision) const {
    if (decision == Decision::Pending) return {};
    if (decision != Decision::Accepted && decision != Decision::Rejected)
        throw std::invalid_argument("Only Accepted, Rejected or Pending can be chosen");
    Side winner = decision == Decision::Accepted ? Side::Right : Side::Left;
    return {true, winner == FileSide() ? Whole(entry) : 0};
}

size_t State::Changing(std::vector<size_t> const& list, Decision decision) const {
    size_t n = 0;
    for (auto e : list)
        if (entries.at(e).IsDiff() && !(choices[e] == Target(e, decision))) ++n;
    return n;
}

void State::Commit(std::vector<Step> steps) {
    for (auto const& s : steps) choices[s.entry] = s.after;
    undo.push_back(std::move(steps));
    redo.clear();
}

bool State::Decide(std::vector<size_t> const& list, Decision decision) {
    std::vector<Step> steps;
    for (auto e : list) {
        if (!entries.at(e).IsDiff()) continue;
        auto after = Target(e, decision);
        if (!(choices[e] == after)) steps.push_back({e, choices[e], after});
    }
    if (steps.empty()) return false;
    Commit(std::move(steps));
    return true;
}

bool State::SetField(size_t entry, model::Field field, Side winner) {
    auto const& e = entries.at(entry);
    if (e.change != Change::Modified) throw std::logic_error("Only modified lines have fields to choose");
    if (!(e.changed & field)) throw std::invalid_argument("Field did not change in this line");
    auto after = choices[entry];
    if (!after.decided) after = {true, 0};
    if (winner == FileSide()) after.take |= field;
    else after.take &= ~model::Fields(field);
    if (after == choices[entry]) return false;
    Commit({{entry, choices[entry], after}});
    return true;
}

bool State::Undo() {
    if (undo.empty()) return false;
    auto steps = std::move(undo.back());
    undo.pop_back();
    for (auto const& s : steps) choices[s.entry] = s.before;
    redo.push_back(std::move(steps));
    return true;
}

bool State::Redo() {
    if (redo.empty()) return false;
    auto steps = std::move(redo.back());
    redo.pop_back();
    for (auto const& s : steps) choices[s.entry] = s.after;
    undo.push_back(std::move(steps));
    return true;
}

void State::Check(size_t entry, bool value) {
    if (entries.at(entry).IsDiff()) checked[entry] = value;
}

void State::CheckAll(bool value) {
    for (auto e : diffs) checked[e] = value;
}

std::vector<size_t> State::CheckedEntries() const {
    std::vector<size_t> out;
    for (auto e : diffs) if (checked[e]) out.push_back(e);
    return out;
}

std::vector<subtitle_apply::Choice> State::Choices() const {
    std::vector<subtitle_apply::Choice> out;
    for (auto e : diffs) {
        auto const& c = choices[e];
        if (!c.decided || !c.take) continue;
        out.push_back({entries[e].item, entries[e].change == Change::Modified ? c.take : model::All});
    }
    return out;
}

std::vector<LayoutRow> State::Layout(bool show_all, std::set<size_t> const& expanded, size_t context) const {
    std::vector<LayoutRow> rows;
    size_t n = entries.size();
    for (size_t i = 0; i < n;) {
        if (entries[i].IsDiff()) { rows.push_back({i, 0}); ++i; continue; }
        size_t end = i;
        while (end < n && !entries[end].IsDiff()) ++end;
        size_t head = i == 0 ? 0 : std::min(context, end - i);
        size_t tail = end == n ? 0 : std::min(context, end - i - head);
        size_t first = i + head, hidden = end - i - head - tail;
        if (show_all || hidden < 2 || expanded.count(first)) {
            for (size_t k = i; k < end; ++k) rows.push_back({k, 0});
        }
        else {
            for (size_t k = i; k < first; ++k) rows.push_back({k, 0});
            rows.push_back({first, hidden});
            for (size_t k = first + hidden; k < end; ++k) rows.push_back({k, 0});
        }
        i = end;
    }
    return rows;
}
}

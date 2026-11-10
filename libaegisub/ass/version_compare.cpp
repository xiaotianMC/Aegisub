// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <libaegisub/ass/version_compare.h>

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace agi::ass::compare {
void CheckCancelled(Cancel const& cancel) {
    if (cancel && cancel()) throw Cancelled();
}

Fields Capabilities(Format format) {
    switch (format) {
        case Format::Ass: return All;
        case Format::Ssa: return All & ~(Layer | Extra);
        case Format::Srt:
        case Format::MicroDvd:
        case Format::Ttxt: return Text | Start | End;
        case Format::Txt: return Text | Actor | Comment;
        default: return 0;
    }
}

Fields ChangedFields(Row const& b, Row const& c, Fields fields) {
    Fields changed = 0;
    auto test = [&](Field field, bool different) { if (different) changed |= field; };
    test(Text, b.text != c.text); test(Start, b.start_ms != c.start_ms);
    test(End, b.end_ms != c.end_ms); test(Layer, b.layer != c.layer);
    test(Style, b.style != c.style); test(Actor, b.actor != c.actor);
    test(Effect, b.effect != c.effect); test(Comment, b.comment != c.comment);
    test(MarginL, b.margins[0] != c.margins[0]);
    test(MarginR, b.margins[1] != c.margins[1]);
    test(MarginV, b.margins[2] != c.margins[2]); test(Extra, b.extra != c.extra);
    return changed & fields;
}

namespace {
void append(std::string& out, std::string const& value) {
    out += std::to_string(value.size()); out += ':'; out += value;
}
std::string key(Row const& row, Fields fields) {
    std::string out;
    if (fields & Text) append(out, row.text);
    if (fields & Start) append(out, std::to_string(row.start_ms));
    if (fields & End) append(out, std::to_string(row.end_ms));
    if (fields & Layer) append(out, std::to_string(row.layer));
    if (fields & Style) append(out, row.style);
    if (fields & Actor) append(out, row.actor);
    if (fields & Effect) append(out, row.effect);
    if (fields & Comment) append(out, row.comment ? "1" : "0");
    for (int i = 0; i < 3; ++i)
        if (fields & (MarginL << i)) append(out, std::to_string(row.margins[i]));
    if (fields & Extra) for (auto const& [k, v] : row.extra) { append(out, k); append(out, v); }
    return out;
}
struct Occurrence { size_t index = 0, count = 0; };
using Index = std::unordered_map<std::string, Occurrence>;
struct Pair { size_t b, c; MatchReason reason; };
}

std::vector<Item> Compare(Document const& baseline, Document const& current, Cancel const& cancel) {
    auto const& B = baseline.rows; auto const& C = current.rows;
    Fields fields = baseline.fields & current.fields;
    if (!fields) throw std::invalid_argument("No reliably comparable fields");
    CheckCancelled(cancel);
    std::vector<Pair> pairs;
    std::vector<Ambiguity> why_b(B.size(), Ambiguity::NoReliableMatch), why_c(C.size(), Ambiguity::NoReliableMatch);
    // Equal boundary runs are safe to display together even when occurrences
    // are duplicated: they imply no write and do not assert historical identity.
    size_t prefix = 0;
    while (prefix < B.size() && prefix < C.size() && !ChangedFields(B[prefix], C[prefix], fields)) {
        CheckCancelled(cancel);
        pairs.push_back({prefix, prefix, MatchReason::EqualSequence});
        ++prefix;
    }
    size_t be = B.size(), ce = C.size();
    while (be > prefix && ce > prefix && !ChangedFields(B[be-1], C[ce-1], fields)) {
        CheckCancelled(cancel); --be; --ce;
        pairs.push_back({be, ce, MatchReason::EqualSequence});
    }
    // Three matching passes plus an equal-run cleanup. Each scans disjoint gaps; no n*m matrix
    // and no recursive rescanning of progressively smaller intervals.
    for (int pass = 0; pass < 4; ++pass) {
        Fields match_fields = pass == 1 ? Start | End : pass == 2 ? Text : fields;
        if ((fields & match_fields) != match_fields) continue;
        std::sort(pairs.begin(), pairs.end(), [](Pair a, Pair b) { return a.b < b.b; });
        auto anchors = pairs;
        anchors.push_back({B.size(), C.size(), MatchReason::None});
        size_t b0 = 0, c0 = 0;
        for (auto anchor : anchors) {
            CheckCancelled(cancel);
            size_t b_end = anchor.b, c_end = anchor.c;
            while (b0 < b_end && c0 < c_end && !ChangedFields(B[b0], C[c0], fields)) {
                CheckCancelled(cancel);
                pairs.push_back({b0++, c0++, MatchReason::EqualSequence});
            }
            while (b_end > b0 && c_end > c0 && !ChangedFields(B[b_end-1], C[c_end-1], fields)) {
                CheckCancelled(cancel);
                pairs.push_back({--b_end, --c_end, MatchReason::EqualSequence});
            }
            if (pass == 3) { b0 = anchor.b + 1; c0 = anchor.c + 1; continue; }
            Index bi, ci, bt, ct, btime, ctime;
            auto build = [&](auto const& rows, size_t begin, size_t end, Index& index, Fields f) {
                for (size_t i = begin; i < end; ++i) {
                    CheckCancelled(cancel); auto& occ = index[key(rows[i], f)]; occ.index = i; ++occ.count;
                }
            };
            build(B, b0, b_end, bi, match_fields); build(C, c0, c_end, ci, match_fields);
            if (pass && (fields & Text) && (fields & (Start | End)) == (Start | End)) {
                build(B, b0, b_end, bt, Text); build(C, c0, c_end, ct, Text);
                build(B, b0, b_end, btime, Start | End); build(C, c0, c_end, ctime, Start | End);
            }
            auto unique_partner = [&](Row const& row, Index const& left, Index const& right, Fields f) -> std::optional<size_t> {
                auto k = key(row, f); auto l = left.find(k), r = right.find(k);
                if (l != left.end() && r != right.end() && l->second.count == 1 && r->second.count == 1) return r->second.index;
                return {};
            };
            std::vector<Pair> candidates;
            for (size_t b = b0; b < b_end; ++b) {
                CheckCancelled(cancel);
                if (why_b[b] == Ambiguity::Crossing || why_b[b] == Ambiguity::ConflictingEvidence) continue;
                auto it = ci.find(key(B[b], match_fields));
                if (it == ci.end()) continue;
                auto const& bo = bi.at(key(B[b], match_fields)); auto const& co = it->second;
                if (bo.count != 1 || co.count != 1) {
                    if (why_b[b] == Ambiguity::NoReliableMatch) why_b[b] = Ambiguity::Duplicate;
                    continue;
                }
                size_t c = co.index;
                if (why_c[c] == Ambiguity::Crossing || why_c[c] == Ambiguity::ConflictingEvidence) {
                    why_b[b] = why_c[c]; continue;
                }
                auto time = unique_partner(B[b], btime, ctime, Start | End);
                auto text = unique_partner(B[b], bt, ct, Text);
                if (time && text && *time != *text) {
                    why_b[b] = why_c[*time] = why_c[*text] = Ambiguity::ConflictingEvidence; continue;
                }
                candidates.push_back({b, c, pass == 0 ? MatchReason::ExactFields : pass == 1 ? MatchReason::UniqueTime : MatchReason::UniqueText});
            }
            // Mark BOTH endpoints of every inversion, rather than picking an
            // arbitrary LIS and then silently matching moved lines by time.
            std::vector<size_t> suffix(candidates.size(), C.size());
            for (size_t i = candidates.size(); i > 1; --i)
                suffix[i-2] = std::min(suffix[i-1], candidates[i-1].c);
            size_t max_c = 0; bool have_max = false;
            for (size_t i = 0; i < candidates.size(); ++i) {
                CheckCancelled(cancel); auto p = candidates[i];
                bool crossed = (have_max && max_c > p.c) || suffix[i] < p.c;
                max_c = have_max ? std::max(max_c, p.c) : p.c; have_max = true;
                if (crossed) { why_b[p.b] = why_c[p.c] = Ambiguity::Crossing; continue; }
                if (why_c[p.c] == Ambiguity::ConflictingEvidence) { why_b[p.b] = Ambiguity::ConflictingEvidence; continue; }
                pairs.push_back(p);
            }
            // Mark duplicate candidates on the current side as well.
            for (size_t c = c0; c < c_end; ++c) {
                auto k = key(C[c], match_fields); auto it = bi.find(k);
                if (it != bi.end() && (it->second.count > 1 || ci.at(k).count > 1) && why_c[c] == Ambiguity::NoReliableMatch)
                    why_c[c] = Ambiguity::Duplicate;
            }
            b0 = anchor.b + 1; c0 = anchor.c + 1;
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](Pair a, Pair b) { return a.b < b.b; });
    pairs.push_back({B.size(), C.size(), MatchReason::None});
    std::vector<Item> result;
    size_t b = 0, c = 0;
    for (auto p : pairs) {
        for (; b < p.b; ++b) { CheckCancelled(cancel); result.push_back({b, {}, Kind::Added, 0, MatchReason::None, why_b[b], c}); }
        for (; c < p.c; ++c) { CheckCancelled(cancel); result.push_back({{}, c, Kind::Deleted, 0, MatchReason::None, why_c[c], c}); }
        if (p.b == B.size()) break;
        auto changed = ChangedFields(B[p.b], C[p.c], fields);
        result.push_back({p.b, p.c, changed ? Kind::Modified : Kind::Unchanged, changed, p.reason, Ambiguity::None, p.c});
        b = p.b + 1; c = p.c + 1;
    }
    CheckCancelled(cancel);
    return result;
}

ExtraAnalysis ResolveExtras(std::vector<ExtraEntry> const& entries,
    std::vector<std::vector<uint32_t>> const& references, Cancel const& cancel) {
    CheckCancelled(cancel);
    ExtraAnalysis out;
    std::map<uint32_t, ExtraEntry const*> index;
    for (auto const& entry : entries) {
        CheckCancelled(cancel);
        if (!index.emplace(entry.id, &entry).second) { out.valid = false; out.issues.push_back({ExtraIssueKind::DuplicateId, entry.id, entry.key}); }
    }
    for (auto const& refs : references) {
        CheckCancelled(cancel); Extras row;
        for (auto id : refs) {
            CheckCancelled(cancel); auto it = index.find(id);
            if (it == index.end()) { out.valid = false; out.issues.push_back({ExtraIssueKind::MissingId, id, {}}); continue; }
            auto const& entry = *it->second;
            if (!row.emplace(entry.key, entry.value).second) { out.valid = false; out.issues.push_back({ExtraIssueKind::DuplicateKey, id, entry.key}); }
            out.issues.push_back({entry.key == "aegisub.enhanced.subtitle-binding" ? ExtraIssueKind::BindingPolicyRequired : ExtraIssueKind::UnknownDependency, id, entry.key});
        }
        out.rows.push_back(std::move(row));
    }
    return out;
}

ExtraMapping PlanExtraMapping(std::vector<ExtraEntry> const& source,
    std::vector<ExtraEntry> const& destination, uint32_t next_id, Cancel const& cancel) {
    CheckCancelled(cancel);
    ExtraMapping out; out.next_id = next_id;
    std::map<std::pair<std::string, std::string>, uint32_t> values;
    std::map<uint32_t, bool> ids;
    for (auto const& entry : destination) {
        CheckCancelled(cancel);
        if (!ids.emplace(entry.id, true).second) throw std::invalid_argument("Duplicate destination extradata ID");
        if (entry.id == std::numeric_limits<uint32_t>::max()) throw std::overflow_error("Extradata ID exhausted");
        out.next_id = std::max(out.next_id, entry.id + 1);
        values.emplace(std::make_pair(entry.key, entry.value), entry.id);
    }
    for (auto const& entry : source) {
        CheckCancelled(cancel);
        if (out.ids.count(entry.id)) throw std::invalid_argument("Duplicate source extradata ID");
        auto value = std::make_pair(entry.key, entry.value); auto found = values.find(value);
        uint32_t id;
        if (found != values.end()) id = found->second;
        else {
            if (out.next_id == std::numeric_limits<uint32_t>::max()) throw std::overflow_error("Extradata ID exhausted");
            id = out.next_id++; values.emplace(std::move(value), id);
            out.additions.push_back({id, entry.key, entry.value});
        }
        out.ids.emplace(entry.id, id);
    }
    return out;
}
}

// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include "subtitle_project_access.h"
#include "ass_info.h"
#include "ass_attachment.h"
#include <boost/algorithm/string/case_conv.hpp>
#include <thread>
#include <algorithm>
#include <unordered_map>

struct SubtitleProjectAccess::ActiveWrite {
    agi::SubtitleEditGate::WriteLease lease;
    explicit ActiveWrite(agi::SubtitleEditGate::WriteLease&& lease) : lease(std::move(lease)) { }
};
struct SubtitleProjectAccess::State {
    AssFile *file;
    std::thread::id thread = std::this_thread::get_id();
    bool alive = true;
    std::weak_ptr<ActiveWrite> active;
    agi::SubtitleEditGate *gate = nullptr;
    void Check() const {
        if (thread != std::this_thread::get_id()) throw std::logic_error("Live subtitles require the owner thread");
        if (!alive) throw std::logic_error("Subtitle project access closed");
    }
};
SubtitleProjectAccess::WriteScope::WriteScope(std::shared_ptr<State> const& owner, std::shared_ptr<ActiveWrite> active)
: owner(owner), active(std::move(active)) { }
agi::SubtitleEditGate::WriteLease const& SubtitleProjectAccess::WriteScope::Lease() const {
    auto state = owner.lock();
    if (!state) throw std::logic_error("Expired subtitle edit scope");
    state->Check();
    if (!active || !active->lease.Valid()) throw std::logic_error("Invalid subtitle edit scope");
    return active->lease;
}
std::optional<SubtitleProjectAccess::WriteScope> SubtitleProjectAccess::TryWrite() {
    state->Check();
    // Undo snapshot maintenance (PushState) precedes observer notification.
    // Ordinary observers may refresh UI, but may not reenter model editing.
    if (state->file->IsNotifying()) return {};
    if (auto active = state->active.lock()) {
        if (!active->lease.Valid()) return {};
        return WriteScope(state, std::move(active));
    }
    auto lease = gate.TryBeginWrite();
    if (!lease) return {};
    auto active = std::make_shared<ActiveWrite>(std::move(*lease));
    state->active = active;
    return WriteScope(state, std::move(active));
}
SubtitleProjectAccess::SubtitleProjectAccess(AssFile& file, agi::SubtitleEditGate& gate)
: state(std::make_shared<State>(&file)), gate(gate) {
    if (file.edit_gate) throw std::logic_error("Subtitle file already belongs to a project");
    file.edit_gate = &gate;
    state->gate = &gate;
}
SubtitleProjectAccess::Interaction::Interaction(std::shared_ptr<State> const& owner, WriteScope scope,
    agi::SubtitleEditGate::RevisionToken revision, std::vector<int> targets)
: owner(owner), scope(std::move(scope)), revision(std::move(revision)), targets(std::move(targets)) { }
bool SubtitleProjectAccess::Interaction::Current() const {
    auto state = owner.lock();
    if (!state || !scope || !revision.Current()) return false;
    if (state->thread != std::this_thread::get_id()) throw std::logic_error("Interaction requires owner thread");
    return state->alive && !state->file->IsNotifying();
}
std::optional<SubtitleProjectAccess::Interaction> SubtitleProjectAccess::TryBeginInteraction(std::vector<int> targets) {
    state->Check();
    std::sort(targets.begin(), targets.end());
    if (targets.empty() || std::adjacent_find(targets.begin(), targets.end()) != targets.end())
        throw std::invalid_argument("Interaction requires distinct target line IDs");
    std::unordered_map<int, bool> present;
    for (auto const& row : state->file->Events)
        if (!present.emplace(row.Id, true).second) throw std::logic_error("Duplicate live subtitle ID");
    for (auto id : targets) if (!present.contains(id)) throw std::out_of_range("Interaction target missing");
    auto scope = TryWrite();
    if (!scope) return {};
    return Interaction(state, std::move(*scope), gate.Capture(), std::move(targets));
}
void SubtitleProjectAccess::Interaction::UpdateRows(std::vector<AssDialogueBase> const& updates) {
    try {
        if (!Current()) throw std::logic_error("Stale subtitle interaction");
        auto state = owner.lock();
        scope->Lease();
        std::unordered_map<int, AssDialogue*> live;
        for (auto& row : state->file->Events)
            if (!live.emplace(row.Id, &row).second) throw std::logic_error("Duplicate live subtitle ID");
        // Validate every original target, not only the subset written by this callback.
        for (auto id : targets) if (!live.contains(id)) throw std::out_of_range("Interaction target removed");
        for (auto const& row : updates)
            if (!std::binary_search(targets.begin(), targets.end(), row.Id))
                throw std::invalid_argument("Unauthorized interaction update target");
        WriteAccess(state, scope->Lease().Capture()).UpdateRows(updates);
    }
    catch (...) { End(); throw; }
}
void SubtitleProjectAccess::Interaction::UpdateLayout(AssFile const& calculated) {
    try {
        if(!Current())throw std::logic_error("Stale video properties");
        auto state=owner.lock();scope->Lease();
        auto& file=*state->file;
        if(file.Events.size()!=calculated.Events.size() || file.Events.size()!=targets.size() || file.Styles.size()!=calculated.Styles.size())
            throw std::invalid_argument("Layout update cannot change structure");
        auto info=calculated.Info;
        std::vector<AssStyle> styles;styles.reserve(calculated.Styles.size());
        auto original=file.Styles.begin();
        for(auto const& style:calculated.Styles) {
            if(style.name!=(original++)->name)throw std::invalid_argument("Layout update cannot rename styles");
            styles.push_back(style);
        }
        std::vector<AssDialogueBase> rows;rows.reserve(calculated.Events.size());
        auto row=file.Events.begin();
        for(auto const& candidate:calculated.Events) {
            if(candidate.Id!=row->Id || candidate.Row!=row->Row)throw std::invalid_argument("Layout update cannot reorder rows");
            rows.push_back(candidate);++row;
        }
        // All validation and allocations finish before changing the first value.
        // UpdateRows copies existing flyweight handles, never constructs new text.
        UpdateRows(rows);
        auto prepared=styles.begin();for(auto& style:file.Styles)style.SwapData(*prepared++);
        file.Info.swap(info);
    } catch (...) {End();throw;}
}
int SubtitleProjectAccess::Interaction::InsertBefore(int target_id, AssDialogueBase const& data, AssStyle const* missing_style) {
    try {
        if (!Current() || !std::binary_search(targets.begin(), targets.end(), target_id))
            throw std::logic_error("Invalid visual insertion target");
        auto state = owner.lock();
        scope->Lease();
        std::unordered_map<int, AssDialogue*> live;
        for (auto& row : state->file->Events)
            if (!live.emplace(row.Id, &row).second) throw std::logic_error("Duplicate live subtitle ID");
        for (auto id : targets) if (!live.contains(id)) throw std::out_of_range("Visual target removed");
        if (data.Id <= 0 || live.contains(data.Id)) throw std::invalid_argument("Invalid inserted row ID");
        if (!data.ExtradataIds.get().empty()) throw std::invalid_argument("Drawing insertion does not import extradata");
        if (missing_style && data.Style.get() != missing_style->name)
            throw std::invalid_argument("Drawing style does not match inserted style");
        if (!missing_style && !state->file->GetStyle(data.Style))
            throw std::invalid_argument("Missing drawing style");
        if (missing_style && state->file->GetStyle(missing_style->name))
            throw std::logic_error("Insertion style already exists");
        auto row = std::make_unique<AssDialogue>(data);
        auto style = missing_style ? std::make_unique<AssStyle>(*missing_style) : nullptr;
        // Intrusive insertions below allocate nothing. A node belongs to one file only.
        if (style) state->file->Styles.push_back(*style.release());
        state->file->Events.insert(state->file->iterator_to(*live.at(target_id)), *row.release());
        return data.Id;
    }
    catch (...) { End(); throw; }
}
bool SubtitleProjectAccess::Interaction::LinkAdjacent(int id,bool start,int old,int threshold) {
    try {
        if(!Current() || !std::binary_search(targets.begin(),targets.end(),id))throw std::logic_error("Invalid audio neighbor target");
        auto state=owner.lock();scope->Lease();
        // The existing helper may also change a neighbor. Require authorization
        // for the complete live set before delegating its neighbor search.
        std::vector<int> live_ids;
        live_ids.reserve(state->file->Events.size());
        for(auto const& row:state->file->Events) live_ids.push_back(row.Id);
        std::sort(live_ids.begin(),live_ids.end());
        if(live_ids!=targets)throw std::logic_error("Unauthorized or stale audio neighbor set");
        for(auto& row:state->file->Events)if(row.Id==id)return state->file->LinkAdjacentLine(&row,start,old,threshold,true);
        throw std::out_of_range("Audio target missing");
    } catch (...) { End();throw; }
}
int SubtitleProjectAccess::Interaction::Commit(wxString const& description, int type, int amend_id, int single_line_id) {
    try {
        if (!Current()) throw std::logic_error("Stale subtitle interaction");
        auto state = owner.lock();
        scope->Lease();
        AssDialogue *single=nullptr;
        if(single_line_id) {
            if(!std::binary_search(targets.begin(),targets.end(),single_line_id))throw std::logic_error("Unauthorized single line commit");
            for(auto& row:state->file->Events)if(row.Id==single_line_id)single=&row;
            if(!single)throw std::out_of_range("Commit target missing");
        }
        auto id = state->file->Commit(description,type,amend_id,single);
        auto next = state->gate->Capture();
        if (next.Revision().generation != revision.Revision().generation + 1)
            throw std::logic_error("Unexpected nested subtitle revision during interaction commit");
        revision = std::move(next);
        return id;
    }
    catch (...) { End(); throw; }
}
SubtitleProjectAccess::~SubtitleProjectAccess() { Close(); }
void SubtitleProjectAccess::Close() noexcept {
    state->alive = false;
    if (state->file->edit_gate == &gate) state->file->edit_gate = nullptr;
}
AssFile const& SubtitleProjectAccess::Read() const { state->Check(); return *state->file; }
AssFile const& SubtitleProjectAccess::Read(agi::SubtitleEditGate::RevisionToken const& token) const {
    state->Check();
    if (!gate.Authorizes(token)) throw std::logic_error("Stale subtitle observer");
    return *state->file;
}
SubtitleProjectAccess::WriteAccess SubtitleProjectAccess::Edit(agi::SubtitleEditGate::WriteLease const& lease) {
    state->Check();
    if (!gate.Authorizes(lease)) throw std::logic_error("Subtitle write lease belongs to another project or expired");
    return WriteAccess(state, lease.Capture());
}
SubtitleProjectAccess::WriteAccess::WriteAccess(std::shared_ptr<State> const& state, agi::SubtitleEditGate::WriteLease::Token token)
: state(state), token(std::move(token)) { }
SubtitleProjectAccess::WriteAccess::Borrow SubtitleProjectAccess::WriteAccess::Check() const {
    auto owner = state.lock();
    if (!owner) throw std::logic_error("Subtitle project access expired");
    owner->Check();
    if (owner->file->IsNotifying()) throw std::logic_error("Subtitle observers cannot write during notification");
    auto authority = token.Hold();
    auto file = owner->file;
    return {std::move(authority), std::move(owner), file};
}
void SubtitleProjectAccess::WriteAccess::SetExtradataValue(uint32_t id, std::string value) const {
    auto access = Check();
    for (auto& extra : access.file->Extradata)
        if (extra.id == id) { extra.value = std::move(value); return; }
    throw std::out_of_range("Subtitle extradata no longer exists");
}

void SubtitleProjectAccess::WriteAccess::UpdateRows(std::vector<AssDialogueBase> const& updates) const {
    auto access = Check();
    std::unordered_map<int, AssDialogue*> live;
    for (auto& row : access.file->Events)
        if (!live.emplace(row.Id, &row).second) throw std::logic_error("Duplicate live subtitle ID");
    std::set<int> written;
    for (auto const& row : updates) {
        auto found = live.find(row.Id);
        if (found == live.end()) throw std::out_of_range("Subtitle update target missing");
        if (!written.insert(row.Id).second || row.Row != found->second->Row)
            throw std::invalid_argument("Invalid or duplicate subtitle update");
    }
    for (auto const& row : updates) static_cast<AssDialogueBase&>(*live.at(row.Id)) = row;
}
bool SubtitleProjectAccess::WriteAccess::LinkAdjacent(int id, bool start, int old, int threshold, bool exact) const {
    auto access = Check();
    for (auto& row : access.file->Events)
        if (row.Id == id) return access.file->LinkAdjacentLine(&row, start, old, threshold, exact);
    throw std::out_of_range("Subtitle neighbor target missing");
}
int SubtitleProjectAccess::WriteAccess::Commit(wxString const& message, int type, int amend, int id) const {
    auto access = Check();
    AssDialogue* single = nullptr;
    for (auto& row : access.file->Events) if (row.Id == id) single = &row;
    if (id && !single) throw std::out_of_range("Subtitle commit target missing");
    return access.file->Commit(message, type, amend, single);
}

namespace {
auto RowFields(AssDialogueBase const& row) {
    return std::make_tuple(row.Id,row.Row,row.Comment,row.Layer,row.Margin,
        row.Start.GetMilliseconds(),row.End.GetMilliseconds(),row.Style,row.Actor,row.Effect,row.ExtradataIds,row.Text);
}
}
std::vector<std::unique_ptr<AssDialogue>> SubtitleProjectAccess::WriteAccess::ReplaceRows(
    agi::SubtitleEditGate::RevisionToken const& revision, std::vector<AssDialogueBase> const& originals,
    std::vector<AssDialogueBase> const& result) const {
    auto access=Check();
    if(!access.owner->gate->Authorizes(revision))throw std::logic_error("Stale row-list edit");
    auto& events=access.file->Events;
    if(events.size()!=originals.size())throw std::logic_error("Row-list structure changed");
    std::unordered_map<int,AssDialogue*> live;
    size_t i=0;
    for(auto& row:events) {
        if(RowFields(row)!=RowFields(originals[i++]))throw std::logic_error("Row-list data changed");
        if(!live.emplace(row.Id,&row).second)throw std::logic_error("Duplicate live row ID");
    }
    std::set<int> seen;
    std::vector<std::unique_ptr<AssDialogue>> allocated;
    std::vector<AssDialogue*> prepared;
    allocated.reserve(result.size());prepared.reserve(result.size());
    for(auto const& row:result) {
        if(row.Id<=0 || !seen.insert(row.Id).second)throw std::invalid_argument("Invalid row-list identity");
        auto found=live.find(row.Id);
        if(found!=live.end())prepared.push_back(found->second);
        else {allocated.push_back(std::make_unique<AssDialogue>(row));prepared.push_back(allocated.back().get());}
    }
    std::vector<std::unique_ptr<AssDialogue>> retired;retired.reserve(originals.size());
    EntryList<AssDialogue> old;
    old.swap(events);
    for(size_t j=0;j<prepared.size();++j) {
        auto row=prepared[j];
        if(live.contains(row->Id))old.erase(old.iterator_to(*row));
        static_cast<AssDialogueBase&>(*row)=result[j];
        events.push_back(*row);
    }
    for(auto& row:allocated)row.release();
    while(!old.empty()) {auto row=&old.front();old.pop_front();retired.emplace_back(row);}
    return retired;
}

namespace {
auto StyleFields(AssStyle const& s) {
    return std::tie(s.name,s.font,s.fontsize,s.primary,s.secondary,s.outline,s.shadow,s.bold,s.italic,
        s.underline,s.strikeout,s.scalex,s.scaley,s.spacing,s.angle,s.borderstyle,s.outline_w,s.shadow_w,
        s.alignment,s.Margin,s.encoding,s.GetEntryData());
}
}
std::vector<std::unique_ptr<AssStyle>> SubtitleProjectAccess::WriteAccess::UpdateValues(
    agi::SubtitleEditGate::RevisionToken const& revision, AssFile const& original, AssFile const& result,
    std::vector<int> const& sources, int parts) const {
    auto access=Check();auto& file=*access.file;
    if(!access.owner->gate->Authorizes(revision))throw std::logic_error("Stale ordinary value edit");
    constexpr int supported=AssFile::COMMIT_DIAG_FULL|AssFile::COMMIT_STYLES|AssFile::COMMIT_SCRIPTINFO|AssFile::COMMIT_EXTRADATA|AssFile::COMMIT_ATTACHMENT;
    if(!parts || (parts & ~supported))throw std::invalid_argument("Unsupported value edit");
    if(file.Events.size()!=original.Events.size() || result.Events.size()!=original.Events.size() ||
       file.Styles.size()!=original.Styles.size() || file.Info.size()!=original.Info.size() ||
       file.Attachments.size()!=original.Attachments.size() || file.Extradata.size()!=original.Extradata.size() ||
       file.next_extradata_id!=original.next_extradata_id)throw std::logic_error("Ordinary value dependencies changed");
    auto old_row=original.Events.begin();auto output=result.Events.begin();
    std::vector<AssDialogueBase> rows;rows.reserve(result.Events.size());
    for(auto const& row:file.Events) {
        if(RowFields(row)!=RowFields(*old_row++) || output->Id!=row.Id || output->Row!=row.Row)
            throw std::logic_error("Ordinary value row changed or reordered");
        rows.push_back(*output++);
    }
    auto old_style=original.Styles.begin();std::vector<AssStyle*> live_styles;
    for(auto& style:file.Styles) {
        if(StyleFields(style)!=StyleFields(*old_style++))throw std::logic_error("Style dependencies changed");
        live_styles.push_back(&style);
    }
    for(size_t i=0;i<file.Info.size();++i)
        if(file.Info[i].Key()!=original.Info[i].Key() || file.Info[i].Value()!=original.Info[i].Value())throw std::logic_error("Script info changed");
    for(size_t i=0;i<file.Attachments.size();++i)
        if(file.Attachments[i].Group()!=original.Attachments[i].Group() || file.Attachments[i].GetEntryData()!=original.Attachments[i].GetEntryData())throw std::logic_error("Attachments changed");
    for(size_t i=0;i<file.Extradata.size();++i) {
        auto const& a=file.Extradata[i];auto const& b=original.Extradata[i];
        if(std::tie(a.id,a.key,a.value)!=std::tie(b.id,b.key,b.value))throw std::logic_error("Extradata changed");
    }
    std::vector<AssStyle> values;std::vector<std::unique_ptr<AssStyle>> allocated;
    std::vector<AssStyle*> prepared;
    if(parts & AssFile::COMMIT_STYLES) {
        if(sources.size()!=result.Styles.size())throw std::invalid_argument("Missing style origins");
        std::set<int> used;size_t i=0;
        std::map<std::string,size_t> existing_names,names;
        for(auto const& style:original.Styles)++existing_names[boost::to_lower_copy(style.name)];
        values.reserve(sources.size());allocated.reserve(sources.size());prepared.reserve(sources.size());
        for(auto const& style:result.Styles) {
            auto name=boost::to_lower_copy(style.name);
            if(++names[name]>std::max<size_t>(1,existing_names[name]))throw std::invalid_argument("Conflicting style name");
            int source=sources[i++];
            if(source < -1 || source>=static_cast<int>(live_styles.size()) || (source>=0 && !used.insert(source).second))throw std::invalid_argument("Invalid style origin");
            values.push_back(style);
            if(source<0) {allocated.push_back(std::make_unique<AssStyle>(style));prepared.push_back(allocated.back().get());}
            else prepared.push_back(live_styles[source]);
        }
    }
    auto info=result.Info;auto attachments=result.Attachments;auto extras=result.Extradata;
    if(parts & AssFile::COMMIT_EXTRADATA) {
        std::set<uint32_t> ids;
        for(auto const& entry:extras)
            if(!ids.insert(entry.id).second || entry.id>=result.next_extradata_id)throw std::invalid_argument("Invalid extradata identity");
        old_row=original.Events.begin();
        for(auto const& row:result.Events) {
            auto const& previous=(old_row++)->ExtradataIds.get();
            for(auto id:row.ExtradataIds.get())
                if(!ids.contains(id) && std::find(previous.begin(),previous.end(),id)==previous.end())throw std::invalid_argument("Unresolved new extradata reference");
        }
    }
    std::vector<std::unique_ptr<AssStyle>> retired;retired.reserve(live_styles.size());
    // All validation and preparation precede the first live write.
    if(parts & AssFile::COMMIT_DIAG_FULL)UpdateRows(rows);
    if(parts & AssFile::COMMIT_STYLES) {
        EntryList<AssStyle> old;old.swap(file.Styles);
        for(size_t i=0;i<prepared.size();++i) {
            if(sources[i]>=0)old.erase(old.iterator_to(*prepared[i]));
            prepared[i]->SwapData(values[i]);file.Styles.push_back(*prepared[i]);
        }
        for(auto& value:allocated)value.release();
        while(!old.empty()){auto style=&old.front();old.pop_front();retired.emplace_back(style);}
    }
    if(parts & AssFile::COMMIT_SCRIPTINFO)file.Info.swap(info);
    if(parts & AssFile::COMMIT_ATTACHMENT)file.Attachments.swap(attachments);
    if(parts & AssFile::COMMIT_EXTRADATA){file.Extradata.swap(extras);file.next_extradata_id=result.next_extradata_id;}
    return retired;
}

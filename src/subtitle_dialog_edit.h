// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include "include/aegisub/context.h"
#include "subtitle_project_access.h"
#include "selection_controller.h"
#include "ass_info.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/scope_exit.hpp>
#include <wx/window.h>
#include <wx/weakref.h>
#include <set>
#include <map>
#include <tuple>

// Dialog-owned values and a pending task, not a second lock or an application
// transaction. Idle dialogs hold no lease. Callers block their own Commit slot.
class SubtitleDialogEdit {
    agi::Context* c;
    wxWeakRef<wxWindow> parent;
    agi::SubtitleEditGate::RevisionToken lifetime;
    std::optional<agi::SubtitleEditGate::RevisionToken> observed;
    std::optional<SubtitleProjectAccess::Interaction> interaction;
    std::vector<AssDialogueBase> rows;
    std::map<int,size_t> index;
    std::vector<AssStyle> styles;
    std::vector<std::pair<std::string,std::string>> info;
    std::vector<int> order;
    std::vector<std::tuple<uint32_t,std::string,std::string>> extras;
    size_t file_rows = 0;
    bool stale = false;
    static auto Fields(AssDialogueBase const& r) {
        return std::make_tuple(r.Id,r.Row,r.Comment,r.Layer,r.Margin,r.Start.GetMilliseconds(),r.End.GetMilliseconds(),
            r.Style,r.Actor,r.Effect,r.ExtradataIds,r.Text);
    }
    static auto StyleFields(AssStyle const& s) {
        return std::tie(s.name,s.font,s.fontsize,s.primary,s.secondary,s.outline,s.shadow,s.bold,s.italic,
            s.underline,s.strikeout,s.scalex,s.scaley,s.spacing,s.angle,s.borderstyle,s.outline_w,s.shadow_w,
            s.alignment,s.Margin,s.encoding,s.GetEntryData());
    }
public:
    explicit SubtitleDialogEdit(agi::Context* c) : c(c),parent(c->parent),lifetime(c->subtitleEditGate.Capture()) {}
    bool Alive() const { return parent && lifetime.Alive(); }
    bool Current() const { return Alive() && !stale && (interaction ? interaction->Current() : observed && observed->Current()); }
    void End() noexcept { interaction.reset(); }
    void Invalidate() noexcept { End();stale=true; }
    static int ActiveID(agi::Context const* c) {
        auto active=c->selectionController->ReadActiveLine();
        for(auto const& row:c->subtitleAccess->Read().Events)if(&row==active)return row.Id;
        return 0;
    }
    void Reset(std::vector<int> const& ids) {
        End();stale=true;rows.clear();index.clear();styles.clear();info.clear();
        if(!Alive())return;
        std::set<int> requested(ids.begin(),ids.end());
        if(requested.size()!=ids.size())throw std::invalid_argument("Duplicate dialog target ID");
        auto const& file=c->subtitleAccess->Read();file_rows=file.Events.size();
        order.clear();extras.clear();
        for(auto const& row:file.Events)order.push_back(row.Id);
        for(auto const& e:file.Extradata)extras.emplace_back(e.id,e.key,e.value);
        for(auto const& row:file.Events)if(requested.contains(row.Id))rows.push_back(row);
        if(rows.size()!=ids.size())throw std::out_of_range("Dialog target missing");
        for(size_t i=0;i<rows.size();++i)if(!index.emplace(rows[i].Id,i).second)throw std::logic_error("Duplicate live dialog ID");
        for(auto const& style:file.Styles)styles.push_back(style);
        for(auto const& entry:file.Info)info.emplace_back(entry.Key(),entry.Value());
        observed=c->subtitleEditGate.Capture();stale=false;
    }
    void ResetAll() {
        if(!Alive())return;
        std::vector<int> ids;for(auto const& row:c->subtitleAccess->Read().Events)ids.push_back(row.Id);
        Reset(ids);
    }
    void ResetActive() { if(Alive()){auto id=ActiveID(c);Reset(id?std::vector<int>{id}:std::vector<int>{});} }
    std::vector<AssDialogueBase> const& Rows() const { return rows; }
    AssDialogueBase const* Find(int id) const {
        auto it=index.find(id);return it==index.end()?nullptr:&rows[it->second];
    }
    bool HasStyle(std::string const& name) const {
        return std::any_of(styles.begin(),styles.end(),[&](auto const& s){return boost::iequals(s.name,name);});
    }
    bool Validate() const {
        // Acquiring an ordinary no-op UI lease also advances the gate. Exact
        // values are checked below before rebasing an idle snapshot.
        if(!Alive() || stale || (interaction && !interaction->Current()))return false;
        auto const& file=c->subtitleAccess->Read();
        if(file.Events.size()!=file_rows || file.Styles.size()!=styles.size() || file.Info.size()!=info.size())return false;
        auto style=styles.begin();for(auto const& current:file.Styles)if(StyleFields(current)!=StyleFields(*style++))return false;
        size_t i=0;for(auto const& entry:file.Info)if(entry.Key()!=info[i].first || entry.Value()!=info[i++].second)return false;
        size_t position=0;for(auto const& row:file.Events)if(row.Id!=order[position++])return false;
        if(file.Extradata.size()!=extras.size())return false;
        position=0;for(auto const& e:file.Extradata)if(std::tie(e.id,e.key,e.value)!=extras[position++])return false;
        size_t found=0;
        for(auto const& current:file.Events)if(auto original=Find(current.Id)) {
            if(Fields(current)!=Fields(*original))return false;
            ++found;
        }
        return found==rows.size();
    }
    bool Begin() {
        if(!Validate()){Invalidate();return false;}
        if(interaction)return true;
        std::vector<int> ids;for(auto const& row:rows)ids.push_back(row.Id);
        if(ids.empty())return false;
        interaction=c->subtitleAccess->TryBeginInteraction(std::move(ids));
        return interaction.has_value();
    }
    bool Commit(std::vector<AssDialogueBase> const& updates,wxString const& message,int type) {
        if(!Begin())return false;
        BOOST_SCOPE_EXIT_ALL(this){End();};
        std::set<int> written;
        for(auto const& row:updates)if(!Find(row.Id)||!written.insert(row.Id).second)
            throw std::invalid_argument("Invalid or duplicate dialog result");
        try {
            interaction->UpdateRows(updates);interaction->Commit(message,type);
            std::vector<int> ids;for(auto const& row:rows)ids.push_back(row.Id);
            Reset(ids);
            return true;
        } catch (...) {Invalidate();throw;}
    }
    bool CommitLayout(AssFile const& calculated,wxString const& message,int type) {
        if(!Begin())return false;
        BOOST_SCOPE_EXIT_ALL(this){End();};
        try {interaction->UpdateLayout(calculated);interaction->Commit(message,type);ResetAll();return true;}
        catch (...) {Invalidate();throw;}
    }
};

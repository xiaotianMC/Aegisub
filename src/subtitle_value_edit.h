// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include "subtitle_row_edit.h"
#include <map>

// Low-frequency ordinary value batches (styles/headers/attachments/bindings).
// The two files are detached; Properties are never published by this helper.
class SubtitleValueEdit {
    agi::Context* c;
    std::optional<SubtitleProjectAccess::WriteScope> scope;
    agi::SubtitleEditGate::RevisionToken revision;
    AssFile original;
    std::map<AssStyle const*, AssStyle*> style_copies;
    std::map<AssStyle const*, int> style_origins;
    bool published = false;
public:
    AssFile file;
    explicit SubtitleValueEdit(agi::Context* c)
    : c(c), scope(c->subtitleAccess->TryWrite()), revision(c->subtitleEditGate.Capture()),
      original(c->subtitleAccess->Read()), file(c->subtitleAccess->Read()) {
        if (!scope) throw std::logic_error("Ordinary value edit requires authorization");
        auto a=original.Events.begin(), b=file.Events.begin();
        for(auto const& row:c->subtitleAccess->Read().Events){(a++)->Id=row.Id;(b++)->Id=row.Id;}
        auto copy=file.Styles.begin();int index=0;
        for(auto const& style:c->subtitleAccess->Read().Styles) {
            style_copies.emplace(&style,&*copy);style_origins.emplace(&*copy,index++);++copy;
        }
    }
    AssStyle* Source(AssStyle const* live) const {
        auto found=style_copies.find(live);
        if(found==style_copies.end())throw std::logic_error("Style cache is stale");
        return found->second;
    }
    void Publish(int parts) {
        if(published)return;
        std::vector<int> sources;
        for(auto const& style:file.Styles) {
            auto found=style_origins.find(&style);
            sources.push_back(found==style_origins.end()?-1:found->second);
        }
        c->subtitleAccess->Edit(scope->Lease()).UpdateValues(revision,original,file,sources,parts);
        published=true;
    }
    AssStyle const* LiveStyle(AssStyle const* snapshot) const {
        size_t index=0;for(auto const& style:file.Styles){if(&style==snapshot)break;++index;}
        auto const& current=c->subtitleAccess->Read().Styles;
        if(index>=current.size())throw std::logic_error("Missing published style");
        return &*std::next(current.begin(),index);
    }
    int Commit(wxString const& message,int type) {
        Publish(type);
        return c->subtitleAccess->Edit(scope->Lease()).Commit(message,type);
    }
};

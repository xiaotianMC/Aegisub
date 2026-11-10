// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <main.h>
#include "../../src/subtitle_compare.h"
#include "../../src/ass_dialogue.h"
#include "../../src/ass_style.h"
#include "../../src/ass_attachment.h"
#include "../../src/ass_info.h"
#include "../../src/subtitle_format_ass.h"
#include "../../src/subtitle_format_srt.h"
#include "../../src/options.h"
#include <libaegisub/path.h>
#include <fstream>

// Only host configuration is supplied here. Models, parsers, readers, style
// analysis and comparison all come from production sources.
namespace config { agi::Options *opt = nullptr; agi::Path *path = nullptr; agi::MRUManager *mru = nullptr; }
namespace {
using namespace subtitle_compare;
class subtitle_comparison : public ::testing::Test {
    agi::Options options{"", R"({"Subtitle":{"Default Resolution":{"Auto":true}},"Subtitle Format":{"SRT":{"Default Style Catalog":""}},"App":{"Save Charset":"UTF-8"}})", agi::Options::FLUSH_SKIP};
protected:
    void SetUp() override { config::opt = &options; }
    void TearDown() override { config::opt = nullptr; }
    agi::fs::path write(std::string const& name, std::string const& text) {
        std::ofstream output(name, std::ios::binary); output << text; output.close(); return agi::fs::path(name);
    }
    AssDialogue& add(AssFile& f, std::string const& text) {
        auto line = new AssDialogue; line->Text = text; f.Events.push_back(*line); return *line;
    }
    void style(AssFile& f, std::string const& name, double size = 48) {
        auto s = new AssStyle; s->name = name; s->fontsize = size; s->UpdateData(); f.Styles.push_back(*s);
    }
};
TEST_F(subtitle_comparison, projection_is_readonly_exact_and_independent_of_ids) {
    AssFile a, b; auto& first = add(a, " 中 文  "); auto& second = add(b, " 中 文  ");
    first.Start = 1001; second.Start = 1002; first.End = second.End = 2000;
    first.Layer = second.Layer = 4; first.Margin = second.Margin = {1,2,3};
    a.Extradata = {{7,"k","v"}}; b.Extradata = {{19,"k","v"}};
    first.ExtradataIds = std::vector<uint32_t>{7}; second.ExtradataIds = std::vector<uint32_t>{19};
    auto id = first.Id; a.Properties.video_file = "existing.mp4";
    auto A = ProjectDocument(a), B = ProjectDocument(b);
    EXPECT_EQ(1001, A.document.rows[0].start_ms); EXPECT_EQ(" 中 文  ", A.document.rows[0].text);
    auto diff = model::Compare(A.document, B.document); ASSERT_EQ(1u, diff.size()); EXPECT_EQ(model::Start, diff[0].changed);
    EXPECT_EQ(id, first.Id); EXPECT_EQ("existing.mp4", a.Properties.video_file); EXPECT_EQ(7u, first.ExtradataIds.get()[0]);
}
TEST_F(subtitle_comparison, invalid_extradata_fails_instead_of_resolving_to_next_entry) {
    AssFile f; auto& line = add(f,"a"); f.Extradata = {{5,"wrong","data"}}; line.ExtradataIds = std::vector<uint32_t>{4};
    EXPECT_THROW(ProjectDocument(f), std::invalid_argument);
}
TEST_F(subtitle_comparison, style_and_named_reset_analysis_does_not_rewrite_text) {
    AssFile b, c;
    std::string text = "{\\rMissing\\pos(1,2)\\t(0,100,\\rNested)\\clip(1,2,3,4)} 中文 {\\r}";
    auto& line = add(b,text); style(b,"Default",60); style(c,"default",48); style(b,"Nested");
    auto result = AnalyzeDependencies(b,c,{0});
    EXPECT_EQ(text, line.Text.get()); EXPECT_EQ(1u, b.Events.size()); EXPECT_EQ(1u, c.Styles.size());
    auto find = [&](std::string const& name) {
        return std::find_if(result.styles.begin(), result.styles.end(), [&](auto const& s) { return s.name == name; });
    };
    ASSERT_NE(result.styles.end(), find("Default")); EXPECT_EQ(StyleState::Conflict, find("Default")->state);
    ASSERT_NE(result.styles.end(), find("Missing")); EXPECT_EQ(StyleState::Missing, find("Missing")->state);
    ASSERT_NE(result.styles.end(), find("Nested")); EXPECT_EQ(StyleState::Importable, find("Nested")->state);
}
TEST_F(subtitle_comparison, duplicate_style_names_are_not_silently_chosen) {
    AssFile b,c; add(b,"x"); style(b,"Default"); style(b,"default");
    EXPECT_EQ(StyleState::Ambiguous, AnalyzeDependencies(b,c,{0}).styles[0].state);
    EXPECT_THROW(AnalyzeDependencies(b,c,{1}), std::out_of_range);
}
TEST_F(subtitle_comparison, partial_binding_and_unknown_dependency_analysis_never_imports) {
    AssFile b,c; auto& C=add(b,"C"); auto& D=add(b,"D"); auto& A=add(c,"A");
    b.Extradata={{1,"aegisub.enhanced.subtitle-binding","0:002"},{9,"opaque","{references:unknown}"}};
    c.Extradata={{7,"aegisub.enhanced.subtitle-binding","0:002"}};
    C.ExtradataIds=std::vector<uint32_t>{1,9}; D.ExtradataIds=std::vector<uint32_t>{1}; A.ExtradataIds=std::vector<uint32_t>{7};
    auto result=AnalyzeDependencies(b,c,{0}); ASSERT_EQ(1u,result.bindings.size());
    EXPECT_EQ(2u,result.bindings[0].baseline_members); EXPECT_EQ(1u,result.bindings[0].selected_members);
    EXPECT_EQ(1u,result.bindings[0].current_members); EXPECT_EQ(2u,result.extra_issues.size());
    EXPECT_EQ(1u,c.Events.size()); EXPECT_EQ(1u,c.Extradata.size()); EXPECT_EQ(7u,A.ExtradataIds.get()[0]);
}
TEST_F(subtitle_comparison, baseline_ass_preserves_spaces_all_fields_and_project_isolation) {
    auto path=write("baseline.ass", "[Script Info]\r\nScriptType: v4.00+\r\nVideo File: baseline.mp4\r\n[Events]\r\nComment: 3,0:00:01.001,0:00:02.002,Default,Actor,1,2,3,fx,{=7} 中文 {\\pos(1,2)}  \r\n[Aegisub Extradata]\r\nData: 7,k,ev\r\n");
    AssFile current; auto& line=add(current,"unsaved"); current.Properties.video_file="current.mp4";
    auto id=line.Id; AssSubtitleFormat reader;
    auto result=ReadBaseline(reader,path,{},"UTF-8"); ASSERT_EQ(LoadState::Completed,result.state) << result.error;
    ASSERT_TRUE(result.file); auto projection=ProjectDocument(*result.file,result.fields).document;
    ASSERT_EQ(1u,projection.rows.size()); auto const& r=projection.rows[0];
    EXPECT_EQ(" 中文 {\\pos(1,2)}  ",r.text); EXPECT_EQ(1001,r.start_ms); EXPECT_EQ(2002,r.end_ms);
    EXPECT_TRUE(r.comment); EXPECT_EQ(3,r.layer); EXPECT_EQ("Actor",r.actor); EXPECT_EQ("fx",r.effect);
    EXPECT_EQ((std::array<int,3>{1,2,3}),r.margins); EXPECT_EQ("v",r.extra.at("k"));
    EXPECT_EQ("unsaved",line.Text.get()); EXPECT_EQ(id,line.Id); EXPECT_EQ("current.mp4",current.Properties.video_file);
    AssFile legacy; reader.ReadFile(&legacy,path,{},"UTF-8"); EXPECT_EQ(" 中文 {\\pos(1,2)}",legacy.Events.front().Text.get());
}
TEST_F(subtitle_comparison, baseline_srt_preserves_spaces_milliseconds_and_limits_fields) {
    auto path=write("baseline.srt","1\r\n00:00:01,001 --> 00:00:02,002\r\n 中 文  \r\n第二 行 \r\n\r\n2\r\n00:00:03,000 --> 00:00:04,000\r\n<i>斜体</i>  \r\n");
    SRTSubtitleFormat reader; auto result=ReadBaseline(reader,path,{},"UTF-8");
    ASSERT_EQ(LoadState::Completed,result.state) << result.error; ASSERT_EQ(2u,result.file->Events.size());
    EXPECT_EQ(" 中 文  \\N第二 行 ",result.file->Events.front().Text.get());
    EXPECT_EQ(1001,result.file->Events.front().Start.GetMilliseconds());
    EXPECT_EQ(model::Text|model::Start|model::End,result.fields);
    EXPECT_NE(std::string::npos,result.file->Events.back().Text.get().find("  "));
    AssFile legacy; reader.ReadFile(&legacy,path,{},"UTF-8"); EXPECT_EQ("中 文\\N第二 行",legacy.Events.front().Text.get());
}
TEST_F(subtitle_comparison, srt_whitespace_only_body_is_data_and_legacy_behavior_remains) {
    auto path=write("spaces.srt","  \n1\n00:00:01,000 --> 00:00:02,000\n一\n\n   \n二\n");
    SRTSubtitleFormat reader; auto result=ReadBaseline(reader,path,{},"UTF-8");
    ASSERT_EQ(LoadState::Completed,result.state) << result.error;
    EXPECT_NE(std::string::npos,result.file->Events.front().Text.get().find("   "));
}
TEST_F(subtitle_comparison, ssa_generated_layer_and_extra_are_not_source_fields) {
    auto path=write("baseline.ssa","[Script Info]\nScriptType: v4.00\n[Events]\nDialogue: Marked=0,0:00:01.00,0:00:02.00,Default,Actor,1,2,3,fx, 台词  \n");
    AssSubtitleFormat reader; auto result=ReadBaseline(reader,path,{},"UTF-8");
    ASSERT_EQ(LoadState::Completed,result.state) << result.error;
    EXPECT_EQ(0u,result.fields & (model::Layer|model::Extra));
    EXPECT_EQ(" 台词  ",result.file->Events.front().Text.get());
}
TEST_F(subtitle_comparison, failed_and_cancelled_loads_publish_no_partial_file) {
    AssSubtitleFormat reader; auto path=write("bad.ass","[Events]\nDialogue: invalid\n");
    auto failed=ReadBaseline(reader,path,{},"UTF-8"); EXPECT_EQ(LoadState::Failed,failed.state); EXPECT_FALSE(failed.file); EXPECT_FALSE(failed.error.empty());
    auto cancelled=ReadBaseline(reader,path,{},"UTF-8",[]{return true;}); EXPECT_EQ(LoadState::Cancelled,cancelled.state); EXPECT_FALSE(cancelled.file);
    path=write("cancel.ass","[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\nDialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,b\n");
    int checks=0; cancelled=ReadBaseline(reader,path,{},"UTF-8",[&]{return ++checks>3;});
    EXPECT_EQ(LoadState::Cancelled,cancelled.state); EXPECT_FALSE(cancelled.file);
}
}

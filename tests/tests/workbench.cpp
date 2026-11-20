// SPDX-License-Identifier: ISC
#include "../../src/workbench_store.h"
#include "../../src/workbench_internal.h"
#include "../../src/auto4_base.h"
#include "../../src/compat.h"
#include "../../src/frame_main.h"
#include "../../src/include/aegisub/context.h"
#include "../../src/libresrc/libresrc.h"
#include "../../src/main.h"
#include "../../src/options.h"

#include <libaegisub/cajun/writer.h>
#include <libaegisub/json.h>
#include <libaegisub/path.h>
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/listbox.h>
#include <wx/log.h>
#include <wx/modalhook.h>
#include <wx/mstream.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/textctrl.h>
#include <wx/weakref.h>

namespace {
using namespace workbench;
namespace fs = std::filesystem;

std::string Bytes(Path const& path) {
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(input), {}};
}
void Put(Path const& path, std::string const& bytes) {
	std::ofstream output(path, std::ios::binary);
	output.exceptions(std::ios::badbit | std::ios::failbit);
	output << bytes;
	output.close();
}
json::UnknownElement Decode(std::string const& bytes) {
	std::istringstream input(bytes);
	return agi::json_util::parse(input);
}
void WriteJson(Path const& path, json::UnknownElement const& object) {
	std::ostringstream output;
	agi::JsonWriter::Write(object, output);
	Put(path, output.str());
}

class SqlFixture {
	std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db{nullptr, &sqlite3_close};
public:
	explicit SqlFixture(Path const& file) {
		sqlite3* raw = nullptr;
		auto status = sqlite3_open(file.string().c_str(), &raw);
		db.reset(raw);
		if (status != SQLITE_OK) throw std::runtime_error("Cannot open test database");
		Exec("PRAGMA foreign_keys=ON");
	}
	void Exec(char const* sql) {
		if (sqlite3_exec(db.get(), sql, nullptr, nullptr, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.get()));
	}
	std::string Query(char const* sql) const {
		sqlite3_stmt* raw = nullptr;
		if (sqlite3_prepare_v2(db.get(), sql, -1, &raw, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.get()));
		std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> s(raw, &sqlite3_finalize);
		if (sqlite3_step(s.get()) != SQLITE_ROW) throw std::runtime_error("Expected a test query result");
		auto value = sqlite3_column_text(s.get(), 0);
		return value ? reinterpret_cast<char const*>(value) : "";
	}
};

class workbench_storage : public testing::Test {
protected:
	Path root;
	Folders folders = DefaultFolders(Kind::Project);
	void SetUp() override {
		root = Path(fs::current_path()) / "workbench-storage" / ::testing::UnitTest::GetInstance()->current_test_info()->name();
		ASSERT_TRUE(fs::create_directories(root));
	}
};

TEST_F(workbench_storage, directory_templates_apply_only_to_new_projects_and_series) {
	auto directory = root / "my templates";
	auto custom = folders;
	custom["subtitles"] = "生产/字幕";
	SaveTemplate(directory, Kind::Project, custom, LoadTemplate(directory, Kind::Project));
	auto old_template = LoadTemplate(directory, Kind::Project);
	auto first = Create(root, "第一集", Kind::Project, old_template.folders);
	EXPECT_TRUE(fs::is_directory(first.root / "生产/字幕"));
	EXPECT_EQ(custom, Open(first.root).folders);
	SaveTemplate(directory, Kind::Project, folders, old_template);
	EXPECT_THROW(SaveTemplate(directory, Kind::Project, custom, old_template), std::runtime_error);
	auto second = Create(root, "第二集", Kind::Project, LoadTemplate(directory, Kind::Project).folders);
	EXPECT_NE(first.id, second.id);
	EXPECT_TRUE(fs::is_directory(second.root / "subtitles"));
	EXPECT_EQ(custom, Open(first.root).folders);
	auto series = Create(root, "合集", Kind::Series, LoadTemplate(directory, Kind::Series).folders);
	EXPECT_TRUE(fs::is_regular_file(series.root / ".aeg/series.json"));
	EXPECT_TRUE(fs::is_directory(series.root / "episodes"));
	EXPECT_TRUE(fs::is_directory(series.root / ".aeg/covers"));
	EXPECT_EQ(Kind::Series, Open(series.root).kind);
	auto json = Decode(first.bytes);
	EXPECT_EQ("todo", static_cast<json::String const&>(static_cast<json::Object const&>(json).at("status")));
}

TEST_F(workbench_storage, registering_an_existing_folder_preserves_its_contents_and_rejects_a_second_registration) {
	auto material = root / "原片";
	fs::create_directory(material);
	Put(material / "movie.dat", "existing material");
	auto manifest = RegisterFolder(material, "旧素材", Kind::Project, folders);
	EXPECT_EQ("existing material", Bytes(material / "movie.dat"));
	EXPECT_FALSE(fs::exists(material / "video"));
	EXPECT_EQ(manifest.id, Open(material).id);
	EXPECT_ANY_THROW(RegisterFolder(material, "replacement", Kind::Series, DefaultFolders(Kind::Series)));
	EXPECT_ANY_THROW(Create(root, "原片", Kind::Project, folders));
	EXPECT_EQ(manifest.bytes, Open(material).bytes);
	EXPECT_EQ("existing material", Bytes(material / "movie.dat"));
}

TEST_F(workbench_storage, a_shared_template_round_trips_and_does_not_replace_unrelated_files) {
	auto library = root / "library", shared = root / "share.json";
	auto edited = folders;
	edited["video"] = "视频素材";
	ExportTemplate(shared, Kind::Project, edited);
	EXPECT_EQ(edited, ImportTemplate(shared, Kind::Project));
	EXPECT_ANY_THROW(ImportTemplate(shared, Kind::Series));
	auto defaults = LoadTemplate(library, Kind::Project);
	EXPECT_EQ(folders, defaults.folders);
	auto initial = Decode(Bytes(shared));
	auto id = static_cast<json::String const&>(static_cast<json::Object const&>(initial).at("template_id"));
	auto incoming = ImportTemplate(shared, Kind::Project);
	SaveTemplate(library, Kind::Project, incoming, defaults);
	auto project = Create(root, "from-template", Kind::Project, LoadTemplate(library, Kind::Project).folders);
	EXPECT_EQ(incoming, Open(project.root).folders);
	ExportTemplate(shared, Kind::Project, folders);
	auto updated = Decode(Bytes(shared));
	auto const& fields = static_cast<json::Object const&>(updated);
	EXPECT_EQ(id, static_cast<json::String const&>(fields.at("template_id")));
	EXPECT_EQ(2, static_cast<json::Integer const&>(fields.at("revision")));
	auto unrelated = root / "original.json";
	Put(unrelated, R"({"name":"unrelated document"})");
	EXPECT_ANY_THROW(ExportTemplate(unrelated, Kind::Project, folders));
	EXPECT_EQ(R"({"name":"unrelated document"})", Bytes(unrelated));
}

TEST_F(workbench_storage, invalid_templates_and_names_fail_before_creating_a_project) {
	for (std::string bad : {"../outside", ".aeg/elsewhere", "one//two", "/tmp", "AUX.txt", "audio", "a\\b", ""}) {
		auto invalid = folders;
		invalid["video"] = bad;
		EXPECT_ANY_THROW(Create(root, "bad", Kind::Project, invalid)) << bad;
		EXPECT_FALSE(fs::exists(root / "bad"));
	}
	EXPECT_ANY_THROW(Create(root, "..", Kind::Project, folders));
	EXPECT_ANY_THROW(Create(root, "test.", Kind::Project, folders));
}

TEST_F(workbench_storage, concurrent_or_external_writers_preserve_the_first_save_and_its_backup) {
	auto original = Create(root, "project", Kind::Project, folders);
	auto file = original.root / ".aeg/project.json";
	auto fields = Decode(original.bytes);
	static_cast<json::Object&>(fields)["custom"] = "preserve this extension";
	WriteJson(file, fields);
	auto left = Open(original.root), right = Open(original.root);
	EXPECT_ANY_THROW(Rename(original, "stale with same revision"));
	auto changed = Rename(left, "新名称");
	EXPECT_EQ(left.id, changed.id);
	EXPECT_EQ(left.revision + 1, changed.revision);
	EXPECT_EQ("新名称", Open(original.root).name);
	EXPECT_EQ(left.bytes, Bytes(original.root / ".aeg/backups/config/project-r1.json"));
	auto saved = Decode(changed.bytes);
	auto const& view = static_cast<json::Object const&>(saved);
	EXPECT_EQ("preserve this extension", static_cast<json::String const&>(view.at("custom")));
	EXPECT_EQ(1u, static_cast<json::Array const&>(view.at("history")).size());
	EXPECT_ANY_THROW(Rename(right, "late write"));
	EXPECT_ANY_THROW(Rename(changed, std::string("bad\0name", 8)));
	EXPECT_EQ(changed.bytes, Bytes(file));
}

TEST_F(workbench_storage, unreadable_future_ambiguous_and_conflicting_metadata_stay_untouched) {
	auto project = Create(root, "project", Kind::Project, folders);
	auto data = project.root / ".aeg", file = data / "project.json";
	auto reserved = data / "backups/config/project-r1.json";
	Put(reserved, "other backup");
	EXPECT_ANY_THROW(Rename(project, "cannot publish"));
	EXPECT_EQ("other backup", Bytes(reserved));
	EXPECT_EQ(project.bytes, Bytes(file));
	Put(data / "series.json", "other manifest");
	EXPECT_ANY_THROW(Open(project.root));
	ASSERT_TRUE(fs::remove(data / "series.json"));
	Put(file, "broken");
	EXPECT_ANY_THROW(Open(project.root));
	EXPECT_EQ("broken", Bytes(file));
	auto future = Decode(project.bytes);
	static_cast<json::Object&>(future)["schema_version"] = 999;
	WriteJson(file, future);
	auto expected = Bytes(file);
	EXPECT_ANY_THROW(Open(project.root));
	EXPECT_EQ(expected, Bytes(file));
}

TEST_F(workbench_storage, project_locations_survive_a_new_connection_and_reject_an_ambiguous_copy) {
	auto db = root / "machine/workbench.sqlite";
	auto first = Create(root, "first", Kind::Project, folders);
	auto second = Create(root, "second", Kind::Project, folders);
	{
		Index index(db);
		index.Remember(second);
		index.Remember(first);
		index.Remember(first);
	}
	Index reopened(db);
	auto page = reopened.List(0, 1);
	ASSERT_EQ(1u, page.size());
	EXPECT_EQ(first.id, page[0].id);
	EXPECT_EQ(first.root, page[0].root);
	EXPECT_EQ(second.id, reopened.List(1, 1).at(0).id);
	auto copy = root / "copy";
	fs::copy(first.root, copy, fs::copy_options::recursive);
	EXPECT_ANY_THROW(reopened.Remember(Open(copy)));
	EXPECT_EQ(first.root, reopened.List().at(0).root);
	auto renamed = Rename(first, "updated");
	reopened.Remember(renamed);
	EXPECT_EQ("updated", reopened.List(1, 1).at(0).name);
	EXPECT_TRUE(reopened.List(2, 1).empty());
	EXPECT_ANY_THROW(reopened.List(0, 101));
}

TEST_F(workbench_storage, database_with_unknown_version_or_damaged_contents_is_preserved) {
	auto file = root / "workbench.sqlite";
	auto project = Create(root, "project", Kind::Project, folders);
	{ Index db(file); db.Remember(project); }
	auto future = Bytes(file);
	ASSERT_GT(future.size(), 64u);
	future[63] = 9; // SQLite database header bytes 60–63 contain the big-endian user_version.
	for (auto const& bytes : {future, std::string("damaged library"), std::string{}}) {
		Put(file, bytes);
		EXPECT_ANY_THROW({ Index index(file); });
		EXPECT_EQ(bytes, Bytes(file));
		EXPECT_EQ(project.id, Open(project.root).id);
	}
}

TEST_F(workbench_storage, a_missing_initialized_database_stays_missing_until_the_original_is_restored) {
	auto file = root / "workbench.sqlite", moved = root / "saved-original";
	auto project = Create(root, "project", Kind::Project, folders);
	{ Index index(file); index.Remember(project); }
	fs::rename(file, moved);
	EXPECT_ANY_THROW({ Index index(file); });
	EXPECT_FALSE(fs::exists(file));
	EXPECT_EQ(project.id, Open(project.root).id);
	fs::rename(moved, file);
	Index restored(file);
	EXPECT_EQ(project.id, restored.List().at(0).id);
#ifndef _WIN32
	fs::rename(file, moved);
	EXPECT_ANY_THROW(restored.List());
	EXPECT_ANY_THROW(restored.Remember(project));
	EXPECT_FALSE(fs::exists(file));
#endif
}

TEST_F(workbench_storage, opening_or_saving_metadata_does_not_follow_a_link_outside_the_project) {
	auto project = Create(root, "project", Kind::Project, folders);
	auto outside = root / "unrelated";
	fs::create_directory(outside);
	auto backups = project.root / ".aeg/backups/config";
	ASSERT_TRUE(fs::remove(backups));
	std::error_code error;
	fs::create_directory_symlink(outside, backups, error);
	if (error) GTEST_SKIP() << "Cannot create a directory symlink in this test account: " << error.message();
	EXPECT_ANY_THROW(Rename(project, "other"));
	EXPECT_TRUE(fs::is_empty(outside));
	EXPECT_EQ(project.bytes, Open(project.root).bytes);
	auto alias = root / "directory";
	fs::create_directory(alias);
	fs::create_directory_symlink(project.root / ".aeg", alias / ".aeg");
	EXPECT_ANY_THROW(Open(alias));
}

TEST_F(workbench_storage, legacy_preferences_are_previewed_selected_and_backed_up_without_touching_the_source) {
	auto source = root / "old.json", target = root / "config.json";
	std::string defaults = R"({"App":{"Call Tips":false,"Show Toolbar":true}})";
	std::string legacy = R"({"App":{"Call Tips":true,"Show Toolbar":false},"Path":{"Automation":"do not copy"}})";
	std::string previous = R"({"App":{"Show Toolbar":true},"Unrelated":"keep"})";
	Put(source, legacy);
	Put(target, previous);
	agi::Options active(target, defaults, agi::Options::FLUSH_SKIP);
	auto preview = PreviewPreferences(source, target, active);
	ASSERT_EQ(2u, preview.choices.size());
	EXPECT_EQ("App/Call Tips", preview.choices[0].key);
	EXPECT_TRUE(ImportPreferences(preview, {}, active).empty());
	EXPECT_EQ(previous, Bytes(target));
	EXPECT_FALSE(active.Get("App/Call Tips")->GetBool());
	EXPECT_FALSE(fs::exists(root / "workbench"));
	auto backup = ImportPreferences(preview, {0}, active);
	EXPECT_EQ(previous, Bytes(backup));
	EXPECT_EQ(legacy, Bytes(source));
	EXPECT_TRUE(active.Get("App/Call Tips")->GetBool());
	EXPECT_TRUE(active.Get("App/Show Toolbar")->GetBool());
	auto persisted = Decode(Bytes(target));
	auto const& values = static_cast<json::Object const&>(persisted);
	EXPECT_TRUE(static_cast<json::Boolean const&>(static_cast<json::Object const&>(values.at("App")).at("Call Tips")));
	EXPECT_EQ("keep", static_cast<json::String const&>(values.at("Unrelated")));
	EXPECT_FALSE(values.contains("Path"));
	EXPECT_ANY_THROW(ImportPreferences(preview, {1}, active));
	EXPECT_ANY_THROW(PreviewPreferences(target, target, active));
}

TEST_F(workbench_storage, invalid_old_preferences_and_external_target_changes_leave_both_files_intact) {
	auto source = root / "old.json", target = root / "config.json";
	auto defaults = R"({"App":{"Call Tips":false}})";
	agi::Options active(target, defaults, agi::Options::FLUSH_SKIP);
	Put(source, R"({"App":{"Call Tips":"not a boolean"}})");
	EXPECT_ANY_THROW(PreviewPreferences(source, target, active));
	EXPECT_FALSE(fs::exists(target));
	Put(source, R"({"App":{"Call Tips":true}})");
	auto preview = PreviewPreferences(source, target, active);
	Put(target, "external edit");
	EXPECT_ANY_THROW(ImportPreferences(preview, {0}, active));
	EXPECT_EQ("external edit", Bytes(target));
	EXPECT_FALSE(active.Get("App/Call Tips")->GetBool());
	EXPECT_ANY_THROW(PreviewPreferences(source, target, active));
}

TEST_F(workbench_storage, portable_roots_share_one_database_and_keep_library_data_outside_projects) {
	agi::Path paths;
	for (auto token : {"?user", "?cache", "?state", "?data"}) paths.SetToken(token, root);
	auto portable = Roots::Resolve(paths);
	EXPECT_EQ(root / "workbench/workbench.sqlite", portable.database);
	EXPECT_EQ(root / "workbench/templates", portable.templates);
	auto project = Create(root, "project", Kind::Project, folders);
	{ Index index(portable.database); index.Remember(project); }
	{ Index index(Roots::Resolve(paths).database); EXPECT_EQ(project.id, index.List().at(0).id); }
	unsigned count = 0;
	for (auto const& file : fs::recursive_directory_iterator(root))
		if (file.path().extension() == ".sqlite") ++count;
	EXPECT_EQ(1u, count);
	paths.SetToken("?user", root / "profile");
	paths.SetToken("?state", root / "state");
	paths.SetToken("?cache", root / "cache");
	auto installed = Roots::Resolve(paths);
	EXPECT_NE(portable.database, installed.database);
	EXPECT_NE((root / "cache/workbench/workbench.sqlite"), installed.database);
	EXPECT_EQ(root / "profile/workbench/templates", installed.templates);
#if !defined(_WIN32) && !defined(__APPLE__)
	EXPECT_EQ(root / "state/workbench/workbench.sqlite", installed.database);
#endif
}

TEST_F(workbench_storage, upgrading_a_v1_database_keeps_locations_and_a_readable_preupgrade_copy) {
	auto file = root / "workbench.sqlite";
	{
		SqlFixture old(file);
		old.Exec("PRAGMA application_id=0x41454757; PRAGMA user_version=1;"
		         "CREATE TABLE locations(id TEXT PRIMARY KEY NOT NULL, kind TEXT NOT NULL, name TEXT NOT NULL, root TEXT NOT NULL UNIQUE);"
		         "INSERT INTO locations VALUES('97f8b42c-bef1-469b-b08c-2cbaa80e2550','project','existing','/existing')");
	}
	Put(root / "backups", "keep this unrelated file");
	EXPECT_ANY_THROW({ Index unavailable(file); });
	EXPECT_EQ("1", SqlFixture(file).Query("PRAGMA user_version"));
	EXPECT_EQ("keep this unrelated file", Bytes(root / "backups"));
	ASSERT_TRUE(fs::remove(root / "backups"));
	Index migrated(file);
	SqlFixture current(file);
	EXPECT_EQ("existing", current.Query("SELECT name FROM locations"));
	EXPECT_EQ("2", current.Query("PRAGMA user_version"));
	EXPECT_EQ("0", current.Query("SELECT count(*) FROM tasks"));
	EXPECT_EQ("ok", current.Query("PRAGMA quick_check"));
	int backups = 0;
	for (auto const& entry : fs::recursive_directory_iterator(root / "backups")) {
		if (entry.path().extension() != ".sqlite3") continue;
		++backups;
		SqlFixture backup(Path(entry.path()));
		EXPECT_EQ("1", backup.Query("PRAGMA user_version"));
		EXPECT_EQ("existing", backup.Query("SELECT name FROM locations"));
		EXPECT_EQ("0", backup.Query("SELECT count(*) FROM sqlite_master WHERE name='tasks'"));
	}
	EXPECT_EQ(1, backups);
}

bool DecodesCover(std::string const& bytes) {
	wxLogNull quiet;
	wxMemoryInputStream stream(bytes.data(), bytes.size());
	wxImage image;
	return image.LoadFile(stream);
}

TEST_F(workbench_storage, metadata_preserves_other_fields_and_stale_edits_cannot_replace_the_latest_revision) {
	auto project = Create(root, "metadata", Kind::Project, folders);
	auto json = Decode(project.bytes);
	auto& object = static_cast<json::Object&>(json);
	object.erase("owner"); object.erase("tags");
	object["extension"] = "keep this field";
	WriteJson(project.root / ".aeg/project.json", json);
	project = Open(project.root);
	EXPECT_TRUE(project.metadata.owner.empty());
	EXPECT_TRUE(project.metadata.tags.empty());
	Metadata values;
	values.description = "第一行\n第二行"; values.source = "example-id";
	values.source_language = "日本語"; values.target_language = "简体中文";
	values.owner = "字幕组"; values.tags = {"资料,待核", "访谈"}; values.status = "review";
	auto updated = UpdateMetadata(project, values);
	EXPECT_EQ(values, Open(updated.root).metadata);
	EXPECT_EQ(updated.bytes, UpdateMetadata(updated, values).bytes);
	EXPECT_EQ(project.bytes, Bytes(updated.root / ".aeg/backups/config/project-r1.json"));
	EXPECT_ANY_THROW(UpdateMetadata(project, Metadata{}));
	auto invalid = values; invalid.status = "unrecognized";
	EXPECT_ANY_THROW(UpdateMetadata(updated, invalid));
	EXPECT_EQ(updated.bytes, Open(updated.root).bytes);
	Put(updated.root / "video/clip", "clip");
	Index index(root / "workbench.sqlite"); index.Remember(updated);
	index.RunRegistration(index.QueueRegistration(updated, {"video/clip"}));
	auto registered = Open(updated.root);
	EXPECT_EQ(values, registered.metadata);
	auto after = Decode(registered.bytes);
	auto const& fields = static_cast<json::Object const&>(after);
	EXPECT_EQ("keep this field", static_cast<json::String const&>(fields.at("extension")));
	auto const& history = static_cast<json::Array const&>(fields.at("history"));
	ASSERT_EQ(2u, history.size());
	auto const& edit = static_cast<json::Object const&>(history.front());
	EXPECT_EQ("todo", static_cast<json::String const&>(edit.at("previous_status")));
	EXPECT_EQ("review", static_cast<json::String const&>(edit.at("status")));
}

TEST_F(workbench_storage, project_and_series_covers_travel_with_the_manifest_and_its_backups) {
	auto source = root / "source.png", replacement = root / "replacement.png";
	wxImage red(3, 2); red.SetRGB(wxRect(0, 0, 3, 2), 255, 0, 0);
	wxImage blue(2, 3); blue.SetRGB(wxRect(0, 0, 2, 3), 0, 0, 255);
	ASSERT_TRUE(red.SaveFile(to_wx(source), wxBITMAP_TYPE_PNG));
	ASSERT_TRUE(blue.SaveFile(to_wx(replacement), wxBITMAP_TYPE_PNG));
	auto red_bytes = Bytes(source), blue_bytes = Bytes(replacement);
	for (auto kind : {Kind::Project, Kind::Series}) {
		std::string name = kind == Kind::Project ? "project" : "series";
		auto blank = Create(root, name, kind, DefaultFolders(kind));
		auto first = SetCover(blank, source, DecodesCover);
		EXPECT_EQ(2, first.revision);
		EXPECT_EQ(red_bytes, Bytes(CoverFile(first)));
		EXPECT_EQ(first.bytes, SetCover(first, source, DecodesCover).bytes);
		EXPECT_ANY_THROW(SetCover(blank, replacement, DecodesCover));
		Put(root / "bad.png", red_bytes.substr(0, 14));
		EXPECT_ANY_THROW(SetCover(first, root / "bad.png", DecodesCover));
		EXPECT_EQ(first.bytes, Open(first.root).bytes);
		auto second = SetCover(first, replacement, DecodesCover);
		EXPECT_EQ(first.bytes, Bytes(first.root / ".aeg/backups/config" / (name + "-r2.json")));
		ASSERT_TRUE(second.cover.parent_path() == Path(".aeg/covers"));
		ASSERT_NE(second.cover, first.cover);
		auto moved = root / (name + "-moved");
		fs::rename(second.root, moved);
		auto reopened = Open(moved);
		EXPECT_EQ(second.id, reopened.id);
		EXPECT_EQ(second.bytes, reopened.bytes);
		EXPECT_EQ(blue_bytes, Bytes(CoverFile(reopened)));
		EXPECT_EQ(red_bytes, Bytes(moved / first.cover));
		std::error_code ec;
		fs::rename(moved / ".aeg/covers", moved / "saved-covers");
		fs::create_directory_symlink(moved / "saved-covers", moved / ".aeg/covers", ec);
		EXPECT_TRUE(CoverFile(reopened).empty());
		if (!ec) { EXPECT_ANY_THROW(SetCover(reopened, source, DecodesCover)); }
		EXPECT_EQ(reopened.bytes, Open(moved).bytes);
	}
	EXPECT_EQ(red_bytes, Bytes(source));
	EXPECT_EQ(blue_bytes, Bytes(replacement));
}

TEST_F(workbench_storage, only_the_requested_project_index_is_replaced_while_business_records_survive) {
	auto file = root / "workbench.sqlite";
	auto first = Create(root, "first", Kind::Project, folders), other = Create(root, "other", Kind::Project, folders);
	Put(first.root / "subtitles/字幕_100%.srt", "subtitle");
	Put(first.root / "old", "old"); Put(other.root / "keep", "other project");
	Create(first.root, "nested", Kind::Project, folders);
	Put(first.root / "nested/video/unrelated", "child project");
	Index index(file); index.Remember(first); index.Remember(other);
	index.RebuildFiles(first); index.RebuildFiles(other);
	ASSERT_EQ(2, index.Scan(first.id).files);
	EXPECT_EQ(1, index.Scan(other.id).files);
	auto task = index.QueueRegistration(first, {"subtitles/字幕_100%.srt"});
	auto output = index.Outputs(task).at(0);
	SqlFixture actual(file);
	actual.Exec("INSERT INTO account_profiles VALUES('fixture-account','test.invalid','fixture','opaque-reference',2);"
	            "INSERT INTO tasks(task_id,name,source_kind,auth_mode,account_id,state,created_at,updated_at)"
	            " VALUES('fixture-web','fixture','web','account','fixture-account','paused',1,1);"
	            "INSERT INTO attempts VALUES('fixture-attempt','fixture-web','fixture-account',2,'interrupted',1,2)");
	ASSERT_TRUE(fs::remove(first.root / "old"));
	Put(first.root / "video/new", "new");
	index.RebuildFiles(first);
	auto reopened = Index(file);
	EXPECT_EQ(2, reopened.Scan(first.id).generation);
	EXPECT_EQ(1, reopened.Scan(other.id).generation);
	ASSERT_EQ(2u, reopened.Files(first.id).size());
	EXPECT_EQ(Path("subtitles/字幕_100%.srt"), reopened.Files(first.id, 0, 1).at(0).relative);
	EXPECT_EQ(Path("video/new"), reopened.Files(first.id, 1, 1).at(0).relative);
	EXPECT_EQ(Path("keep"), reopened.Files(other.id).at(0).relative);
	EXPECT_EQ("queued", reopened.Registrations().at(0).state);
	EXPECT_EQ(output.id, reopened.Outputs(task).at(0).id);
	EXPECT_EQ(2u, reopened.List().size());
	EXPECT_EQ("paused", actual.Query("SELECT state FROM tasks WHERE task_id='fixture-web'"));
	EXPECT_EQ("opaque-reference", actual.Query("SELECT secret_ref FROM account_profiles"));
	EXPECT_EQ("2", actual.Query("SELECT credential_revision FROM attempts"));
	EXPECT_EQ("0", actual.Query("SELECT count(*) FROM pragma_foreign_key_check"));
}

TEST_F(workbench_storage, a_failed_or_cancelled_rescan_preserves_the_last_complete_list) {
	auto file = root / "workbench.sqlite";
	auto project = Create(root, "project", Kind::Project, folders);
	Put(project.root / "before", "original");
	Index index(file); index.Remember(project); index.RebuildFiles(project);
	Put(project.root / "added", "new");
	SqlFixture sql(file);
	sql.Exec("CREATE TRIGGER refuse_scan BEFORE INSERT ON file_entries BEGIN SELECT RAISE(ABORT,'test disk failure'); END");
	EXPECT_ANY_THROW(index.RebuildFiles(project));
	EXPECT_EQ(1, index.Scan(project.id).generation);
	EXPECT_EQ(Path("before"), index.Files(project.id).at(0).relative);
	EXPECT_EQ(1u, index.Files(project.id).size());
	sql.Exec("DROP TRIGGER refuse_scan");
	std::stop_source halt; halt.request_stop();
	EXPECT_ANY_THROW(index.RebuildFiles(project, halt.get_token()));
	EXPECT_EQ(1, index.Scan(project.id).generation);
	index.RebuildFiles(project);
	EXPECT_EQ(2u, index.Files(project.id).size());
}

TEST_F(workbench_storage, native_file_scans_and_paged_reads_cover_ten_thousand_paths) {
	using Clock = std::chrono::steady_clock;
	using Milliseconds = std::chrono::duration<double, std::milli>;
	std::vector<Manifest> projects;
	Index index(root / "workbench.sqlite");
	for (int p = 0; p < 40; ++p) {
		auto project = Create(root, "project-" + std::to_string(p), Kind::Project, folders);
		for (int f = 0; f < 250; ++f) Put(project.root / "subtitles" / (std::to_string(f) + ".srt"), "");
		index.Remember(project);
		projects.push_back(std::move(project));
	}
	auto start = Clock::now();
	for (auto const& project : projects) index.RebuildFiles(project);
	double scan_ms = Milliseconds(Clock::now() - start).count();
	std::vector<double> read_ms;
	for (int i = 0; i < 100; ++i) {
		auto const& project = projects[i % projects.size()];
		start = Clock::now();
		auto rows = index.Files(project.id, 0, 50);
		read_ms.push_back(Milliseconds(Clock::now() - start).count());
		ASSERT_EQ(50u, rows.size());
		ASSERT_EQ(250, index.Scan(project.id).files);
	}
	auto first = read_ms.front();
	std::sort(read_ms.begin(), read_ms.end());
	std::cout << "Native index: 40 projects, 10000 files, scan " << scan_ms << " ms; first page "
	          << first << " ms; 50-row page p95 " << read_ms[94] << " ms\n";
	EXPECT_EQ("10000", SqlFixture(root / "workbench.sqlite").Query("SELECT count(*) FROM file_entries"));
}

TEST_F(workbench_storage, a_changed_input_stays_unregistered_and_its_original_task_can_resume) {
	auto file = root / "workbench.sqlite";
	auto project = Create(root, "project", Kind::Project, folders);
	auto path = project.root / "video/clip";
	Put(path, "good");
	Index index(file); index.Remember(project);
	auto task = index.QueueRegistration(project, {"video/clip", "video/clip"});
	auto output = index.Outputs(task).at(0);
	EXPECT_EQ(1u, index.Outputs(task).size());
	auto mtime = fs::last_write_time(path);
	Put(path, "evil"); fs::last_write_time(path, mtime);
	EXPECT_ANY_THROW(index.RunRegistration(task));
	EXPECT_EQ("needs_attention", index.Registrations().at(0).state);
	EXPECT_EQ(project.bytes, Open(project.root).bytes);
	Index reopen(file); reopen.RecoverInterrupted();
	EXPECT_EQ("needs_attention", reopen.Registrations().at(0).state);
	Put(path, "good");
	reopen.RunRegistration(task);
	EXPECT_EQ(0, reopen.UnfinishedRegistrations());
	EXPECT_EQ("succeeded", reopen.Registrations().at(0).state);
	auto registered = Open(project.root);
	auto json = Decode(registered.bytes);
	auto const& data = static_cast<json::Object const&>(json);
	auto const& rows = static_cast<json::Array const&>(data.at("assets"));
	ASSERT_EQ(1u, rows.size());
	auto const& asset = static_cast<json::Object const&>(rows[0]);
	EXPECT_EQ(output.asset_id, static_cast<json::String const&>(asset.at("asset_id")));
	EXPECT_EQ(output.id, static_cast<json::String const&>(asset.at("output_id")));
	EXPECT_EQ("video/clip", static_cast<json::String const&>(asset.at("path")));
	EXPECT_EQ("todo", static_cast<json::String const&>(data.at("status")));
	EXPECT_EQ(1u, static_cast<json::Array const&>(data.at("history")).size());
	EXPECT_EQ(std::string::npos, registered.bytes.find(project.root.string()));
	reopen.RunRegistration(task);
	EXPECT_EQ(registered.bytes, Open(project.root).bytes);
	EXPECT_EQ("good", Bytes(path));
}

TEST_F(workbench_storage, retry_after_manifest_commit_reconciles_each_output_without_repeating_history) {
	auto file = root / "workbench.sqlite";
	auto project = Create(root, "project", Kind::Project, folders);
	Put(project.root / "a", "first"); Put(project.root / "b", "second");
	Index index(file); index.Remember(project);
	auto task = index.QueueRegistration(project, {"b","a"});
	SqlFixture sql(file);
	sql.Exec("CREATE TRIGGER refuse_ack BEFORE UPDATE OF stage ON outputs WHEN NEW.path='b' AND NEW.stage='registered' "
	         "BEGIN SELECT RAISE(ABORT,'lost acknowledgement'); END");
	EXPECT_ANY_THROW(index.RunRegistration(task));
	EXPECT_EQ(1, index.Registrations().at(0).registered);
	EXPECT_EQ("1", sql.Query("SELECT count(*) FROM pending_steps"));
	auto published = Open(project.root);
	EXPECT_EQ(3, published.revision);
	index.RebuildFiles(project);
	EXPECT_EQ("1", sql.Query("SELECT count(*) FROM pending_steps"));
	sql.Exec("DROP TRIGGER refuse_ack");
	Index reboot(file);
	reboot.RecoverInterrupted();
	reboot.RunRegistration(task);
	reboot.RunRegistration(task);
	EXPECT_EQ("0", sql.Query("SELECT count(*) FROM pending_steps"));
	EXPECT_EQ("succeeded", reboot.Registrations().at(0).state);
	EXPECT_EQ(2, reboot.Registrations().at(0).registered);
	EXPECT_EQ(published.bytes, Open(project.root).bytes);
	EXPECT_EQ("2", sql.Query("SELECT count(*) FROM attempts"));
}

TEST_F(workbench_storage, cancellation_checks_a_pending_manifest_without_registering_the_missing_output) {
	auto file = root / "workbench.sqlite";
	auto project = Create(root, "project", Kind::Project, folders);
	Put(project.root / "a", "first"); Put(project.root / "b", "second");
	Index index(file); index.Remember(project);
	auto task = index.QueueRegistration(project, {"a","b"});
	Put(project.root / ".aeg/backups/config/project-r2.json", "other backup");
	EXPECT_ANY_THROW(index.RunRegistration(task));
	auto published = Open(project.root);
	EXPECT_EQ(2, published.revision);
	EXPECT_EQ(1, index.Registrations().at(0).registered);
	ASSERT_TRUE(fs::remove(project.root / "b"));
	index.CancelRegistration(task);
	Index reboot(file); reboot.RecoverInterrupted(); reboot.RunRegistration(task);
	auto cancelled = reboot.Registrations().at(0);
	EXPECT_EQ("cancelled", cancelled.state); EXPECT_EQ(1, cancelled.registered);
	EXPECT_EQ("0", SqlFixture(file).Query("SELECT count(*) FROM pending_steps"));
	EXPECT_EQ(published.bytes, Open(project.root).bytes);
	reboot.RunRegistration(task);
	EXPECT_FALSE(fs::exists(project.root / "b"));
}

TEST_F(workbench_storage, cancellation_after_the_only_output_was_published_preserves_the_completed_registration) {
	auto file = root / "workbench.sqlite";
	auto project = Create(root, "project", Kind::Project, folders);
	Put(project.root / "only", "verified");
	Index index(file); index.Remember(project);
	auto task = index.QueueRegistration(project, {"only"});
	SqlFixture sql(file);
	sql.Exec("CREATE TRIGGER wait_ack BEFORE UPDATE OF stage ON outputs WHEN NEW.stage='registered' "
	         "BEGIN SELECT RAISE(ABORT,'test acknowledgement failure'); END");
	EXPECT_ANY_THROW(index.RunRegistration(task));
	auto published = Open(project.root);
	index.CancelRegistration(task);
	sql.Exec("DROP TRIGGER wait_ack");
	Index restart(file); restart.RunRegistration(task);
	EXPECT_EQ("succeeded", restart.Registrations().at(0).state);
	EXPECT_EQ(1, restart.Registrations().at(0).registered);
	EXPECT_EQ("0", sql.Query("SELECT count(*) FROM pending_steps"));
	EXPECT_EQ(published.bytes, Open(project.root).bytes);
}

TEST_F(workbench_storage, startup_only_interrupts_abandoned_active_work_and_honours_a_saved_cancellation) {
	auto file = root / "workbench.sqlite";
	auto project = Create(root, "project", Kind::Project, folders);
	Put(project.root / "a", "first"); Put(project.root / "b", "second");
	Index index(file); index.Remember(project);
	auto active = index.QueueRegistration(project, {"a"});
	auto queued = index.QueueRegistration(project, {"b"});
	SqlFixture sql(file);
	sql.Exec("UPDATE tasks SET state='registering' WHERE name='a'; INSERT INTO attempts(attempt_id,task_id,state,started_at)"
	         " SELECT 'fixture-active',task_id,'registering',1 FROM tasks WHERE name='a'");
	{
		workbench::detail::WriteLock owner(root / "registrations.lock");
		Index other(file); other.RecoverInterrupted();
		EXPECT_EQ("registering", sql.Query("SELECT state FROM tasks WHERE name='a'"));
		EXPECT_ANY_THROW(other.RunRegistration(active));
		other.CancelRegistration(queued);
		EXPECT_EQ("cancelled", sql.Query("SELECT state FROM tasks WHERE name='b'"));
		other.CancelRegistration(active);
		EXPECT_EQ("1", sql.Query("SELECT cancel_requested FROM tasks WHERE name='a'"));
	}
	Index restart(file); restart.RecoverInterrupted(); restart.RecoverInterrupted();
	EXPECT_EQ("interrupted", sql.Query("SELECT state FROM tasks WHERE name='a'"));
	EXPECT_EQ("interrupted", sql.Query("SELECT state FROM attempts WHERE attempt_id='fixture-active'"));
	restart.RunRegistration(active);
	EXPECT_EQ("cancelled", sql.Query("SELECT state FROM tasks WHERE name='a'"));
	EXPECT_EQ(project.bytes, Open(project.root).bytes);
}

TEST_F(workbench_storage, registration_refuses_outside_metadata_nested_and_offline_project_paths) {
	auto file = root / "workbench.sqlite";
	auto project = Create(root, "project", Kind::Project, folders);
	auto child = Create(project.root, "child", Kind::Project, folders);
	Put(root / "outside", "keep"); Put(child.root / "inside", "keep"); Put(project.root / "usable", "local");
	Index index(file); index.Remember(project);
	for (auto const& name : {"../outside", ".aeg/project.json", "child/inside", "video/../usable"})
		EXPECT_ANY_THROW(index.QueueRegistration(project, {name})) << name;
	std::error_code ec;
	fs::create_symlink(root / "outside", project.root / "alias", ec);
	if (!ec) { EXPECT_ANY_THROW(index.QueueRegistration(project, {"alias"})); }
	EXPECT_EQ(0, index.UnfinishedRegistrations());
	auto task = index.QueueRegistration(project, {"usable"});
	fs::rename(project.root, root / "moved");
	EXPECT_ANY_THROW(index.RunRegistration(task));
	EXPECT_ANY_THROW(index.RebuildFiles(project));
	EXPECT_FALSE(fs::exists(project.root));
	EXPECT_EQ("needs_attention", index.Registrations().at(0).state);
	fs::rename(root / "moved", project.root);
	index.RunRegistration(task);
	EXPECT_EQ(1, index.Registrations().at(0).registered);
}

TEST_F(workbench_storage, episodes_keep_relative_membership_sort_numbers_and_read_each_live_manual_status) {
	auto series_folders = DefaultFolders(Kind::Series);
	series_folders["episodes"] = "制作/各集";
	auto series = Create(root, "一季", Kind::Series, series_folders);
	auto ten = CreateEpisodeProject(series, "十", folders);
	auto two = CreateEpisodeProject(series, "二", folders);
	Index index(root / "workbench.sqlite");
	auto first = index.AddEpisode(series, ten, "10");
	auto both = index.AddEpisode(first, two, "02");
	EXPECT_EQ(both.bytes, index.AddEpisode(first, two, "02").bytes);
	EXPECT_EQ(3, both.revision);
	EXPECT_EQ(first.bytes, Bytes(series.root / ".aeg/backups/config/series-r2.json"));
	ASSERT_EQ(2u, both.episodes.size());
	EXPECT_EQ(two.id, both.episodes[0].project_id);
	EXPECT_EQ(Path("制作/各集/十"), both.episodes[1].relative);
	EXPECT_EQ(ten.bytes, Open(ten.root).bytes);
	auto metadata = two.metadata; metadata.status = "done";
	two = Rename(UpdateMetadata(two, metadata), "已校对");
	auto overview = InspectSeries(series);
	EXPECT_EQ("已校对", overview.states.at(0).name); EXPECT_EQ("done", overview.states[0].status);
	EXPECT_EQ("todo", overview.states.at(1).status);
	EXPECT_EQ(two.id, OpenEpisode(both, both.episodes[0]).id);
	EXPECT_EQ(ten.id, OpenEpisode(both, both.episodes[1]).id);
	fs::rename(ten.root, root / "temporarily-away");
	overview = InspectSeries(series);
	EXPECT_FALSE(overview.states[1].error.empty()); EXPECT_TRUE(overview.states[1].status.empty());
	EXPECT_EQ("10", overview.series.episodes[1].label);
	EXPECT_EQ("done", overview.states[0].status);
	EXPECT_ANY_THROW(OpenEpisode(both, both.episodes[1]));
	auto replacement = Create(ten.root.parent_path(), "十", Kind::Project, folders);
	EXPECT_NE(ten.id, replacement.id);
	EXPECT_ANY_THROW(OpenEpisode(both, both.episodes[1]));
	fs::rename(replacement.root, root / "unrelated");
	fs::rename(root / "temporarily-away", ten.root);
	fs::rename(series.root, root / "moved");
	auto moved = InspectSeries(Open(root / "moved"));
	EXPECT_EQ(both.bytes, moved.series.bytes);
	EXPECT_TRUE(moved.states[1].error.empty());
	EXPECT_EQ(ten.id, OpenEpisode(moved.series, moved.series.episodes[1]).id);
}

TEST_F(workbench_storage, a_project_survives_an_interruption_or_conflict_before_it_joins_a_series) {
	auto series = Create(root, "show", Kind::Series, DefaultFolders(Kind::Series));
	auto child = CreateEpisodeProject(series, "独立", folders);
	Put(child.root / "subtitles/input.srt", "keep original");
	auto changed = Rename(series, "updated");
	Index index(root / "workbench.sqlite"); index.Remember(changed);
	EXPECT_ANY_THROW(index.AddEpisode(series, child, "SP"));
	EXPECT_EQ(changed.bytes, Open(series.root).bytes); EXPECT_EQ(child.bytes, Open(child.root).bytes);
	EXPECT_EQ(1u, index.List().size());
	auto joined = index.AddEpisode(changed, Open(child.root), "SP");
	EXPECT_EQ(child.id, joined.episodes.at(0).project_id);
	EXPECT_ANY_THROW(index.AddEpisode(joined, child, "changed label"));
	EXPECT_EQ(joined.bytes, Open(series.root).bytes);
	EXPECT_EQ("keep original", Bytes(child.root / "subtitles/input.srt"));
}

TEST_F(workbench_storage, linking_checks_conflicting_claims_nested_projects_and_path_boundaries) {
	auto a = Create(root, "A", Kind::Series, DefaultFolders(Kind::Series));
	auto b = Create(root, "B", Kind::Series, DefaultFolders(Kind::Series));
	auto child = CreateEpisodeProject(a, "first", folders);
	Index index(root / "workbench.sqlite");
	a = index.AddEpisode(a, child, "1");
	EXPECT_ANY_THROW(index.AddEpisode(b, child, "1"));
	auto clone_root = b.root / "episodes/copied";
	fs::copy(child.root, clone_root, fs::copy_options::recursive);
	// Only the two series are known to this installation. Their actual manifests decide the conflict.
	Index separate(root / "second/workbench.sqlite"); separate.Remember(a); separate.Remember(b);
	EXPECT_ANY_THROW(separate.AddEpisode(b, Open(clone_root), "1"));
	EXPECT_EQ(b.bytes, Open(b.root).bytes); EXPECT_EQ(a.bytes, Open(a.root).bytes);
	auto inner = Create(b.root, "another series", Kind::Series, DefaultFolders(Kind::Series));
	auto inner_child = CreateEpisodeProject(inner, "inner", folders);
	EXPECT_ANY_THROW(index.AddEpisode(b, inner_child, "2"));
	std::error_code ec;
	fs::create_directory_symlink(a.root, b.root / "alias", ec);
	if (!ec) {
		auto unsafe = Decode(b.bytes);
		json::Object entry; entry["project_id"] = child.id; entry["label"] = "2"; entry["name"] = "linked"; entry["path"] = "alias/episodes/first";
		auto& entries = static_cast<json::Array&>(static_cast<json::Object&>(unsafe).at("episodes"));
		entries.emplace_back(std::move(entry)); WriteJson(b.root / ".aeg/series.json", unsafe);
		EXPECT_FALSE(InspectSeries(b).states.at(0).error.empty());
		static_cast<json::Object&>(entries[0])["path"] = "../A/episodes/first";
		WriteJson(b.root / ".aeg/series.json", unsafe);
		EXPECT_ANY_THROW(Open(b.root));
	}
}

TEST_F(workbench_storage, retry_after_series_publication_recovers_local_positions_without_a_second_membership) {
	auto series = Create(root, "series", Kind::Series, DefaultFolders(Kind::Series));
	auto unrelated = Create(root, "unrelated", Kind::Series, DefaultFolders(Kind::Series));
	auto child = CreateEpisodeProject(series, "clip", folders);
	auto file = root / "workbench.sqlite";
	Index index(file); index.Remember(series); index.Remember(unrelated);
	Put(unrelated.root / ".aeg/series.json", "{}");
	SqlFixture sql(file);
	sql.Exec("CREATE TRIGGER fail_position BEFORE UPDATE ON locations WHEN NEW.name='series' AND "
	         "EXISTS(SELECT 1 FROM locations WHERE kind='project') BEGIN SELECT RAISE(ABORT,'test interrupted positions'); END");
	EXPECT_ANY_THROW(index.AddEpisode(series, child, "1"));
	auto written = Open(series.root);
	ASSERT_EQ(2, written.revision);
	EXPECT_EQ(1u, written.episodes.size());
	EXPECT_EQ("0", sql.Query("SELECT count(*) FROM locations WHERE kind='project'"));
	sql.Exec("DROP TRIGGER fail_position");
	Index fresh(file);
	EXPECT_EQ(written.bytes, fresh.AddEpisode(series, child, "1").bytes);
	EXPECT_EQ("1", sql.Query("SELECT count(*) FROM locations WHERE kind='project'"));
	EXPECT_EQ("{}", Bytes(unrelated.root / ".aeg/series.json"));
}

class workbench_home : public workbench_storage {
protected:
	agi::Path paths;
	agi::Path *old_path = nullptr;
	agi::Options *old_options = nullptr;
	std::unique_ptr<agi::Options> options;
	std::unique_ptr<Automation4::AutoloadScriptManager> scripts;
	std::vector<wxWeakRef<FrameMain>> frames;
	void SetUp() override {
		workbench_storage::SetUp();
		for (auto token : {"?user", "?cache", "?state", "?data"}) paths.SetToken(token, root);
		old_path = config::path;
		old_options = config::opt;
		config::path = &paths;
		scripts = std::make_unique<Automation4::AutoloadScriptManager>("?user/no-scripts");
		config::global_scripts = scripts.get();
	}
	FrameMain *Launch() {
		auto frame = wxGetApp().NewWorkbenchContext().frame;
		frames.emplace_back(frame);
		return frame;
	}
	void TearDown() override {
		for (auto& frame : frames) if (frame && !frame->IsClosing()) frame->Destroy();
		wxTheApp->ProcessPendingEvents();
		wxTheApp->ProcessIdle();
		config::global_scripts = nullptr;
		scripts.reset();
		config::opt = old_options;
		options.reset();
		config::path = old_path;
	}
	void Click(wxWindow *owner, char const* name) {
		auto button = dynamic_cast<wxButton*>(owner->FindWindow(name));
		ASSERT_NE(nullptr, button) << name;
		ASSERT_TRUE(button->IsEnabled()) << name;
		wxCommandEvent event(wxEVT_BUTTON, button->GetId());
		event.SetEventObject(button);
		ASSERT_TRUE(button->ProcessWindowEvent(event));
	}
	wxDialog* Activity(FrameMain* frame) {
		auto home = frame->FindWindow("workbench-home");
		for (auto child : home->GetChildren())
			if (child->GetName() == "workbench-activity") return dynamic_cast<wxDialog*>(child);
		return nullptr;
	}
	void SelectItem(wxListBox* list, int row) {
		list->SetSelection(row);
		wxCommandEvent event(wxEVT_LISTBOX, list->GetId());
		event.SetEventObject(list);
		event.SetInt(row);
		ASSERT_TRUE(list->ProcessWindowEvent(event));
	}
	template<class Check> bool WaitFor(Check ready) {
		auto end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
		while (!ready() && std::chrono::steady_clock::now() < end) {
			wxYield();
			wxMilliSleep(10);
		}
		return ready();
	}
};

struct InputDialogs : wxModalDialogHook {
	Path directory;
	wxString name;
	int answer = wxID_OK;
	int directory_count = 0, name_count = 0;
	InputDialogs() { Register(); }
	int Enter(wxDialog *dialog) override {
		if (auto picker = dynamic_cast<wxFileDialog*>(dialog)) {
			picker->SetPath(to_wx(directory));
			for (int i = 0; i < 250 && picker->GetPath().empty(); ++i) { wxYield(); wxMilliSleep(10); }
			return answer;
		}
		if (auto picker = dynamic_cast<wxDirDialog*>(dialog)) {
			++directory_count;
			picker->wxDirDialogBase::SetPath(to_wx(directory)); // Result normally stored when the native picker returns.
			return answer;
		}
		if (auto entry = dynamic_cast<wxTextEntryDialog*>(dialog)) {
			++name_count;
			entry->SetValue(name);
			return answer;
		}
		ADD_FAILURE() << "Unexpected dialog " << dialog->GetTitle();
		return wxID_CANCEL;
	}
};

TEST_F(workbench_home, series_page_creates_adds_opens_and_rechecks_offline_episodes) {
	auto series = Create(root, "show", Kind::Series, DefaultFolders(Kind::Series));
	auto ten = CreateEpisodeProject(series, "ten", folders);
	auto done = ten.metadata; done.status = "done"; ten = UpdateMetadata(ten, done);
	auto frame = Launch();
	InputDialogs input; input.directory = series.root;
	Click(frame, "workbench-open-project");
	Click(frame, "workbench-episodes");
	auto page = [&]() -> wxWindow* {
		for (auto w : frame->FindWindow("workbench-home")->GetChildren())
			if (!w->IsBeingDeleted() && w->IsShown() && w->GetName() == "workbench-series") return w;
		return nullptr;
	};
	auto dialog = page();
	ASSERT_NE(nullptr, dialog);
	EXPECT_EQ(frame, wxGetTopLevelParent(dialog));
	auto node = [&](char const* name) { return dialog->FindWindow(name); };
	ASSERT_TRUE(WaitFor([&] { return node("series-add")->IsEnabled(); }));
	input.answer = wxID_CANCEL;
	Click(dialog, "series-create");
	EXPECT_TRUE(Open(series.root).episodes.empty());
	input.answer = wxID_OK; input.directory = ten.root; input.name = "10";
	Click(dialog, "series-add");
	auto list = dynamic_cast<wxListBox*>(node("series-list"));
	auto summary = dynamic_cast<wxStaticText*>(node("series-progress"));
	ASSERT_NE(nullptr, list); ASSERT_NE(nullptr, summary);
	ASSERT_TRUE(WaitFor([&] { return list->GetCount() == 1; }));
	input.name = "02";
	Click(dialog, "series-create");
	ASSERT_TRUE(WaitFor([&] { return list->GetCount() == 2; }));
	EXPECT_TRUE(summary->GetLabelText().Contains("1 of 2"));
	EXPECT_TRUE(list->GetString(0).StartsWith("02"));
	SelectItem(list, 1);
	Click(dialog, "series-open");
	auto details = dynamic_cast<wxStaticText*>(frame->FindWindow("workbench-project-details"));
	ASSERT_NE(nullptr, details);
	EXPECT_TRUE(details->GetLabelText().Contains("ten"));
	wxTheApp->ProcessIdle();
	Click(frame, "workbench-back-series");
	dialog = page(); ASSERT_NE(nullptr, dialog);
	list = dynamic_cast<wxListBox*>(node("series-list")); ASSERT_NE(nullptr, list);
	summary = dynamic_cast<wxStaticText*>(node("series-progress")); ASSERT_NE(nullptr, summary);
	ASSERT_TRUE(WaitFor([&] { return list->GetCount() == 2; }));
	fs::rename(ten.root, root / "offline");
	Click(dialog, "series-refresh");
	ASSERT_TRUE(WaitFor([&] { return summary->GetLabelText().Contains("0 of 2 episodes completed, 1 unknown"); }));
	SelectItem(list, 1);
	EXPECT_FALSE(node("series-open")->IsEnabled());
	SelectItem(list, 0);
	Click(dialog, "series-open");
	EXPECT_TRUE(details->GetLabelText().Contains("02"));
	EXPECT_EQ("todo", Open(series.root).metadata.status);
}

TEST_F(workbench_home, create_from_home_then_reopen_and_rename_after_the_window_is_destroyed) {
	InputDialogs input;
	input.directory = root;
	input.name = "sample";
	auto first = Launch();
	ASSERT_TRUE(first->IsHome());
	input.answer = wxID_CANCEL;
	Click(first, "workbench-create-project");
	EXPECT_FALSE(fs::exists(root / "sample"));
	input.answer = wxID_OK;
	Click(first, "workbench-create-project");
	ASSERT_TRUE(fs::is_directory(root / "sample/.aeg"));
	auto made = Open(root / "sample");
	first->Destroy();
	wxTheApp->ProcessPendingEvents();
	wxTheApp->ProcessIdle();
	ASSERT_FALSE(frames.front());
	auto next = Launch();
	auto list = dynamic_cast<wxListBox*>(next->FindWindow("workbench-projects"));
	ASSERT_NE(nullptr, list);
	ASSERT_EQ(1u, list->GetCount());
	EXPECT_TRUE(list->GetString(0).Contains("sample"));
	input.directory = made.root;
	Click(next, "workbench-open-project");
	input.name = "updated";
	Click(next, "workbench-rename");
	EXPECT_TRUE(list->GetString(0).Contains("updated"));
	EXPECT_EQ(0, list->GetSelection());
	auto persisted = Open(made.root);
	EXPECT_EQ(made.id, persisted.id);
	EXPECT_EQ("updated", persisted.name);
	EXPECT_EQ(3, input.directory_count);
	EXPECT_EQ(2, input.name_count);
}

TEST_F(workbench_home, a_damaged_local_database_still_allows_inspection_and_opening_the_editor) {
	auto project = Create(root, "readable", Kind::Project, folders);
	auto db = Roots::Resolve(paths).database;
	fs::create_directories(db.parent_path());
	Put(db, "damaged database");
	auto frame = Launch();
	InputDialogs input;
	input.directory = project.root;
	Click(frame, "workbench-open-project");
	auto details = dynamic_cast<wxStaticText*>(frame->FindWindow("workbench-project-details"));
	auto status = dynamic_cast<wxStaticText*>(frame->FindWindow("workbench-status"));
	ASSERT_NE(nullptr, details);
	ASSERT_NE(nullptr, status);
	EXPECT_TRUE(details->GetLabelText().Contains("readable"));
	EXPECT_FALSE(status->GetLabelText().empty());
	EXPECT_FALSE(frame->FindWindow("workbench-rename")->IsEnabled());
	EXPECT_EQ("damaged database", Bytes(db));
	EXPECT_EQ(project.bytes, Open(project.root).bytes);
	frame->ShowEditor();
	EXPECT_FALSE(frame->IsHome());
}

TEST_F(workbench_home, an_unavailable_library_still_allows_reading_a_series_and_its_child) {
	auto series = Create(root, "season", Kind::Series, DefaultFolders(Kind::Series));
	auto child = CreateEpisodeProject(series, "episode", folders);
	{ Index other(root / "other/workbench.sqlite"); other.AddEpisode(series, child, "1"); }
	auto local = Roots::Resolve(paths).database;
	fs::create_directories(local.parent_path()); Put(local, "damaged fixture");
	auto frame = Launch();
	InputDialogs input; input.directory = series.root;
	Click(frame, "workbench-open-project");
	Click(frame, "workbench-episodes");
	auto page = frame->FindWindow("workbench-series"); ASSERT_NE(nullptr, page);
	auto list = dynamic_cast<wxListBox*>(page->FindWindow("series-list")); ASSERT_NE(nullptr, list);
	ASSERT_TRUE(WaitFor([&] { return list->GetCount() == 1; }));
	EXPECT_FALSE(page->FindWindow("series-create")->IsEnabled());
	EXPECT_FALSE(page->FindWindow("series-add")->IsEnabled());
	SelectItem(list, 0); Click(page, "series-open");
	auto details = dynamic_cast<wxStaticText*>(frame->FindWindow("workbench-project-details"));
	ASSERT_NE(nullptr, details); EXPECT_TRUE(details->GetLabelText().Contains("episode"));
	EXPECT_FALSE(frame->FindWindow("workbench-edit-metadata")->IsEnabled());
	EXPECT_EQ("damaged fixture", Bytes(local));
	EXPECT_EQ(child.bytes, Open(child.root).bytes);
}

TEST_F(workbench_home, choosing_a_local_cover_updates_the_preview_and_preserves_the_selected_file) {
	auto project = Create(root, "series", Kind::Series, DefaultFolders(Kind::Series));
	wxImage image(5, 3); image.SetRGB(wxRect(0, 0, 5, 3), 30, 80, 160);
	auto source = root / "chosen.png";
	auto type = wxBITMAP_TYPE_PNG;
	if (wxImage::FindHandler(wxBITMAP_TYPE_JPEG)) { source = root / "chosen.jpg"; type = wxBITMAP_TYPE_JPEG; }
	ASSERT_TRUE(image.SaveFile(to_wx(source), type));
	auto frame = Launch();
	InputDialogs dialogs; dialogs.directory = project.root;
	Click(frame, "workbench-open-project");
	dialogs.directory = source;
	dialogs.answer = wxID_CANCEL;
	Click(frame, "workbench-choose-cover");
	EXPECT_EQ(project.bytes, Open(project.root).bytes);
	dialogs.answer = wxID_OK;
	Click(frame, "workbench-choose-cover");
	auto updated = Open(project.root);
	ASSERT_EQ(2, updated.revision);
	EXPECT_EQ(Bytes(source), Bytes(CoverFile(updated)));
	auto preview = dynamic_cast<wxStaticBitmap*>(frame->FindWindow("workbench-cover"));
	ASSERT_NE(nullptr, preview);
	auto bitmap = preview->GetBitmap();
	EXPECT_EQ(wxSize(5, 3), bitmap.GetDIPSize());
	wxImage original(to_wx(source));
	auto shown = bitmap.ConvertToImage();
	EXPECT_EQ(original.GetBlue(2, 1), shown.GetBlue(shown.GetWidth() / 2, shown.GetHeight() / 2));
}

TEST_F(workbench_home, metadata_dialog_keeps_source_and_target_languages_and_manual_status_across_reopen) {
	auto project = Create(root, "episode", Kind::Project, folders);
	auto frame = Launch();
	{
		InputDialogs input; input.directory = project.root;
		Click(frame, "workbench-open-project");
	}
	struct EditDialog : wxModalDialogHook {
		std::function<void(wxDialog*)> edit;
		int answer = wxID_CANCEL, visits = 0;
		EditDialog() { Register(); }
		int Enter(wxDialog* dialog) override {
			if (dialog->GetName() != "workbench-metadata") { ADD_FAILURE() << "Unexpected dialog"; return wxID_CANCEL; }
			++visits; edit(dialog); return answer;
		}
	};
	auto text = [](wxDialog* dialog, char const* name) { return dynamic_cast<wxTextCtrl*>(dialog->FindWindow(name)); };
	{
		EditDialog hook;
		hook.edit = [&](wxDialog* dialog) {
			text(dialog, "metadata-source-language")->SetValue(to_wx("日语"));
			text(dialog, "metadata-target-language")->SetValue(to_wx("中文"));
			text(dialog, "metadata-tags")->SetValue(to_wx("采访,现场\n字幕"));
			static_cast<wxChoice*>(dialog->FindWindow("metadata-status"))->SetSelection(2);
		};
		Click(frame, "workbench-edit-metadata");
		EXPECT_EQ(project.bytes, Open(project.root).bytes);
		hook.answer = wxID_OK;
		Click(frame, "workbench-edit-metadata");
		EXPECT_EQ(2, hook.visits);
	}
	EXPECT_EQ("review", Open(project.root).metadata.status);
	frame->Destroy(); wxTheApp->ProcessPendingEvents(); wxTheApp->ProcessIdle();
	auto next = Launch();
	auto list = dynamic_cast<wxListBox*>(next->FindWindow("workbench-projects"));
	ASSERT_NE(nullptr, list); ASSERT_EQ(1u, list->GetCount()); SelectItem(list, 0);
	EditDialog read;
	read.edit = [&](wxDialog* dialog) {
		EXPECT_EQ(to_wx("日语"), text(dialog, "metadata-source-language")->GetValue());
		EXPECT_EQ(to_wx("中文"), text(dialog, "metadata-target-language")->GetValue());
		EXPECT_EQ(to_wx("采访,现场\n字幕"), text(dialog, "metadata-tags")->GetValue());
		EXPECT_EQ(2, static_cast<wxChoice*>(dialog->FindWindow("metadata-status"))->GetSelection());
	};
	Click(next, "workbench-edit-metadata");
	EXPECT_EQ(1, read.visits);
}

TEST_F(workbench_home, index_and_queue_from_the_project_page_then_complete_from_the_reopened_workbench) {
	auto project = Create(root, "work", Kind::Project, folders);
	for (int i = 0; i < 50; ++i) Put(project.root / ("item-" + std::to_string(i)), "other");
	Put(project.root / "subtitles/字幕.srt", "local subtitle");
	auto first = Launch();
	{
		InputDialogs dialogs;
		dialogs.directory = project.root;
		Click(first, "workbench-open-project");
	}
	Click(first, "workbench-files");
	auto file_page = Activity(first);
	ASSERT_NE(nullptr, file_page);
	EXPECT_FALSE(file_page->IsModal());
	auto list = dynamic_cast<wxListBox*>(file_page->FindWindow("workbench-index-files"));
	ASSERT_NE(nullptr, list);
	Click(file_page, "workbench-rebuild-files");
	first->ShowEditor();
	EXPECT_FALSE(first->IsHome());
	first->ShowHome();
	ASSERT_TRUE(WaitFor([&] { return list->GetCount() == 50; }));
	Click(file_page, "workbench-files-next");
	ASSERT_EQ(1u, list->GetCount());
	EXPECT_EQ(to_wx("subtitles/字幕.srt"), list->GetString(0));
	SelectItem(list, 0);
	Click(file_page, "workbench-queue-files");
	auto home_tasks = dynamic_cast<wxButton*>(first->FindWindow("workbench-tasks"));
	ASSERT_NE(nullptr, home_tasks);
	ASSERT_TRUE(WaitFor([&] { return home_tasks->GetLabel().Contains("1"); }));
	auto database = Roots::Resolve(paths).database;
	auto task = Index(database).Registrations().at(0);
	EXPECT_EQ("queued", task.state);
	first->Destroy();
	wxTheApp->ProcessPendingEvents();
	wxTheApp->ProcessIdle();
	ASSERT_FALSE(frames.front());
	auto next = Launch();
	ASSERT_FALSE(next->FindWindow("workbench-files")->IsEnabled());
	Click(next, "workbench-tasks");
	auto task_page = Activity(next);
	ASSERT_NE(nullptr, task_page);
	auto tasks = dynamic_cast<wxListBox*>(task_page->FindWindow("workbench-registration-list"));
	ASSERT_NE(nullptr, tasks);
	ASSERT_EQ(1u, tasks->GetCount());
	EXPECT_TRUE(tasks->GetString(0).Contains(to_wx("字幕.srt")));
	SelectItem(tasks, 0);
	Click(task_page, "workbench-run-task");
	ASSERT_TRUE(WaitFor([&] { return tasks->GetCount() == 1 && tasks->GetString(0).Contains("1/1"); }));
	EXPECT_EQ("succeeded", Index(database).Registrations().at(0).state);
	EXPECT_EQ("registered", Index(database).Outputs(task.id).at(0).stage);
	EXPECT_EQ(project.revision + 1, Open(project.root).revision);
	EXPECT_EQ("local subtitle", Bytes(project.root / "subtitles/字幕.srt"));
}

TEST_F(workbench_home, selecting_a_template_library_persists_its_location_and_applies_it_to_new_projects) {
	auto config_file = root / "config.json";
	options = std::make_unique<agi::Options>(config_file, GET_DEFAULT_CONFIG(default_config), agi::Options::FLUSH_SKIP);
	options->Get("App/Auto/Save")->SetBool(false);
	config::opt = options.get();
	auto templates = root / "custom library";
	auto custom = folders;
	custom["audio"] = "配音";
	SaveTemplate(templates, Kind::Project, custom, LoadTemplate(templates, Kind::Project));
	auto frame = Launch();
	InputDialogs dialogs;
	dialogs.directory = templates;
	Click(frame, "workbench-template-library");
	agi::Options reloaded(config_file, GET_DEFAULT_CONFIG(default_config), agi::Options::FLUSH_SKIP);
	reloaded.ConfigUser();
	EXPECT_EQ(templates, paths.Decode(reloaded.Get("Path/Workbench Templates")->GetString()));
	dialogs.directory = root;
	dialogs.name = "custom project";
	Click(frame, "workbench-create-project");
	auto made = Open(root / "custom project");
	EXPECT_EQ(custom, made.folders);
	EXPECT_TRUE(fs::is_directory(made.root / "配音"));
	EXPECT_FALSE(fs::exists(made.root / "audio"));
}
}

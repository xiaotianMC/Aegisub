// SPDX-License-Identifier: ISC
#include "workbench_internal.h"
#include <libaegisub/exception.h>
#include <libaegisub/io.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <stdexcept>

namespace workbench {
namespace fs = std::filesystem;
using namespace detail;
namespace {
using Error = std::runtime_error;
char const *KindName(Kind kind) { return kind == Kind::Project ? "project" : "series"; }
void Sql(sqlite3 *db, char const *sql) {
	if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) throw Error(sqlite3_errmsg(db));
}
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Statement Prepare(sqlite3 *db, char const *sql) {
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) throw Error(sqlite3_errmsg(db));
	return {stmt, &sqlite3_finalize};
}
int Scalar(sqlite3 *db, char const *sql) {
	auto result = Prepare(db, sql);
	if (sqlite3_step(result.get()) != SQLITE_ROW) throw Error(sqlite3_errmsg(db));
	return sqlite3_column_int(result.get(), 0);
}
void Bind(Statement const& s, int column, std::string_view value) {
	if (sqlite3_bind_text64(s.get(), column, value.empty() ? "" : value.data(), value.size(), SQLITE_TRANSIENT, SQLITE_UTF8) != SQLITE_OK)
		throw Error(sqlite3_errmsg(sqlite3_db_handle(s.get())));
}
void Bind(Statement const& s, int column, int64_t value) {
	if (sqlite3_bind_int64(s.get(), column, value) != SQLITE_OK) throw Error(sqlite3_errmsg(sqlite3_db_handle(s.get())));
}
bool Row(Statement const& statement) {
	auto result = sqlite3_step(statement.get());
	if (result == SQLITE_DONE) return false;
	if (result != SQLITE_ROW) throw Error(sqlite3_errmsg(sqlite3_db_handle(statement.get())));
	return true;
}
void Done(Statement const& statement) {
	if (Row(statement)) throw Error("Unexpected result in a database update");
}
std::string Text(Statement const& stmt, int column) {
	if (sqlite3_column_type(stmt.get(), column) != SQLITE_TEXT) throw Error("Damaged workbench record");
	auto value = sqlite3_column_text(stmt.get(), column);
	if (!value) throw Error(sqlite3_errmsg(sqlite3_db_handle(stmt.get())));
	std::string text(reinterpret_cast<char const*>(value), static_cast<size_t>(sqlite3_column_bytes(stmt.get(), column)));
	if (text.find('\0') != text.npos) throw Error("Invalid text in workbench database");
	return text;
}
int64_t Number(Statement const& stmt, int column) {
	if (sqlite3_column_type(stmt.get(), column) != SQLITE_INTEGER) throw Error("Damaged numeric workbench record");
	return sqlite3_column_int64(stmt.get(), column);
}
void Page(int offset, int count) {
	if (offset < 0 || count < 1 || count > 100) throw Error("Invalid workbench list page");
}
void Update(sqlite3* db, char const* sql, std::initializer_list<std::string_view> args) {
	auto stmt = Prepare(db, sql);
	int n = 1;
	for (auto value : args) Bind(stmt, n++, value);
	Done(stmt);
}
class Transaction {
	sqlite3* db;
public:
	explicit Transaction(sqlite3* db, bool immediate = true) : db(db) { Sql(db, immediate ? "BEGIN IMMEDIATE" : "BEGIN"); }
	Transaction(Transaction const&) = delete;
	~Transaction() { if (db) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
	void Commit() { Sql(db, "COMMIT"); db = nullptr; }
};

void CheckReserved(sqlite3* db, std::string const& id, Path const& root = {}) {
	auto rows = Prepare(db, "SELECT project_id,root FROM folder_removals");
	while (Row(rows))
		if (Text(rows, 0) == id || ContainsPath(Path(Text(rows, 1)), root))
			throw Error("A folder removal is pending for this project.");
}

void CheckInactive(sqlite3* db, std::string const& id) {
	auto active = Prepare(db, "SELECT 1 FROM tasks WHERE project_id=? AND state IN ('registering','running','downloading','archiving') LIMIT 1");
	Bind(active, 1, id);
	if (Row(active)) throw Error("Stop the active task before removing this project.");
}

void Detach(sqlite3* db, std::string const& id) {
	CheckInactive(db, id);
	Update(db, "INSERT INTO paused_tasks SELECT task_id,state FROM tasks WHERE project_id=? AND state NOT IN ('succeeded','cancelled','failed')", {id});
	Update(db, "UPDATE tasks SET state='paused_project' WHERE project_id=? AND task_id IN (SELECT task_id FROM paused_tasks)", {id});
	for (auto sql : {"DELETE FROM visits WHERE project_id=?", "DELETE FROM recent_files WHERE project_id=?",
	                 "DELETE FROM file_entries WHERE project_id=?", "DELETE FROM scan_state WHERE project_id=?",
	                 "DELETE FROM status_hints WHERE project_id=?"}) Update(db, sql, {id});
	Update(db, "UPDATE locations SET last_root=root,root=NULL,generation=generation+1 WHERE id=?", {id});
}

constexpr int app_id = 0x41454757, schema_version = 5;
char const* const add_folder_removals = R"sql(
CREATE TABLE folder_removals(project_id TEXT PRIMARY KEY NOT NULL REFERENCES locations(id), root TEXT NOT NULL,
 generation INTEGER NOT NULL, series_id TEXT NOT NULL, series_root TEXT NOT NULL, membership TEXT NOT NULL, staged TEXT NOT NULL,
 identity TEXT NOT NULL, scope TEXT NOT NULL, phase TEXT NOT NULL CHECK(phase IN ('planned','ready','disposed')));
PRAGMA user_version=5;
)sql";
char const* const add_detached_locations = R"sql(
CREATE TABLE replacement_locations(id TEXT PRIMARY KEY NOT NULL, kind TEXT NOT NULL, name TEXT NOT NULL,
 root TEXT UNIQUE, last_root TEXT, generation INTEGER NOT NULL DEFAULT 1);
INSERT INTO replacement_locations(id,kind,name,root) SELECT id,kind,name,root FROM locations;
DROP TABLE locations;
ALTER TABLE replacement_locations RENAME TO locations;
CREATE INDEX locations_name ON locations(name COLLATE NOCASE,id);
CREATE TABLE paused_tasks(task_id TEXT PRIMARY KEY REFERENCES tasks(task_id), previous_state TEXT NOT NULL);
PRAGMA user_version=4;
)sql";
char const* const add_dashboard_tables = R"sql(
CREATE TABLE visits(project_id TEXT NOT NULL UNIQUE REFERENCES locations(id), page TEXT NOT NULL,
 status TEXT NOT NULL, updated_at INTEGER NOT NULL, cover TEXT NOT NULL);
CREATE TABLE recent_files(project_id TEXT NOT NULL REFERENCES locations(id), path TEXT NOT NULL, UNIQUE(project_id,path));
CREATE TABLE status_hints(project_id TEXT PRIMARY KEY REFERENCES locations(id), token TEXT NOT NULL, revision INTEGER NOT NULL, status TEXT NOT NULL);
CREATE TABLE reviewed_downloads(project_id TEXT PRIMARY KEY REFERENCES locations(id), task_id TEXT NOT NULL REFERENCES tasks(task_id));
PRAGMA user_version=3;
)sql";
char const* const add_work_tables = R"sql(
CREATE TABLE account_profiles(
 account_id TEXT PRIMARY KEY NOT NULL, site TEXT NOT NULL, name TEXT NOT NULL,
 secret_ref TEXT, credential_revision INTEGER NOT NULL CHECK(credential_revision>=0));
CREATE TABLE tasks(
 task_id TEXT PRIMARY KEY NOT NULL, project_id TEXT REFERENCES locations(id), name TEXT NOT NULL,
 source_kind TEXT NOT NULL CHECK(source_kind IN ('local_import','web')),
 auth_mode TEXT NOT NULL, account_id TEXT REFERENCES account_profiles(account_id),
 state TEXT NOT NULL, cancel_requested INTEGER NOT NULL DEFAULT 0 CHECK(cancel_requested IN (0,1)),
 reason TEXT NOT NULL DEFAULT '', created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL,
 CHECK ((source_kind='local_import' AND auth_mode='none' AND account_id IS NULL) OR
        (source_kind='web' AND ((auth_mode='anonymous' AND account_id IS NULL) OR (auth_mode='account' AND account_id IS NOT NULL)))));
CREATE INDEX tasks_state ON tasks(source_kind,state,created_at,task_id);
CREATE TABLE attempts(
 attempt_id TEXT PRIMARY KEY NOT NULL, task_id TEXT NOT NULL REFERENCES tasks(task_id),
 account_id TEXT REFERENCES account_profiles(account_id), credential_revision INTEGER,
 state TEXT NOT NULL, started_at INTEGER NOT NULL, finished_at INTEGER,
 CHECK ((account_id IS NULL AND credential_revision IS NULL) OR (account_id IS NOT NULL AND credential_revision>=0 AND credential_revision IS NOT NULL)));
CREATE INDEX attempts_task ON attempts(task_id,state);
CREATE UNIQUE INDEX attempts_registering ON attempts(task_id) WHERE state='registering';
CREATE TABLE outputs(
 output_id TEXT PRIMARY KEY NOT NULL, task_id TEXT NOT NULL REFERENCES tasks(task_id),
 asset_id TEXT NOT NULL UNIQUE, path TEXT NOT NULL, size INTEGER NOT NULL CHECK(size>=0),
 sha256 TEXT NOT NULL, file_id TEXT NOT NULL, stage TEXT NOT NULL, UNIQUE(task_id,path));
CREATE INDEX outputs_task ON outputs(task_id,stage,output_id);
CREATE TABLE pending_steps(
 output_id TEXT PRIMARY KEY NOT NULL REFERENCES outputs(output_id), operation TEXT NOT NULL, started_at INTEGER NOT NULL);
CREATE TABLE file_entries(
 project_id TEXT NOT NULL REFERENCES locations(id), path TEXT NOT NULL, size INTEGER NOT NULL CHECK(size>=0),
 PRIMARY KEY(project_id,path)) WITHOUT ROWID;
CREATE TABLE scan_state(
 project_id TEXT PRIMARY KEY NOT NULL REFERENCES locations(id), generation INTEGER NOT NULL CHECK(generation>0),
 files INTEGER NOT NULL CHECK(files>=0), scanned_at INTEGER NOT NULL);
PRAGMA user_version=2;
)sql";

using Connection = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
Connection Connect(Path const& path, int flags) {
	sqlite3* raw = nullptr;
	int result = sqlite3_open_v2(path.string().c_str(), &raw, flags, nullptr);
	Connection db(raw, &sqlite3_close);
	if (result != SQLITE_OK) throw Error(raw ? sqlite3_errmsg(raw) : "Cannot open workbench database");
	sqlite3_busy_timeout(raw, 250);
	return db;
}

void BackupBeforeMigration(Path const& file, int version) {
	// A separate reader sees the last committed state under the migrator's write lock.
	auto source = Connect(file, SQLITE_OPEN_READONLY);
	auto backups = file.parent_path() / "backups";
	if (Linked(backups)) throw Error("Linked database backup directory");
	fs::create_directories(backups);
	auto directory = backups / ("before-v" + std::to_string(schema_version) + "-" + NewId());
	if (!fs::create_directory(directory)) throw Error("Cannot reserve the database backup directory");
	auto copy = directory / "workbench.sqlite3";
	try {
		auto target = Connect(copy, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
		auto backup = sqlite3_backup_init(target.get(), "main", source.get(), "main");
		if (!backup) throw Error(sqlite3_errmsg(target.get()));
		int copied = sqlite3_backup_step(backup, -1);
		int finished = sqlite3_backup_finish(backup);
		if (copied != SQLITE_DONE || finished != SQLITE_OK) throw Error("Could not back up the previous workbench database");
		auto check = Prepare(target.get(), "PRAGMA quick_check");
		if (!Row(check) || Text(check, 0) != "ok" || Scalar(target.get(), "PRAGMA user_version") != version)
			throw Error("Could not verify the previous workbench database");
	}
	catch (...) {
		std::error_code ec;
		fs::remove(copy, ec);
		fs::remove(directory, ec);
		throw;
	}
}

void ValidateTables(sqlite3* db) {
	for (auto sql : {"SELECT id,kind,name,root,last_root,generation FROM locations LIMIT 0",
	     "SELECT task_id,previous_state FROM paused_tasks LIMIT 0",
	     "SELECT project_id,root,generation,series_id,series_root,membership,staged,identity,scope,phase FROM folder_removals LIMIT 0",
	     "SELECT project_id,page,status,updated_at,cover FROM visits LIMIT 0", "SELECT project_id,path FROM recent_files LIMIT 0",
	     "SELECT project_id,token,revision,status FROM status_hints LIMIT 0",
	     "SELECT project_id,task_id FROM reviewed_downloads LIMIT 0",
	     "SELECT account_id,site,name,secret_ref,credential_revision FROM account_profiles LIMIT 0",
	     "SELECT task_id,project_id,name,source_kind,auth_mode,account_id,state,cancel_requested,reason,created_at,updated_at FROM tasks LIMIT 0",
	     "SELECT attempt_id,task_id,account_id,credential_revision,state,started_at,finished_at FROM attempts LIMIT 0",
	     "SELECT output_id,task_id,asset_id,path,size,sha256,file_id,stage FROM outputs LIMIT 0",
	     "SELECT output_id,operation,started_at FROM pending_steps LIMIT 0",
	     "SELECT project_id,path,size FROM file_entries LIMIT 0", "SELECT project_id,generation,files,scanned_at FROM scan_state LIMIT 0"})
		Prepare(db, sql);
}
}

void Index::CloseDatabase::operator()(sqlite3 *db) const { sqlite3_close(db); }
Index::Index(Path const& database) : file(database) {
	fs::create_directories(database.parent_path());
	if (Linked(database)) throw Error("Linked workbench database: " + database.string());
	auto marker = database.parent_path() / (database.filename().string() + ".initialized");
	constexpr char marker_text[] = "Aegisub workbench database\n";
	if (Linked(marker)) throw Error("Invalid database initialization marker: " + marker.string());
	bool initialized = fs::exists(marker);
	if (initialized && ReadBytes(marker) != marker_text) throw Error("Invalid database initialization marker: " + marker.string());
	bool existed = fs::exists(database);
	if (initialized && !existed) throw Error("The workbench database is missing. Restore it at " + database.string());
	int flags = SQLITE_OPEN_READWRITE;
	if (!initialized && !existed) flags |= SQLITE_OPEN_CREATE;
	db.reset(Connect(database, flags).release());
	auto raw = db.get();
	if (sqlite3_create_function_v2(raw, "media_kind", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr,
	    [](sqlite3_context* c, int, sqlite3_value** args) {
		    auto path = sqlite3_value_text(args[0]);
		    if (!path) { sqlite3_result_error_nomem(c); return; }
		    sqlite3_result_int(c, static_cast<int>(MediaKind(reinterpret_cast<char const*>(path))));
	    }, nullptr, nullptr, nullptr) != SQLITE_OK) throw Error(sqlite3_errmsg(raw));
	if (Scalar(raw, "PRAGMA application_id") != app_id || Scalar(raw, "PRAGMA user_version") != schema_version) {
		// The v4 migration rebuilds the parent table without changing any IDs.
		Sql(raw, "PRAGMA foreign_keys=OFF");
		Transaction migration(raw);
		auto identity = Scalar(raw, "PRAGMA application_id"), version = Scalar(raw, "PRAGMA user_version");
		if (identity == 0 && version == 0 && !initialized && !existed && Scalar(raw, "SELECT count(*) FROM sqlite_master") == 0) {
			Sql(raw, "CREATE TABLE locations(id TEXT PRIMARY KEY NOT NULL, kind TEXT NOT NULL, name TEXT NOT NULL, root TEXT NOT NULL UNIQUE);"
			         "CREATE INDEX locations_name ON locations(name COLLATE NOCASE, id); PRAGMA application_id=0x41454757");
			Sql(raw, add_work_tables);
			Sql(raw, add_dashboard_tables);
			Sql(raw, add_detached_locations);
			Sql(raw, add_folder_removals);
		}
		else if (identity == app_id && version >= 1 && version < schema_version) {
			Prepare(raw, "SELECT id,kind,name,root FROM locations LIMIT 0");
			BackupBeforeMigration(database, version);
			if (version == 1) Sql(raw, add_work_tables);
			if (version < 3) Sql(raw, add_dashboard_tables);
			if (version < 4) Sql(raw, add_detached_locations);
			Sql(raw, add_folder_removals);
		}
		else if (identity != app_id || version != schema_version)
			throw Error("Unrecognized or damaged workbench database: " + database.string());
		ValidateTables(raw);
		if (Row(Prepare(raw, "PRAGMA foreign_key_check"))) throw Error("Invalid workbench references after migration");
		migration.Commit();
	}
	else ValidateTables(raw);
	Sql(raw, "PRAGMA foreign_keys=ON");
	if (!initialized) {
		agi::io::Save out(marker, true);
		out.Get() << marker_text;
		out.Commit();
	}
}

void Index::CheckLocation() const {
	int moved = 0;
	auto result = sqlite3_file_control(db.get(), "main", SQLITE_FCNTL_HAS_MOVED, &moved);
	if (!fs::is_regular_file(file) || Linked(file) || (result == SQLITE_OK && moved))
		throw Error("The workbench database changed location. Reopen it at " + file.string());
	if (result != SQLITE_OK && result != SQLITE_NOTFOUND) throw Error(sqlite3_errstr(result));
}

void Index::Remember(Manifest const& m) {
	CheckLocation();
	std::optional<Transaction> write;
	if (sqlite3_get_autocommit(db.get())) write.emplace(db.get());
	CheckReserved(db.get(), m.id, m.root);
	auto stmt = Prepare(db.get(), "INSERT INTO locations(id,kind,name,root) VALUES(?1,?2,?3,?4) "
	                    "ON CONFLICT(id) DO UPDATE SET name=excluded.name WHERE locations.root=excluded.root AND locations.kind=excluded.kind");
	std::array<std::string, 4> values{m.id, KindName(m.kind), m.name, m.root.string()};
	for (int i = 0; i < 4; ++i)
		if (sqlite3_bind_text(stmt.get(), i+1, values[i].c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
			throw Error(sqlite3_errmsg(db.get()));
	if (sqlite3_step(stmt.get()) != SQLITE_DONE) throw Error(sqlite3_errmsg(db.get()));
	if (!sqlite3_changes(db.get())) throw Error("This project was removed from management or is registered elsewhere. Use Register Existing Folder to check its registration.");
	if (write) write->Commit();
}

Location Index::RegisteredLocation(std::string const& id) const {
	CheckLocation();
	auto row = Prepare(db.get(), "SELECT name,root,generation FROM locations WHERE id=? AND root IS NOT NULL");
	Bind(row, 1, id);
	if (!Row(row)) throw Error("The project is no longer registered.");
	Location location{id, Text(row, 0), Path(Text(row, 1))};
	location.generation = Number(row, 2);
	return location;
}

int64_t Index::PendingTasks(std::string const& id) const {
	CheckLocation();
	auto row = Prepare(db.get(), "SELECT count(*) FROM tasks WHERE project_id=? AND state NOT IN ('succeeded','cancelled','failed')");
	Bind(row, 1, id); Row(row); return Number(row, 0);
}

namespace {
bool SameLocation(Location const& left, Location const& right) {
	return left.id == right.id && left.root == right.root && left.generation == right.generation;
}

std::string TaskScope(sqlite3* db, std::string const& id) {
	auto rows = Prepare(db, "SELECT task_id,state FROM tasks WHERE project_id=? AND state NOT IN ('succeeded','cancelled','failed') ORDER BY task_id");
	Bind(rows, 1, id);
	std::string result;
	while (Row(rows)) result += Text(rows, 0) + '\0' + Text(rows, 1) + '\0';
	return result;
}

void CheckRemovalScope(sqlite3* db, Path const& database, Location const& location) {
	for (Path directory(fs::canonical(database.parent_path()));; directory = directory.parent_path()) {
		std::error_code ignored;
		if (ContainsPath(location.root, directory) || fs::equivalent(location.root, directory, ignored))
			throw Error("The project directory contains the workbench database.");
		if (directory == directory.root_path()) break;
	}
	auto rows = Prepare(db, "SELECT root FROM locations WHERE root IS NOT NULL AND id<>?");
	Bind(rows, 1, location.id);
	while (Row(rows)) {
		Path root(Text(rows, 0));
		if (ContainsPath(location.root, root)) throw Error("The project contains another registered project: " + root.string());
	}
}

bool SameSeries(std::optional<Manifest> const& left, std::optional<Manifest> const& right) {
	return left && right ? left->root == right->root && left->bytes == right->bytes : !left && !right;
}

void ConfirmPreview(FolderRemoval const& expected, FolderRemoval const& current) {
	if (!SameLocation(expected.location, current.location) || expected.project.bytes != current.project.bytes ||
	    !SameSeries(expected.series, current.series) || expected.files != current.files || expected.tasks != current.tasks ||
	    expected.identity != current.identity || expected.scope != current.scope || expected.task_scope != current.task_scope)
		throw Error("The project, series membership, files or tasks changed. Review the removal again.");
}

struct RemovalRecord {
	Location location;
	std::string series_id, membership;
	Path series_root, staged;
	std::string identity, scope, phase;
};

std::vector<RemovalRecord> RemovalRecords(sqlite3* db) {
	auto rows = Prepare(db, "SELECT project_id,name,folder_removals.root,folder_removals.generation,"
	                        "series_id,membership,series_root,staged,identity,scope,phase FROM folder_removals JOIN locations ON id=project_id");
	std::vector<RemovalRecord> records;
	while (Row(rows)) {
		RemovalRecord record{{Text(rows, 0), Text(rows, 1), Path(Text(rows, 2))}, Text(rows, 4), Text(rows, 5),
		                     Path(Text(rows, 6)), Path(Text(rows, 7)), Text(rows, 8), Text(rows, 9), Text(rows, 10)};
		record.location.generation = Number(rows, 3);
		auto const& root = record.location.root;
		if (!ValidId(record.location.id) || !root.is_absolute() || root != root.lexically_normal() || root == root.root_path() ||
		    (!record.series_root.empty() && (!ValidId(record.series_id) || record.membership.empty() || !record.series_root.is_absolute() ||
		                                    root == record.series_root || !ContainsPath(record.series_root, root))))
			throw Error("Invalid folder removal record.");
		if (!record.staged.empty()) {
			constexpr std::string_view prefix = ".aegisub-remove-";
			auto wrapper = record.staged.parent_path();
			auto name = wrapper.filename().string();
			if (wrapper.parent_path() != root.parent_path() || record.staged.filename() != root.filename() ||
			    !name.starts_with(prefix) || !ValidId(name.substr(prefix.size())))
				throw Error("Invalid staged folder removal record.");
		}
		records.push_back(std::move(record));
	}
	return records;
}

void CleanStage(RemovalRecord const& record) {
	if (record.staged.empty()) return;
	std::error_code ignored;
	auto wrapper = record.staged.parent_path();
	if (!fs::is_directory(fs::symlink_status(wrapper, ignored))) return;
	try { InspectRemovalIdentity(wrapper); }
	catch (std::exception const&) { return; }
	fs::remove(wrapper, ignored);
}

void ClearRemoval(sqlite3* db, RemovalRecord const& record) {
	CleanStage(record);
	Update(db, "DELETE FROM folder_removals WHERE project_id=?", {record.location.id});
}

bool MatchesRemoval(RemovalRecord const& record, Path const& path) {
	if (!PathOccupied(path)) return false;
	if (record.phase == "planned") return InspectRemovalIdentity(path) == record.identity;
	auto current = InspectRemovalScope(path);
	return current.identity == record.identity && current.digest == record.scope;
}

void RollBackRemoval(sqlite3* db, RemovalRecord const& record) {
	auto const& root = record.location.root;
	if (!record.staged.empty() && PathOccupied(record.staged)) {
		if (PathOccupied(root) || !MatchesRemoval(record, record.staged)) return;
		MoveRemovalFolder(record.staged, root);
	}
	// An atomic move to Trash leaves the original directory alone on failure.
	// A surviving staged directory moved back by the user has the same identity.
	if (PathOccupied(root) && InspectRemovalIdentity(root) == record.identity) ClearRemoval(db, record);
}

bool CanRestoreRemainder(RemovalRecord const& record) {
	try {
		return record.phase != "disposed" && !record.staged.empty() && !PathOccupied(record.location.root) &&
		       PathOccupied(record.staged) && InspectRemovalIdentity(record.staged) == record.identity;
	}
	catch (std::exception const&) { return false; }
}

bool CanConfirmRemoval(RemovalRecord const& record) {
	if (record.phase == "disposed") return true;
	try {
		if (record.phase != "ready" || (!record.staged.empty() && PathOccupied(record.staged))) return false;
		auto const& root = record.location.root;
		return !PathOccupied(root) || Linked(root) || !fs::is_directory(fs::symlink_status(root)) ||
		       InspectRemovalIdentity(root) != record.identity;
	}
	catch (std::exception const&) { return false; }
}

bool OriginalRestored(RemovalRecord const& record) {
	auto const& root = record.location.root;
	try {
		if (!PathOccupied(root) || !fs::is_directory(fs::symlink_status(root))) return false;
		auto project = Open(root);
		return project.kind == Kind::Project && project.root == root && project.id == record.location.id;
	}
	catch (agi::Exception const&) { return false; }
	catch (std::exception const&) { return false; }
}

void FinalizeRemoval(sqlite3* db, RemovalRecord const& record) {
	Transaction write(db);
	auto row = Prepare(db, "SELECT root,generation FROM locations WHERE id=? AND root IS NOT NULL");
	Bind(row, 1, record.location.id);
	if (!Row(row) || Path(Text(row, 0)) != record.location.root || Number(row, 1) != record.location.generation)
		throw Error("The project registration changed while completing folder removal.");
	Detach(db, record.location.id);
	ClearRemoval(db, record);
	write.Commit();
}

void FinishInterruptedRemoval(sqlite3* db, RemovalRecord const& record) {
	if (OriginalRestored(record)) {
		ClearRemoval(db, record);
		return;
	}
	if (!record.series_root.empty()) {
		// Only remove the confirmed member. Later series changes to other members are preserved.
		WriteLock lock(ProjectWriteLock(record.series_root));
		auto series = Open(record.series_root);
		if (series.kind != Kind::Series || series.root != record.series_root || series.id != record.series_id)
			throw Error("The original parent series is unavailable: " + record.series_root.string());
		Path relative(record.location.root.lexically_relative(record.series_root));
		if (EpisodeMembership(series, record.location.id, relative) == record.membership) {
			auto output = PrepareEpisodeRemoval(series, record.location.id, relative);
			if (output) CommitEpisodeRemoval(series, *output);
		}
	}
	FinalizeRemoval(db, record);
}
}

FolderRemoval Index::PreviewFolderRemoval(Location const& expected) const {
	auto location = RegisteredLocation(expected.id);
	if (!SameLocation(location, expected)) throw Error("The project registration changed. Refresh it first.");
	CheckReserved(db.get(), location.id, location.root);
	CheckInactive(db.get(), location.id);
	CheckRemovalScope(db.get(), file, location);
	auto scope = InspectRemovalScope(location.root);
	auto project = Open(location.root);
	if (project.kind != Kind::Project || project.id != location.id || project.root != location.root)
		throw Error("The selected directory no longer contains this project.");
	auto series = ParentSeries(project);
	return {std::move(location), std::move(project), std::move(series), scope.files, PendingTasks(expected.id),
	        std::move(scope.identity), std::move(scope.digest), TaskScope(db.get(), expected.id)};
}

void Index::RemoveFolder(FolderRemoval const& expected, FolderDisposition mode,
                         std::function<void(Path const&)> const& dispose) {
	CheckLocation();
	RecoverInterrupted();
	WriteLock owner(file.parent_path() / "removals.lock");
	auto current = PreviewFolderRemoval(expected.location);
	ConfirmPreview(expected, current);
	auto const root = current.location.root;
	std::optional<WriteLock> series_lock, project_lock;
	if (current.series) series_lock.emplace(ProjectWriteLock(current.series->root));
	project_lock.emplace(ProjectWriteLock(root));
	RemovalRecord record;
	std::unique_ptr<agi::io::Save> episode_update;
	bool stage_created = false;
	try {
		Transaction mark(db.get());
		current = PreviewFolderRemoval(expected.location);
		ConfirmPreview(expected, current);
		record = {current.location, {}, {}, {}, {}, current.identity, current.scope, "ready"};
		if (current.series) {
			record.series_id = current.series->id;
			record.series_root = current.series->root;
			Path relative(root.lexically_relative(record.series_root));
			record.membership = EpisodeMembership(*current.series, current.project.id, relative);
			episode_update = PrepareEpisodeRemoval(*current.series, current.project.id, relative);
			if (record.membership.empty() || !episode_update) throw Error("The episode membership changed. Refresh the series.");
		}
		if (mode == FolderDisposition::Permanent) {
			record.staged = root.parent_path() / (".aegisub-remove-" + NewId()) / root.filename();
			record.phase = "planned";
			CreateRemovalStage(record.staged.parent_path());
			stage_created = true;
		}
		auto insert = Prepare(db.get(), "INSERT INTO folder_removals VALUES(?,?,?,?,?,?,?,?,?,?)");
		Bind(insert, 1, record.location.id); Bind(insert, 2, root.string()); Bind(insert, 3, record.location.generation);
		Bind(insert, 4, record.series_id); Bind(insert, 5, record.series_root.string()); Bind(insert, 6, record.membership);
		Bind(insert, 7, record.staged.string()); Bind(insert, 8, record.identity); Bind(insert, 9, record.scope); Bind(insert, 10, record.phase);
		Done(insert);
		mark.Commit();
	}
	catch (...) {
		if (stage_created) CleanStage(record);
		throw;
	}
	// A Windows lock handle inside the directory can prevent a native move.
	project_lock.reset();
	auto const& target = record.staged.empty() ? root : record.staged;
	try {
		if (!record.staged.empty()) {
			MoveRemovalFolder(root, target);
			auto staged = InspectRemovalScope(target);
			if (staged.identity != record.identity || staged.digest != record.scope) {
				// The move completed, but the disposer has never seen this directory.
				MoveRemovalFolder(target, root);
				throw Error("The project directory contents changed before removal.");
			}
			Update(db.get(), "UPDATE folder_removals SET phase='ready' WHERE project_id=?", {record.location.id});
			record.phase = "ready";
		}
		else if (!MatchesRemoval(record, root)) throw Error("The project directory contents changed before removal.");
		dispose(target);
		if (PathOccupied(target)) throw Error("The disposal did not remove the project directory: " + target.string());
		Update(db.get(), "UPDATE folder_removals SET phase='disposed' WHERE project_id=?", {record.location.id});
	}
	catch (...) {
		try { RollBackRemoval(db.get(), record); }
		catch (...) {
			throw Error("Could not restore the project directory. Check " + target.string() + " and " + root.string());
		}
		if (!record.staged.empty() && PathOccupied(record.staged))
			throw Error("Permanent removal did not finish. Check the remaining files at " + record.staged.string());
		throw;
	}
	if (OriginalRestored(record)) {
		ClearRemoval(db.get(), record);
		throw Error("The project directory was restored before the removal could be completed.");
	}
	if (episode_update) CommitEpisodeRemoval(*current.series, *episode_update);
	FinalizeRemoval(db.get(), record);
}

std::vector<Location> Index::UncertainFolderRemovals() const {
	CheckLocation();
	std::vector<Location> result;
	for (auto const& record : RemovalRecords(db.get()))
		result.push_back(record.location);
	return result;
}

std::optional<FolderRemovalRecovery> Index::FolderRecovery(Location const& expected) const {
	CheckLocation();
	for (auto const& record : RemovalRecords(db.get())) {
		if (SameLocation(expected, record.location))
			return FolderRemovalRecovery{record.staged, CanConfirmRemoval(record), CanRestoreRemainder(record)};
	}
	return {};
}

void Index::ConfirmFolderRemoval(Location const& expected) {
	CheckLocation();
	WriteLock owner(file.parent_path() / "removals.lock");
	for (auto const& record : RemovalRecords(db.get())) {
		if (!SameLocation(expected, record.location)) continue;
		if (!CanConfirmRemoval(record))
			throw Error("The interrupted folder removal cannot be confirmed at this location.");
		Update(db.get(), "UPDATE folder_removals SET phase='disposed' WHERE project_id=?", {record.location.id});
		FinishInterruptedRemoval(db.get(), record);
		return;
	}
	throw Error("The pending folder removal changed. Refresh it first.");
}

void Index::RestoreFolderRemainder(Location const& expected) {
	CheckLocation();
	WriteLock owner(file.parent_path() / "removals.lock");
	for (auto const& record : RemovalRecords(db.get())) {
		if (!SameLocation(expected, record.location)) continue;
		if (!CanRestoreRemainder(record)) throw Error("The remaining project directory cannot be restored at this location.");
		MoveRemovalFolder(record.staged, record.location.root);
		if (InspectRemovalIdentity(record.location.root) != record.identity) {
			MoveRemovalFolder(record.location.root, record.staged);
			throw Error("The remaining project directory changed before it could be restored.");
		}
		ClearRemoval(db.get(), record);
		return;
	}
	throw Error("The pending folder removal changed. Refresh it first.");
}

void Index::RecoverFolderRemovals() {
	if (!Scalar(db.get(), "SELECT EXISTS(SELECT 1 FROM folder_removals)")) return;
	WriteLock owner(file.parent_path() / "removals.lock", true);
	if (!owner.Acquired()) return;
	for (auto const& record : RemovalRecords(db.get())) {
		try {
			if (record.phase == "disposed")
				FinishInterruptedRemoval(db.get(), record);
			else RollBackRemoval(db.get(), record);
		}
		catch (agi::Exception const&) { /* Keep the journal until the original locations are available. */ }
		catch (std::exception const&) { /* Other projects remain available while this removal is pending. */ }
	}
}

void Index::Unmanage(Location const& expected) {
	CheckLocation();
	RecoverInterrupted();
	Transaction write(db.get());
	auto location = RegisteredLocation(expected.id);
	if (location.root != expected.root || location.generation != expected.generation) throw Error("The project registration changed. Refresh it first.");
	CheckReserved(db.get(), expected.id, location.root);
	Detach(db.get(), expected.id);
	write.Commit();
}

void Index::RegisterExisting(Manifest const& expected) {
	CheckLocation();
	Transaction write(db.get());
	auto actual = Open(expected.root);
	if (actual.id != expected.id || actual.root != expected.root || actual.kind != expected.kind) throw Error("The selected folder changed.");
	Update(db.get(), "UPDATE locations SET root=?,last_root=NULL,generation=generation+1 WHERE id=? AND kind=? AND root IS NULL",
	       {actual.root.string(), actual.id, KindName(actual.kind)});
	Remember(actual);
	Update(db.get(), "UPDATE tasks SET state=(SELECT previous_state FROM paused_tasks WHERE task_id=tasks.task_id) "
	       "WHERE project_id=? AND state='paused_project' AND task_id IN (SELECT task_id FROM paused_tasks)", {actual.id});
	Update(db.get(), "DELETE FROM paused_tasks WHERE task_id IN (SELECT task_id FROM tasks WHERE project_id=?)", {actual.id});
	write.Commit();
}

Manifest Index::AddEpisode(Manifest const& series, Manifest const& child, std::string const& label) {
	CheckLocation();
	std::vector<Location> others;
	{
		auto rows = Prepare(db.get(), "SELECT id,name,root FROM locations WHERE kind='series' AND root IS NOT NULL AND id<>?");
		Bind(rows, 1, series.id);
		while (Row(rows)) others.push_back({Text(rows, 0), Text(rows, 1), Path(Text(rows, 2))});
	}
	for (auto const& location : others) {
		if (!location.root.is_absolute()) continue;
		std::optional<Manifest> other;
		try {
			if (!Linked(location.root) && fs::is_regular_file(location.root / ".aeg/series.json")) other = Open(location.root);
		}
		catch (agi::Exception const&) { continue; }
		catch (std::out_of_range const&) { continue; }
		catch (std::runtime_error const&) { continue; }
		if (other && other->kind == Kind::Series && other->id == location.id)
			for (auto const& member : other->episodes)
				if (member.project_id == child.id) throw Error("This project already belongs to another series: " + other->name);
	}
	// The manifest owns membership. If the final database commit fails, retrying
	// the same link restores the local positions without appending another entry.
	Transaction positions(db.get());
	Remember(series);
	auto latest = Open(child.root);
	if (latest.id != child.id || latest.kind != Kind::Project) throw Error("The episode folder contains a different project");
	Remember(latest);
	auto updated = LinkEpisode(series, latest, label);
	Remember(updated);
	positions.Commit();
	return updated;
}

void Index::Visit(Manifest const& project, std::string const& page) {
	if (!page.empty() && page != "home" && page != "dashboard" && page != "series" && page != "files" && page != "tasks" && page != "downloads")
		throw Error("Invalid workbench page");
	CheckLocation();
	Transaction write(db.get());
	Remember(project);
	auto stmt = Prepare(db.get(), page.empty()
	    ? "UPDATE visits SET status=?2,updated_at=?3,cover=?4 WHERE project_id=?1"
	    : "INSERT OR REPLACE INTO visits(project_id,status,updated_at,cover,page) VALUES(?1,?2,?3,?4,?5)");
	Bind(stmt, 1, project.id); Bind(stmt, 2, project.metadata.status);
	Bind(stmt, 3, project.updated_at); Bind(stmt, 4, project.cover.generic_string());
	if (!page.empty()) Bind(stmt, 5, page);
	Done(stmt);
	write.Commit();
}

std::vector<Location> Index::List(int offset, int count, LibraryView view, std::string const& search) const {
	CheckLocation();
	Page(offset, count);
	auto stmt = Prepare(db.get(), "SELECT id,name,root,coalesce(page,''),coalesce(status,''),coalesce(updated_at,0),coalesce(cover,''),generation "
	    "FROM locations LEFT JOIN visits ON id=project_id "
	    "WHERE root IS NOT NULL AND (?3=0 OR (?3=1 AND kind='series') OR (?3=2 AND visits.rowid IS NOT NULL)) "
	    "AND (?4='' OR instr(lower(name),lower(?4)) OR instr(lower(root),lower(?4))) "
	    "ORDER BY CASE WHEN ?3=2 THEN visits.rowid END DESC,name COLLATE NOCASE,id LIMIT ?1 OFFSET ?2");
	Bind(stmt, 1, count);
	Bind(stmt, 2, offset);
	Bind(stmt, 3, static_cast<int>(view));
	Bind(stmt, 4, search);
	std::vector<Location> result;
	while (Row(stmt)) {
		auto id = Text(stmt, 0), name = Text(stmt, 1), root = Text(stmt, 2);
		if (!ValidId(id) || !ValidDisplayName(name) || !Path(root).is_absolute()) throw Error("Invalid project location");
		auto cover = Path(Text(stmt, 6));
		if (!cover.empty() && cover.parent_path() != Path(".aeg/covers")) throw Error("Invalid cached project cover");
		result.push_back({std::move(id), std::move(name), Path(root), Text(stmt, 3), Text(stmt, 4), Number(stmt, 5), std::move(cover), Number(stmt, 7)});
	}
	return result;
}

namespace {
Manifest CurrentProject(sqlite3* db, std::string const& id) {
	auto stmt = Prepare(db, "SELECT root FROM locations WHERE id=?1 AND kind='project' AND root IS NOT NULL");
	Bind(stmt, 1, id);
	if (!Row(stmt)) throw Error("The project location is unavailable");
	Path root(Text(stmt, 0));
	CheckReserved(db, id, root);
	if (!root.is_absolute() || Linked(root)) throw Error("Invalid project location");
	auto project = Open(root);
	if (project.id != id || project.kind != Kind::Project || project.root != root) throw Error("This folder now contains a different project");
	return project;
}
void ConfirmLocation(sqlite3* db, Manifest const& expected) {
	if (CurrentProject(db, expected.id).root != expected.root) throw Error("The project moved. Reopen it first.");
}
}

ScanState Index::Scan(std::string const& project_id) const {
	CheckLocation();
	auto stmt = Prepare(db.get(), "SELECT generation,files FROM scan_state WHERE project_id=?");
	Bind(stmt, 1, project_id);
	return Row(stmt) ? ScanState{Number(stmt, 0), Number(stmt, 1)} : ScanState{};
}

FileView MediaKind(std::string_view path) {
	auto pos = path.find_last_of('.');
	if (pos == path.npos) return FileView::All;
	auto ext = path.substr(pos);
	auto matches = [&](std::string_view lower) {
		return ext.size() == lower.size() && std::equal(ext.begin(), ext.end(), lower.begin(), [](char a, char b) {
			return (a >= 'A' && a <= 'Z' ? a + ('a' - 'A') : a) == b;
		});
	};
	for (auto s : {".ass", ".ssa", ".srt", ".sub", ".ttxt", ".txt"})
		if (matches(s)) return FileView::Subtitles;
	for (auto s : {".mkv", ".mp4", ".mov", ".avi", ".webm", ".m4v", ".ts", ".m2ts", ".mpeg", ".mpg", ".wmv", ".flv",
	               ".avs", ".y4m", ".yuv", ".asf", ".d2v", ".h264", ".hevc", ".ogm"})
		if (matches(s)) return FileView::Video;
	return FileView::All;
}

bool Index::RecordFile(Manifest const& project, Path const& relative, FileEvent event) {
	CheckLocation();
	Transaction write(db.get());
	CheckReserved(db.get(), project.id, project.root);
	auto row = Prepare(db.get(), "SELECT id FROM locations WHERE root=? AND kind='project'");
	Bind(row, 1, project.root.string());
	if (!Row(row) || Text(row, 0) != project.id) return false;
	ResolveProjectFile(project, relative);
	Update(db.get(), "INSERT OR REPLACE INTO recent_files(project_id,path) VALUES(?,?)", {project.id, relative.generic_string()});
	Update(db.get(), "DELETE FROM recent_files WHERE project_id=?1 AND rowid NOT IN "
	       "(SELECT rowid FROM recent_files WHERE project_id=?1 ORDER BY rowid DESC LIMIT 100)", {project.id});
	std::string proposed;
	if (event == FileEvent::Saved && project.metadata.status == "todo") proposed = "doing";
	if (event == FileEvent::Exported && (project.metadata.status == "doing" || project.metadata.status == "todo")) proposed = "review";
	if (!proposed.empty()) {
		auto stmt = Prepare(db.get(), "INSERT OR REPLACE INTO status_hints(project_id,token,revision,status) VALUES(?,?,?,?)");
		Bind(stmt, 1, project.id); Bind(stmt, 2, NewId()); Bind(stmt, 3, project.revision); Bind(stmt, 4, proposed); Done(stmt);
	}
	write.Commit();
	return true;
}

std::optional<StatusHint> Index::SuggestedStatus(Manifest const& project) const {
	CheckLocation();
	auto stmt = Prepare(db.get(), "SELECT token,status FROM status_hints WHERE project_id=? AND revision=?");
	Bind(stmt, 1, project.id); Bind(stmt, 2, project.revision);
	if (Row(stmt)) return StatusHint{Text(stmt, 0), Text(stmt, 1)};
	if (project.metadata.status != "todo") return {};
	auto task = Prepare(db.get(), "SELECT task_id FROM (SELECT t.task_id FROM tasks t WHERE t.project_id=?1 "
	    "AND t.source_kind='web' AND t.state='succeeded' AND t.updated_at>=?2 "
	    "AND EXISTS(SELECT 1 FROM outputs o WHERE o.task_id=t.task_id AND o.stage='registered') "
	    "AND NOT EXISTS(SELECT 1 FROM outputs o WHERE o.task_id=t.task_id AND o.stage<>'registered') "
	    "ORDER BY t.updated_at DESC,t.rowid DESC LIMIT 1) "
	    "WHERE task_id NOT IN (SELECT task_id FROM reviewed_downloads WHERE project_id=?1)");
	Bind(task, 1, project.id); Bind(task, 2, project.updated_at);
	if (Row(task)) return StatusHint{Text(task, 0), "doing", true};
	return {};
}

Manifest Index::ResolveSuggestion(Manifest const& project, StatusHint const& hint, bool accept) {
	CheckLocation();
	Transaction write(db.get());
	auto latest = CurrentProject(db.get(), project.id);
	auto saved = SuggestedStatus(latest);
	if (!saved || saved->token != hint.token || project.bytes != latest.bytes) throw Error("This suggestion changed. Refresh the dashboard.");
	if (accept) {
		auto values = latest.metadata;
		values.status = saved->status;
		latest = UpdateMetadata(latest, values);
	}
	if (saved->download)
		Update(db.get(), "INSERT OR REPLACE INTO reviewed_downloads(project_id,task_id) VALUES(?,?)", {project.id, saved->token});
	else Update(db.get(), "DELETE FROM status_hints WHERE project_id=? AND token=?", {project.id, saved->token});
	write.Commit();
	return latest;
}

std::vector<FileEntry> Index::Files(std::string const& project_id, int offset, int count, FileView view) const {
	CheckLocation();
	Page(offset, count);
	auto stmt = Prepare(db.get(), view == FileView::Recent ?
	    "SELECT r.path,coalesce(f.size,0) FROM recent_files r LEFT JOIN file_entries f USING(project_id,path) "
	    "WHERE r.project_id=?1 ORDER BY r.rowid DESC LIMIT ?2 OFFSET ?3" :
	    "SELECT path,size FROM file_entries WHERE project_id=?1 AND (?4=0 OR media_kind(path)=?4) ORDER BY path LIMIT ?2 OFFSET ?3");
	Bind(stmt, 1, project_id); Bind(stmt, 2, count); Bind(stmt, 3, offset);
	if (view != FileView::Recent) Bind(stmt, 4, static_cast<int>(view));
	std::vector<FileEntry> result;
	while (Row(stmt)) result.push_back({Path(Text(stmt, 0)), Number(stmt, 1)});
	return result;
}

void Index::RebuildFiles(Manifest const& project, std::stop_token stop) {
	CheckStop(stop);
	CheckLocation();
	ConfirmLocation(db.get(), project);
	auto registration = RegisteredLocation(project.id).generation;
	auto before = Scan(project.id);
	if (before.generation == (std::numeric_limits<int64_t>::max)()) throw Error("File index generation limit reached");
	auto machine = fs::canonical(file.parent_path());
	if (fs::equivalent(project.root, machine)) throw Error("The workbench data directory cannot be indexed as a project");
	// Only the connection-local staging table is written during the directory walk.
	Sql(db.get(), "CREATE TEMP TABLE IF NOT EXISTS scanning(path TEXT PRIMARY KEY,size INTEGER NOT NULL) WITHOUT ROWID");
	int64_t total = 0;
	{
		Transaction staging(db.get(), false);
		Sql(db.get(), "DELETE FROM temp.scanning");
		auto insert = Prepare(db.get(), "INSERT INTO temp.scanning VALUES(?,?)");
		for (fs::recursive_directory_iterator it(project.root), end; it != end; ++it) {
			CheckStop(stop);
			Path path(it->path());
			auto name = path.filename();
			if (Linked(path) || InternalDirectoryName(name)) { it.disable_recursion_pending(); continue; }
			if (it->is_directory()) {
				if (fs::equivalent(path, machine) || fs::exists(path / ".aeg") || Linked(path / ".aeg"))
					it.disable_recursion_pending();
				continue;
			}
			if (!it->is_regular_file() || (name.string().starts_with(".aegisub-save-") && name.extension() == ".tmp")) continue;
			auto bytes = it->file_size();
			if (bytes > static_cast<uintmax_t>((std::numeric_limits<int64_t>::max)())) throw Error("File is too large to index");
			Bind(insert, 1, Path(path.lexically_relative(project.root)).generic_string());
			Bind(insert, 2, static_cast<int64_t>(bytes));
			Done(insert);
			sqlite3_reset(insert.get());
			++total;
		}
		staging.Commit();
	}
	Transaction publish(db.get());
	CheckLocation();
	ConfirmLocation(db.get(), project);
	if (RegisteredLocation(project.id).generation != registration) throw Error("The project registration changed during the scan.");
	if (Scan(project.id).generation != before.generation) throw Error("Another scan already updated this project");
	Update(db.get(), "DELETE FROM file_entries WHERE project_id=?", {project.id});
	Update(db.get(), "INSERT INTO file_entries(project_id,path,size) SELECT ?,path,size FROM temp.scanning", {project.id});
	auto state = Prepare(db.get(), "INSERT INTO scan_state VALUES(?,?,?,?) ON CONFLICT(project_id) DO UPDATE SET "
	                     "generation=excluded.generation,files=excluded.files,scanned_at=excluded.scanned_at");
	Bind(state, 1, project.id); Bind(state, 2, before.generation + 1); Bind(state, 3, total); Bind(state, 4, Now());
	Done(state);
	CheckStop(stop);
	publish.Commit();
}

std::string Index::QueueRegistration(Manifest const& project, std::vector<Path> const& files, std::stop_token stop) {
	CheckLocation();
	ConfirmLocation(db.get(), project);
	auto registration = RegisteredLocation(project.id).generation;
	if (files.empty()) throw Error("Choose at least one project file");
	auto task = NewId();
	std::vector<RegistrationOutput> outputs;
	std::set<std::string> seen;
	for (auto const& relative : files) {
		if (!seen.insert(relative.generic_string()).second) continue;
		auto fingerprint = HashFile(project, relative, stop);
		outputs.push_back({NewId(), NewId(), relative, fingerprint.stamp.bytes,
		                   fingerprint.sha256, fingerprint.stamp.identity, "none"});
	}
	Transaction write(db.get());
	CheckLocation();
	ConfirmLocation(db.get(), project);
	if (RegisteredLocation(project.id).generation != registration) throw Error("The project registration changed while checking files.");
	auto insert = Prepare(db.get(), "INSERT INTO tasks(task_id,project_id,name,source_kind,auth_mode,state,created_at,updated_at) "
	                      "VALUES(?1,?2,?3,'local_import','none','queued',?4,?4)");
	Bind(insert, 1, task); Bind(insert, 2, project.id); Bind(insert, 3, outputs.front().relative.filename().string()); Bind(insert, 4, Now());
	Done(insert);
	auto add = Prepare(db.get(), "INSERT INTO outputs(output_id,task_id,asset_id,path,size,sha256,file_id,stage)"
	                   " VALUES(?,?,?,?,?,?,?,'none')");
	for (auto const& out : outputs) {
		auto duplicate = Prepare(db.get(), "SELECT 1 FROM outputs o JOIN tasks t ON t.task_id=o.task_id "
		                         "WHERE t.project_id=? AND o.path=? AND (o.stage='registered' OR t.state<>'cancelled') LIMIT 1");
		Bind(duplicate, 1, project.id); Bind(duplicate, 2, out.relative.generic_string());
		if (Row(duplicate)) throw Error("This file already has a registration task: " + out.relative.string());
		Bind(add, 1, out.id); Bind(add, 2, task); Bind(add, 3, out.asset_id); Bind(add, 4, out.relative.generic_string());
		Bind(add, 5, out.bytes); Bind(add, 6, out.sha256); Bind(add, 7, out.file_id);
		Done(add);
		sqlite3_reset(add.get());
	}
	CheckStop(stop);
	write.Commit();
	return task;
}

std::vector<RegistrationTask> Index::Tasks(TaskView view, int offset, int count, std::string const& project_id) const {
	CheckLocation();
	Page(offset, count);
	auto stmt = Prepare(db.get(), "SELECT t.task_id,coalesce(t.project_id,''),coalesce(l.name,''),t.name,t.state,t.reason,"
	    "(SELECT count(*) FROM outputs WHERE task_id=t.task_id),"
	    "(SELECT count(*) FROM outputs WHERE task_id=t.task_id AND stage='registered') "
	    "FROM tasks t LEFT JOIN locations l ON l.id=t.project_id WHERE (?4='' OR t.project_id=?4) AND "
	    "((?3='' AND state NOT IN ('succeeded','cancelled','failed')) OR source_kind=?3) "
	    "ORDER BY (state IN ('succeeded','cancelled')),created_at DESC,t.rowid DESC LIMIT ?1 OFFSET ?2");
	Bind(stmt, 1, count); Bind(stmt, 2, offset);
	Bind(stmt, 3, view == TaskView::Unfinished ? "" : view == TaskView::Downloads ? "web" : "local_import");
	Bind(stmt, 4, project_id);
	std::vector<RegistrationTask> result;
	while (Row(stmt)) result.push_back({Text(stmt, 0), Text(stmt, 1), Text(stmt, 2), Text(stmt, 3), Text(stmt, 4),
	                                    Text(stmt, 5), Number(stmt, 6), Number(stmt, 7)});
	return result;
}
int64_t Index::UnfinishedRegistrations() const {
	CheckLocation();
	auto stmt = Prepare(db.get(), "SELECT count(*) FROM tasks WHERE source_kind='local_import' AND state NOT IN ('succeeded','cancelled')");
	if (!Row(stmt)) throw Error("Cannot read registration task count");
	return Number(stmt, 0);
}

namespace {
RegistrationOutput ReadOutput(Statement const& row) {
	RegistrationOutput out{Text(row, 0), Text(row, 1), Path(Text(row, 2)), Number(row, 3), Text(row, 4), Text(row, 5), Text(row, 6)};
	if (!ValidId(out.id) || !ValidId(out.asset_id) || out.bytes < 0 || out.sha256.size() != 64 ||
	    out.sha256.find_first_not_of("0123456789abcdef") != std::string::npos || out.file_id.empty())
		throw Error("Damaged registration output");
	return out;
}
}
std::vector<RegistrationOutput> Index::Outputs(std::string const& task_id, int offset, int count) const {
	CheckLocation();
	Page(offset, count);
	auto stmt = Prepare(db.get(), "SELECT output_id,asset_id,path,size,sha256,file_id,stage FROM outputs "
	                    "WHERE task_id=? ORDER BY path LIMIT ? OFFSET ?");
	Bind(stmt, 1, task_id); Bind(stmt, 2, count); Bind(stmt, 3, offset);
	std::vector<RegistrationOutput> result;
	while (Row(stmt)) result.push_back(ReadOutput(stmt));
	return result;
}

namespace {
struct TaskState { std::string project, state; bool cancel; };
TaskState State(sqlite3* db, std::string const& task) {
	auto stmt = Prepare(db, "SELECT project_id,state,cancel_requested FROM tasks WHERE task_id=? AND source_kind='local_import'");
	Bind(stmt, 1, task);
	if (!Row(stmt)) throw Error("Local registration task not found");
	TaskState result{Text(stmt, 0), Text(stmt, 1), Number(stmt, 2) != 0};
	if (!ValidId(result.project) || (result.state != "paused_project" && result.state != "queued" && result.state != "registering" &&
	    result.state != "interrupted" && result.state != "needs_attention" && result.state != "cancelled" && result.state != "succeeded"))
		throw Error("Damaged local registration task");
	return result;
}
void RecoverAbandoned(sqlite3* db) {
	Transaction write(db);
	Sql(db, "UPDATE attempts SET state='interrupted',finished_at=CAST(strftime('%s','now') AS INTEGER) "
	        "WHERE state='registering' AND task_id IN (SELECT task_id FROM tasks WHERE source_kind='local_import' AND state='registering');"
	        "UPDATE tasks SET state='interrupted',updated_at=CAST(strftime('%s','now') AS INTEGER) WHERE source_kind='local_import' AND state='registering'");
	write.Commit();
}
std::optional<RegistrationOutput> NextOutput(sqlite3* db, std::string const& task, bool pending_only) {
	auto sql = pending_only ?
		"SELECT o.output_id,asset_id,path,size,sha256,file_id,stage FROM outputs o JOIN pending_steps p ON p.output_id=o.output_id "
		"WHERE task_id=? AND p.operation='register_existing' ORDER BY path LIMIT 1" :
		"SELECT output_id,asset_id,path,size,sha256,file_id,stage FROM outputs WHERE task_id=? AND stage<>'registered' ORDER BY path LIMIT 1";
	auto stmt = Prepare(db, sql);
	Bind(stmt, 1, task);
	if (!Row(stmt)) return {};
	auto out = ReadOutput(stmt);
	if (out.stage != "none" && out.stage != "archived") throw Error("Unexpected local output stage");
	return out;
}
std::string SetState(sqlite3* db, std::string const& task, std::string state, std::string reason = {}) {
	Transaction write(db);
	// A cancellation received while verifying a file can finish as soon as all
	// potentially published manifest changes have been accounted for.
	if ((state == "interrupted" || state == "needs_attention") && State(db, task).cancel && !NextOutput(db, task, true)) {
		state = NextOutput(db, task, false) ? "cancelled" : "succeeded";
		reason.clear();
	}
	auto set = Prepare(db, "UPDATE tasks SET state=?,reason=?,updated_at=? WHERE task_id=?");
	Bind(set, 1, state); Bind(set, 2, reason); Bind(set, 3, Now()); Bind(set, 4, task);
	Done(set);
	auto finish = Prepare(db, "UPDATE attempts SET state=?,finished_at=? WHERE task_id=? AND state='registering'");
	Bind(finish, 1, state); Bind(finish, 2, Now()); Bind(finish, 3, task);
	Done(finish);
	write.Commit();
	return state;
}
}

void Index::RecoverInterrupted() {
	CheckLocation();
	RecoverFolderRemovals();
	if (!Scalar(db.get(), "SELECT EXISTS(SELECT 1 FROM tasks WHERE source_kind='local_import' AND state='registering')")) return;
	WriteLock owner(file.parent_path() / "registrations.lock", true);
	if (owner.Acquired()) RecoverAbandoned(db.get());
}

void Index::RunRegistration(std::string const& task, std::stop_token stop) {
	CheckLocation();
	WriteLock owner(file.parent_path() / "registrations.lock");
	RecoverAbandoned(db.get());
	{
		Transaction begin(db.get());
		auto initial = State(db.get(), task);
		if (initial.state == "succeeded" || initial.state == "cancelled") return;
		CheckReserved(db.get(), initial.project);
		if (initial.state == "paused_project") throw Error("Register the original project before continuing this task.");
		auto attempt = Prepare(db.get(), "INSERT INTO attempts(attempt_id,task_id,state,started_at) VALUES(?,?,'registering',?)");
		Bind(attempt, 1, NewId()); Bind(attempt, 2, task); Bind(attempt, 3, Now()); Done(attempt);
		Update(db.get(), "UPDATE tasks SET state='registering',reason='',updated_at=CAST(strftime('%s','now') AS INTEGER) WHERE task_id=?", {task});
		begin.Commit();
	}
	try {
		for (;;) {
			CheckLocation();
			auto current = State(db.get(), task);
			// A cancelled task only reconciles outputs whose manifest write may already have happened.
			auto out = NextOutput(db.get(), task, current.cancel);
			if (!out) {
				bool remaining = current.cancel && NextOutput(db.get(), task, false).has_value();
				SetState(db.get(), task, remaining ? "cancelled" : "succeeded");
				return;
			}
			CheckStop(stop);
			auto project = CurrentProject(db.get(), current.project);
			if (current.cancel && !ContainsOutput(project, task, *out)) {
				Update(db.get(), "DELETE FROM pending_steps WHERE output_id=?", {out->id});
				continue;
			}
			auto fingerprint = HashFile(project, out->relative, stop);
			if (fingerprint.stamp.bytes != out->bytes || fingerprint.stamp.identity != out->file_id || fingerprint.sha256 != out->sha256)
				throw Error("The selected file changed. Check it before registration: " + out->relative.string());
			CheckLocation();
			if (!current.cancel) {
				Transaction checkpoint(db.get());
				Update(db.get(), "UPDATE outputs SET stage='archived' WHERE output_id=?", {out->id});
				auto mark = Prepare(db.get(), "INSERT INTO pending_steps VALUES(?,'register_existing',?) ON CONFLICT(output_id) DO NOTHING");
				Bind(mark, 1, out->id); Bind(mark, 2, Now()); Done(mark);
				checkpoint.Commit();
			}
			CheckStop(stop);
			CheckLocation();
			auto cancelled = State(db.get(), task).cancel;
			bool published = PublishAsset(project, task, *out, fingerprint.stamp, cancelled);
			Transaction complete(db.get());
			CheckLocation();
			if (published) Update(db.get(), "UPDATE outputs SET stage='registered' WHERE output_id=?", {out->id});
			Update(db.get(), "DELETE FROM pending_steps WHERE output_id=?", {out->id});
			complete.Commit();
		}
	}
	catch (agi::UserCancelException const&) {
		CheckLocation();
		if (SetState(db.get(), task, "interrupted") == "interrupted") throw;
	}
	catch (agi::Exception const& e) {
		CheckLocation();
		if (SetState(db.get(), task, "needs_attention", e.GetMessage()) == "needs_attention") throw;
	}
	catch (std::exception const& e) {
		CheckLocation();
		if (SetState(db.get(), task, "needs_attention", e.what()) == "needs_attention") throw;
	}
}

void Index::CancelRegistration(std::string const& task) {
	CheckLocation();
	{
		Transaction intent(db.get());
		auto current = State(db.get(), task);
		if (current.state == "succeeded" || current.state == "cancelled") return;
		CheckReserved(db.get(), current.project);
		if (current.state == "paused_project") throw Error("Register the original project before changing this task.");
		Update(db.get(), "UPDATE tasks SET cancel_requested=1 WHERE task_id=?", {task});
		if (current.state != "registering" && !NextOutput(db.get(), task, true)) {
			Update(db.get(), "UPDATE tasks SET state=?,reason='',updated_at=CAST(strftime('%s','now') AS INTEGER) WHERE task_id=?",
			       {NextOutput(db.get(), task, false) ? "cancelled" : "succeeded", task});
			intent.Commit();
			return;
		}
		intent.Commit();
	}
	WriteLock owner(file.parent_path() / "registrations.lock", true);
	if (!owner.Acquired()) return;
	if (NextOutput(db.get(), task, true)) {
		SetState(db.get(), task, "interrupted", "Check the pending registration to finish cancellation.");
		return;
	}
	SetState(db.get(), task, NextOutput(db.get(), task, false) ? "cancelled" : "succeeded");
}
}

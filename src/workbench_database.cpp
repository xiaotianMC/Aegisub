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

constexpr int app_id = 0x41454757, schema_version = 2;
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

void BackupVersionOne(Path const& file) {
	// The migrator already holds BEGIN IMMEDIATE. A separate reader sees the last committed V1 state.
	auto source = Connect(file, SQLITE_OPEN_READONLY);
	auto backups = file.parent_path() / "backups";
	if (Linked(backups)) throw Error("Linked database backup directory");
	fs::create_directories(backups);
	auto directory = backups / ("before-v2-" + NewId());
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
		if (!Row(check) || Text(check, 0) != "ok" || Scalar(target.get(), "PRAGMA user_version") != 1)
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
	for (auto sql : {"SELECT id,kind,name,root FROM locations LIMIT 0",
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
	Sql(raw, "PRAGMA foreign_keys=ON");
	if (Scalar(raw, "PRAGMA application_id") != app_id || Scalar(raw, "PRAGMA user_version") != schema_version) {
		Transaction migration(raw);
		auto identity = Scalar(raw, "PRAGMA application_id"), version = Scalar(raw, "PRAGMA user_version");
		if (identity == 0 && version == 0 && !initialized && !existed && Scalar(raw, "SELECT count(*) FROM sqlite_master") == 0) {
			Sql(raw, "CREATE TABLE locations(id TEXT PRIMARY KEY NOT NULL, kind TEXT NOT NULL, name TEXT NOT NULL, root TEXT NOT NULL UNIQUE);"
			         "CREATE INDEX locations_name ON locations(name COLLATE NOCASE, id); PRAGMA application_id=0x41454757");
			Sql(raw, add_work_tables);
		}
		else if (identity == app_id && version == 1) {
			Prepare(raw, "SELECT id,kind,name,root FROM locations LIMIT 0");
			BackupVersionOne(database);
			Sql(raw, add_work_tables);
		}
		else if (identity != app_id || version != schema_version)
			throw Error("Unrecognized or damaged workbench database: " + database.string());
		ValidateTables(raw);
		migration.Commit();
	}
	else ValidateTables(raw);
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
	auto stmt = Prepare(db.get(), "INSERT INTO locations(id,kind,name,root) VALUES(?1,?2,?3,?4) "
	                    "ON CONFLICT(id) DO UPDATE SET name=excluded.name WHERE locations.root=excluded.root AND locations.kind=excluded.kind");
	std::array<std::string, 4> values{m.id, KindName(m.kind), m.name, m.root.string()};
	for (int i = 0; i < 4; ++i)
		if (sqlite3_bind_text(stmt.get(), i+1, values[i].c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
			throw Error(sqlite3_errmsg(db.get()));
	if (sqlite3_step(stmt.get()) != SQLITE_DONE) throw Error(sqlite3_errmsg(db.get()));
	if (!sqlite3_changes(db.get())) throw Error("This project ID is already registered at a different location.");
}

Manifest Index::AddEpisode(Manifest const& series, Manifest const& child, std::string const& label) {
	CheckLocation();
	std::vector<Location> others;
	{
		auto rows = Prepare(db.get(), "SELECT id,name,root FROM locations WHERE kind='series' AND id<>?");
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

std::vector<Location> Index::List(int offset, int count) const {
	CheckLocation();
	Page(offset, count);
	auto stmt = Prepare(db.get(), "SELECT id,name,root FROM locations ORDER BY name COLLATE NOCASE,id LIMIT ?1 OFFSET ?2");
	Bind(stmt, 1, count);
	Bind(stmt, 2, offset);
	std::vector<Location> result;
	while (Row(stmt)) {
		auto id = Text(stmt, 0), name = Text(stmt, 1), root = Text(stmt, 2);
		if (!ValidId(id) || !ValidDisplayName(name) || !Path(root).is_absolute()) throw Error("Invalid project location");
		result.push_back({std::move(id), std::move(name), Path(root)});
	}
	return result;
}

namespace {
Manifest CurrentProject(sqlite3* db, std::string const& id) {
	auto stmt = Prepare(db, "SELECT root FROM locations WHERE id=?1 AND kind='project'");
	Bind(stmt, 1, id);
	if (!Row(stmt)) throw Error("The project location is unavailable");
	Path root(Text(stmt, 0));
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

std::vector<FileEntry> Index::Files(std::string const& project_id, int offset, int count) const {
	CheckLocation();
	Page(offset, count);
	auto stmt = Prepare(db.get(), "SELECT path,size FROM file_entries WHERE project_id=?1 ORDER BY path LIMIT ?2 OFFSET ?3");
	Bind(stmt, 1, project_id); Bind(stmt, 2, count); Bind(stmt, 3, offset);
	std::vector<FileEntry> result;
	while (Row(stmt)) result.push_back({Path(Text(stmt, 0)), Number(stmt, 1)});
	return result;
}

void Index::RebuildFiles(Manifest const& project, std::stop_token stop) {
	CheckStop(stop);
	CheckLocation();
	ConfirmLocation(db.get(), project);
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

std::vector<RegistrationTask> Index::Registrations(int offset, int count) const {
	CheckLocation();
	Page(offset, count);
	auto stmt = Prepare(db.get(), "SELECT t.task_id,t.project_id,l.name,t.name,t.state,t.reason,"
	    "(SELECT count(*) FROM outputs WHERE task_id=t.task_id),"
	    "(SELECT count(*) FROM outputs WHERE task_id=t.task_id AND stage='registered') "
	    "FROM tasks t JOIN locations l ON l.id=t.project_id WHERE source_kind='local_import' "
	    "ORDER BY (state IN ('succeeded','cancelled')),created_at DESC,t.rowid DESC LIMIT ? OFFSET ?");
	Bind(stmt, 1, count); Bind(stmt, 2, offset);
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
	if (!ValidId(result.project) || (result.state != "queued" && result.state != "registering" &&
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

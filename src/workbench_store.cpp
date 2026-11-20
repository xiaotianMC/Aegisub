// SPDX-License-Identifier: ISC
#include "workbench_internal.h"

#include <libaegisub/cajun/writer.h>
#include <libaegisub/io.h>
#include <libaegisub/json.h>
#include <libaegisub/option.h>
#include <libaegisub/option_value.h>
#include <libaegisub/path.h>
#include <libaegisub/sha256.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace workbench {
namespace fs = std::filesystem;
using namespace detail;
using Error = std::runtime_error;
bool detail::Linked(Path const& path) {
#ifdef _WIN32
	auto attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
	return fs::is_symlink(path);
#endif
}
static char const *KindName(Kind kind) { return kind == Kind::Project ? "project" : "series"; }
static Path Filename(Kind kind) { return std::string(KindName(kind)) + ".json"; }
int64_t detail::Now() {
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string detail::NewId() {
	std::array<unsigned char, 16> bytes;
	std::random_device rng;
	std::uniform_int_distribution<unsigned> next(0, 255);
	for (auto& byte : bytes) byte = static_cast<unsigned char>(next(rng));
	bytes[6] = (bytes[6] & 15) | 0x40;
	bytes[8] = (bytes[8] & 63) | 0x80;
	char const hex[] = "0123456789abcdef";
	std::string result;
	for (size_t i = 0; i < bytes.size(); ++i) {
		if (i == 4 || i == 6 || i == 8 || i == 10) result += '-';
		result += hex[bytes[i] >> 4];
		result += hex[bytes[i] & 15];
	}
	return result;
}

bool detail::ValidId(std::string const& id) {
	if (id.size() != 36) return false;
	for (size_t i = 0; i < id.size(); ++i) {
		if (i == 8 || i == 13 || i == 18 || i == 23) { if (id[i] != '-') return false; }
		else if (!std::isxdigit(static_cast<unsigned char>(id[i]))) return false;
	}
	return true;
}

bool detail::ValidDisplayName(std::string const& name) {
	return name.find_first_not_of(' ') != name.npos &&
	       std::none_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32 || ch == 127; });
}

namespace {
void CheckComponent(std::string const& name) {
	if (name.empty() || name.back() == ' ' || name.back() == '.' || name.find_first_of("/\\:<>|\"?*") != name.npos
	    || std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32; }))
		throw Error("Invalid portable folder name: " + name);
	std::string stem = name.substr(0, name.find('.'));
	std::string upper = name;
	auto uppercase = [](unsigned char c) { return char(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c); };
	std::transform(upper.begin(), upper.end(), upper.begin(), uppercase);
	std::transform(stem.begin(), stem.end(), stem.begin(), uppercase);
	if (upper == ".AEG" || stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL"
	    || (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' && stem[3] <= '9'))
		throw Error("Reserved folder name: " + name);
}

json::Object CheckedFolders(Kind kind, Folders const& folders) {
	auto roles = DefaultFolders(kind);
	if (folders.size() != roles.size()) throw Error("Unexpected template folder roles");
	std::set<std::string> seen;
	json::Object result;
	for (auto const& [role, value] : folders) {
		if (!roles.contains(role)) throw Error("Unknown template folder role: " + role);
		Path relative(value);
		if (value.empty() || relative.is_absolute() || relative.generic_string() != value)
			throw Error("Expected a relative folder path: " + value);
		for (auto const& part : relative) CheckComponent(Path(part).string());
		std::string folded = value;
		std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		if (!seen.insert(folded).second) throw Error("Duplicate template folder: " + value);
		result[role] = value;
	}
	return result;
}

Folders ReadFolders(json::Object const& values, Kind kind) {
	Folders folders;
	for (auto const& [role, value] : values) folders.emplace(role, static_cast<json::String const&>(value));
	CheckedFolders(kind, folders);
	return folders;
}

}

std::string detail::ReadBytes(Path const& file) {
	if (!fs::is_regular_file(file)) throw Error("Expected a regular file: " + file.string());
	auto in = agi::io::Open(file, true);
	std::array<char, 8192> buffer;
	std::string text;
	do {
		in->read(buffer.data(), buffer.size());
		text.append(buffer.data(), static_cast<size_t>(in->gcount()));
	} while (*in);
	if (in->bad() || !in->eof()) throw Error("Could not read " + file.string());
	return text;
}

static json::UnknownElement Parse(std::string const& bytes) {
	std::istringstream input(bytes);
	return agi::json_util::parse(input);
}

static void WriteJson(Path const& path, json::Object const& object) {
	agi::io::Save out(path, true);
	agi::JsonWriter::Write(object, out.Get());
	out.Commit();
}

// Keep the lock file in place. Both OSes release the lock on process exit.
detail::WriteLock::WriteLock(Path const& path, bool allow_busy) {
#ifdef _WIN32
	auto native = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (native == INVALID_HANDLE_VALUE) {
		if (allow_busy && GetLastError() == ERROR_SHARING_VIOLATION) return;
		throw Error("Cannot lock " + path.string());
	}
	BY_HANDLE_FILE_INFORMATION info{};
	if (GetFileType(native) != FILE_TYPE_DISK || !GetFileInformationByHandle(native, &info) ||
	    (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
		CloseHandle(native);
		throw Error("Invalid lock file: " + path.string());
	}
	handle = reinterpret_cast<intptr_t>(native);
#else
	int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
	if (fd == -1) throw Error("Cannot open lock " + path.string());
	struct stat info{};
	if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
		::close(fd);
		throw Error("Invalid lock file: " + path.string());
	}
	if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
		auto error = errno;
		::close(fd);
		if (allow_busy && (error == EWOULDBLOCK || error == EAGAIN)) return;
		throw Error("Cannot lock " + path.string());
	}
	handle = fd;
#endif
}

detail::WriteLock::~WriteLock() {
	if (!Acquired()) return;
#ifdef _WIN32
	CloseHandle(reinterpret_cast<HANDLE>(handle));
#else
	::close(static_cast<int>(handle));
#endif
}

static Path ManifestPath(Path const& root, Kind kind) { return root / ".aeg" / Filename(kind); }

Roots Roots::Resolve(agi::Path const& paths) {
	auto user = paths.Decode("?user"), data = paths.Decode("?data");
	auto state = paths.Decode("?state"), cache = paths.Decode("?cache");
	Path machine;
	if (user == data && state == data && cache == data) machine = data;
	else {
#ifdef _WIN32
		PWSTR local = nullptr;
		if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
			throw Error("Cannot locate Local AppData");
		std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(local, &CoTaskMemFree);
		machine = Path(fs::path(local)) / "AegisubWorkbench";
#elif defined(__APPLE__)
		machine = Path(state.parent_path()) / "AegisubWorkbench";
#else
		machine = state;
		if (machine == cache) {
			auto xdg = std::getenv("XDG_STATE_HOME");
			auto home = std::getenv("HOME");
			if (xdg && Path(xdg).is_absolute()) machine = Path(xdg) / "aegisub-workbench";
			else if (home && Path(home).is_absolute()) machine = Path(home) / ".local/state/aegisub-workbench";
			else throw Error("Cannot locate the workbench state directory");
		}
#endif
	}
	return {user / "workbench/templates", machine / "workbench/workbench.sqlite"};
}

Folders DefaultFolders(Kind kind) {
	if (kind == Kind::Series) return {{"episodes", "episodes"}};
	return {{"video", "video"}, {"audio", "audio"}, {"subtitles", "subtitles"}, {"translations", "translations"}, {"exports", "exports"}};
}

namespace {
Folders TemplateFolders(json::Object const& fields, Kind kind) {
	if (static_cast<json::Integer const&>(fields.at("schema_version")) != 1) throw Error("Unsupported template version");
	if (auto i = fields.find("kind"); i != fields.end() && static_cast<json::String const&>(i->second) != KindName(kind))
		throw Error("The template belongs to a different project type");
	if (auto i = fields.find("template_id"); i != fields.end() && !ValidId(static_cast<json::String const&>(i->second)))
		throw Error("Invalid directory template ID");
	if (auto i = fields.find("revision"); i != fields.end() && static_cast<json::Integer const&>(i->second) < 1)
		throw Error("Invalid directory template revision");
	return ReadFolders(fields.at("folders"), kind);
}
void WriteTemplate(Path const& file, Kind kind, Folders const& folders, DirectoryTemplate const* expected) {
	if (Linked(file)) throw Error("Linked directory template: " + file.string());
	bool existed = fs::exists(file);
	auto bytes = existed ? ReadBytes(file) : std::string{};
	if (expected && (existed != expected->bytes.has_value() || (existed && bytes != *expected->bytes)))
		throw Error("The directory template changed. Reopen it before saving.");
	auto json = existed ? Parse(bytes) : json::UnknownElement(json::Object{});
	auto& fields = static_cast<json::Object&>(json);
	auto previous = existed ? TemplateFolders(fields, kind) : DefaultFolders(kind);
	auto checked = CheckedFolders(kind, folders);
	if (fields.contains("revision") && previous == folders) return;
	int64_t revision = fields.contains("revision") ? static_cast<json::Integer const&>(fields.at("revision")) : 0;
	if (revision == (std::numeric_limits<int64_t>::max)()) throw Error("Template revision limit reached");
	if (!fields.contains("template_id")) fields["template_id"] = NewId();
	fields["kind"] = KindName(kind);
	fields["schema_version"] = 1;
	fields["revision"] = revision + 1;
	fields["folders"] = std::move(checked);
	agi::io::Save out(file, true);
	agi::JsonWriter::Write(fields, out.Get());
	if (Linked(file) || fs::exists(file) != existed || (existed && ReadBytes(file) != bytes))
		throw Error("The template changed while it was being saved.");
	out.Commit();
}
}
Folders ImportTemplate(Path const& file, Kind kind) {
	if (Linked(file)) throw Error("Linked directory template: " + file.string());
	auto const json = Parse(ReadBytes(file));
	return TemplateFolders(json, kind);
}
DirectoryTemplate LoadTemplate(Path const& directory, Kind kind) {
	auto file = directory / Filename(kind);
	if (Linked(file)) throw Error("Linked directory template: " + file.string());
	if (!fs::exists(file)) return {DefaultFolders(kind), {}};
	auto bytes = ReadBytes(file);
	auto const json = Parse(bytes);
	return {TemplateFolders(json, kind), std::move(bytes)};
}
void SaveTemplate(Path const& directory, Kind kind, Folders const& folders, DirectoryTemplate const& expected) {
	fs::create_directories(directory);
	WriteLock lock(directory / "write.lock");
	WriteTemplate(directory / Filename(kind), kind, folders, &expected);
}
void ExportTemplate(Path const& file, Kind kind, Folders const& folders) {
	if (file.filename() == Filename(kind)) {
		auto directory = file.parent_path();
		SaveTemplate(directory, kind, folders, LoadTemplate(directory, kind));
		return;
	}
	WriteTemplate(file, kind, folders, nullptr);
}

namespace {
// These independent editor switches can be imported into a running session.
// Machine paths, external commands and account settings have their own lifecycles.
constexpr char const *editor_switches[] = {
	"App/Call Tips", "App/Show Toolbar", "Audio/Auto/Focus", "Audio/Auto/Scroll",
	"Subtitle/Edit Box/Auto Completion", "Subtitle/Edit Box/Fields Below Text",
	"Subtitle/Edit Box/Soft Line Break", "Subtitle/Grid/Alternate Rows",
	"Subtitle/Grid/Highlight Subtitles in Frame", "Subtitle/Highlight/Syntax",
	"Subtitle/Show Original", "Subtitle/Time Edit/Insert Mode",
	"Video/Overscan Mask", "Video/Slider/Show Keyframes",
};

json::UnknownElement const* FindOption(json::Object const& object, std::string_view key) {
	auto slash = key.find('/');
	auto item = object.find(key.substr(0, slash));
	if (item == object.end()) return nullptr;
	if (slash == key.npos) return &item->second;
	return FindOption(static_cast<json::Object const&>(item->second), key.substr(slash + 1));
}
void PutOption(json::Object& object, std::string_view key, bool value) {
	auto slash = key.find('/');
	auto& entry = object[std::string(key.substr(0, slash))];
	if (slash == key.npos) entry = value;
	else PutOption(static_cast<json::Object&>(entry), key.substr(slash + 1), value);
}
std::optional<std::string> ExistingConfig(Path const& path) {
	if (Linked(path)) throw Error("Cannot replace a linked configuration: " + path.string());
	if (!fs::exists(path)) return {};
	if (!fs::is_regular_file(path)) throw Error("Invalid configuration file: " + path.string());
	return ReadBytes(path);
}
}

PreferenceImport PreviewPreferences(Path const& source, Path const& destination, agi::Options& active) {
	if (fs::exists(destination) && fs::equivalent(source, destination)) throw Error("This configuration is already active.");
	auto const imported = Parse(ReadBytes(source));
	auto const& fields = static_cast<json::Object const&>(imported);
	PreferenceImport preview{source, destination, ExistingConfig(destination), {}};
	if (preview.previous_file) {
		auto const parsed = Parse(*preview.previous_file);
		(void)static_cast<json::Object const&>(parsed);
	}
	for (auto key : editor_switches) {
		auto incoming = FindOption(fields, key);
		if (!incoming) continue;
		bool value = static_cast<json::Boolean const&>(*incoming);
		bool current = active.Get(key)->GetBool();
		if (value != current) preview.choices.push_back({key, current, value});
	}
	return preview;
}

Path ImportPreferences(PreferenceImport const& preview, std::vector<size_t> const& selected, agi::Options& active) {
	if (selected.empty()) return {};
	auto directory = preview.destination.parent_path() / "workbench";
	fs::create_directories(directory);
	WriteLock lock(directory / "preferences.lock");
	if (ExistingConfig(preview.destination) != preview.previous_file) throw Error("The active configuration changed. Preview the import again.");
	auto json = preview.previous_file ? Parse(*preview.previous_file) : json::UnknownElement(json::Object{});
	auto& fields = static_cast<json::Object&>(json);
	std::vector<std::pair<agi::OptionValue*, PreferenceChange const*>> changes;
	for (auto i : selected) {
		auto const& change = preview.choices.at(i);
		auto option = active.Get(change.key);
		if (option->GetBool() != change.current) throw Error("An editor preference changed. Preview the import again.");
		PutOption(fields, change.key, change.incoming);
		changes.emplace_back(option, &change);
	}
	Path backup;
	if (preview.previous_file) {
		auto backups = directory / "backups";
		fs::create_directories(backups);
		backup = backups / ("config-before-import-" + NewId() + ".json");
		agi::io::Save out(backup, true);
		out.Get() << *preview.previous_file;
		out.Commit();
	}
	size_t applied = 0;
	try {
		while (applied < changes.size()) {
			auto [option, change] = changes[applied++];
			option->SetBool(change->incoming);
		}
		WriteJson(preview.destination, fields);
	}
	catch (...) {
		while (applied) {
			auto [option, change] = changes[--applied];
			option->SetBool(change->current);
		}
		throw;
	}
	return backup;
}

namespace {
void ValidateMetadata(Metadata const& values) {
	auto const& status = values.status;
	if (status != "todo" && status != "doing" && status != "review" && status != "done" && status != "paused")
		throw Error("Invalid project status: " + status);
	auto single_line = [](std::string const& value) {
		return std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
	};
	for (auto* field : {&values.source, &values.source_language, &values.target_language, &values.owner})
		if (!single_line(*field)) throw Error("Expected a single line of project metadata");
	for (auto const& tag : values.tags)
		if (!ValidDisplayName(tag)) throw Error("Invalid project tag");
	if (values.description.find('\0') != std::string::npos) throw Error("Invalid project description");
}
void CheckEpisodePath(Path const& relative) {
	if (relative.empty() || relative.has_root_path() || relative != relative.lexically_normal())
		throw Error("Expected a project folder inside this series");
	for (auto const& component : relative) CheckComponent(Path(component).string());
}

std::vector<Episode> ReadEpisodes(json::Array const& records) {
	std::set<std::string> ids, paths;
	std::vector<Episode> result;
	for (json::Object const& row : records) {
		auto const& path = static_cast<json::String const&>(row.at("path"));
		Episode ep{row.at("project_id"), row.at("label"), row.at("name"), Path(path)};
		CheckEpisodePath(ep.relative);
		if (!ValidId(ep.project_id) || !ValidDisplayName(ep.label) || !ValidDisplayName(ep.name) || path != ep.relative.generic_string()
		    || !ids.insert(ep.project_id).second || !paths.insert(path).second)
			throw Error("Invalid or duplicate series episode");
		result.push_back(std::move(ep));
	}
	// Compare decimal labels without converting them to a bounded integer.
	// Specials retain their manifest order after the numbered episodes.
	auto number = [](std::string_view label) -> std::optional<std::string_view> {
		if (label.find_first_not_of("0123456789") != label.npos) return {};
		auto pos = label.find_first_not_of('0');
		return label.substr(pos == label.npos ? label.size() - 1 : pos);
	};
	std::stable_sort(result.begin(), result.end(), [&](Episode const& a, Episode const& b) {
		auto lhs = number(a.label), rhs = number(b.label);
		if (!lhs || !rhs) return lhs.has_value() && !rhs.has_value();
		return lhs->size() == rhs->size() ? *lhs < *rhs : lhs->size() < rhs->size();
	});
	return result;
}

Metadata ReadMetadata(json::Object const& fields) {
	auto optional_text = [&](char const* key) -> std::string {
		auto it = fields.find(key);
		return it == fields.end() ? std::string{} : static_cast<json::String const&>(it->second);
	};
	Metadata values;
	values.description = optional_text("description");
	values.source = optional_text("source");
	values.source_language = optional_text("source_language");
	values.target_language = optional_text("target_language");
	values.owner = optional_text("owner");
	values.status = static_cast<json::String const&>(fields.at("status"));
	if (auto it = fields.find("tags"); it != fields.end())
		for (json::String const& tag : static_cast<json::Array const&>(it->second)) values.tags.push_back(tag);
	ValidateMetadata(values);
	return values;
}
}

Manifest Open(Path const& directory) {
	Path root(fs::canonical(directory));
	auto metadata = root / ".aeg";
	if (Linked(metadata) || !fs::is_directory(metadata)) throw Error("Missing local .aeg directory: " + root.string());
	auto project = ManifestPath(root, Kind::Project), series = ManifestPath(root, Kind::Series);
	if (Linked(project) || Linked(series)) throw Error("Linked manifest in " + metadata.string());
	bool has_project = fs::exists(project), has_series = fs::exists(series);
	if (has_project == has_series) throw Error("Expected one project.json or series.json in " + metadata.string());
	auto kind = has_project ? Kind::Project : Kind::Series;
	auto file = has_project ? project : series;
	if (!fs::is_regular_file(file)) throw Error("Invalid manifest file: " + file.string());
	auto bytes = ReadBytes(file);
	auto const json = Parse(bytes);
	auto const& fields = static_cast<json::Object const&>(json);
	if (static_cast<json::Integer const&>(fields.at("schema_version")) != 1) throw Error("Unsupported manifest version: " + file.string());
	auto id = static_cast<json::String const&>(fields.at(std::string(KindName(kind)) + "_id"));
	auto name = static_cast<json::String const&>(fields.at("name"));
	auto revision = static_cast<json::Integer const&>(fields.at("revision"));
	if (!ValidId(id) || revision < 1 || !ValidDisplayName(name)) throw Error("Invalid manifest identity, name or revision: " + file.string());
	auto metadata_values = ReadMetadata(fields);
	(void)static_cast<json::Array const&>(fields.at("history"));
	std::vector<Episode> episodes;
	if (kind == Kind::Series) episodes = ReadEpisodes(fields.at("episodes"));
	else (void)static_cast<json::Array const&>(fields.at("assets"));
	auto folders = ReadFolders(fields.at("template_snapshot"), kind);
	Path cover;
	if (auto it = fields.find("cover"); it != fields.end()) {
		auto const& value = static_cast<json::String const&>(it->second);
		if (!value.empty()) {
			cover = Path(value);
			if (cover.parent_path() != Path(".aeg/covers") || cover.generic_string() != value)
				throw Error("Invalid project cover path");
			CheckComponent(cover.filename().string());
		}
	}
	return {root, kind, id, name, revision, std::move(folders), std::move(bytes), std::move(cover), std::move(metadata_values), std::move(episodes)};
}

namespace {
Manifest Initialize(Path const& root, std::string const& name, Kind kind, Folders const& folders, bool create_root) {
	if (!ValidDisplayName(name)) throw Error("Invalid project display name");
	json::Object fields;
	fields["template_snapshot"] = CheckedFolders(kind, folders);
	fields[std::string(KindName(kind)) + "_id"] = NewId();
	fields["schema_version"] = 1;
	fields["revision"] = 1;
	fields["name"] = name;
	fields["created_at"] = Now();
	fields["updated_at"] = Now();
	fields["status"] = "todo";
	fields["description"] = "";
	fields["source"] = "";
	fields["source_language"] = "";
	fields["target_language"] = "";
	fields["tags"] = json::Array{};
	fields["owner"] = "";
	fields["cover"] = "";
	fields["history"] = json::Array{};
	fields[kind == Kind::Project ? "assets" : "episodes"] = json::Array{};
	std::vector<Path> created;
	if (create_root) {
		if (!fs::create_directory(root)) throw Error("This project directory already exists: " + root.string());
		created.push_back(root);
	}
	try {
		if (!fs::create_directory(root / ".aeg")) throw Error("This folder already contains .aeg metadata: " + root.string());
		created.push_back(root / ".aeg");
		auto make = [&](Path const& relative) {
			Path dir = root;
			for (auto const& part : relative) {
				dir /= part;
				if (fs::create_directory(dir)) created.push_back(dir);
				else if (Linked(dir) || !fs::is_directory(dir)) throw Error("Invalid folder: " + dir.string());
			}
		};
		make(".aeg/covers");
		make(".aeg/backups/config");
		if (create_root)
			for (auto const& folder : folders) make(folder.second);
		WriteJson(ManifestPath(root, kind), fields);
	}
	catch (...) {
		std::error_code ignored;
		for (auto it = created.rbegin(); it != created.rend(); ++it) fs::remove(*it, ignored);
		throw;
	}
	return Open(root);
}
}

Manifest Create(Path const& parent, std::string const& name, Kind kind, Folders const& folders) {
	CheckComponent(name);
	return Initialize(Path(fs::canonical(parent)) / name, name, kind, folders, true);
}

Manifest RegisterFolder(Path const& directory, std::string const& name, Kind kind, Folders const& folders) {
	Path root(fs::canonical(directory));
	if (!fs::is_directory(root)) throw Error("Expected a project directory");
	return Initialize(root, name, kind, folders, false);
}

namespace {
void CommitChange(Manifest const& current, json::Object& fields, json::Object history, std::function<void()> verify = {}) {
	if (current.revision == (std::numeric_limits<int64_t>::max)()) throw Error("Manifest revision limit reached");
	auto metadata = current.root / ".aeg";
	auto now = Now();
	history["at"] = now;
	history["event_id"] = NewId();
	static_cast<json::Array&>(fields.at("history")).emplace_back(std::move(history));
	fields["revision"] = current.revision + 1;
	fields["updated_at"] = now;
	auto backups = metadata / "backups/config";
	for (auto const& dir : {metadata / "backups", backups}) {
		if (Linked(dir)) throw Error("Invalid backup directory: " + dir.string());
		fs::create_directory(dir);
	}
	auto backup = backups / (std::string(KindName(current.kind)) + "-r" + std::to_string(current.revision) + ".json");
	if (Linked(backup)) throw Error("Linked backup: " + backup.string());
	if (fs::exists(backup)) {
		if (!fs::is_regular_file(backup) || ReadBytes(backup) != current.bytes) throw Error("Conflicting backup: " + backup.string());
	}
	else {
		agi::io::Save out(backup, true);
		out.Get() << current.bytes;
		out.Commit();
	}
	auto file = ManifestPath(current.root, current.kind);
	agi::io::Save out(file, true);
	agi::JsonWriter::Write(fields, out.Get());
	if (verify) verify();
	if (Open(current.root).bytes != current.bytes) throw Error("The manifest changed while saving.");
	out.Commit();
}
Path LockPath(Path const& root) {
	auto metadata = root / ".aeg";
	if (!fs::is_directory(metadata) || Linked(metadata)) throw Error("Invalid project metadata directory");
	return metadata / "write.lock";
}
Manifest OpenExpected(Manifest const& expected) {
	auto current = Open(expected.root);
	if (current.kind != expected.kind || current.bytes != expected.bytes)
		throw Error("The manifest changed on disk. Reopen it before saving.");
	return current;
}
std::string AssetString(json::Object const& asset, char const *key) {
	auto it = asset.find(key);
	return it == asset.end() ? std::string{} : static_cast<json::String const&>(it->second);
}
bool FindOutput(json::Array const& assets, std::string const& task, RegistrationOutput const& out, bool check_conflicts) {
	auto path = out.relative.generic_string();
	bool found = false;
	for (json::Object const& asset : assets) {
		if (AssetString(asset, "origin_task_id") == task && AssetString(asset, "output_id") == out.id) {
			if (found || AssetString(asset, "asset_id") != out.asset_id || AssetString(asset, "path") != path ||
			    AssetString(asset, "sha256") != out.sha256 || static_cast<json::Integer const&>(asset.at("size")) != out.bytes)
				throw Error("The registered output differs from the pending output");
			found = true;
		}
		else if (check_conflicts && (AssetString(asset, "asset_id") == out.asset_id || AssetString(asset, "path") == path))
			throw Error("A different asset is already registered for this file");
	}
	return found;
}
}

namespace {
Manifest CurrentSeries(Manifest const& expected) {
	if (expected.kind != Kind::Series || Linked(expected.root)) throw Error("Invalid series location");
	auto current = Open(expected.root);
	if (current.kind != Kind::Series || current.id != expected.id || current.root != expected.root)
		throw Error("This folder no longer contains the selected series");
	return current;
}
Path MemberDirectory(Manifest const& series, Path const& relative, bool create_parent = false) {
	CheckEpisodePath(relative);
	Path dir = series.root;
	for (auto const& part : relative) {
		dir /= part;
		if (Linked(dir)) throw Error("Linked episode directory: " + dir.string());
		if (create_parent) fs::create_directory(dir);
		if ((create_parent || dir != series.root / relative) && (Linked(dir / ".aeg") || fs::exists(dir / ".aeg")))
			throw Error("Episode path crosses another project: " + dir.string());
	}
	return dir;
}
Manifest ReadChild(Manifest const& series, Episode const& ep) {
	auto dir = MemberDirectory(series, ep.relative);
	auto child = Open(dir);
	if (child.kind != Kind::Project || child.id != ep.project_id || child.root != dir)
		throw Error("The folder contains a different project: " + dir.string());
	return child;
}
}

SeriesOverview InspectSeries(Manifest const& expected, std::stop_token stop) {
	SeriesOverview overview{CurrentSeries(expected), {}};
	for (auto const& episode : overview.series.episodes) {
		CheckStop(stop);
		try {
			auto child = ReadChild(overview.series, episode);
			overview.states.push_back({std::move(child.name), std::move(child.metadata.status), {}});
		}
		catch (agi::Exception const& e) { overview.states.push_back({episode.name, {}, e.GetMessage()}); }
		catch (std::exception const& e) { overview.states.push_back({episode.name, {}, e.what()}); }
	}
	return overview;
}

Manifest OpenEpisode(Manifest const& expected, Episode const& episode) {
	auto current = CurrentSeries(expected);
	for (auto const& member : current.episodes)
		if (member.project_id == episode.project_id && member.relative == episode.relative) return ReadChild(current, member);
	throw Error("The episode membership changed. Refresh the series.");
}

Manifest CreateEpisodeProject(Manifest const& expected, std::string const& name, Folders const& folders) {
	CheckComponent(name);
	WriteLock lock(LockPath(expected.root));
	auto series = CurrentSeries(expected);
	if (series.bytes != expected.bytes) throw Error("The series changed. Refresh it before creating an episode.");
	auto parent = MemberDirectory(series, series.folders.at("episodes"), true);
	return Create(parent, name, Kind::Project, folders);
}

Manifest detail::LinkEpisode(Manifest const& expected, Manifest const& child, std::string const& label) {
	if (child.kind != Kind::Project || !ValidDisplayName(label)) throw Error("Choose a project and enter an episode number or label");
	WriteLock lock(LockPath(expected.root));
	auto current = CurrentSeries(expected);
	Episode ep{child.id, label, child.name, Path(child.root.lexically_relative(current.root))};
	auto confirmed = ReadChild(current, ep);
	for (auto const& member : current.episodes) {
		if (member.project_id == ep.project_id && member.relative == ep.relative && member.label == label) return current;
		if (member.project_id == ep.project_id || member.relative == ep.relative) throw Error("The episode is already linked with different details");
	}
	if (current.bytes != expected.bytes) throw Error("The series changed. Refresh it before adding an episode.");
	auto json = Parse(current.bytes);
	auto& fields = static_cast<json::Object&>(json);
	json::Object row;
	row["project_id"] = ep.project_id; row["name"] = confirmed.name;
	row["label"] = label; row["path"] = ep.relative.generic_string();
	static_cast<json::Array&>(fields.at("episodes")).emplace_back(std::move(row));
	json::Object history;
	history["operation"] = "add_episode"; history["project_id"] = ep.project_id; history["label"] = label;
	CommitChange(current, fields, std::move(history), [&] { ReadChild(current, ep); });
	return Open(current.root);
}

Manifest Rename(Manifest const& expected, std::string const& name) {
	if (!ValidDisplayName(name)) throw Error("Invalid project display name");
	WriteLock lock(LockPath(expected.root));
	auto current = OpenExpected(expected);
	if (name == current.name) return current;
	auto json = Parse(current.bytes);
	auto& fields = static_cast<json::Object&>(json);
	json::Object history;
	history["operation"] = "rename";
	history["previous_name"] = current.name;
	history["name"] = name;
	fields["name"] = name;
	CommitChange(current, fields, std::move(history));
	return Open(current.root);
}

Manifest UpdateMetadata(Manifest const& expected, Metadata const& values) {
	ValidateMetadata(values);
	WriteLock lock(LockPath(expected.root));
	auto current = OpenExpected(expected);
	if (current.metadata == values) return current;
	auto json = Parse(current.bytes);
	auto& fields = static_cast<json::Object&>(json);
	auto const& previous = current.metadata;
	json::Array changed;
	auto set = [&](char const* key, std::string const& old_value, std::string const& value) {
		if (old_value == value) return;
		fields[key] = value;
		changed.emplace_back(key);
	};
	set("description", previous.description, values.description);
	set("source", previous.source, values.source);
	set("source_language", previous.source_language, values.source_language);
	set("target_language", previous.target_language, values.target_language);
	set("owner", previous.owner, values.owner);
	set("status", previous.status, values.status);
	if (values.tags != previous.tags) {
		json::Array tags;
		for (auto const& value : values.tags) tags.emplace_back(value);
		fields["tags"] = std::move(tags);
		changed.emplace_back("tags");
	}
	json::Object history;
	history["operation"] = "edit_metadata";
	history["fields"] = std::move(changed);
	if (previous.status != values.status) {
		history["previous_status"] = previous.status;
		history["status"] = values.status;
	}
	CommitChange(current, fields, std::move(history));
	return Open(current.root);
}

Path CoverFile(Manifest const& manifest) {
	if (manifest.cover.empty()) return {};
	auto file = manifest.root / manifest.cover;
	if (Linked(manifest.root / ".aeg") || Linked(file.parent_path()) || Linked(file) || !fs::is_regular_file(file)) return {};
	return file;
}

Manifest SetCover(Manifest const& expected, Path const& source,
                  std::function<bool(std::string const&)> const& can_decode) {
	auto bytes = ReadBytes(source);
	auto extension = bytes.starts_with("\x89PNG\r\n\x1a\n") ? ".png"
	               : bytes.starts_with("\xff\xd8\xff") ? ".jpg" : "";
	if (!*extension || !can_decode(bytes)) throw Error("Cannot read this PNG or JPEG cover");
	agi::Sha256 hash;
	hash.Update(bytes);
	auto relative = Path(".aeg/covers") / (hash.HexDigest() + extension);
	WriteLock lock(LockPath(expected.root));
	auto current = OpenExpected(expected);
	auto target = current.root / relative;
	auto publish_image = [&] {
		if (Linked(current.root / ".aeg") || Linked(target.parent_path()) || Linked(target)) throw Error("Invalid cover location");
		fs::create_directory(target.parent_path());
		if (fs::exists(target)) {
			if (ReadBytes(target) != bytes) throw Error("A different file occupies the cover location");
			return;
		}
		agi::io::Save output(target, true);
		output.Get().write(bytes.data(), bytes.size());
		output.Commit();
	};
	if (relative == current.cover) {
		publish_image();
		return current;
	}
	auto json = Parse(current.bytes);
	auto& fields = static_cast<json::Object&>(json);
	fields["cover"] = relative.generic_string();
	json::Object history;
	history["operation"] = "set_cover";
	history["previous_cover"] = current.cover.generic_string();
	history["cover"] = relative.generic_string();
	CommitChange(current, fields, std::move(history), publish_image);
	return Open(current.root);
}

bool detail::ContainsOutput(Manifest const& project, std::string const& task, RegistrationOutput const& output) {
	WriteLock lock(LockPath(project.root));
	auto current = Open(project.root);
	if (current.kind != Kind::Project || current.id != project.id) throw Error("The project at this location changed");
	auto const json = Parse(current.bytes);
	return FindOutput(static_cast<json::Object const&>(json).at("assets"), task, output, false);
}

bool detail::PublishAsset(Manifest const& project, std::string const& task, RegistrationOutput const& output,
                          FileStamp const& verified, bool existing_only) {
	WriteLock lock(LockPath(project.root));
	auto current = Open(project.root);
	if (current.kind != Kind::Project || current.id != project.id) throw Error("The project at this location changed");
	auto json = Parse(current.bytes);
	auto& fields = static_cast<json::Object&>(json);
	auto& assets = static_cast<json::Array&>(fields.at("assets"));
	auto path = output.relative.generic_string();
	auto verify = [&] {
		if (InspectFile(current, output.relative) != verified) throw Error("The project file changed before registration");
	};
	verify();
	if (FindOutput(assets, task, output, !existing_only)) return true;
	if (existing_only) return false;

	std::string kind = "file";
	size_t matched = 0;
	for (auto const& [role, folder] : current.folders) {
		if (folder.size() > matched && path.starts_with(folder + "/")) {
			kind = role; matched = folder.size();
		}
	}
	json::Object asset;
	asset["asset_id"] = output.asset_id;
	asset["kind"] = kind;
	asset["path"] = path;
	asset["size"] = output.bytes;
	asset["sha256"] = output.sha256;
	asset["origin_task_id"] = task;
	asset["output_id"] = output.id;
	assets.emplace_back(std::move(asset));
	json::Object history;
	history["operation"] = "register_asset";
	history["asset_id"] = output.asset_id;
	history["path"] = path;
	CommitChange(current, fields, std::move(history), verify);
	return true;
}
}

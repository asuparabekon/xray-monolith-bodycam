#include "pch_script.h"
#include "bodycam_camera.h"
#include "bodycam_settings.h"
#include "Actor.h"
#include "ai_space.h"
#include "script_engine.h"

using namespace luabind;

static bool bodycam_request_motion_mode(int mode)
{
	return Bodycam::RequestMotionMode(mode);
}

static int bodycam_get_motion_mode()
{
	return static_cast<int>(Bodycam::GetMotionSelection().Requested());
}

static int bodycam_get_active_motion_mode()
{
	return static_cast<int>(Bodycam::GetMotionSelection().Active());
}

static Fvector bodycam_get_reference_weapon_position(LPCSTR section, bool ads)
{
	const auto& value = Bodycam::GetReferenceWeaponPosition(section);
	return ads ? value.ads : value.hip;
}
static Fvector bodycam_get_reference_weapon_position_baseline(LPCSTR section, bool ads)
{
	const auto& value = Bodycam::GetReferenceWeaponPositionBaseline(section);
	return ads ? value.ads : value.hip;
}
static bool bodycam_set_reference_weapon_position(LPCSTR section, bool ads, float x, float y, float z)
{
	Fvector value;
	value.set(x, y, z);
	return Bodycam::SetReferenceWeaponPosition(section, ads, value);
}

// Logs each unknown setting name once.
static void bodycam_report_unknown_setting(LPCSTR name)
{
	static xr_set<shared_str> reported;
	if (reported.insert(name).second)
		Msg("! bodycam: unknown setting '%s', using the script's fallback", name);
}

static float bodycam_get_float(LPCSTR name, float fallback)
{
	float value = fallback;
	if (!Bodycam::GetFloat(name, value))
		bodycam_report_unknown_setting(name);
	return value;
}

static bool bodycam_get_bool(LPCSTR name, bool fallback)
{
	bool value = fallback;
	if (!Bodycam::GetBool(name, value))
		bodycam_report_unknown_setting(name);
	return value;
}

static void bodycam_set_viewmodel_profile(float pos_x, float pos_y, float pos_z, float pitch, float yaw, float roll, float blend_speed)
{
	CActor* actor = Bodycam::CurrentActor();
	if (!actor)
		return;

	Fvector pos;
	pos.set(pos_x, pos_y, pos_z);
	Fvector rot;
	rot.set(yaw, pitch, roll);
	actor->cam_BodycamSetViewmodelProfile(pos, rot, blend_speed);
}

static void bodycam_clear_viewmodel_profile(float blend_speed)
{
	if (CActor* actor = Bodycam::CurrentActor())
		actor->cam_BodycamClearViewmodelProfile(blend_speed);
}

static object bodycam_get_bindings()
{
	lua_State* L = ai().script_engine().lua();
	object bindings = newtable(L);
	int index = 1;

	u32 count = 0;
	const Bodycam::FloatBinding* floats = Bodycam::GetFloatBindings(count);
	for (u32 i = 0; i < count; ++i)
	{
		const Bodycam::FloatBinding& binding = floats[i];
		object entry = newtable(L);
		entry["name"] = binding.name;
		entry["console_name"] = binding.console_name;
		entry["type"] = "float";
		entry["default"] = binding.default_value;
		entry["min"] = binding.min_value;
		entry["max"] = binding.max_value;
		bindings[index++] = entry;
	}

	const Bodycam::BoolBinding* bools = Bodycam::GetBoolBindings(count);
	for (u32 i = 0; i < count; ++i)
	{
		const Bodycam::BoolBinding& binding = bools[i];
		object entry = newtable(L);
		entry["name"] = binding.name;
		entry["console_name"] = binding.console_name;
		entry["type"] = "bool";
		entry["default"] = binding.default_value;
		bindings[index++] = entry;
	}

	return bindings;
}

// --- files on disk, for the reticle editor's file browser --------------------------------------------------------
// Real paths, read and written directly: the game's file index (FS) only knows what existed at start, and listing a
// folder through it rescans that folder, which under MO2 drops the mods' files from the index.

// The parent folders of `path`, made as needed
static void bodycam_make_parent_dirs(const std::string& path)
{
	for (size_t i = path.find_first_of("\\/", 3); i != std::string::npos; i = path.find_first_of("\\/", i + 1))
		CreateDirectoryA(path.substr(0, i).c_str(), nullptr);
}

// The folders and files in `folder`, folders first, each sorted by name: { { name =, dir = }, ... }; nil if unreadable
static object bodycam_list_dir(LPCSTR folder)
{
	lua_State* L = ai().script_engine().lua();
	std::string pattern = folder ? folder : "";
	if (!pattern.empty() && pattern.back() != '\\' && pattern.back() != '/')
		pattern += '\\';
	WIN32_FIND_DATAA data;
	HANDLE find = FindFirstFileA((pattern + "*").c_str(), &data);
	if (find == INVALID_HANDLE_VALUE)
		return object();
	xr_vector<std::pair<bool, std::string>> entries;
	do
	{
		if (strcmp(data.cFileName, ".") && strcmp(data.cFileName, ".."))
			entries.emplace_back(!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), data.cFileName);
	} while (FindNextFileA(find, &data));
	FindClose(find);
	std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b)
		{ return a.first != b.first ? a.first < b.first : _stricmp(a.second.c_str(), b.second.c_str()) < 0; });
	object list = newtable(L);
	int index = 1;
	for (const auto& [file, name] : entries)
	{
		object entry = newtable(L);
		entry["name"] = name.c_str();
		entry["dir"] = !file;
		list[index++] = entry;
	}
	return list;
}

// The drive roots ("C:\\", "Z:\\", ...)
static object bodycam_drives()
{
	lua_State* L = ai().script_engine().lua();
	char buffer[512] = {};
	const DWORD length = GetLogicalDriveStringsA(sizeof(buffer) - 1, buffer);
	object list = newtable(L);
	int index = 1;
	for (const char* drive = buffer; drive < buffer + length && *drive; drive += strlen(drive) + 1)
		list[index++] = drive;
	return list;
}

static bool bodycam_file_exists(LPCSTR path)
{
	const DWORD attributes = path ? GetFileAttributesA(path) : INVALID_FILE_ATTRIBUTES;
	return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// The whole file as a string (binary safe), nil if unreadable
static object bodycam_read_file(LPCSTR path)
{
	FILE* f = path ? fopen(path, "rb") : nullptr;
	if (!f)
		return object();
	std::string text;
	char buffer[16384];
	for (size_t n; (n = fread(buffer, 1, sizeof(buffer), f)) > 0;)
		text.append(buffer, n);
	fclose(f);
	lua_State* L = ai().script_engine().lua();
	lua_pushlstring(L, text.data(), text.size());
	luabind::detail::lua_reference ref;
	ref.set(L); // takes the string off the stack
	return object(L, ref, true);
}

// Writes `text` to `path`, making its folders (text only: luabind passes strings to C++ as C strings; copy_file
// copies any file)
static bool bodycam_write_file(LPCSTR path, LPCSTR text)
{
	if (!path || !*path || !text)
		return false;
	bodycam_make_parent_dirs(path);
	FILE* f = fopen(path, "wb");
	if (!f)
		return false;
	const size_t size = xr_strlen(text);
	const bool ok = fwrite(text, 1, size, f) == size;
	return fclose(f) == 0 && ok;
}

// Copies `from` to `to`, making the folders of `to`
static bool bodycam_copy_file(LPCSTR from, LPCSTR to)
{
	if (!from || !to || !*to)
		return false;
	bodycam_make_parent_dirs(to);
	return CopyFileA(from, to, FALSE) != FALSE;
}

static bool bodycam_remove_file(LPCSTR path)
{
	return path && DeleteFileA(path) != FALSE;
}

#pragma optimize("s", on)
void Bodycam::script_register(lua_State* L)
{
	module(L, "bodycam")[def("request_motion_mode", &bodycam_request_motion_mode), def("get_motion_mode", &bodycam_get_motion_mode),
		def("get_active_motion_mode", &bodycam_get_active_motion_mode),
		def("get_reference_weapon_position", &bodycam_get_reference_weapon_position),
		def("get_reference_weapon_position_baseline", &bodycam_get_reference_weapon_position_baseline),
		def("has_reference_weapon_position_baseline", &Bodycam::HasReferenceWeaponPositionBaseline),
		def("restore_reference_weapon_position", &Bodycam::RestoreReferenceWeaponPosition),
		def("set_reference_weapon_position", &bodycam_set_reference_weapon_position),
		def("save_reference_weapon_position", &Bodycam::SaveReferenceWeaponPosition), def("get_float", &bodycam_get_float),
		def("set_float", &Bodycam::SetFloat), def("get_bool", &bodycam_get_bool), def("set_bool", &Bodycam::SetBool),
		def("apply_preset", &Bodycam::ApplyPreset), def("set_viewmodel_profile", &bodycam_set_viewmodel_profile),
		def("clear_viewmodel_profile", &bodycam_clear_viewmodel_profile), def("dump", &Bodycam::DumpState),
		def("get_bindings", &bodycam_get_bindings), def("list_dir", &bodycam_list_dir), def("drives", &bodycam_drives),
		def("file_exists", &bodycam_file_exists), def("read_file", &bodycam_read_file), def("write_file", &bodycam_write_file),
		def("copy_file", &bodycam_copy_file), def("remove_file", &bodycam_remove_file)];
}

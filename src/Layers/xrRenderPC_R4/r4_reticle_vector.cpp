#include "stdafx.h"
#include "r4_reticle_vector.h"
#include "../xrRender/reticle_vector/reticle_vector.h"
#include "../xrRender/ResourceManager.h"
#include "../xrRender/dxRenderDeviceRender.h"
#include "../../xrEngine/xr_ioconsole.h"
#include "../../xrEngine/xr_ioc_cmd.h"

#include <mutex>
#include <set>

int ps_r__reticle_svg = 0;

namespace
{
using Bytes = std::vector<std::uint8_t>;

// A file through the game's file system (mods, archives)
Bytes fs_bytes(LPCSTR path)
{
	IReader* F = FS.r_open(path);
	if (!F)
		return {};
	const auto* data = static_cast<const std::uint8_t*>(F->pointer());
	Bytes bytes(data, data + _max(F->length(), 0));
	FS.r_close(F);
	return bytes;
}

// A file straight from disk: the user's reticles and the editor's files are written after the game indexed its
// folders, so FS cannot see them
Bytes disk_bytes(const std::string& path)
{
	Bytes bytes;
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return bytes;
	std::uint8_t buffer[16384];
	for (size_t n; (n = fread(buffer, 1, sizeof(buffer), f)) > 0;)
		bytes.insert(bytes.end(), buffer, buffer + n);
	fclose(f);
	return bytes;
}

std::string text_of(const Bytes& bytes)
{
	return std::string(bytes.begin(), bytes.end());
}

std::string folder_of(const std::string& path)
{
	const size_t slash = path.find_last_of("\\/");
	return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string lower(std::string s)
{
	for (char& c : s)
		c = char(tolower(static_cast<unsigned char>(c)));
	return s;
}

// The user's own reticles, appdata\reticles\<texture>.svg and .reticle.ini (the Reticle Editor writes them). The
// folder is listed once and again after each edit or reload, so a texture load does not touch the disk to learn
// that a sight has none.
class UserReticles
{
public:
	static std::string file(LPCSTR name, LPCSTR extension)
	{
		string_path path;
		FS.update_path(path, "$app_data_root$", (std::string("reticles\\") + name + extension).c_str());
		return path;
	}

	// both the drawing and its settings: one of them alone is not a reticle
	bool has(LPCSTR name)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!listed)
			list();
		const std::string key = lower(name);
		return files.count(key + ".svg") && files.count(key + ".reticle.ini");
	}

	void refresh()
	{
		std::lock_guard<std::mutex> lock(mutex);
		listed = false;
	}

private:
	void list()
	{
		files.clear();
		string_path root;
		FS.update_path(root, "$app_data_root$", "reticles\\");
		walk(root, "");
		listed = true;
	}

	void walk(const std::string& root, const std::string& relative)
	{
		WIN32_FIND_DATAA data;
		HANDLE find = FindFirstFileA((root + relative + "*").c_str(), &data);
		if (find == INVALID_HANDLE_VALUE)
			return;
		do
		{
			const std::string name = data.cFileName;
			if (name == "." || name == "..")
				continue;
			if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				walk(root, relative + name + "\\");
			else
				files.insert(lower(relative + name));
		} while (FindNextFileA(find, &data));
		FindClose(find);
	}

	std::mutex mutex;
	std::set<std::string> files; // lower case, relative to appdata\reticles
	bool listed = false;
} user_reticles;

// Where a reticle is drawn from: the user's own when both of its files exist, else the shipped drawing
struct Drawing
{
	bool user = false;
	std::string svg, folder; // the SVG text and the folder its files are looked for in
};

bool find_drawing(LPCSTR name, Drawing& d)
{
	d.user = user_reticles.has(name);
	if (d.user)
	{
		const std::string svg = UserReticles::file(name, ".svg");
		d.svg = text_of(disk_bytes(svg));
		d.folder = folder_of(svg);
		return true;
	}
	string_path svg;
	if (!FS.exist(svg, "$game_textures$", name, ".svg"))
		return false;
	d.svg = text_of(fs_bytes(svg));
	d.folder = folder_of(svg);
	return true;
}

// Builds `name` from its drawing with `settings`. A file the settings name (the grain mask) is looked for beside
// the user's reticle, then beside the drawing; an absolute path is read as given.
bool build(LPCSTR name, const Drawing& d, const std::string& settings, reticle_vector::Texture& built, reticle_vector::Report& report)
{
	const std::string user_folder = folder_of(UserReticles::file(name, ".svg"));
	const auto read = [&](const std::string& relative)
	{
		const bool absolute = relative.size() > 2 && (relative[1] == ':' || relative[0] == '\\' || relative[0] == '/');
		if (absolute)
			return disk_bytes(relative);
		Bytes bytes = disk_bytes(user_folder + relative);
		if (bytes.empty() && !d.user)
			bytes = fs_bytes((d.folder + relative).c_str());
		return bytes;
	};
	return reticle_vector::Build(name, d.svg, settings, read, built, report);
}

ID3DTexture2D* create_texture(const reticle_vector::Texture& built)
{
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = built.width;
	desc.Height = built.height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA data{};
	data.pSysMem = built.rgba.data();
	data.SysMemPitch = built.width * 4;
	ID3DTexture2D* texture = nullptr;
	if (FAILED(HW.pDevice->CreateTexture2D(&desc, &data, &texture)))
		return nullptr;
	return texture;
}

// The vector reticle `name` as it loads: the user's reticle, else the shipped drawing when the rules let it win
// over a .dds. Null when the texture loads as usual; `problem` then says why if a drawing was there but did not
// build.
ID3DBaseTexture* load_vector(LPCSTR name, u32& memory, float& glow_size, std::string* problem)
{
	Drawing drawing;
	if (!find_drawing(name, drawing))
		return nullptr;
	std::string settings;
	if (drawing.user)
		settings = text_of(disk_bytes(UserReticles::file(name, ".reticle.ini")));
	else
	{
		string_path ini, dds;
		if (FS.exist(dds, "$level$", name, ".dds") || FS.exist(dds, "$game_saves$", name, ".dds"))
			return nullptr;
		if (!ps_r__reticle_svg && FS.exist(dds, "$game_textures$", name, ".dds"))
			return nullptr;
		if (!FS.exist(ini, "$game_textures$", name, ".reticle.ini"))
		{
			Msg("! [RETICLE-SVG] %s.svg has no %s.reticle.ini beside it; loading the texture as usual", name, name);
			return nullptr;
		}
		settings = text_of(fs_bytes(ini));
	}

	reticle_vector::Texture built;
	reticle_vector::Report report;
	const bool ok = build(name, drawing, settings, built, report);
	for (const auto& w : report.warnings)
		Msg("~ [RETICLE-SVG] %s: %s", name, w.c_str());
	ID3DTexture2D* texture = ok ? create_texture(built) : nullptr;
	if (!texture)
	{
		for (const auto& e : report.errors)
			Msg("! [RETICLE-SVG] %s: %s", name, e.c_str());
		if (ok)
			Msg("! [RETICLE-SVG] %s: could not create the %ux%u texture", name, built.width, built.height);
		Msg("! [RETICLE-SVG] %s: not built; loading the texture as usual", name);
		if (problem)
			*problem = report.errors.empty() ? std::string("the texture could not be created") : report.errors.front();
		return nullptr;
	}
	memory = u32(built.rgba.size());
	glow_size = built.glow_size;
	for (const auto& n : report.notes)
		Msg("* [RETICLE-SVG] %s: %s", name, n.c_str());
	return texture;
}

// --- in-game editing ------------------------------------------------------------------------------------------

struct EditRequest
{
	std::string texture, settings_path; // no settings path: load the texture again from its sources
};
std::mutex edit_mutex;
std::vector<EditRequest> edit_requests;
std::string edit_status = "idle";

void set_status(const std::string& status)
{
	std::lock_guard<std::mutex> lock(edit_mutex);
	edit_status = status;
	Msg("%s [RETICLE-EDIT] %s", status.compare(0, 2, "ok") ? "!" : "*", status.c_str());
}

void reload(const EditRequest& r, CTexture* texture, bool live)
{
	if (!live)
		return set_status("ok: " + r.texture + " is not in view; it loads from its sources when it is");
	const LPCSTR name = r.texture.c_str();
	u32 memory = 0;
	float glow_size = -1.f; // -1: read from the file when first drawn
	std::string problem;
	ID3DBaseTexture* surface = load_vector(name, memory, glow_size, &problem);
	if (!surface)
		surface = ::RImplementation.texture_load(name, memory, false); // not staging: it is drawn at once
	if (!surface)
		return set_status("error: " + r.texture + " did not load");
	texture->surface_set(surface);
	surface->Release();
	texture->reticle_glow_size = glow_size;
	if (!problem.empty())
		set_status("error: your drawing did not build (" + problem + "); showing the original");
	else
		set_status(std::string("ok: reloaded ") + (user_reticles.has(name) ? "your reticle" : "the original reticle"));
}

void apply(const EditRequest& r)
{
	user_reticles.refresh(); // the editor writes the user's files before it asks
	CTexture* texture = DEV->_FindTexture(r.texture.c_str());
	const bool live = texture && texture->flags.bLoaded && !texture->IsLoading();
	if (r.settings_path.empty())
		return reload(r, texture, live);
	const std::string settings = text_of(disk_bytes(r.settings_path));
	if (settings.empty())
		return set_status("error: settings file " + r.settings_path + " is missing or empty");
	Drawing drawing;
	if (!find_drawing(r.texture.c_str(), drawing))
		return set_status("error: no drawing: load an SVG for " + r.texture);
	reticle_vector::Texture built;
	reticle_vector::Report report;
	if (!build(r.texture.c_str(), drawing, settings, built, report))
	{
		std::string message = "error:";
		for (const auto& e : report.errors)
			message += " " + e;
		return set_status(message);
	}
	if (live)
	{
		ID3DTexture2D* surface = create_texture(built);
		if (!surface)
			return set_status("error: could not create the " + std::to_string(built.width) + "x" + std::to_string(built.height) + " texture");
		texture->surface_set(surface);
		surface->Release();
		texture->reticle_glow_size = built.glow_size;
	}
	set_status(std::string("ok: ") + (report.notes.empty() ? std::string("built") : report.notes.back()) +
		(live ? "" : "; not in view, so nothing changed on screen"));
}

void queue(const std::string& texture, const std::string& settings_path)
{
	std::lock_guard<std::mutex> lock(edit_mutex);
	edit_requests.push_back({ texture, settings_path });
	edit_status = "pending";
}

// r__reticle_edit <texture> <settings file>: the texture name has no spaces, the path may
class CCC_ReticleEdit : public IConsole_Command
{
public:
	CCC_ReticleEdit(LPCSTR name) : IConsole_Command(name) { bLowerCaseArgs = false; }
	void Execute(LPCSTR args) override
	{
		const std::string line = args ? args : "";
		const size_t space = line.find(' ');
		const size_t path = space == std::string::npos ? std::string::npos : line.find_first_not_of(' ', space);
		if (path == std::string::npos)
			return set_status("error: usage " + std::string(cName) + " <texture> <settings file>");
		queue(line.substr(0, space), line.substr(path));
	}
	void Info(TInfo& I) override { xr_strcpy(I, "<texture> <settings file>: rebuild the reticle from its drawing with these settings, live"); }
};

// r__reticle_reload <texture>: load it again from its sources, live (after the user's reticle was saved or removed)
class CCC_ReticleReload : public IConsole_Command
{
public:
	CCC_ReticleReload(LPCSTR name) : IConsole_Command(name) { bLowerCaseArgs = false; }
	void Execute(LPCSTR args) override
	{
		const std::string texture = args ? args : "";
		if (texture.empty() || texture.find(' ') != std::string::npos)
			return set_status("error: usage " + std::string(cName) + " <texture>");
		queue(texture, std::string());
	}
	void Info(TInfo& I) override { xr_strcpy(I, "<texture>: load the reticle again from its sources, live"); }
};

// the last edit's result: "pending", "ok: ..." or "error: ..." (the full text is in the log); never saved
class CCC_ReticleEditStatus : public IConsole_Command
{
public:
	CCC_ReticleEditStatus(LPCSTR name) : IConsole_Command(name) { bEmptyArgsHandled = true; }
	void Execute(LPCSTR) override {}
	void Status(TStatus& S) override
	{
		std::lock_guard<std::mutex> lock(edit_mutex);
		const size_t n = _min(edit_status.size(), sizeof(TStatus) - 1);
		memcpy(S, edit_status.data(), n);
		S[n] = 0;
	}
	void Save(IWriter*) override {}
	void Info(TInfo& I) override { xr_strcpy(I, "the last r__reticle_edit / r__reticle_reload result"); }
};
}

ID3DBaseTexture* reticle_vector_texture_load(LPCSTR name, u32& memory, float& glow_size)
{
	if (!name || !*name)
		return nullptr;
	return load_vector(name, memory, glow_size, nullptr);
}

void reticle_vector_console_init()
{
	CMD1(CCC_ReticleEdit, "r__reticle_edit");
	CMD1(CCC_ReticleReload, "r__reticle_reload");
	CMD1(CCC_ReticleEditStatus, "r__reticle_edit_status");
}

void reticle_vector_edit_apply()
{
	std::vector<EditRequest> requests;
	{
		std::lock_guard<std::mutex> lock(edit_mutex);
		requests.swap(edit_requests);
	}
	for (const auto& r : requests)
		apply(r);
}

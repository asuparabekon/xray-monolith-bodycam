use mlua::prelude::*;

// Minimal Luau host for running Roblox-style ModuleScripts outside Roblox.
// Exposes: __fs_list(dir) -> {names}, __fs_isdir(path), __fs_read(path), __load(src, name) -> function.
fn main() -> LuaResult<()> {
    let args: Vec<String> = std::env::args().collect();
    let lua = Lua::new();
    let g = lua.globals();
    g.set("__fs_list", lua.create_function(|_, p: String| {
        let mut v: Vec<String> = std::fs::read_dir(&p).map_err(LuaError::external)?
            .filter_map(|e| e.ok().map(|e| e.file_name().to_string_lossy().to_string())).collect();
        v.sort();
        Ok(v)
    })?)?;
    g.set("__fs_isdir", lua.create_function(|_, p: String| Ok(std::path::Path::new(&p).is_dir()))?)?;
    g.set("__fs_read", lua.create_function(|_, p: String| std::fs::read_to_string(&p).map_err(LuaError::external))?)?;
    g.set("__load", lua.create_function(|lua, (src, name): (String, String)| {
        lua.load(&src).set_name(format!("@{}", name)).into_function()
    })?)?;
    g.set("__args", args[1..].to_vec())?;
    let src = std::fs::read_to_string(&args[1]).map_err(LuaError::external)?;
    lua.load(&src).set_name(format!("@{}", &args[1])).exec()
}

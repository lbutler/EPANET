---
name: Lua Link Setting API
overview: Add a `get_link("ID")` Lua function returning a userdata with a `__newindex` metamethod, so that `link.setting = value` calls `EN_setlinkvalue` with `EN_SETTING`.
todos:
  - id: link-api
    content: Add LuaLink struct, lua_link_newindex metamethod, lua_get_link function, and registration in luascript_open
    status: completed
  - id: build-test-link
    content: Build with BUILD_TESTS=ON and run ctest to verify compilation and all tests pass
    status: completed
isProject: false
---

# Lua Link Setting API

## Design

Follows the same pattern as the existing node API. A `get_link("ID")` function returns a `LuaLink` userdata. A `__newindex` metamethod on that userdata intercepts property assignment, so `link.setting = 50` calls `EN_setlinkvalue(pr, index, EN_SETTING, 50)`.

## Changes (single file: [src/luascript.c](src/luascript.c))

### 1. LuaLink struct (next to existing LuaNode)

```c
typedef struct {
    int index;
} LuaLink;
```

### 2. `__newindex` metamethod for setting writable properties

```c
static int lua_link_newindex(lua_State *L)
{
    Project *pr = ...upvalue...;
    LuaLink *link = checkudata "epanet.link";
    const char *key = checkstring(2);
    double value = checknumber(3);

    if (strcmp(key, "setting") == 0) property = 12; // EN_SETTING
    EN_setlinkvalue(pr, link->index, property, value);
    return 0;
}
```

### 3. `get_link` function

Resolves ID via `findlink(&pr->network, id)`, creates `LuaLink` userdata with `"epanet.link"` metatable.

### 4. Registration in `luascript_open`

After the existing node metatable setup, add:

```c
luaL_newmetatable(L, "epanet.link");
lua_pushlightuserdata(L, pr);
lua_pushcclosure(L, lua_link_newindex, 1);
lua_setfield(L, -2, "__newindex");
lua_pop(L, 1);

lua_pushlightuserdata(L, pr);
lua_pushcclosure(L, lua_get_link, 1);
lua_setglobal(L, "get_link");
```

## Usage

```lua
local valve = get_link("Valve1")
valve.setting = 50
```

---
name: Lua Node Properties API
overview: Add a `get_node("ID")` Lua function that returns a userdata object with a `__index` metamethod, allowing property access like `node.pressure` that internally calls `EN_getnodevalue`.
todos:
  - id: node-userdata
    content: Add LuaNode struct, lua_node_index metamethod, lua_get_node function, and registration in luascript_open to src/luascript.c
    status: completed
  - id: build-test
    content: Build with BUILD_TESTS=ON and run ctest to verify compilation and all tests pass
    status: completed
isProject: false
---

# Lua Node Properties API

## Design

Register a `get_node("ID")` function in the Lua environment. It returns a userdata object representing a node. Property access like `node.pressure` is handled by a `__index` metamethod that calls `EN_getnodevalue` with the appropriate `EN_NodeProperty` code.

The userdata stores just the node index (an `int`), resolved once via `findnode()` at lookup time. The `Project*` is passed as an upvalue to both `get_node` and the `__index` metamethod.

## Lua Usage Example

```lua
local tank = get_node("Tank1")
print(tank.pressure)
```

## Changes (single file)

All changes are in [src/luascript.c](src/luascript.c).

### 1. Node userdata type

Define a struct to hold the node index inside Lua userdata:

```c
typedef struct {
    int index;
} LuaNode;
```

### 2. `__index` metamethod

A C function that receives the `LuaNode` userdata and a string key, maps the key to an `EN_NodeProperty`, calls `EN_getnodevalue`, and pushes the result:

```c
static int lua_node_index(lua_State *L)
{
    Project *pr = (Project *)lua_touserdata(L, lua_upvalueindex(1));
    LuaNode *node = (LuaNode *)luaL_checkudata(L, 1, "epanet.node");
    const char *key = luaL_checkstring(L, 2);
    int property = -1;
    double value;

    if (strcmp(key, "pressure") == 0) property = 11; // EN_PRESSURE
    // (future properties added here)

    if (property < 0) return luaL_error(L, "unknown node property: %s", key);
    EN_getnodevalue(pr, node->index, property, &value);
    lua_pushnumber(L, value);
    return 1;
}
```

### 3. `get_node` function

A C function that takes a node ID string, resolves it to an index via `findnode()`, creates a `LuaNode` userdata, and attaches the metatable:

```c
static int lua_get_node(lua_State *L)
{
    Project *pr = (Project *)lua_touserdata(L, lua_upvalueindex(1));
    const char *id = luaL_checkstring(L, 1);
    int index = findnode(&pr->network, id);
    if (index == 0) return luaL_error(L, "node not found: %s", id);

    LuaNode *node = (LuaNode *)lua_newuserdata(L, sizeof(LuaNode));
    node->index = index;
    luaL_getmetatable(L, "epanet.node");
    lua_setmetatable(L, -2);
    return 1;
}
```

### 4. Registration in `luascript_open`

After the existing `print` override, create the `"epanet.node"` metatable and register `get_node`:

```c
luaL_newmetatable(L, "epanet.node");
lua_pushlightuserdata(L, pr);
lua_pushcclosure(L, lua_node_index, 1);
lua_setfield(L, -2, "__index");
lua_pop(L, 1);

lua_pushlightuserdata(L, pr);
lua_pushcclosure(L, lua_get_node, 1);
lua_setglobal(L, "get_node");
```

### 5. Include header

Add `#include "epanet2_2.h"` to `luascript.c` so `EN_getnodevalue` is available.

## Why this design

- **Userdata + metatable**: idiomatic Lua pattern for C objects. The `__index` metamethod makes `node.pressure` work naturally.
- **Node index cached**: `findnode` is called once when `get_node` is called, not on every property access.
- **Extensible**: adding more properties (head, demand, elevation, quality, etc.) is just adding `strcmp` lines to `lua_node_index`. No structural changes needed.
- **Single file change**: everything stays in `luascript.c` since we already have the `Project*` upvalue pattern established.

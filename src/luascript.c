#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "luascript.h"
#include "funcs.h"
#include "epanet2_2.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

typedef struct
{
    int index;
} LuaNode;

typedef struct
{
    int index;
} LuaLink;

static int lua_epanet_print(lua_State *L)
{
    Project *pr = (Project *)lua_touserdata(L, lua_upvalueindex(1));
    int nargs = lua_gettop(L);
    char buf[1024];
    int pos = 0;

    for (int i = 1; i <= nargs; i++)
    {
        const char *s = luaL_tolstring(L, i, NULL);
        if (s == NULL)
            s = "";
        if (i > 1 && pos < (int)sizeof(buf) - 1)
            buf[pos++] = '\t';
        int len = (int)strlen(s);
        if (pos + len >= (int)sizeof(buf) - 1)
            len = (int)sizeof(buf) - 1 - pos;
        memcpy(buf + pos, s, len);
        pos += len;
        lua_pop(L, 1);
    }
    buf[pos] = '\0';
    writeline(pr, buf);
    return 0;
}

int scriptdata(Project *pr, char *line)
{
    size_t len = strlen(line);
    if (pr->Script == NULL)
    {
        pr->Script = (char *)malloc(len + 1);
        if (pr->Script == NULL)
            return 101;
        memcpy(pr->Script, line, len + 1);
    }
    else
    {
        size_t oldlen = strlen(pr->Script);
        char *newbuf = (char *)realloc(pr->Script, oldlen + len + 1);
        if (newbuf == NULL)
            return 101;
        memcpy(newbuf + oldlen, line, len + 1);
        pr->Script = newbuf;
    }
    return 0;
}

static int lua_node_index(lua_State *L)
{
    Project *pr = (Project *)lua_touserdata(L, lua_upvalueindex(1));
    LuaNode *node = (LuaNode *)luaL_checkudata(L, 1, "epanet.node");
    const char *key = luaL_checkstring(L, 2);
    int property = -1;
    double value;

    if (strcmp(key, "pressure") == 0)
        property = 11;
    else if (strcmp(key, "demand") == 0)
        property = 9;
    else if (strcmp(key, "head") == 0)
        property = 10;

    if (property < 0)
        return luaL_error(L, "unknown node property: %s", key);
    EN_getnodevalue(pr, node->index, property, &value);
    lua_pushnumber(L, value);
    return 1;
}

static int lua_get_node(lua_State *L)
{
    Project *pr = (Project *)lua_touserdata(L, lua_upvalueindex(1));
    const char *id = luaL_checkstring(L, 1);
    int index = findnode(&pr->network, id);
    if (index == 0)
        return luaL_error(L, "node not found: %s", id);

    LuaNode *node = (LuaNode *)lua_newuserdata(L, sizeof(LuaNode));
    node->index = index;
    luaL_getmetatable(L, "epanet.node");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_link_index(lua_State *L)
{
    Project *pr = (Project *)lua_touserdata(L, lua_upvalueindex(1));
    LuaLink *link = (LuaLink *)luaL_checkudata(L, 1, "epanet.link");
    const char *key = luaL_checkstring(L, 2);
    int property = -1;
    double value;

    if (strcmp(key, "flow") == 0)
        property = 8;
    else if (strcmp(key, "velocity") == 0)
        property = 9;
    else if (strcmp(key, "status") == 0)
        property = 11;
    else if (strcmp(key, "setting") == 0)
        property = 12;

    if (property < 0)
        return luaL_error(L, "unknown link property: %s", key);
    EN_getlinkvalue(pr, link->index, property, &value);
    lua_pushnumber(L, value);
    return 1;
}

static int lua_link_newindex(lua_State *L)
{
    Project *pr = (Project *)lua_touserdata(L, lua_upvalueindex(1));
    LuaLink *link = (LuaLink *)luaL_checkudata(L, 1, "epanet.link");
    const char *key = luaL_checkstring(L, 2);
    double value = luaL_checknumber(L, 3);
    int property = -1;

    if (strcmp(key, "setting") == 0)
        property = 12;
    else if (strcmp(key, "status") == 0)
        property = 11;

    if (property < 0)
        return luaL_error(L, "unknown link property: %s", key);
    EN_setlinkvalue(pr, link->index, property, value);
    return 0;
}

static int lua_get_link(lua_State *L)
{
    Project *pr = (Project *)lua_touserdata(L, lua_upvalueindex(1));
    const char *id = luaL_checkstring(L, 1);
    int index = findlink(&pr->network, id);
    if (index == 0)
        return luaL_error(L, "link not found: %s", id);

    LuaLink *link = (LuaLink *)lua_newuserdata(L, sizeof(LuaLink));
    link->index = index;
    luaL_getmetatable(L, "epanet.link");
    lua_setmetatable(L, -2);
    return 1;
}

void luascript_open(Project *pr)
{
    lua_State *L;
    if (pr->Script == NULL)
        return;

    L = luaL_newstate();
    if (L == NULL)
        return;
    luaL_openlibs(L);

    lua_pushlightuserdata(L, pr);
    lua_pushcclosure(L, lua_epanet_print, 1);
    lua_setglobal(L, "print");

    luaL_newmetatable(L, "epanet.node");
    lua_pushlightuserdata(L, pr);
    lua_pushcclosure(L, lua_node_index, 1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_pushlightuserdata(L, pr);
    lua_pushcclosure(L, lua_get_node, 1);
    lua_setglobal(L, "node");

    luaL_newmetatable(L, "epanet.link");
    lua_pushlightuserdata(L, pr);
    lua_pushcclosure(L, lua_link_index, 1);
    lua_setfield(L, -2, "__index");
    lua_pushlightuserdata(L, pr);
    lua_pushcclosure(L, lua_link_newindex, 1);
    lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);

    lua_pushlightuserdata(L, pr);
    lua_pushcclosure(L, lua_get_link, 1);
    lua_setglobal(L, "link");

    pr->lua = L;
}

void luascript_run(Project *pr)
{
    lua_State *L;
    if (pr->lua == NULL || pr->Script == NULL)
        return;
    L = (lua_State *)pr->lua;
    luaL_dostring(L, pr->Script);
}

void luascript_close(Project *pr)
{
    if (pr->lua != NULL)
    {
        lua_close((lua_State *)pr->lua);
        pr->lua = NULL;
    }
    if (pr->Script != NULL)
    {
        free(pr->Script);
        pr->Script = NULL;
    }
}

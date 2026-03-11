#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "luascript.h"
#include "funcs.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

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

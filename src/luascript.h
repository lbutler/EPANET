#ifndef LUASCRIPT_H
#define LUASCRIPT_H

#include "types.h"

int scriptdata(Project *pr, char *line);
void luascript_open(Project *pr);
void luascript_run(Project *pr);
void luascript_close(Project *pr);

#endif

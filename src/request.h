#ifndef REQUEST_H
#define REQUEST_H

#include "arena.h"
#include "base.h"

String download_resource(Arena *persistent_arena, Arena *scratch_arena, String urlstr);

#endif

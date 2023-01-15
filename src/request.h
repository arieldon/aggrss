#ifndef REQUEST_H
#define REQUEST_H

#include "arena.h"
#include "base.h"

typedef struct {
	String scheme;
	String domain;
	String path;
} URL;

URL parse_http_url(String urlstr);
String download_resource(Arena *persistent_arena, Arena *scratch_arena, String urlstr);

#endif

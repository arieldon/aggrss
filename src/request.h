#ifndef REQUEST_H
#define REQUEST_H

#include "arena.h"
#include "base.h"

typedef struct URL URL;
struct URL
{
	String scheme;
	String domain;
	String path;
};

URL parse_http_url(String urlstr);
String download_resource(Arena *persistent_arena, Arena *scratch_arena, String urlstr);

#endif

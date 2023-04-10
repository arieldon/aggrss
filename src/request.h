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

typedef struct Resource Resource;
struct Resource
{
	String result;
	String error;
};

URL parse_http_url(String urlstr);
Resource download_resource(Arena *persistent_arena, Arena *scratch_arena, String urlstr);

#endif

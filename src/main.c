#include <stdio.h>

#include "arena.h"
#include "err.h"
#include "rss.h"

global char *type_to_str[] = {

	[RSS_TOKEN_EQUAL]           = "RSS_TOKEN_EQUAL",
	[RSS_TOKEN_STAG_OPEN]       = "RSS_TOKEN_STAG_OPEN",
	[RSS_TOKEN_ETAG_OPEN]       = "RSS_TOKEN_ETAG_OPEN",
	[RSS_TOKEN_EMPTY_TAG_CLOSE] = "RSS_TOKEN_EMPTY_TAG_CLOSE",
	[RSS_TOKEN_TAG_CLOSE]       = "RSS_TOKEN_TAG_CLOSE",
	[RSS_TOKEN_COMMENT]         = "RSS_TOKEN_COMMENT",
	[RSS_TOKEN_PI_OPEN]         = "RSS_TOKEN_PI_OPEN",
	[RSS_TOKEN_PI_CLOSE]        = "RSS_TOKEN_PI_CLOSE",
	[RSS_TOKEN_NAME]            = "RSS_TOKEN_NAME",
	[RSS_TOKEN_ATTRIBUTE_VALUE] = "RSS_TOKEN_ATTRIBUTE_VALUE",
	[RSS_TOKEN_CONTENT]         = "RSS_TOKEN_CONTENT",
	[RSS_TOKEN_ERROR]           = "RSS_TOKEN_ERROR",
	[RSS_TOKEN_END]             = "RSS_TOKEN_END",
};

global Arena g_arena;
global Arena g_rss_arena;

String
load_file(FILE *file)
{
	String contents;

	fseek(file, 0, SEEK_END);
	contents.len = ftell(file);
	rewind(file);
	contents.str = arena_alloc(&g_arena, contents.len + 1);
	fread(contents.str, contents.len, sizeof(char), file);
	contents.str[contents.len] = 0;
	fclose(file);

	return contents;
}

int
main(void)
{
	arena_init(&g_arena);
	arena_init(&g_rss_arena);

	FILE *file = fopen("./sample-rss-2.xml", "rb");
	if (!file) err_exit("failed to open RSS file");

	String source = load_file(file);
	RSS_Token_List tokens = tokenize_rss(&g_rss_arena, source);

	RSS_Token_Node *token = tokens.first;
	while (token) {
		printf("[%s] %.*s\n", type_to_str[token->type], (int)token->text.len, token->text.str);
		token = token->next;
	}

	RSS_Tree tree = parse_rss(&g_rss_arena, tokens);
	(void)tree;

	arena_release(&g_rss_arena);
	arena_release(&g_arena);
	return 0;
}

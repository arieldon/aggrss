#include <ctype.h>
#include <stdio.h>

#include "arena.h"
#include "base.h"
#include "rss.h"
#include "str.h"

/* ---
 * Tokenizer
 * ---
 */

typedef enum {
	CURSOR_IN_TAG,
	CURSOR_IN_CDATA,
	CURSOR_IN_CONTENT,
} Cursor_Location;

typedef struct {
	Arena *arena;
	char *source;
	i32 end;
	i32 cursor;
	i32 token_start;
	Cursor_Location cursor_location;
	RSS_Token_List tokens;
} Tokenizer;

internal RSS_Token_Node *
make_token(Tokenizer *tokenizer, RSS_Token_Type type)
{
	RSS_Token_Node *token = arena_alloc(tokenizer->arena, sizeof(RSS_Token_Node));
	MEM_ZERO_STRUCT(token);
	token->type = type;
	token->text.str = tokenizer->source + tokenizer->token_start;
	token->text.len = tokenizer->cursor - tokenizer->token_start;
	return token;
}

internal RSS_Token_Node *
make_error_token(Tokenizer *tokenizer, char *message)
{
	RSS_Token_Node *token = arena_alloc(tokenizer->arena, sizeof(RSS_Token_Node));
	MEM_ZERO_STRUCT(token);
	token->type = RSS_TOKEN_ERROR;
	token->text.str = message;
	token->text.len = strlen(message);
	return token;
}

internal inline bool
more_source_exists(Tokenizer *tokenizer)
{
	return tokenizer->source && tokenizer->cursor < tokenizer->end;
}

internal inline char
peek_char(Tokenizer *tokenizer)
{
	assert(tokenizer->source && tokenizer->cursor <= tokenizer->end);
	return tokenizer->source[tokenizer->cursor];
}

internal inline char
peek_next_char(Tokenizer *tokenizer)
{
	assert(more_source_exists(tokenizer));
	char c = 0;
	if (more_source_exists(tokenizer)) {
		c = tokenizer->source[tokenizer->cursor + 1];
	}
	return c;
}

internal inline char
eat_char(Tokenizer *tokenizer)
{
	assert(more_source_exists(tokenizer));
	return tokenizer->source[tokenizer->cursor++];
}

internal inline bool
match_char(Tokenizer *tokenizer, char expected)
{
	if (!more_source_exists(tokenizer)) {
		return false;
	} else if (peek_char(tokenizer) != expected) {
		return false;
	} else {
		eat_char(tokenizer);
		return true;
	}
}

internal inline bool
eat_string(Tokenizer *tokenizer, String s)
{
	i32 prev_cursor = tokenizer->cursor;
	for (i32 i = 0; i < s.len; ++i) {
		if (!more_source_exists(tokenizer) || !match_char(tokenizer, s.str[i])) {
			tokenizer->cursor = prev_cursor;
			return false;
		}
	}
	return true;
}

internal inline bool
peek_string(Tokenizer *tokenizer, String s)
{
	bool match = false;
	if (tokenizer->cursor + s.len <= tokenizer->end) {
		String t = {
			.str = tokenizer->source + tokenizer->cursor,
			.len = s.len,
		};
		if (string_match(s, t)) match = true;
	}
	return match;
}

internal void
ignore_whitespace(Tokenizer *tokenizer)
{
	for (;;) {
		switch (peek_char(tokenizer)) {
		case ' ':
		case '\n':
		case '\t':
			eat_char(tokenizer);
			break;
		default:
			return;
		}
	}
}

internal void
ignore_comment(Tokenizer *tokenizer)
{
	while (more_source_exists(tokenizer)) {
		if (match_char(tokenizer, '-') && match_char(tokenizer, '-') && match_char(tokenizer, '>')) {
			return;
		}
		eat_char(tokenizer);
	}
}

internal bool
isnamechar(char c)
{
	return isalnum(c) || c == ':' || c == '_' || c == '-' || c == '.';
}

internal RSS_Token_Node *
tokenize_name(Tokenizer *tokenizer)
{
	while (more_source_exists(tokenizer) && isnamechar(peek_char(tokenizer))) {
		eat_char(tokenizer);
	}
	return make_token(tokenizer, RSS_TOKEN_NAME);
}

internal RSS_Token_Node *
tokenize_attribute_value(Tokenizer *tokenizer, char quote)
{
	assert(quote == '"' || quote == '\'');
	while (more_source_exists(tokenizer) && peek_char(tokenizer) != quote) {
		eat_char(tokenizer);
	}
	if (!more_source_exists(tokenizer)) {
		return make_error_token(tokenizer, "unterminated string");
	}
	eat_char(tokenizer);
	return make_token(tokenizer, RSS_TOKEN_ATTRIBUTE_VALUE);
}

internal RSS_Token_Node *
tokenize_cdata_open(Tokenizer *tokenizer)
{
	local_persist String cdata = {
		.str = "CDATA",
		.len = 5,
	};

	if (eat_string(tokenizer, cdata)) {
		if (match_char(tokenizer, '[')) {
			tokenizer->cursor_location = CURSOR_IN_CDATA;
			return make_token(tokenizer, RSS_TOKEN_CDATA_OPEN);
		} else {
			return make_error_token(tokenizer, "unexpected character trailing '<![CDATA'");
		}
	}

	return make_error_token(tokenizer, "unexpected sequence of characters trailing '<!['");
}

internal RSS_Token_Node *
scan_token(Tokenizer *tokenizer)
{
start:
	ignore_whitespace(tokenizer);
	tokenizer->token_start = tokenizer->cursor;
	if (!more_source_exists(tokenizer)) {
		return make_token(tokenizer, RSS_TOKEN_END);
	}

	if (tokenizer->cursor_location == CURSOR_IN_TAG) {
		char c = eat_char(tokenizer);
		if (isalnum(c)) {
			return tokenize_name(tokenizer);
		}

		switch (c) {
		case '<': {
			// NOTE(ariel) Tokenize opening bracket of a tag.
			if (match_char(tokenizer, '/')) {
				return make_token(tokenizer, RSS_TOKEN_ETAG_OPEN);
			} else if (match_char(tokenizer, '!')) {
				if (peek_char(tokenizer) == '-' &&  peek_next_char(tokenizer) == '-') {
					tokenizer->cursor += 2;
					ignore_comment(tokenizer);
					if (!more_source_exists(tokenizer)) {
						return make_error_token(tokenizer, "unterminated comment");
					}
					goto start;
				} else if (match_char(tokenizer, '[')) {
					return tokenize_cdata_open(tokenizer);
				} else {
					return make_token(tokenizer, RSS_TOKEN_DECL_OPEN);
				}
			} else if (match_char(tokenizer, '?')) {
				return make_token(tokenizer, RSS_TOKEN_PI_OPEN);
			} else {
				return make_token(tokenizer, RSS_TOKEN_STAG_OPEN);
			}
		}
		case '>':
			tokenizer->cursor_location = CURSOR_IN_CONTENT;
			return make_token(tokenizer, RSS_TOKEN_TAG_CLOSE);
		case '=':
			return make_token(tokenizer, RSS_TOKEN_EQUAL);
		case '\'':
		case '"':
			return tokenize_attribute_value(tokenizer, c);
		case '?':
			return match_char(tokenizer, '>')
				? make_token(tokenizer, RSS_TOKEN_PI_CLOSE)
				: make_error_token(tokenizer, "unexpected '?'");
		case '/':
			return match_char(tokenizer, '>')
				? make_token(tokenizer, RSS_TOKEN_EMPTY_TAG_CLOSE)
				: make_error_token(tokenizer, "unexpected '/'");
		case ']':
			return match_char(tokenizer, ']')
				? (match_char(tokenizer, '>')
					? make_token(tokenizer, RSS_TOKEN_CDATA_CLOSE)
					: make_error_token(tokenizer, "unexpected character following ]"))
				: make_error_token(tokenizer, "unexpected ']'");
		default:
			return make_error_token(tokenizer, "unexpected character");
		}
	} else if (tokenizer->cursor_location == CURSOR_IN_CDATA) {
		// NOTE(ariel) Tokenize character data -- temporarily drop the meaning of
		// strings in RSS/XML, barring the CDATA close tag "]]>".
		local_persist String cdata_close_tag = {
			.str = "]]>",
			.len = 3,
		};
		while (!peek_string(tokenizer, cdata_close_tag)) ++tokenizer->cursor;
		if (!more_source_exists(tokenizer)) return make_error_token(tokenizer, "unterminated cdata");
		tokenizer->cursor_location = CURSOR_IN_TAG;
		return make_token(tokenizer, RSS_TOKEN_CDATA);
	} else {
		// NOTE(ariel) Tokenize content.
		while (more_source_exists(tokenizer) && peek_char(tokenizer) != '<') {
			++tokenizer->cursor;
		}
		tokenizer->cursor_location = CURSOR_IN_TAG;
		if (tokenizer->cursor - tokenizer->token_start > 0) {
			if (!more_source_exists(tokenizer)) {
				return make_error_token(tokenizer, "unterminated tag");
			} else {
				return make_token(tokenizer, RSS_TOKEN_CONTENT);
			}
		} else {
			goto start;
		}
	}
}

RSS_Token_List
tokenize_rss(Arena *arena, String source)
{
	Tokenizer tokenizer = {
		.arena = arena,
		.source = source.str,
		.end = source.len,
	};
	for (;;) {
		RSS_Token_Node *token = scan_token(&tokenizer);
		SLL_PUSH_BACK(tokenizer.tokens.first, tokenizer.tokens.last, token);
		if (token->type == RSS_TOKEN_END) return tokenizer.tokens;
	}
}


/* ---
 * Parser
 * ---
 */

typedef struct {
	Arena *arena;
	RSS_Token_List tokens;
	RSS_Token_Node *current_token;
	RSS_Token_Node *previous_token;
	RSS_Tree *tree;
} Parser;

internal RSS_Tree_Node *
make_tree_node(Parser *parser)
{
	RSS_Tree_Node *node = arena_alloc(parser->arena, sizeof(RSS_Tree_Node));
	MEM_ZERO_STRUCT(node);
	return node;
}

internal RSS_Node_Attribute *
make_attribute(Parser *parser, RSS_Token_Node *name, RSS_Token_Node *value)
{
	RSS_Node_Attribute *attr = arena_alloc(parser->arena, sizeof(RSS_Node_Attribute));
	MEM_ZERO_STRUCT(attr);
	attr->name = name->text;
	attr->value = value->text;
	return attr;
}

internal bool
more_tokens_exist(Parser *parser)
{
	return parser->current_token->next;
}

internal void
generate_error_message(Parser *parser, char *message)
{
	// TODO(ariel) Handle (report) error messages without crashing.
	(void)parser;
	(void)message;
	fprintf(stderr, "[ERROR] %s\n", message);
}

internal inline RSS_Token_Node *
peek_token(Parser *parser)
{
	assert(parser->current_token);
	return parser->current_token;
}

internal inline RSS_Token_Node *
eat_token(Parser *parser)
{
	assert(more_tokens_exist(parser));
	parser->previous_token = parser->current_token;
	parser->current_token = parser->current_token->next;
	return parser->current_token;
}

internal RSS_Token_Node *
match_token(Parser *parser, RSS_Token_Type type)
{
	if (!more_tokens_exist(parser)) {
		return 0;
	} else if (parser->current_token->type != type) {
		return 0;
	} else {
		eat_token(parser);
		return parser->previous_token;
	}
}

internal void
expect_token(Parser *parser, RSS_Token_Type type, char *message)
{
	if (!match_token(parser, type)) {
		generate_error_message(parser, message);
	}
}

internal inline bool
is_start_tag(RSS_Token_Type type)
{
	return type > RSS_TOKEN_START_LOWER_BOUND && type < RSS_TOKEN_START_UPPER_BOUND;
}

internal inline void
parse_processing_instructions(Parser *parser)
{
	// NOTE(ariel) I don't care for processing instructions in this parser, so I
	// ignore them -- they're not inserted into the parse tree. They're still
	// expected at the beginning of an RSS file though.
	expect_token(parser, RSS_TOKEN_PI_OPEN, "expected <?");
	expect_token(parser, RSS_TOKEN_NAME, "expected name");
	while (match_token(parser, RSS_TOKEN_NAME)) {
		expect_token(parser, RSS_TOKEN_EQUAL, "expected =");
		expect_token(parser, RSS_TOKEN_ATTRIBUTE_VALUE, "expected attribute value");
	}
	expect_token(parser, RSS_TOKEN_PI_CLOSE, "expected ?>");
}

typedef enum {
	TAG_TYPE_UNSPECIFIED,
	TAG_TYPE_ELEMENT,
	TAG_TYPE_CDATA,
} Tag_Type;

internal RSS_Tree_Node *
parse_tag(Parser *parser)
{
	RSS_Tree_Node *node = make_tree_node(parser);

	// NOTE(ariel) Parse start tag.
	Tag_Type tag_type = TAG_TYPE_UNSPECIFIED;
	if (match_token(parser, RSS_TOKEN_STAG_OPEN)) {
		tag_type = TAG_TYPE_ELEMENT;
	} else if (match_token(parser, RSS_TOKEN_CDATA_OPEN)) {
		tag_type = TAG_TYPE_CDATA;
	}

	if (tag_type == TAG_TYPE_ELEMENT) {
		// NOTE(ariel) Parse tag name.
		expect_token(parser, RSS_TOKEN_NAME, "expected name");
		node->token = parser->previous_token;

		// NOTE(ariel) Parse attributes if they exist.
		while (match_token(parser, RSS_TOKEN_NAME)) {
			RSS_Token_Node *attr_name = parser->previous_token;

			expect_token(parser, RSS_TOKEN_EQUAL, "expected '='");
			expect_token(parser, RSS_TOKEN_ATTRIBUTE_VALUE, "expected attribute value");
			RSS_Token_Node *attr_value = parser->previous_token;

			RSS_Node_Attribute *attr = make_attribute(parser, attr_name, attr_value);
			STACK_PUSH(node->attrs.first, attr);
		}

		// NOTE(ariel) Parse close tag that corresponds to open tag.
		bool empty_tag = false;
		switch (peek_token(parser)->type) {
		case RSS_TOKEN_TAG_CLOSE:
			eat_token(parser);
			break;
		case RSS_TOKEN_EMPTY_TAG_CLOSE:
			empty_tag = true;
			eat_token(parser);
			break;
		default:
			generate_error_message(parser, "expected '>' or '/>'");
		}

		// NOTE(ariel) Parse content if it exists.
		if (peek_token(parser)->type == RSS_TOKEN_CONTENT) {
			eat_token(parser);
			node->content = parser->previous_token->text;
		} else if (peek_token(parser)->type == RSS_TOKEN_CDATA_OPEN) {
			eat_token(parser);
			eat_token(parser);
			node->content = parser->previous_token->text;
			eat_token(parser);
		}

		// NOTE(ariel) Match start tag of child tag if it exists.
		while (is_start_tag(peek_token(parser)->type)) {
			RSS_Tree_Node *child = parse_tag(parser);
			if (!node->first_child) {
				node->first_child = child;
			}
			node->last_child = child;
		}

		if (!empty_tag) {
			expect_token(parser, RSS_TOKEN_ETAG_OPEN, "expected '</'");
			expect_token(parser, RSS_TOKEN_NAME, "expected name");
			expect_token(parser, RSS_TOKEN_TAG_CLOSE, "expected '>'");
		}
	} else {
		generate_error_message(parser, "encountered unexpected tag type");
	}

	while (is_start_tag(peek_token(parser)->type)) {
		RSS_Tree_Node *sibling = parse_tag(parser);
		sibling->prev_sibling = node;
		node->next_sibling = sibling;
	}

	return node;
}

#ifdef PRINT_RSS_TREE
global char *type_to_str[] = {
	[RSS_TOKEN_STAG_OPEN]       = "RSS_TOKEN_STAG_OPEN",
	[RSS_TOKEN_ETAG_OPEN]       = "RSS_TOKEN_ETAG_OPEN",
	[RSS_TOKEN_DECL_OPEN]       = "RSS_TOKEN_DECL_OPEN",
	[RSS_TOKEN_PI_OPEN]         = "RSS_TOKEN_PI_OPEN",
	[RSS_TOKEN_CDATA_OPEN]      = "RSS_TOKEN_CDATA_OPEN",
	[RSS_TOKEN_EMPTY_TAG_CLOSE] = "RSS_TOKEN_EMPTY_TAG_CLOSE",
	[RSS_TOKEN_TAG_CLOSE]       = "RSS_TOKEN_TAG_CLOSE",
	[RSS_TOKEN_PI_CLOSE]        = "RSS_TOKEN_PI_CLOSE",
	[RSS_TOKEN_CDATA_CLOSE]     = "RSS_TOKEN_CDATA_CLOSE",
	[RSS_TOKEN_COMMENT]         = "RSS_TOKEN_COMMENT",
	[RSS_TOKEN_NAME]            = "RSS_TOKEN_NAME",
	[RSS_TOKEN_ATTRIBUTE_VALUE] = "RSS_TOKEN_ATTRIBUTE_VALUE",
	[RSS_TOKEN_CONTENT]         = "RSS_TOKEN_CONTENT",
	[RSS_TOKEN_CDATA]           = "RSS_TOKEN_CDATA",
	[RSS_TOKEN_EQUAL]           = "RSS_TOKEN_EQUAL",
	[RSS_TOKEN_ERROR]           = "RSS_TOKEN_ERROR",
	[RSS_TOKEN_END]             = "RSS_TOKEN_END",
};

internal void
print_rss_tree_recursively(RSS_Tree_Node *node, i8 layer)
{
	while (node) {
		for (int i = 0; i < layer; ++i) putc('\t', stderr);
		fprintf(stderr, "[%s] %.*s\n",
			type_to_str[node->token->type], node->token->text.len, node->token->text.str);
		print_rss_tree_recursively(node->first_child, layer + 1);
		node = node->next_sibling;
	}
}

internal void
print_rss_tree(RSS_Tree *tree)
{
	print_rss_tree_recursively(tree->root, 0);
}
#endif

RSS_Tree *
parse_rss(Arena *arena, RSS_Token_List tokens)
{
	Parser parser = {
		.arena = arena,
		.tokens = tokens,
		.current_token = tokens.first,
		.tree = arena_alloc(arena, sizeof(RSS_Tree)),
	};

	parse_processing_instructions(&parser);
	parser.tree->root = parse_tag(&parser);

#ifdef PRINT_RSS_TREE
	print_rss_tree(parser.tree);
#endif

	assert(parser.current_token->type == RSS_TOKEN_END);
	return parser.tree;
}

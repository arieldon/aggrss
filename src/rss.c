#include <ctype.h>
#include <stdio.h>

#include "arena.h"
#include "base.h"
#include "rss.h"

/* ---
 * Tokenizer
 * ---
 */

typedef struct {
	Arena *arena;
	char *source;
	i32 end;
	i32 cursor;
	i32 token_start;
	bool tag;
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
	return tokenizer->cursor < tokenizer->end;
}

internal inline char
peek_char(Tokenizer *tokenizer)
{
	assert(tokenizer->cursor <= tokenizer->end);
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
scan_token(Tokenizer *tokenizer)
{
start:
	ignore_whitespace(tokenizer);
	tokenizer->token_start = tokenizer->cursor;
	if (!more_source_exists(tokenizer)) {
		return make_token(tokenizer, RSS_TOKEN_END);
	}

	if (tokenizer->tag) {
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
			tokenizer->tag = false;
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
		default:
			return make_error_token(tokenizer, "unexpected character");
		}
	} else {
		// NOTE(ariel) Tokenize content.
		while (more_source_exists(tokenizer) && peek_char(tokenizer) != '<') {
			++tokenizer->cursor;
		}
		tokenizer->tag = true;
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
	RSS_Tree tree;
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

internal RSS_Tree_Node *
parse_tag(Parser *parser)
{
	RSS_Tree_Node *node = make_tree_node(parser);

	// NOTE(ariel) Parse start tag.
	expect_token(parser, RSS_TOKEN_STAG_OPEN, "expected '<'");

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
		// TODO(ariel) Create node for content and connect to tree.
		eat_token(parser);
	}

	// NOTE(ariel) Match start tag of child tag if it exists.
	while (peek_token(parser)->type == RSS_TOKEN_STAG_OPEN) {
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

	while (peek_token(parser)->type == RSS_TOKEN_STAG_OPEN) {
		RSS_Tree_Node *sibling = parse_tag(parser);
		sibling->prev_sibling = node;
		node->next_sibling = sibling;
	}

	return node;
}

RSS_Tree
parse_rss(Arena *arena, RSS_Token_List tokens)
{
	Parser parser = {
		.arena = arena,
		.tokens = tokens,
		.current_token = tokens.first,
	};

	parse_processing_instructions(&parser);
	parser.tree.root = parse_tag(&parser);

	assert(parser.current_token->type == RSS_TOKEN_END);
	return parser.tree;
}

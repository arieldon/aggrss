#include "arena.h"
#include "base.h"
#include "rss.h"
#include "str.h"

/* ---
 * Parser
 * ---
 */

typedef struct Parser Parser;
struct Parser
{
	Arena *arena;
	RSS_Tree *tree;
	RSS_Tree_Node *current_node;
	String source;
	i32 cursor;
};

internal void
error(Parser *parser, String message)
{
	RSS_Error *e = arena_alloc(parser->arena, sizeof(RSS_Error));
	e->next = 0;
	e->text = message;
	e->source_offset = parser->cursor;

	if (!parser->tree->errors.first)
	{
		parser->tree->errors.first = e;
	}
	else if (!parser->tree->errors.last)
	{
		parser->tree->errors.last = parser->tree->errors.first->next = e;
	}
	else
	{
		parser->tree->errors.last = parser->tree->errors.last->next = e;
	}
}

internal char
peek_char(Parser *parser)
{
	char ch = 0;
	if (parser->cursor < parser->source.len)
	{
		ch = parser->source.str[parser->cursor];
	}
	return ch;
}

internal b32
is_alphanumeric(char c)
{
	b32 number = c >= '0' && c <= '9';
	b32 lower = c >= 'a' && c <= 'z';
	b32 upper = c >= 'A' && c <= 'Z';
	b32 result = number | lower | upper;
	return result;
}

internal b32
is_name_char(char c)
{
	b32 alphanumeral = is_alphanumeric(c);
	b32 colon = c == ':';
	b32 underscore = c == '_';
	b32 dash = c == '-';
	b32 dot = c == '.';
	b32 result = alphanumeral | colon | underscore | dash | dot;
	return result;
}

internal b32
is_whitespace(char c)
{
	b32 space = c == ' ';
	b32 line = c == '\n';
	b32 tab = c == '\t';
	b32 ret = c == '\r';
	b32 result = space | line | tab | ret;
	return result;
}

internal void
skip_whitespace(Parser *parser)
{
	while (is_whitespace(peek_char(parser)))
	{
		++parser->cursor;
	}
}

internal b32
accept_char(Parser *parser, char c)
{
	b32 accept = false;
	if (c == peek_char(parser))
	{
		++parser->cursor;
		accept = true;
	}
	return accept;
}

internal b32
accept_string(Parser *parser, String s)
{
	b32 accept = false;

	if (parser->cursor + s.len < parser->source.len)
	{
		String source_snippet =
		{
			.str = parser->source.str + parser->cursor,
			.len = MIN(s.len, parser->source.len - parser->cursor),
		};
		if (string_match(s, source_snippet))
		{
			parser->cursor += s.len;
			accept = true;
		}
	}

	return accept;
}

internal String
expect_name(Parser *parser)
{
	String s = {0};

	skip_whitespace(parser);
	s.str = parser->source.str + parser->cursor;
	while (is_name_char(peek_char(parser)))
	{
		++s.len;
		++parser->cursor;
	}

	if (s.len == 0)
	{
		error(parser, string_literal("expected tag name"));
	}
	else if (s.len < 0)
	{
		error(parser, string_literal("length of name exceeds maximum"));
	}

	return s;
}

internal void
continue_to_char(Parser *parser, char c)
{
	while (parser->cursor < parser->source.len && c != parser->source.str[parser->cursor])
	{
		++parser->cursor;
	}
}

internal void
continue_to_string(Parser *parser, String s)
{
	while (parser->cursor < parser->source.len && !accept_string(parser, s))
	{
		++parser->cursor;
	}
}

internal void
push_rss_node(Parser *parser)
{
	if (!parser->current_node)
	{
		parser->current_node = parser->tree->root = arena_alloc(parser->arena, sizeof(RSS_Tree_Node));
	}
	else
	{
		RSS_Tree_Node *child = arena_alloc(parser->arena, sizeof(RSS_Tree_Node));
		RSS_Tree_Node *parent = parser->current_node;
		child->parent = parent;

		if (parent->last_child)
		{
			child->prev_sibling = parent->last_child;
			parent->last_child->next_sibling = child;
			parent->last_child = child;
		}
		else if (!parent->first_child)
		{
			parent->first_child = child;
		}
		else if (!parent->last_child)
		{
			child->prev_sibling = parent->first_child;
			parent->first_child->next_sibling = child;
			parent->last_child = child;
		}
		else
		{
			assert(!"UNREACHABLE");
		}

		parser->current_node = child;
	}
}

internal void
pop_rss_node(Parser *parser)
{
	parser->current_node = parser->current_node->parent;
}

internal void
continue_past_end_of_tag(Parser *parser)
{
	while (parser->cursor < parser->source.len)
	{
		char c = parser->source.str[parser->cursor++];
		if (c == '/')
		{
			if (parser->cursor < parser->source.len)
			{
				c = parser->source.str[parser->cursor++];
				if (c == '>')
				{
					pop_rss_node(parser);
					return;
				}
			}
		}
		else if (c == '>')
		{
			return;
		}
	}
}

internal b32
expect_char(Parser *parser, char c)
{
	b32 expected = false;
	if (c == peek_char(parser))
	{
		++parser->cursor;
		expected = true;
	}
	else
	{
		error(parser, string_literal("encountered unexpected character"));
	}
	return expected;
}

internal String
expect_string_literal(Parser *parser)
{
	String s = {0};

	char quote = peek_char(parser);
	if (quote == '"' || quote == '\'')
	{
		expect_char(parser, quote);
		i32 start = parser->cursor;
		continue_to_char(parser, quote);
		s.str = parser->source.str + start;
		s.len = parser->cursor - start;
		expect_char(parser, quote);
	}
	else
	{
		error(parser, string_literal("expected single or double quote for string literal"));
	}

	return s;
}

internal RSS_Attribute *
accept_attributes(Parser *parser)
{
	RSS_Attribute *attributes = 0;

	for (;;)
	{
		skip_whitespace(parser);
		if (parser->cursor >= parser->source.len || parser->tree->errors.first)
		{
			break;
		}

		char c = peek_char(parser);
		if (c == '/' || c == '>')
		{
			break;
		}

		RSS_Attribute *attribute = arena_alloc(parser->arena, sizeof(RSS_Attribute));
		attribute->name = expect_name(parser);
		expect_char(parser, '=');
		attribute->value = expect_string_literal(parser);

		if (attributes)
		{
			attribute->next = attributes;
		}
		else
		{
			attribute->next = 0;
		}
		attributes = attribute;
	}

	return attributes;
}

internal void
parse_tree(Parser *parser)
{
	for (;;)
	{
		skip_whitespace(parser);
		if (parser->cursor >= parser->source.len || parser->tree->errors.first)
		{
			break;
		}

		if (accept_char(parser, '<'))
		{
			if (accept_char(parser, '/'))
			{
				pop_rss_node(parser);
				continue_to_string(parser, string_literal(">"));
			}
			else if (accept_char(parser, '!'))
			{
				if (accept_string(parser, string_literal("[CDATA[")))
				{
					i32 start = parser->cursor;
					String cdend = string_literal("]]>");
					continue_to_string(parser, cdend);
					parser->current_node->content.str = parser->source.str + start;
					parser->current_node->content.len = parser->cursor - start - cdend.len;
				}
				else if (accept_string(parser, string_literal("--")))
				{
					continue_to_string(parser, string_literal("--!>"));
				}
			}
			else if (accept_char(parser, '?'))
			{
				continue_to_string(parser, string_literal("?>"));
			}
			else
			{
				push_rss_node(parser);
				parser->current_node->name = expect_name(parser);
				if (string_match(string_literal("link"), parser->current_node->name))
				{
					parser->current_node->attributes = accept_attributes(parser);
				}
				continue_past_end_of_tag(parser);
			}
		}
		else
		{
			i32 start = parser->cursor;
			continue_to_char(parser, '<');
			parser->current_node->content.str = parser->source.str + start;
			parser->current_node->content.len = parser->cursor - start;
		}
	}
}

#ifdef PRINT_TREE
#include <stdio.h>

internal void
print_rss_tree_recursively(RSS_Tree_Node *node, i8 layer)
{
	while (node)
	{
		for (int i = 0; i < layer; ++i)
		{
			putc('\t', stdout);
		}
		fprintf(stdout, "%.*s -> %.*s\n",
		node->name.len, node->name.str,
		node->content.len, node->content.str);
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
parse_rss(Arena *arena, String source)
{
	Parser parser =
	{
		.arena = arena,
		.source = source,
		.tree = arena_alloc(arena, sizeof(RSS_Tree)),
	};
	parse_tree(&parser);
	assert(parser.cursor == parser.source.len);
#ifdef PRINT_TREE
	print_rss_tree(parser.tree);
#endif
	return parser.tree;
}


/* ---
 * RSS Tree Traversal
 * ---
 */

typedef struct Node Node;
struct Node
{
	void *data;
	Node *next;
};

typedef struct Stack Stack;
struct Stack
{
	Node *top;
};

internal inline void
push_node(Arena *arena, Stack *s, void *data)
{
	Node *node = arena_alloc(arena, sizeof(Node));
	node->data = data;
	node->next = s->top;
	s->top = node;
}

internal inline void *
pop_node(Stack *s)
{
	Node *node = s->top;
	s->top = s->top->next;
	return node->data;
}

internal inline bool
is_stack_empty(Stack *s)
{
	return !s->top;
}

// NOTE(ariel) RSS uses the keyword "item". Atom uses the keyword "entry".
global String item_string = static_string_literal("item");
global String entry_string = static_string_literal("entry");
global String title_string = static_string_literal("title");

RSS_Tree_Node *
find_feed_title(Arena *arena, RSS_Tree_Node *root)
{
	RSS_Tree_Node *title_node = 0;

	Arena_Checkpoint checkpoint = arena_checkpoint_set(arena);
	{
		Stack s = {0};
		push_node(arena, &s, root);
		while (!is_stack_empty(&s))
		{
			RSS_Tree_Node *node = pop_node(&s);
			if (!node)
			{
				continue;
			}

			if (string_match(title_string, node->name))
			{
				title_node = node;
				break;
			}

			push_node(arena, &s, node->next_sibling);
			push_node(arena, &s, node->first_child);
		}
	}
	arena_checkpoint_restore(checkpoint);

	return title_node;
}

RSS_Tree_Node *
find_item_title(RSS_Tree_Node *item)
{
	RSS_Tree_Node *item_title_node = 0;

	if (string_match(item_string, item->name) || string_match(entry_string, item->name))
	{
		RSS_Tree_Node *node = item->first_child;
		while (node)
		{
			if (string_match(title_string, node->name))
			{
				item_title_node = node;
				break;
			}
			node = node->next_sibling;
		}
	}

	return item_title_node;
}

RSS_Tree_Node *
find_item_link(RSS_Tree_Node *item)
{
	RSS_Tree_Node *item_link_node = 0;

	if (string_match(item_string, item->name) || string_match(entry_string, item->name))
	{
		RSS_Tree_Node *node = item->first_child;
		while (node)
		{
			if (string_match(string_literal("link"), node->name))
			{
				item_link_node = node;
				break;
			}
			node = node->next_sibling;
		}
	}

	return item_link_node;
}

RSS_Tree_Node *
find_item_node(Arena *arena, RSS_Tree_Node *root)
{
	RSS_Tree_Node *item_node = 0;

	Arena_Checkpoint checkpoint = arena_checkpoint_set(arena);
	{
		// NOTE(ariel) Find the first item tag in the XML/RSS tree.
		Stack s = {0};
		push_node(arena, &s, root);
		while (!is_stack_empty(&s))
		{
			RSS_Tree_Node *node = pop_node(&s);
			if (!node)
			{
				continue;
			}

			if (string_match(item_string, node->name) || string_match(entry_string, node->name))
			{
				item_node = node;
				break;
			}

			push_node(arena, &s, node->next_sibling);
			push_node(arena, &s, node->first_child);
		}
	}
	arena_checkpoint_restore(checkpoint);

	return item_node;
}

RSS_Attribute *
find_url(RSS_Tree_Node *item)
{
	RSS_Attribute *href = 0;

	for (RSS_Tree_Node *child = item->first_child; !href && child; child = child->next_sibling)
	{
		if (string_match(child->name, string_literal("link")))
		{
			b32 type_equals_html = false;
			b32 rel_equals_alternate = false;
			RSS_Attribute *potential_href = 0;
			for (RSS_Attribute *attr = child->attributes; attr != 0; attr = attr->next)
			{
				if (string_match(attr->name, string_literal("href")))
				{
					potential_href = attr;
				}
				else if (string_match(attr->name, string_literal("rel")))
				{
					rel_equals_alternate = string_match(attr->value, string_literal("alternate"));
				}
				else if (string_match(attr->name, string_literal("type")))
				{
					type_equals_html = string_match(attr->value, string_literal("text/html"));
				}

				if (rel_equals_alternate && type_equals_html)
				{
					href = potential_href;
					break;
				}
			}
		}
	}

	return href;
}

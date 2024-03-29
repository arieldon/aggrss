/* ---
 * Parser
 * ---
 */

typedef struct Parser Parser;
struct Parser
{
	arena *Arena;
	RSS_Tree *tree;
	RSS_Tree_Node *current_node;
	string source;
	s32 cursor;
};

static void
error(Parser *parser, string message)
{
	RSS_Error *e = PushStructToArena(parser->Arena, RSS_Error);
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

static char
peek_char(Parser *parser)
{
	char ch = 0;
	if (parser->cursor < parser->source.len)
	{
		ch = parser->source.str[parser->cursor];
	}
	return ch;
}

static b32
is_alphanumeric(char c)
{
	b32 number = c >= '0' && c <= '9';
	b32 lower = c >= 'a' && c <= 'z';
	b32 upper = c >= 'A' && c <= 'Z';
	b32 result = number | lower | upper;
	return result;
}

static b32
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

static b32
is_whitespace(char c)
{
	b32 space = c == ' ';
	b32 line = c == '\n';
	b32 tab = c == '\t';
	b32 ret = c == '\r';
	b32 result = space | line | tab | ret;
	return result;
}

static void
skip_whitespace(Parser *parser)
{
	while (is_whitespace(peek_char(parser)))
	{
		++parser->cursor;
	}
}

static b32
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

static b32
accept_string(Parser *parser, string s)
{
	b32 accept = false;

	if (parser->cursor + s.len < parser->source.len)
	{
		string source_snippet =
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

static string
expect_name(Parser *parser)
{
	string s = {0};

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

static void
continue_to_char(Parser *parser, char c)
{
	while (parser->cursor < parser->source.len && c != parser->source.str[parser->cursor])
	{
		++parser->cursor;
	}
}

static void
continue_past_string(Parser *parser, string s)
{
	while (parser->cursor < parser->source.len && !accept_string(parser, s))
	{
		++parser->cursor;
	}
}

static void
push_rss_node(Parser *parser)
{
	if (!parser->current_node)
	{
		parser->current_node = parser->tree->root = PushStructToArena(parser->Arena, RSS_Tree_Node);
	}
	else
	{
		RSS_Tree_Node *child = PushStructToArena(parser->Arena, RSS_Tree_Node);
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

static void
pop_rss_node(Parser *parser)
{
	parser->current_node = parser->current_node->parent;
}

static void
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

static b32
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

static string
expect_string_literal(Parser *parser)
{
	string s = {0};

	char quote = peek_char(parser);
	if (quote == '"' || quote == '\'')
	{
		expect_char(parser, quote);
		s32 start = parser->cursor;
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

static RSS_Attribute *
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

		RSS_Attribute *attribute = PushStructToArena(parser->Arena, RSS_Attribute);
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

static void
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
				continue_past_string(parser, string_literal(">"));
			}
			else if (accept_char(parser, '!'))
			{
				if (accept_string(parser, string_literal("[CDATA[")))
				{
					s32 start = parser->cursor;
					string cdend = string_literal("]]>");
					continue_past_string(parser, cdend);
					parser->current_node->content.str = parser->source.str + start;
					parser->current_node->content.len = parser->cursor - start - cdend.len;
				}
				else if (accept_string(parser, string_literal("--")))
				{
					continue_past_string(parser, string_literal("-->"));
				}
				else if (accept_string(parser, string_literal("DOCTYPE")))
				{
					continue_past_end_of_tag(parser);
				}
			}
			else if (accept_char(parser, '?'))
			{
				continue_past_string(parser, string_literal("?>"));
			}
			else
			{
				push_rss_node(parser);
				parser->current_node->name = expect_name(parser);
				if(string_find_substr(parser->current_node->name, string_literal("link")) != -1)
				{
					parser->current_node->attributes = accept_attributes(parser);
				}
				continue_past_end_of_tag(parser);
			}
		}
		else
		{
			s32 start = parser->cursor;
			continue_to_char(parser, '<');
			parser->current_node->content.str = parser->source.str + start;
			parser->current_node->content.len = parser->cursor - start;
		}
	}
}

#ifdef PRINT_TREE_SUPPORT
static void
RSS_PrintTreeRecursively(RSS_Tree_Node *Node, s8 Layer, FILE *Stream)
{
	while(Node)
	{
		for(s32 _ = 0; _ < Layer; _ += 1)
		{
			putc('\t', Stream);
		}
		if(Node->attributes)
		{
			fprintf(Stream, "[%.*s]", Node->name.len, Node->name.str);
			for(RSS_Attribute *AttributeNode = Node->attributes; AttributeNode; AttributeNode = AttributeNode->next)
			{
				fprintf(Stream, " (%.*s=%.*s)",
					AttributeNode->name.len, AttributeNode->name.str,
					AttributeNode->value.len, AttributeNode->value.str);
			}
			if(Node->content.str)
			{
				string Content = string_trim_spaces(Node->content);
				fprintf(Stream, " %.*s", Content.len, Content.str);
			}
			fprintf(Stream, "\n");
		}
		else
		{
			fprintf(Stream, "[%.*s]", Node->name.len, Node->name.str);
			if(Node->content.str)
			{
				string Content = string_trim_spaces(Node->content);
				fprintf(Stream, " %.*s", Content.len, Content.str);
			}
			fprintf(Stream, "\n");
		}
		RSS_PrintTreeRecursively(Node->first_child, Layer + 1, Stream);
		Node = Node->next_sibling;
	}
}

static void
RSS_PrintTree(RSS_Tree *tree, FILE *stream)
{
	RSS_PrintTreeRecursively(tree->root, 0, stream);
}
#endif

static RSS_Tree *
parse_rss(arena *Arena, string source)
{
	Parser parser =
	{
		.Arena = Arena,
		.source = source,
		.tree = PushStructToArena(Arena, RSS_Tree),
	};
	parse_tree(&parser);
	assert(parser.cursor == parser.source.len);
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

static inline void
push_node(arena *Arena, Stack *s, void *data)
{
	Node *node = PushStructToArena(Arena, Node);
	node->data = data;
	node->next = s->top;
	s->top = node;
}

static inline void *
pop_node(Stack *s)
{
	Node *node = s->top;
	s->top = s->top->next;
	return node->data;
}

static inline bool
is_stack_empty(Stack *s)
{
	return !s->top;
}

// NOTE(ariel) RSS uses the keyword "item". Atom uses the keyword "entry".
global string item_string = static_string_literal("item");
global string entry_string = static_string_literal("entry");
global string title_string = static_string_literal("title");

static RSS_Tree_Node *
find_feed_title(arena *Arena, RSS_Tree_Node *root)
{
	RSS_Tree_Node *title_node = 0;

	arena_checkpoint Checkpoint = SetArenaCheckpoint(Arena);
	{
		Stack s = {0};
		push_node(Arena, &s, root);
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

			push_node(Arena, &s, node->next_sibling);
			push_node(Arena, &s, node->first_child);
		}
	}
	RestoreArenaFromCheckpoint(Checkpoint);

	return title_node;
}

static RSS_Tree_Node *
find_item_child_node(RSS_Tree_Node *item, string name)
{
	RSS_Tree_Node *child_node = 0;

	if (string_match(item_string, item->name) || string_match(entry_string, item->name))
	{
		RSS_Tree_Node *node = item->first_child;
		while (node)
		{
			if (string_match(name, node->name))
			{
				child_node = node;
				break;
			}
			node = node->next_sibling;
		}
	}

	return child_node;
}

static RSS_Tree_Node *
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

static RSS_Tree_Node *
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

static RSS_Tree_Node *
find_item_node(arena *Arena, RSS_Tree_Node *root)
{
	RSS_Tree_Node *item_node = 0;

	arena_checkpoint Checkpoint = SetArenaCheckpoint(Arena);
	{
		// NOTE(ariel) Find the first item tag in the XML/RSS tree.
		Stack s = {0};
		push_node(Arena, &s, root);
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

			push_node(Arena, &s, node->next_sibling);
			push_node(Arena, &s, node->first_child);
		}
	}
	RestoreArenaFromCheckpoint(Checkpoint);

	return item_node;
}

static string
find_link(RSS_Tree_Node *item)
{
	string link = {0};

	RSS_Tree_Node *link_node = find_item_link(item);
	if (link_node)
	{
		if (link_node->content.len > 0)
		{
			link = link_node->content;
		}
		else
		{
			b32 type_equals_html = false;
			b32 rel_equals_alternate = false;
			RSS_Attribute *href = 0;
			for (RSS_Attribute *attr = link_node->attributes; attr != 0; attr = attr->next)
			{
				if (string_match(attr->name, string_literal("href")))
				{
					href = attr;
				}
				else if (string_match(attr->name, string_literal("rel")))
				{
					rel_equals_alternate = string_match(attr->value, string_literal("alternate"));
				}
				else if (string_match(attr->name, string_literal("type")))
				{
					type_equals_html = string_match(attr->value, string_literal("text/html"));
				}

				if (rel_equals_alternate && type_equals_html && href)
				{
					link = href->value;
					break;
				}
			}

			if (!link.str && href)
			{
				// NOTE(ariel) Choose any link if no tag strictly matches the
				// requirements above.
				link = href->value;
			}
		}
	}

	return link;
}

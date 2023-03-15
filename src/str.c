#include <ctype.h>
#include <string.h>

#include "arena.h"
#include "str.h"

bool
string_match(String s, String t)
{
	if (s.len != t.len)
	{
		return false;
	}
	for (i32 i = 0; i < s.len; ++i)
	{
		if (s.str[i] != t.str[i])
		{
			return false;
		}
	}
	return true;
}

char *
string_terminate(Arena *arena, String s)
{
	char *t = arena_alloc(arena, s.len + 1);
	memcpy(t, s.str, s.len);
	t[s.len] = 0;
	return t;
}

String
string_duplicate(Arena *arena, String s)
{
	String t = {0};
	t.len = s.len;
	t.str = arena_alloc(arena, t.len);
	memmove(t.str, s.str, t.len);
	return t;
}

String
string_trim_spaces(String s)
{
	i32 leading_spaces = 0;
	for (i32 i = 0; i < s.len; ++i)
	{
		if (!isspace(s.str[i]))
		{
			break;
		}
		++leading_spaces;
	}

	i32 trailing_spaces = 0;
	for (i32 i = s.len - 1; i >= 0; --i)
	{
		if (!isspace(s.str[i]))
		{
			break;
		}
		++trailing_spaces;
	}

	i32 length = s.len - leading_spaces - trailing_spaces;
	String substr = string_substr(s, leading_spaces, length);
	return substr;
}

String
string_substr(String s, i32 offset, i32 len)
{
	String t = {0};
	if (offset >= s.len)
	{
		return t;
	}
	if (offset + len > s.len)
	{
		len = s.len - offset;
	}
	t.len = len;
	t.str = s.str + offset;
	return t;
}

String
string_prefix(String s, i32 len)
{
	assert(len >= 0);
	String t = {0};
	if (len >= s.len)
	{
		return s;
	}
	t.len = len;
	t.str = s.str;
	return t;
}

String
string_suffix(String s, i32 offset)
{
	assert(offset >= 0);
	String t = {0};
	if (offset >= s.len)
	{
		return s;
	}
	t.len = s.len - offset;
	t.str = s.str + offset;
	return t;
}

i32
string_find_substr(String haystack, String needle)
{
	assert(haystack.len >= needle.len);
	i32 end = haystack.len - needle.len;
	for (i32 i = 0; i <= end; ++i)
	{
		String substr = string_substr(haystack, i, needle.len);
		if (string_match(substr, needle))
		{
			return i;
		}
	}
	return -1;
}

i32
string_find_ch(String s, char c)
{
	for (i32 i = 0; i < s.len; ++i)\
	{
		if (s.str[i] == c)
		{
			return i;
		}
	}
	return -1;
}

u64
string_to_int(String s, u8 base)
{
	assert(base >=  2);
	assert(base <= 16);
	local_persist u8 char_to_value[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	};
	u64 result = 0;
	for (i32 i = 0; i < s.len; ++i)
	{
		result *= base;
		result += char_to_value[(s.str[i] - 0x30) & 0x1F];
	}
	return result;
}

void
string_list_push_node(String_List *ls, String_Node *n)
{
	if (!ls->head)
	{
		ls->head = n;
	}
	else if (!ls->tail)
	{
		ls->head->next = n;
		ls->tail = n;
	}
	else
	{
		ls->tail->next = n;
		ls->tail = n;
	}
	ls->total_len += n->string.len;
	++ls->list_size;
}

void
string_list_push_string(Arena *arena, String_List *ls, String s)
{
	String_Node *n = arena_alloc(arena, sizeof(String_Node));
	n->string = s;
	string_list_push_node(ls, n);
}

String_List
string_split(Arena *arena, String s, u8 delim)
{
	String_List ls = {0};

	i32 j = 0;
	for (i32 i = 0; i < s.len; ++i)
	{
		if (s.str[i] == delim)
		{
			String segment =
			{
				.str = s.str + j,
				.len = i - j,
			};
			string_list_push_string(arena, &ls, segment);

			j = i + 1;
		}
	}

	String segment =
	{
		.str = s.str + j,
		.len = s.len - j,
	};
	if (segment.len > 0)
	{
		string_list_push_string(arena, &ls, segment);
	}

	return ls;
}

String_List
string_strsplit(Arena *arena, String s, String delim)
{
	String_List ls = {0};

	i32 end = s.len - delim.len;
	for (i32 i = 0, prev_split = 0; i < end; ++i)
	{
		String potential_delimiter =
		{
			.str = s.str + i,
			.len = delim.len,
		};
		if (string_match(potential_delimiter, delim))
		{
			String_Node *n = arena_alloc(arena, sizeof(String_Node));
			n->string.str = s.str + i + delim.len;
			n->string.len = s.len - i - delim.len;
			ls.total_len += n->string.len;
			++ls.list_size;

			if (!ls.head)
			{
				ls.head = arena_alloc(arena, sizeof(String_Node));
				ls.head->string.str = s.str;
				ls.head->string.len = i;
				ls.head->next = n;
				ls.tail = n;
				ls.total_len += i;
				++ls.list_size;
			}
			else
			{
				ls.total_len -= ls.tail->string.len;
				ls.tail->string.len = i - prev_split - delim.len;
				ls.total_len += ls.tail->string.len;

				ls.tail->next = n;
				ls.tail = n;
			}

			prev_split = i;
		}
	}

	return ls;
}

String
string_list_concat(Arena *arena, String_List ls)
{
	String s =
	{
		.str = arena_alloc(arena, 0),
	};

	String_Node *n = ls.head;
	while (n)
	{
		String t = n->string;

		s.str = arena_realloc(arena, s.len + t.len);
		memcpy(s.str + s.len, t.str, t.len);
		s.len += t.len;

		n = n->next;
	}

	return s;
}

String
string_list_join(Arena *arena, String_List ls, u8 sep)
{
	if (ls.list_size == 1)
	{
		String t = ls.head->string;
		String s = {
			.str = arena_alloc(arena, t.len),
			.len = t.len,
		};
		memcpy(s.str, t.str, t.len);
		return s;
	}

	String s =
	{
		.str = arena_alloc(arena, 0),
	};

	String_Node *n = ls.head;
	while (n)
	{
		String t = n->string;

		if (n != ls.tail)
		{
			s.str = arena_realloc(arena, s.len + t.len + 1);
			memcpy(s.str + s.len, t.str, t.len);
			s.len += t.len + 1;
			s.str[s.len - 1] = sep;
		}
		else
		{
			s.str = arena_realloc(arena, s.len + t.len);
			memcpy(s.str + s.len, t.str, t.len);
			s.len += t.len;
		}

		n = n->next;
	}

	return s;
}

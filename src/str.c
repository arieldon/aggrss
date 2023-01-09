#include <string.h>

#include "arena.h"
#include "str.h"

bool
string_equal(String s, String t)
{
	if (s.len != t.len) return false;
	for (i32 i = 0; i < s.len; ++i) {
		if (s.str[i] != t.str[i]) {
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
string_substr(String s, i32 offset, i32 len)
{
	String t = {0};
	if (offset > s.len) return t;
	if (offset + len >= s.len) return t;
	t.len = s.len - offset;
	t.str = s.str + offset;
	return t;
}

String
string_prefix(String s, i32 len)
{
	return string_substr(s, 0, len);
}

String
string_suffix(String s, i32 offset)
{
	return string_substr(s, offset, s.len);
}

i32
string_find_substr(String haystack, String needle)
{
	for (i32 i = 0; i < haystack.len; ++i) {
		if (i + needle.len <= haystack.len) {
			String substr = string_substr(haystack, i, i + needle.len);
			if (string_equal(substr, needle)) {
				return i;
			}
		}
	}
	return -1;
}

i32
string_find_ch(String s, char c)
{
	for (i32 i = 0; i < s.len; ++i) {
		if (s.str[i] == c) {
			return i;
		}
	}
	return -1;
}

void
push_string_node(String_List *ls, String_Node *n)
{
	if (!ls->head) {
		ls->head = n;
	} else if (!ls->tail) {
		ls->head->next = n;
		ls->tail = n;
	} else {
		ls->tail->next = n;
		ls->tail = n;
	}
	ls->total_len += n->string.len;
	++ls->list_size;
}

void
push_string(Arena *arena, String_List *ls, String s)
{
	String_Node *n = arena_alloc(arena, sizeof(String_Node));
	n->string = s;
	push_string_node(ls, n);
}

String_List
string_split(Arena *arena, String s, u8 delim)
{
	String_List ls = {0};

	for (i32 i = 0, prev_split = 0; i < s.len; ++i) {
		if (s.str[i] == delim) {
			if (i == s.len - 1) {
				ls.tail->string.len -= 1;
				break;
			}

			String_Node *n = arena_alloc(arena, sizeof(String_Node));
			n->string.str = s.str + i + 1;
			n->string.len = s.len - i - 1;
			ls.total_len += n->string.len;
			++ls.list_size;

			if (!ls.head) {
				ls.head = arena_alloc(arena, sizeof(String_Node));
				ls.head->string.str = s.str;
				ls.head->string.len = i;
				ls.head->next = n;
				ls.tail = n;
				ls.total_len += i;
				++ls.list_size;
			} else {
				ls.total_len -= ls.tail->string.len;
				ls.tail->string.len = i - prev_split - 1;
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
	String s = {
		.str = arena_alloc(arena, 0),
	};

	String_Node *n = ls.head;
	while (n) {
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
	if (ls.list_size == 1) {
		String t = ls.head->string;
		String s = {
			.str = arena_alloc(arena, t.len),
			.len = t.len,
		};
		memcpy(s.str, t.str, t.len);
		return s;
	}

	String s = {
		.str = arena_alloc(arena, 0),
	};

	String_Node *n = ls.head;
	while (n) {
		String t = n->string;

		if (n != ls.tail) {
			s.str = arena_realloc(arena, s.len + t.len + 1);
			memcpy(s.str + s.len, t.str, t.len);
			s.len += t.len + 1;
			s.str[s.len - 1] = sep;
		} else {
			s.str = arena_realloc(arena, s.len + t.len);
			memcpy(s.str + s.len, t.str, t.len);
			s.len += t.len;
		}

		n = n->next;
	}

	return s;
}

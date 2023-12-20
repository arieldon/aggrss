static bool
string_match(string s, string t)
{
	if (s.len != t.len)
	{
		return false;
	}
	for (s32 i = 0; i < s.len; ++i)
	{
		if (s.str[i] != t.str[i])
		{
			return false;
		}
	}
	return true;
}

static char *
string_terminate(arena *Arena, string s)
{
	char *t = PushBytesToArena(Arena, s.len + 1);
	memcpy(t, s.str, s.len);
	t[s.len] = 0;
	return t;
}

static string
string_duplicate(arena *Arena, string s)
{
	string t = {0};
	t.len = s.len;
	t.str = PushBytesToArena(Arena, t.len);
	memmove(t.str, s.str, t.len);
	return t;
}

static string
string_trim_spaces(string s)
{
	s32 leading_spaces = 0;
	for (s32 i = 0; i < s.len; ++i)
	{
		if (!isspace(s.str[i]))
		{
			break;
		}
		++leading_spaces;
	}

	s32 trailing_spaces = 0;
	for (s32 i = s.len - 1; i >= 0; --i)
	{
		if (!isspace(s.str[i]))
		{
			break;
		}
		++trailing_spaces;
	}

	s32 length = s.len - leading_spaces - trailing_spaces;
	string substr = string_substr(s, leading_spaces, length);
	return substr;
}

static string
string_substr(string s, s32 offset, s32 len)
{
	string t = {0};
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

static string
string_prefix(string s, s32 len)
{
	assert(len >= 0);
	string t = {0};
	if (len >= s.len)
	{
		return s;
	}
	t.len = len;
	t.str = s.str;
	return t;
}

static string
string_suffix(string s, s32 offset)
{
	assert(offset >= 0);
	string t = {0};
	if (offset >= s.len)
	{
		return s;
	}
	t.len = s.len - offset;
	t.str = s.str + offset;
	return t;
}

static s32
string_find_substr(string haystack, string needle)
{
	assert(haystack.len >= needle.len);
	s32 end = haystack.len - needle.len;
	for (s32 i = 0; i <= end; ++i)
	{
		string substr = string_substr(haystack, i, needle.len);
		if (string_match(substr, needle))
		{
			return i;
		}
	}
	return -1;
}

static s32
string_find_ch(string s, char c)
{
	for (s32 i = 0; i < s.len; ++i)
	{
		if (s.str[i] == c)
		{
			return i;
		}
	}
	return -1;
}

static u64
string_to_int(string s, u8 base)
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
	for (s32 i = 0; i < s.len; ++i)
	{
		result *= base;
		result += char_to_value[(s.str[i] - 0x30) & 0x1F];
	}
	return result;
}

static void
string_lower(string s)
{
	for (s32 i = 0; i < s.len; ++i)
	{
		s.str[i] = (char)tolower(s.str[i]);
	}
}

static void
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

static void
string_list_push_string(arena *Arena, String_List *ls, string s)
{
	String_Node *n = PushStructToArena(Arena, String_Node);
	n->string = s;
	string_list_push_node(ls, n);
}

static String_List
string_split(arena *Arena, string s, u8 delim)
{
	String_List ls = {0};

	s32 j = 0;
	for (s32 i = 0; i < s.len; ++i)
	{
		if (s.str[i] == delim)
		{
			string segment =
			{
				.str = s.str + j,
				.len = i - j,
			};
			string_list_push_string(Arena, &ls, segment);

			j = i + 1;
		}
	}

	string segment =
	{
		.str = s.str + j,
		.len = s.len - j,
	};
	if (segment.len > 0)
	{
		string_list_push_string(Arena, &ls, segment);
	}

	return ls;
}

static String_List
string_strsplit(arena *Arena, string s, string delim)
{
	String_List ls = {0};

	s32 j = 0;
	for (s32 i = 0; i < s.len; ++i)
	{
		string t =
		{
			.str = s.str + i,
			.len = delim.len,
		};
		if (string_match(t, delim))
		{
			string segment =
			{
				.str = s.str + j,
				.len = i - j,
			};
			string_list_push_string(Arena, &ls, segment);

			j = i + delim.len;
		}
	}

	string segment =
	{
		.str = s.str + j,
		.len = s.len - j,
	};
	if (segment.len > 0)
	{
		string_list_push_string(Arena, &ls, segment);
	}

	return ls;
}

static string
string_list_concat(arena *Arena, String_List ls)
{
	string s =
	{
		.str = PushBytesToArena(Arena, 0),
	};

	String_Node *n = ls.head;
	while (n)
	{
		string t = n->string;

		s.str = ReallocFromArena(Arena, s.len + t.len);
		memcpy(s.str + s.len, t.str, t.len);
		s.len += t.len;

		n = n->next;
	}

	return s;
}

static string
string_list_join(arena *Arena, String_List ls, u8 sep)
{
	if (ls.list_size == 1)
	{
		string t = ls.head->string;
		string s =
		{
			.str = PushBytesToArena(Arena, t.len),
			.len = t.len,
		};
		memcpy(s.str, t.str, t.len);
		return s;
	}

	string s =
	{
		.str = PushBytesToArena(Arena, 0),
	};

	String_Node *n = ls.head;
	while (n)
	{
		string t = n->string;

		if (n != ls.tail)
		{
			s.str = ReallocFromArena(Arena, s.len + t.len + 1);
			memcpy(s.str + s.len, t.str, t.len);
			s.len += t.len + 1;
			s.str[s.len - 1] = sep;
		}
		else
		{
			s.str = ReallocFromArena(Arena, s.len + t.len);
			memcpy(s.str + s.len, t.str, t.len);
			s.len += t.len;
		}

		n = n->next;
	}

	return s;
}

static string
concat_strings(arena *Arena, s32 n_strings, string *strings)
{
	String_List ls = {0};

	for (s32 i = 0; i < n_strings; ++i)
	{
		string_list_push_string(Arena, &ls, strings[i]);
	}

	string result = string_list_concat(Arena, ls);
	return result;
}

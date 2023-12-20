#ifndef STRING_H
#define STRING_H

static bool string_match(string s, string t);
static char *string_terminate(Arena *arena, string s);
static string string_duplicate(Arena *arena, string s);
static string string_trim_spaces(string s);
static string string_substr(string s, s32 len, s32 offset);
static string string_prefix(string s, s32 len);
static string string_suffix(string s, s32 offset);
static s32 string_find_substr(string haystack, string needle);
static s32 string_find_ch(string s, char c);
static u64 string_to_int(string s, u8 base);

static void string_lower(string s);

typedef struct String_Node String_Node;
struct String_Node
{
	String_Node *next;
	string string;
};

typedef struct String_List String_List;
struct String_List
{
	String_Node *head;
	String_Node *tail;
	ssize total_len;
	ssize list_size;
};

static void string_list_push_node(String_List *ls, String_Node *n);
static void string_list_push_string(Arena *arena, String_List *ls, string s);

static String_List string_split(Arena *arena, string s, u8 delim);
static String_List string_strsplit(Arena *arena, string s, string delim);
static string string_list_concat(Arena *arena, String_List ls);
static string string_list_join(Arena *arena, String_List ls, u8 sep);

static string concat_strings(Arena *arena, s32 n_strings, string *strings);

#endif

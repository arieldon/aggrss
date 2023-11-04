#ifndef STRING_H
#define STRING_H

static bool string_match(String s, String t);
static char *string_terminate(Arena *arena, String s);
static String string_duplicate(Arena *arena, String s);
static String string_trim_spaces(String s);
static String string_substr(String s, i32 len, i32 offset);
static String string_prefix(String s, i32 len);
static String string_suffix(String s, i32 offset);
static i32 string_find_substr(String haystack, String needle);
static i32 string_find_ch(String s, char c);
static u64 string_to_int(String s, u8 base);

static void string_lower(String s);

typedef struct String_Node String_Node;
struct String_Node
{
	String_Node *next;
	String string;
};

typedef struct String_List String_List;
struct String_List
{
	String_Node *head;
	String_Node *tail;
	isize total_len;
	isize list_size;
};

static void string_list_push_node(String_List *ls, String_Node *n);
static void string_list_push_string(Arena *arena, String_List *ls, String s);

static String_List string_split(Arena *arena, String s, u8 delim);
static String_List string_strsplit(Arena *arena, String s, String delim);
static String string_list_concat(Arena *arena, String_List ls);
static String string_list_join(Arena *arena, String_List ls, u8 sep);

static String concat_strings(Arena *arena, i32 n_strings, String *strings);

#endif

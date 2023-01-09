#ifndef STRING_H
#define STRING_H

#include <stdbool.h>
#include <stdlib.h>

#include "base.h"

bool string_equal(String s, String t);
char *string_terminate(Arena *arena, String s);
String string_duplicate(Arena *arena, String s);
String string_substr(String s, i32 len, i32 offset);
String string_prefix(String s, i32 len);
String string_suffix(String s, i32 offset);
i32 string_find_substr(String haystack, String needle);
i32 string_find_ch(String s, char c);

typedef struct String_Node {
	struct String_Node *next;
	String string;
} String_Node;

typedef struct {
	String_Node *head;
	String_Node *tail;
	isize total_len;
	isize list_size;
} String_List;

void push_string_node(String_List *ls, String_Node *n);
void push_string(Arena *arena, String_List *ls, String s);

String_List string_split(Arena *arena, String s, u8 delim);
String string_list_concat(Arena *arena, String_List ls);
String string_list_join(Arena *arena, String_List ls, u8 sep);

#endif
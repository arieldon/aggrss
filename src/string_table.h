#ifndef STRING_TABLE_H
#define STRING_TABLE_H

#include <pthread.h>

#include "base.h"

enum
{
	EXPONENT = 16,
	TABLE_CAPACITY = 1 << EXPONENT,
	BUFFER_CAPACITY = 1 << 30,
};

// NOTE(ariel) This is basically a clone of Chris Wellons' MSI hash table. He
// explains it clearly here: https://nullprogram.com/blog/2022/08/08/.
typedef struct String_Table String_Table;
struct String_Table
{

	String buckets[TABLE_CAPACITY];
	i32 size;
	i32 cursor;
	char buffer[BUFFER_CAPACITY];
};

String intern(String_Table *table, String key);

#endif

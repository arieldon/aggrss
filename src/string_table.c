static inline u64
string_table_hash(string s)
{
	u64 hash = 0x100;
	for (s32 i = 0; i < s.len; i++)
	{
		hash ^= s.str[i] & 255;
		hash *= 1111111111111111111;
	}
	hash ^= hash >> 32;
	return hash;
}

static inline s32
get_index(u64 hash, s32 index)
{
	u32 mask = ((u32)1 << EXPONENT) - 1;
	u32 step = (u32)(hash >> (64 - EXPONENT)) | 1;
	s32 result = (index + step) & mask;
	return result;
}

static string
intern(String_Table *table, string s)
{
	string result = {0};

	u64 h = string_table_hash(s);
	s32 index = (s32)h;
	while (!result.str)
	{
		index = get_index(h, index);
		if (!table->buckets[index].str)
		{
			// NOTE(ariel) Exit early without inserting if string fails to fit into
			// remaining space available in statically allocated buffer.
			if (table->cursor + s.len > BUFFER_CAPACITY)
			{
				return result;
			}

			// NOTE(ariel) Insert new string into table.
			++table->size;
			table->buckets[index].str = table->buffer + table->cursor;
			memcpy(table->buckets[index].str, s.str, s.len);

			table->buckets[index].len = s.len;
			table->cursor += s.len;

			result = table->buckets[index];
		}
		else if (string_match(table->buckets[index], s))
		{
			// NOTE(ariel) String already exists in table/buffer.
			result = table->buckets[index];
		}
	}

	return result;
}

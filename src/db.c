#include <stdio.h>
#include <time.h>

#include <sqlite3.h>

#include "base.h"
#include "db.h"
#include "rss.h"
#include "str.h"

internal u32
hash(String s)
{
	u32 hash = 2166136261;
	for (i32 i = 0; i < s.len; ++i)
	{
		hash = (hash ^ s.str[i]) * 16777619;
	}
	return hash;
}

internal inline void
confirm_success(sqlite3 *db, i32 status_code, char *error_message)
{
	b32 ok = status_code == SQLITE_OK;
	b32 row = status_code == SQLITE_ROW;
	b32 done = status_code == SQLITE_DONE;
	b32 success = ok | row | done;
	if (!success)
	{
		const char *sqlite_error_message = sqlite3_errmsg(db);
		fprintf(stderr, "[DB ERROR] %s: %s\n", error_message, sqlite_error_message);
	}
}

void
db_init(sqlite3 **db)
{
	assert(sqlite3_threadsafe());

	i32 error = sqlite3_open("./feeds.db", db);
	if (error)
	{
		fprintf(stderr, "[DB ERROR] failed to open database file\n");
		exit(EXIT_FAILURE);
	}

	char *errmsg = 0;
	char *enable_foreign_keys = "PRAGMA foreign_keys = ON;";
	error = sqlite3_exec(*db, enable_foreign_keys, 0, 0, &errmsg);
	if (error)
	{
		fprintf(stderr, "[DB ERROR] failed to enable foreign keys: %s\n", errmsg);
		exit(EXIT_FAILURE);
	}

	char *create_feeds_table =
		"CREATE TABLE IF NOT EXISTS "
			"feeds("
				"id INTEGER PRIMARY KEY,"
				"link TEXT UNIQUE,"
				"title TEXT);";
	error = sqlite3_exec(*db, create_feeds_table, 0, 0, &errmsg);
	if (error)
	{
		fprintf(stderr, "[DB ERROR] failed to create feeds table: %s\n", errmsg);
		exit(EXIT_FAILURE);
	}

	errmsg = 0;
	char *create_items_table =
		"CREATE TABLE IF NOT EXISTS "
			"items("
				"link TEXT PRIMARY KEY,"
				"title TEXT NOT NULL,"
				"date_last_modified INTEGER NOT NULL DEFAULT 0,"
				"unread BOOLEAN NOT NULL DEFAULT 0,"
				"feed REFERENCES feeds(id) ON DELETE CASCADE);";
	error = sqlite3_exec(*db, create_items_table, 0, 0, &errmsg);
	if (error)
	{
		fprintf(stderr, "[DB ERROR] failed to create items table: %s\n", errmsg);
		exit(EXIT_FAILURE);
	}

	errmsg = 0;
	char *create_tags_table =
		"CREATE TABLE IF NOT EXISTS "
			"tags("
				"id INTEGER PRIMARY KEY,"
				"name TEXT UNIQUE NOT NULL);";
	error = sqlite3_exec(*db, create_tags_table, 0, 0, &errmsg);
	if (error)
	{
		fprintf(stderr, "[DB ERROR] failed to create tags table: %s\n", errmsg);
		exit(EXIT_FAILURE);
	}

	errmsg = 0;
	char *create_mapping_table =
		"CREATE TABLE IF NOT EXISTS "
			"tags_to_feeds("
				"tag INTEGER,"
				"feed INTEGER,"
				"FOREIGN KEY(tag) REFERENCES tags(id) ON DELETE CASCADE,"
				"FOREIGN KEY(feed) REFERENCES feeds(id) ON DELETE CASCADE,"
				"PRIMARY KEY(tag, feed));";
	error = sqlite3_exec(*db, create_mapping_table, 0, 0, &errmsg);
	if (error)
	{
		fprintf(stderr, "[DB ERROR] failed to create tags to feeds mapping table: %s\n", errmsg);
		exit(EXIT_FAILURE);
	}
}

i32
db_count_rows(sqlite3 *db)
{
	i32 count = -1;

	sqlite3_stmt* statement = 0;
	String count_rows = string_literal("SELECT COUNT(*) FROM feeds");
	sqlite3_prepare_v2(db, count_rows.str, count_rows.len, &statement, 0);

	i32 status = sqlite3_step(statement);
	if (status == SQLITE_ROW)
	{
		count = sqlite3_column_int(statement, 0);
	}
	sqlite3_finalize(statement);

	return count;
}

void
db_free(sqlite3 *db)
{
	i32 status = sqlite3_close(db);
	confirm_success(db, status, "failed to close database");
}

void
db_add_feed(sqlite3 *db, String feed_link, String feed_title)
{
	u32 feed_id = hash(feed_link);

	sqlite3_stmt *statement = 0;
	String insert_feed = string_literal("INSERT INTO feeds VALUES(?, ?, ?);");
	sqlite3_prepare_v2(db, insert_feed.str, insert_feed.len, &statement, 0);
	sqlite3_bind_int(statement, 1, feed_id);
	sqlite3_bind_text(statement, 2, feed_link.str, feed_link.len, SQLITE_STATIC);
	sqlite3_bind_text(statement, 3, feed_title.str, feed_title.len, SQLITE_STATIC);
	i32 status = sqlite3_step(statement);
	sqlite3_finalize(statement);

	confirm_success(db, status, "failed to add feed to database");
}

void
db_add_or_update_feed(sqlite3 *db, String feed_link, String feed_title)
{
	u32 feed_id = hash(feed_link);

	sqlite3_stmt *statement = 0;
	String insert_feed = string_literal(
		"INSERT INTO feeds VALUES(?, ?, ?) ON CONFLICT(link) DO UPDATE SET title=excluded.title;");
	sqlite3_prepare_v2(db, insert_feed.str, insert_feed.len, &statement, 0);
	sqlite3_bind_int(statement, 1, feed_id);
	sqlite3_bind_text(statement, 2, feed_link.str, feed_link.len, SQLITE_STATIC);
	sqlite3_bind_text(statement, 3, feed_title.str, feed_title.len, SQLITE_STATIC);
	i32 status = sqlite3_step(statement);
	sqlite3_finalize(statement);

	confirm_success(db, status, "failed to add or update feed in database");
}

internal inline void
get_content_from_node(RSS_Tree_Node *item_node, String term, String default_value, String *value)
{
	RSS_Tree_Node *node = find_item_child_node(item_node, term);
	if (node)
	{
		*value = node->content;
	}
	else
	{
		*value = default_value;
	}
}

typedef struct Timestamp Timestamp;
struct Timestamp
{
	struct tm tm_format;
	u32 unix_format;
};

internal inline void
parse_date(char *date, char *format, Timestamp *timestamp)
{
	if (!timestamp->unix_format && strptime(date, format, &timestamp->tm_format))
	{
		timestamp->unix_format = mktime(&timestamp->tm_format);
	}
}

internal u32
get_unix_timestamp(String date)
{
	Timestamp timestamp = {0};

	char terminated_date[32] = {0};
	i32 max_date_len = sizeof(terminated_date);
	if (date.len > 0 && date.len < max_date_len)
	{
		memcpy(terminated_date, date.str, date.len);

		// NOTE(ariel) Handle dates that specify fractional seconds.
		i32 dot_index = string_find_ch(date, '.');
		if (dot_index > 0 && dot_index < 32)
		{
			terminated_date[dot_index] = 0;
		}

		parse_date(terminated_date, "%a, %d %b %Y %H:%M:%S %Z", &timestamp);
		parse_date(terminated_date, "%Y-%m-%dT%H:%M:%S%z", &timestamp);
		parse_date(terminated_date, "%Y-%m-%dT%H:%M:%S", &timestamp);

		if (!timestamp.unix_format)
		{
			fprintf(stderr, "[DB ERROR] failed to parse date %s\n", terminated_date);
		}
	}

	return timestamp.unix_format;
}

void
db_add_item(sqlite3 *db, String feed_link, RSS_Tree_Node *item_node)
{
	String link = find_link(item_node);

	String title = {0};
	get_content_from_node(item_node, string_literal("title"), title, &title);

	String date = {0};
	get_content_from_node(item_node, string_literal("pubDate"), date, &date);
	get_content_from_node(item_node, string_literal("updated"), date, &date);
	u32 unix_timestamp = get_unix_timestamp(date);

	// NOTE(ariel) 1 in the VALUES(...) expression below indicates the item
	// remains unread.
	sqlite3_stmt *statement = 0;
	String insert_items = string_literal("INSERT OR IGNORE INTO items VALUES(?, ?, ?, 1, ?);");
	sqlite3_prepare_v2(db, insert_items.str, insert_items.len, &statement, 0);
	sqlite3_bind_text(statement, 1, link.str, link.len, SQLITE_STATIC);
	sqlite3_bind_text(statement, 2, title.str, title.len, SQLITE_STATIC);
	sqlite3_bind_int(statement, 3, unix_timestamp);
	u32 feed_id = hash(feed_link);
	sqlite3_bind_int(statement, 4, feed_id);
	i32 status = sqlite3_step(statement);
	sqlite3_finalize(statement);

	confirm_success(db, status, "failed to add item to database");
}

void
db_tag_feed(sqlite3 *db, String tag, String feed_link)
{
	if (tag.len > 0)
	{
		u32 tag_id = hash(tag);
		u32 feed_id = hash(feed_link);

		sqlite3_stmt *statement = 0;
		String insert_tag = string_literal("INSERT OR IGNORE INTO tags VALUES(?, ?);");
		sqlite3_prepare_v2(db, insert_tag.str, insert_tag.len, &statement, 0);
		sqlite3_bind_int(statement, 1, tag_id);
		sqlite3_bind_text(statement, 2, tag.str, tag.len, SQLITE_STATIC);
		i32 status = sqlite3_step(statement);
		sqlite3_finalize(statement);
		confirm_success(db, status, "failed to add tag to database");

		String tag_feed = string_literal("INSERT INTO tags_to_feeds VALUES(?, ?);");
		sqlite3_prepare_v2(db, tag_feed.str, tag_feed.len, &statement, 0);
		sqlite3_bind_int(statement, 1, tag_id);
		sqlite3_bind_int(statement, 2, feed_id);
		status = sqlite3_step(statement);
		sqlite3_finalize(statement);
		confirm_success(db, status, "failed to map tag to feed in database");
	}
}

void
db_del_feed(sqlite3 *db, String feed_link)
{
	u32 feed_id = hash(feed_link);
	sqlite3_stmt *statement = 0;
	String delete_feed = string_literal("DELETE FROM feeds WHERE id = ?");
	sqlite3_prepare_v2(db, delete_feed.str, delete_feed.len, &statement, 0);
	sqlite3_bind_int(statement, 1, feed_id);
	i32 status = sqlite3_step(statement);
	sqlite3_finalize(statement);
	confirm_success(db, status, "failed to delete feed from database");
}

void
db_mark_item_read(sqlite3 *db, String item_link)
{
	sqlite3_stmt *statement = 0;
	String update_item = string_literal("UPDATE items SET unread = 0 WHERE link = ?");
	sqlite3_prepare_v2(db, update_item.str, update_item.len, &statement, 0);
	sqlite3_bind_text(statement, 1, item_link.str, item_link.len, SQLITE_STATIC);
	i32 status = sqlite3_step(statement);
	sqlite3_finalize(statement);
	confirm_success(db, status, "failed to mark item as read in database");
}

void
db_mark_all_read(sqlite3 *db, String feed_link)
{
	u32 feed_id = hash(feed_link);
	sqlite3_stmt *statement = 0;
	String update_items = string_literal("UPDATE items SET unread = 0 WHERE feed = ?");
	sqlite3_prepare_v2(db, update_items.str, update_items.len, &statement, 0);
	sqlite3_bind_int(statement, 1, feed_id);
	i32 status = sqlite3_step(statement);
	sqlite3_finalize(statement);
	confirm_success(db, status, "failed to mark all items of feed as read in database");
}

internal inline String
create_query(String_List tags)
{
#define QUERY_STARTING_LENGTH 166
	String query = {0};

	if (tags.list_size < 1 || tags.list_size > 32)
	{
		return query;
	}

	local_persist char select_feeds[256] =
		"SELECT DISTINCT feeds.link, feeds.title "
		"FROM tags_to_feeds "
		"JOIN tags ON tags.id == tags_to_feeds.tag "
		"JOIN feeds ON feeds.id == tags_to_feeds.feed "
		"WHERE tags.name IN (";
	i32 cursor = QUERY_STARTING_LENGTH;
	for (i32 i = 1; i < tags.list_size; ++i)
	{
		select_feeds[cursor++] = '?';
		select_feeds[cursor++] = ',';
	}
	select_feeds[cursor++] = '?';
	select_feeds[cursor++] = ')';
	select_feeds[cursor++] = ';';

	query.str = select_feeds;
	query.len = cursor;
	return query;
#undef QUERY_STARTING_LENGTH
}

enum
{
	NAME_COLUMN   = 0,
	LINK_COLUMN   = 0,
	TITLE_COLUMN  = 1,
	UNREAD_COLUMN = 2
};

b32
db_filter_feeds_by_tag(sqlite3 *db, String *feed_link, String *feed_title, String_List tags)
{
	b32 feed_exists = false;

	local_persist sqlite3_stmt *statement;
	if (!statement)
	{
		if (!tags.list_size)
		{
			String select_feeds = string_literal("SELECT link, title FROM feeds;");
			sqlite3_prepare_v2(db, select_feeds.str, select_feeds.len, &statement, 0);
		}
		else
		{
			String select_feeds = create_query(tags);
			sqlite3_prepare_v2(db, select_feeds.str, select_feeds.len, &statement, 0);

			String_Node *tag_node = tags.head;
			for (i32 i = 1; tag_node; ++i, tag_node = tag_node->next)
			{
				sqlite3_bind_text(statement, i, tag_node->string.str, tag_node->string.len, SQLITE_STATIC);
			}
		}
	}

	i32 status = sqlite3_step(statement);
	if (status == SQLITE_ROW)
	{
		feed_exists = true;
		feed_link->str = (char *)sqlite3_column_text(statement, LINK_COLUMN);
		feed_link->len = sqlite3_column_bytes(statement, LINK_COLUMN);
		feed_title->str = (char *)sqlite3_column_text(statement, TITLE_COLUMN);
		feed_title->len = sqlite3_column_bytes(statement, TITLE_COLUMN);
	}

	if (!feed_exists)
	{
		sqlite3_finalize(statement);
		statement = 0;
	}

	return feed_exists;
}

b32
db_iterate_feeds(sqlite3 *db, String *feed_link, String *feed_title)
{
	b32 feed_exists = false;

	local_persist sqlite3_stmt *select_statement = 0;
	if (!select_statement)
	{
		String select_feeds = string_literal("SELECT link, title FROM feeds;");
		sqlite3_prepare_v2(db, select_feeds.str, select_feeds.len, &select_statement, 0);
	}

	i32 status = sqlite3_step(select_statement);
	if (status == SQLITE_ROW)
	{
		feed_exists = true;
		feed_link->str = (char *)sqlite3_column_text(select_statement, LINK_COLUMN);
		feed_link->len = sqlite3_column_bytes(select_statement, LINK_COLUMN);
		feed_title->str = (char *)sqlite3_column_text(select_statement, TITLE_COLUMN);
		feed_title->len = sqlite3_column_bytes(select_statement, TITLE_COLUMN);
	}

	if (!feed_exists)
	{
		sqlite3_finalize(select_statement);
		select_statement = 0;
	}

	return feed_exists;
}

b32
db_iterate_items(sqlite3 *db, String feed_link, DB_Item *item)
{
	b32 item_exists = false;

	local_persist sqlite3_stmt *select_statement = 0;
	if (!select_statement)
	{
		String select_items = string_literal(
			"SELECT link, title, unread "
			"FROM items "
			"WHERE feed = ? "
			"ORDER BY datetime(date_last_modified, 'unixepoch') DESC;");
		sqlite3_prepare_v2(db, select_items.str, select_items.len, &select_statement, 0);
		u32 feed_id = hash(feed_link);
		sqlite3_bind_int(select_statement, 1, feed_id);
	}

	i32 status = sqlite3_step(select_statement);
	if (status == SQLITE_ROW)
	{
		item_exists = true;
		item->link.str = (char *)sqlite3_column_text(select_statement, LINK_COLUMN);
		item->link.len = sqlite3_column_bytes(select_statement, LINK_COLUMN);
		item->title.str = (char *)sqlite3_column_text(select_statement, TITLE_COLUMN);
		item->title.len = sqlite3_column_bytes(select_statement, TITLE_COLUMN);
		item->unread = sqlite3_column_int(select_statement, UNREAD_COLUMN);
	}

	if (!item_exists)
	{
		sqlite3_finalize(select_statement);
		select_statement = 0;
	}

	return item_exists;
}

b32
db_iterate_tags(sqlite3 *db, String *tag)
{
	b32 tag_exists = false;

	local_persist sqlite3_stmt *select_statement = 0;
	if (!select_statement)
	{
		String select_tags = string_literal("SELECT name FROM tags;");
		sqlite3_prepare_v2(db, select_tags.str, select_tags.len, &select_statement, 0);
	}

	i32 status = sqlite3_step(select_statement);
	if (status == SQLITE_ROW)
	{
		tag_exists = true;
		tag->str = (char *)sqlite3_column_text(select_statement, NAME_COLUMN);
		tag->len = sqlite3_column_bytes(select_statement, NAME_COLUMN);
	}

	if (!tag_exists)
	{
		sqlite3_finalize(select_statement);
		select_statement = 0;
	}

	return tag_exists;
}

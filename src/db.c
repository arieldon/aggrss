#include <sqlite3.h>

#include "arena.h"
#include "base.h"
#include "db.h"
#include "err.h"
#include "rss.h"
#include "str.h"

// TODO(ariel) Check for errors after SQLite calls.

global Arena db_arena;

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

void
db_init(sqlite3 **db)
{
	assert(sqlite3_threadsafe());
	arena_init(&db_arena);

	i32 error = sqlite3_open("./feeds.db", db);
	if (error)
	{
		err_exit("failed to open feeds file");
	}

	char *errmsg = 0;
	char *enable_foreign_keys = "PRAGMA foreign_keys = ON;";
	error = sqlite3_exec(*db, enable_foreign_keys, 0, 0, &errmsg);
	if (error)
	{
		err_exit("[DB ERROR] %s", errmsg);
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
		err_exit("[DB ERROR] %s", errmsg);
	}

	// TODO(ariel) Include date last updated in the table as well? Then sort by
	// date last updated.
	errmsg = 0;
	char *create_items_table =
		"CREATE TABLE IF NOT EXISTS "
			"items("
				"link TEXT PRIMARY KEY,"
				"title TEXT,"
				"description TEXT,"
				"unread BOOLEAN,"
				"feed REFERENCES feeds(id) ON DELETE CASCADE);";
	error = sqlite3_exec(*db, create_items_table, 0, 0, &errmsg);
	if (error)
	{
		err_exit("[DB ERROR] %s", errmsg);
	}

	errmsg = 0;
	char *create_tags_table =
		"CREATE TABLE IF NOT EXISTS "
			"tags("
				"id INTEGER PRIMARY KEY,"
				"name TEXT UNIQUE);";
	error = sqlite3_exec(*db, create_tags_table, 0, 0, &errmsg);
	if (error)
	{
		err_exit("[DB ERROR] %s", errmsg);
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
		err_exit("[DB ERROR] %s", errmsg);
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
	assert(status == SQLITE_OK);
}

void
db_add_feed(sqlite3 *db, String feed_link, String feed_title)
{
	sqlite3_stmt *statement = 0;
	String insert_feed = string_literal(
		"INSERT INTO feeds VALUES(?, ?, ?) ON CONFLICT(link) DO UPDATE SET title=excluded.title;");
	sqlite3_prepare_v2(db, insert_feed.str, insert_feed.len, &statement, 0);
	u32 feed_id = hash(feed_link);
	sqlite3_bind_int(statement, 1, feed_id);
	sqlite3_bind_text(statement, 2, feed_link.str, feed_link.len, SQLITE_STATIC);
	sqlite3_bind_text(statement, 3, feed_title.str, feed_title.len, SQLITE_STATIC);
	sqlite3_step(statement);
	sqlite3_finalize(statement);
}

void
db_add_item(sqlite3 *db, String feed_link, RSS_Tree_Node *item_node)
{
	String link = find_link(item_node);

	String title = {0};
	RSS_Tree_Node *title_node = find_item_title(item_node);
	if (title_node)
	{
		title = title_node->content;
	}

	String description = {0};
	RSS_Tree_Node *description_node = find_item_child_node(item_node, string_literal("description"));
	if (description_node)
	{
		description = description_node->content;
	}

	// NOTE(ariel) 1 in the VALUES(...) expression below indicates the item
	// remains unread.
	sqlite3_stmt *statement = 0;
	String insert_items = string_literal("INSERT OR IGNORE INTO items VALUES(?, ?, ?, 1, ?);");
	sqlite3_prepare_v2(db, insert_items.str, insert_items.len, &statement, 0);
	sqlite3_bind_text(statement, 1, link.str, link.len, SQLITE_STATIC);
	sqlite3_bind_text(statement, 2, title.str, title.len, SQLITE_STATIC);
	sqlite3_bind_text(statement, 3, description.str, description.len, SQLITE_STATIC);
	u32 feed_id = hash(feed_link);
	sqlite3_bind_int(statement, 4, feed_id);
	sqlite3_step(statement);
	sqlite3_finalize(statement);
}

void
db_tag_feed(sqlite3 *db, String tag, String feed_link)
{
	u32 tag_id = hash(tag);
	u32 feed_id = hash(feed_link);

	sqlite3_stmt *statement = 0;
	String insert_tag = string_literal("INSERT OR IGNORE INTO tags VALUES(?, ?);");
	sqlite3_prepare_v2(db, insert_tag.str, insert_tag.len, &statement, 0);
	sqlite3_bind_int(statement, 1, tag_id);
	sqlite3_bind_text(statement, 2, tag.str, tag.len, SQLITE_STATIC);
	sqlite3_step(statement);
	sqlite3_finalize(statement);

	String tag_feed = string_literal("INSERT INTO tags_to_feeds VALUES(?, ?);");
	sqlite3_prepare_v2(db, tag_feed.str, tag_feed.len, &statement, 0);
	sqlite3_bind_int(statement, 1, tag_id);
	sqlite3_bind_int(statement, 2, feed_id);
	sqlite3_step(statement);
	sqlite3_finalize(statement);
}

void
db_del_feed(sqlite3 *db, String feed_link)
{
	u32 feed_id = hash(feed_link);
	sqlite3_stmt *statement = 0;
	String delete_feed = string_literal("DELETE FROM feeds WHERE id = ?");
	sqlite3_prepare_v2(db, delete_feed.str, delete_feed.len, &statement, 0);
	sqlite3_bind_int(statement, 1, feed_id);
	sqlite3_step(statement);
	sqlite3_finalize(statement);
}

void
db_mark_item_read(sqlite3 *db, String item_link)
{
	sqlite3_stmt *statement = 0;
	String update_item = string_literal("UPDATE items SET unread = 0 WHERE link = ?");
	sqlite3_prepare_v2(db, update_item.str, update_item.len, &statement, 0);
	sqlite3_bind_text(statement, 1, item_link.str, item_link.len, SQLITE_STATIC);
	sqlite3_step(statement);
	sqlite3_finalize(statement);
}

void
db_mark_all_read(sqlite3 *db, String feed_link)
{
	sqlite3_stmt *statement = 0;
	String update_items = string_literal("UPDATE items SET unread = 0 WHERE feed = ?");
	sqlite3_prepare_v2(db, update_items.str, update_items.len, &statement, 0);
	sqlite3_bind_text(statement, 1, feed_link.str, feed_link.len, SQLITE_STATIC);
	sqlite3_step(statement);
	sqlite3_finalize(statement);
}

internal inline String
create_query(String_List tags)
{
	String select_feeds = string_literal(
		"SELECT DISTINCT feeds.* "
		"FROM tags_to_feeds "
		"JOIN tags ON tags.id == tags_to_feeds.tag "
		"JOIN feeds ON feeds.id == tags_to_feeds.feed "
		"WHERE tags.name IN ");

	String_List question_mark_list = {0};
	String question_mark = string_literal("?");
	for (i32 i = 0; i < tags.list_size; ++i)
	{
		string_list_push_string(&db_arena, &question_mark_list, question_mark);
	}
	String question_marks = string_list_join(&db_arena, question_mark_list, ',');

	String_List parameter_list = {0};
	string_list_push_string(&db_arena, &parameter_list, string_literal("("));
	string_list_push_string(&db_arena, &parameter_list, question_marks);
	string_list_push_string(&db_arena, &parameter_list, string_literal(");"));
	String parameters = string_list_concat(&db_arena, parameter_list);

	String_List query_list = {0};
	string_list_push_string(&db_arena, &query_list, select_feeds);
	string_list_push_string(&db_arena, &query_list, parameters);
	String query = string_list_concat(&db_arena, query_list);

	return query;
}

b32
db_filter_feeds_by_tag(sqlite3 *db, String *feed_link, String *feed_title, String_List tags)
{
	b32 feed_exists = false;

	local_persist sqlite3_stmt *statement;
	local_persist Arena_Checkpoint checkpoint;
	if (!statement)
	{
		checkpoint = arena_checkpoint_set(&db_arena);

		if (!tags.list_size)
		{
			String select_feeds = string_literal("SELECT * FROM feeds;");
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
		feed_link->str = (char *)sqlite3_column_text(statement, 1);
		feed_link->len = sqlite3_column_bytes(statement, 1);
		feed_title->str = (char *)sqlite3_column_text(statement, 2);
		feed_title->len = sqlite3_column_bytes(statement, 2);
	}

	if (!feed_exists)
	{
		sqlite3_finalize(statement);
		statement = 0;

		arena_checkpoint_restore(checkpoint);
		MEM_ZERO_STRUCT(&checkpoint);
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
		String select_feeds = string_literal("SELECT * FROM feeds;");
		sqlite3_prepare_v2(db, select_feeds.str, select_feeds.len, &select_statement, 0);
	}

	i32 status = sqlite3_step(select_statement);
	if (status == SQLITE_ROW)
	{
		feed_exists = true;
		feed_link->str = (char *)sqlite3_column_text(select_statement, 1);
		feed_link->len = sqlite3_column_bytes(select_statement, 1);
		feed_title->str = (char *)sqlite3_column_text(select_statement, 2);
		feed_title->len = sqlite3_column_bytes(select_statement, 2);
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
		String select_items = string_literal("SELECT * FROM items WHERE feed = ?;");
		sqlite3_prepare_v2(db, select_items.str, select_items.len, &select_statement, 0);
		u32 feed_id = hash(feed_link);
		sqlite3_bind_int(select_statement, 1, feed_id);
	}

	i32 status = sqlite3_step(select_statement);
	if (status == SQLITE_ROW)
	{
		item_exists = true;
		item->link.str = (char *)sqlite3_column_text(select_statement, 0);
		item->link.len = sqlite3_column_bytes(select_statement, 0);
		item->title.str = (char *)sqlite3_column_text(select_statement, 1);
		item->title.len = sqlite3_column_bytes(select_statement, 1);
		item->unread = sqlite3_column_int(select_statement, 3);
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
		tag->str = (char *)sqlite3_column_text(select_statement, 0);
		tag->len = sqlite3_column_bytes(select_statement, 0);
	}

	if (!tag_exists)
	{
		sqlite3_finalize(select_statement);
		select_statement = 0;
	}

	return tag_exists;
}

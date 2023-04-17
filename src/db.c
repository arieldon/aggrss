#include <sqlite3.h>

#include "arena.h"
#include "base.h"
#include "db.h"
#include "err.h"
#include "rss.h"
#include "str.h"

// TODO(ariel) Check for errors after SQLite calls.

void
db_init(sqlite3 **db)
{
	assert(sqlite3_threadsafe());

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
				"link TEXT PRIMARY KEY,"
				"title TEXT);";
	error = sqlite3_exec(*db, create_feeds_table, 0, 0, &errmsg);
	if (error)
	{
		err_exit("[DB ERROR] %s", errmsg);
	}

	// TODO(ariel) Include date last updated in the table as well?
	errmsg = 0;
	char *create_items_table =
		"CREATE TABLE IF NOT EXISTS "
			"items("
				"link TEXT PRIMARY KEY,"
				"title TEXT,"
				"description TEXT,"
				"unread BOOLEAN,"
				"feed REFERENCES feeds(link) ON DELETE CASCADE);";
	error = sqlite3_exec(*db, create_items_table, 0, 0, &errmsg);
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
		"INSERT INTO feeds VALUES(?, ?) ON CONFLICT(link) DO UPDATE SET title=excluded.title;");
	sqlite3_prepare_v2(db, insert_feed.str, insert_feed.len, &statement, 0);
	sqlite3_bind_text(statement, 1, feed_link.str, feed_link.len, SQLITE_STATIC);
	sqlite3_bind_text(statement, 2, feed_title.str, feed_title.len, SQLITE_STATIC);
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
	sqlite3_bind_text(statement, 4, feed_link.str, feed_link.len, SQLITE_STATIC);
	sqlite3_step(statement);
	sqlite3_finalize(statement);
}

void
db_del_feed(sqlite3 *db, String feed_link)
{
	sqlite3_stmt *statement = 0;
	String delete_feed = string_literal("DELETE FROM feeds WHERE link = ?");
	sqlite3_prepare_v2(db, delete_feed.str, delete_feed.len, &statement, 0);
	sqlite3_bind_text(statement, 1, feed_link.str, feed_link.len, SQLITE_STATIC);
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

enum
{
	LINK_COLUMN   = 0,
	TITLE_COLUMN  = 1,
	UNREAD_COLUMN = 3,
};

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
		String select_items = string_literal("SELECT * FROM items WHERE feed = ?;");
		sqlite3_prepare_v2(db, select_items.str, select_items.len, &select_statement, 0);
		sqlite3_bind_text(select_statement, 1, feed_link.str, feed_link.len, SQLITE_STATIC);
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

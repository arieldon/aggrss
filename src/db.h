#ifndef DB_H
#define DB_H

#include <sqlite3.h>

#include "base.h"
#include "rss.h"

typedef struct DB_Item DB_Item;
struct DB_Item
{
	String link;
	String title;
	b32 unread;
};

void db_init(sqlite3 **db);
void db_free(sqlite3 *db);

i32 db_count_rows(sqlite3 *db);

void db_add_feed(sqlite3 *db, String feed_link, String feed_title);
void db_add_item(sqlite3 *db, String feed_link, RSS_Tree_Node *item_node);

void db_del_feed(sqlite3 *db, String feed_link);

void db_mark_item_read(sqlite3 *db, String item_link);

// NOTE(ariel) The following functions assume one and only one thread calls
// them until exhaustion.
b32 db_iterate_feeds(sqlite3 *db, String *feed_link, String *feed_title);
b32 db_iterate_items(sqlite3 *db, String feed_link, DB_Item *item);

#endif

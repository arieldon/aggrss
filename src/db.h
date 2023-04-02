#ifndef DB_H
#define DB_H

#include <sqlite3.h>

#include "base.h"
#include "rss.h"

void db_init(sqlite3 **db);
void db_free(sqlite3 *db);

i32 db_count_rows(sqlite3 *db);

void db_add_feed(sqlite3 *db, String feed_link, String feed_title);
void db_add_item(sqlite3 *db, String feed_link, RSS_Tree_Node *item_node);

// NOTE(ariel) The following functions assume one and only one thread calls
// them until exhaustion.
b32 db_iterate_feeds(sqlite3 *db, String *feed_link, String *feed_title);
b32 db_iterate_items(sqlite3 *db, String feed_link, String *item_link, String *item_title);

#endif

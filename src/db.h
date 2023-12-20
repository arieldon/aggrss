#ifndef DB_H
#define DB_H

typedef struct DB_Item DB_Item;
struct DB_Item
{
	string link;
	string title;
	b32 unread;
};

static void db_init(sqlite3 **db);
static void db_free(sqlite3 *db);

static s32 db_count_rows(sqlite3 *db);

static void db_add_feed(sqlite3 *db, string feed_link, string feed_title);
static void db_add_or_update_feed(sqlite3 *db, string feed_link, string feed_title);
static void db_add_item(sqlite3 *db, string feed_link, RSS_Tree_Node *item_node);
static void db_tag_feed(sqlite3 *db, string tag, string feed_title);

static void db_del_feed(sqlite3 *db, string feed_link);

static void db_mark_item_read(sqlite3 *db, string item_link);
static void db_mark_all_read(sqlite3 *db, string feed_link);

// NOTE(ariel) The following functions assume one and only one thread calls
// them until exhaustion.
static b32 db_filter_feeds_by_tag(sqlite3 *db, string *feed_link, string *feed_title, String_List tags);
static b32 db_iterate_feeds(sqlite3 *db, string *feed_link, string *feed_title);
static b32 db_iterate_items(sqlite3 *db, string feed_link, DB_Item *item);
static b32 db_iterate_tags(sqlite3 *db, string *tag);

#endif

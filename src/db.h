#ifndef DB_H
#define DB_H

#ifdef USE_SQLITE
#define Database sqlite3
#else
#define Database database

typedef struct database database;
struct database
{
};
#endif

typedef struct DB_Item DB_Item;
struct DB_Item
{
	String link;
	String title;
	b32 unread;
};

static void db_init(Database **db);
static void db_free(Database *db);

static s32 db_count_rows(Database *db);

static void db_add_feed(Database *db, String feed_link, String feed_title);
static void db_add_or_update_feed(Database *db, String feed_link, String feed_title);
static void db_add_item(Database *db, String feed_link, RSS_Tree_Node *item_node);
static void db_tag_feed(Database *db, String tag, String feed_title);

static void db_del_feed(Database *db, String feed_link);

static void db_mark_item_read(Database *db, String item_link);
static void db_mark_all_read(Database *db, String feed_link);

// NOTE(ariel) The following functions assume one and only one thread calls
// them until exhaustion.
static b32 db_filter_feeds_by_tag(Database *db, String *feed_link, String *feed_title, String_List tags);
static b32 db_iterate_feeds(Database *db, String *feed_link, String *feed_title);
static b32 db_iterate_items(Database *db, String feed_link, DB_Item *item);
static b32 db_iterate_tags(Database *db, String *tag);

#endif

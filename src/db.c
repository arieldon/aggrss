static u32
db_hash(String s)
{
}

static inline void
confirm_success(Database *db, s32 status_code, char *error_message)
{
}

static void
db_init(Database **db)
{
}

static s32
db_count_rows(Database *db)
{
}

static void
db_free(Database *db)
{
}

static void
db_add_feed(Database *db, String feed_link, String feed_title)
{
}

static void
db_add_or_update_feed(Database *db, String feed_link, String feed_title)
{
}

static inline void
get_content_from_node(RSS_Tree_Node *item_node, String term, String default_value, String *value)
{
}

static u64
get_unix_timestamp(String feed_link, String date_time)
{
}

static void
db_add_item(Database *db, String feed_link, RSS_Tree_Node *item_node)
{
}

static void
db_tag_feed(Database *db, String tag, String feed_link)
{
}

static void
db_del_feed(Database *db, String feed_link)
{
}

static void
db_mark_item_read(Database *db, String item_link)
{
}

static void
db_mark_all_read(Database *db, String feed_link)
{
}

static inline String
create_query(String_List tags)
{
}

static b32
db_filter_feeds_by_tag(Database *db, String *feed_link, String *feed_title, String_List tags)
{
}

static b32
db_iterate_feeds(Database *db, String *feed_link, String *feed_title)
{
}

static b32
db_iterate_items(Database *db, String feed_link, DB_Item *item)
{
}

static b32
db_iterate_tags(Database *db, String *tag)
{
}

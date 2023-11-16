#ifndef DB_H
#define DB_H

#ifdef USE_SQLITE
#define Database sqlite3
#else

// TODO(ariel) After completeting transition match case of this variable name
// to others.
#define Database database

enum
{
	DB_HEADER_SIZE = 0x80,
	DB_PAGE_SIZE = 0x1000, // TODO(ariel) Query this value dynamically from the system.
	DB_PAGE_COUNT_IN_CACHE = 0x40,
};

// TODO(ariel) I think I will need locks here?
typedef struct page page;
struct page
{
	s32 MoreRecentlyUsedPage; // NOTE(ariel) Maintain an internal free list of pages.
	b32 Dirty;
	u8 Data[DB_PAGE_SIZE];
};

typedef struct page_cache page_cache;
struct page_cache
{
	// NOTE(ariel) Use standard Unix file IO because I don't want to deal with
	// buffered C file IO. Maybe this is a bad decision.
	int DatabaseFileDescriptor;

	s32 PageCountInMemory;
	s16 PageSize;

	s32 LeastRecentlyUsedPageNumber; // NOTE(ariel) Treat this as first page of list.
	s32 MostRecentlyUsedPageNumber; // NOTE(ariel) Treat this as last page of list.
	s32 CacheToFilePageNumberMap[DB_PAGE_COUNT_IN_CACHE];
	page Pages[DB_PAGE_COUNT_IN_CACHE];
};

typedef enum node_type node_type;
enum node_type
{
	DB_NODE_TYPE_INTERNAL = 0,
	DB_NODE_TYPE_LEAF = 1,
} __attribute__((packed));
StaticAssert(sizeof(node_type) == 1);

// NOTE(ariel) SQLite's file format heavily inspires the version of this data
// structure both in memory and on disk [0].
// [0] https://www.sqlite.org/fileformat.html
typedef struct btree_node btree_node;
struct btree_node
{
	// TODO(ariel) Include some sort of dirty bit? How do I know when to flush a
	// node and clear it from the pool to allow new nodes/pages to take its
	// place?
	// TODO(ariel) Do I need to include some sort of free list in the node
	// itself?

	// NOTE(ariel) Index to page in list of pages maintained in file on disk.
	s32 PageNumberInFile;

	// NOTE(ariel) Index to page in list of pages maintained by page cache.
	s32 PageNumberInCache;

	// NOTE(arie) Store index of page in file to which to traverse if search
	// query larger than all keys on page. Only internal nodes, i.e. not leaf
	// nodes, track this value.
	s32 RightPageNumber;

	// NOTE(ariel) Store index of first free block in page or zero if no free
	// blocks in page.
	s16 OffsetToFirstFreeBlock;

	// NOTE(ariel) Store total number of cells in page.
	s16 CellCount;

	// NOTE(ariel) Store number of fragmented bytes in space dedicated to cells
	// in page.
	s8 FragmentedBytesCount;

	// NOTE(ariel) Store index to byte where cells start. In other words, store
	// an offset to the cell with the lowest address on the page.
	s16 OffsetToCells;

	// NOTE(ariel) Indicate B+Tree page type.
	node_type Type;
};

typedef struct database database;
struct database
{
	Arena arena;

	// TODO(ariel) Include read and write locks of some sort? What are some other
	// ways to synchronize access to DB?
	s16 FileFormatVersion;
	s32 TotalPageCountInFile;

	// NOTE(ariel) The database does not dedicate a root node to items of feeds
	// because it only accesses those items from a feed.
	btree_node FeedsRoot;
	btree_node TagsRoot;
	page_cache PageCache;
	u8 Header[DB_PAGE_SIZE];
};
#endif

typedef union DB_Item DB_Item;
union DB_Item
{
	struct
	{
		String link;
		String title;
		b32 unread;
	};
	struct
	{
		String Link;
		String Title;
		b32 Unread;
	};
};

static void db_init(Database **db);
static void db_free(Database *db);

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

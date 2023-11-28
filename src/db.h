#ifndef DB_H
#define DB_H

enum
{
	DB_HEADER_SIZE = 0x80,
	DB_PAGE_SIZE = 0x1000,
	DB_PAGE_COUNT_IN_CACHE = 0x20,
};

typedef struct db_page db_page;
struct db_page
{
	u8 Data[DB_PAGE_SIZE];
};

typedef struct db_page_cache db_page_cache;
struct db_page_cache
{
	// NOTE(ariel) Use standard Unix file IO because I don't want to deal with
	// buffered C file IO. Maybe this is a bad decision.
	int FileDescriptor;
	s16 PageSize;

	// NOTE(ariel) Byte clear if page clear. Byte set if page in use.
	// TODO(ariel) Try a different encoding scheme actually. Let the page be
	// negative if its in use. Then positive pages are more recently used. This
	// way we gain both benefits of maintaining this list separately but also the
	// LRU nature that allows us to keep more "useful" pages in cache.
	s8 WriteLock[DB_PAGE_COUNT_IN_CACHE];
	s32 CacheToFilePageNumberMap[DB_PAGE_COUNT_IN_CACHE];
	db_page Pages[DB_PAGE_COUNT_IN_CACHE];
};

typedef enum db_node_type db_node_type;
enum db_node_type
{
	DB_NODE_TYPE_INTERNAL = 0,
	DB_NODE_TYPE_LEAF = 1,
} __attribute__((packed));
StaticAssert(sizeof(db_node_type) == 1);

// NOTE(ariel) SQLite's file format heavily inspires the version of this data
// structure both in memory and on disk [0].
// [0] https://www.sqlite.org/fileformat.html
typedef struct db_btree_node db_btree_node;
struct db_btree_node
{
	// NOTE(ariel) Index to page in list of pages maintained by page cache.
	s32 PageNumberInCache;


	/* NOTE(ariel) The database tracks fields above only in memory. */
	/* NOTE(ariel) The database serializes fields below to pages on disk. */


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

	// NOTE(ariel) Indicate B+Tree page type.
	db_node_type Type;
};

typedef struct db_cell db_cell;
struct db_cell
{
	u32 ID;
	union
	{
		struct // NOTE(ariel) Leaf nodes store links and titles.
		{
			String Link;
			String Title;
			union
			{
				s32 ItemsPage; // NOTE(ariel) Feed cells store page number to items.
				b32 Unread;    // NOTE(ariel) Item cells store (un)read attribute.
			};
		};
		struct // NOTE(ariel) Internal nodes store pointers to children.
		{
			s32 ChildPage;
		};
	};
};

typedef struct db_item_cell db_item_cell;
struct db_item_cell
{
	String Link;
	String Title;
	b32 Unread;
};

typedef struct database database;
struct database
{
	Arena arena;

	s16 FileFormatVersion;
	s32 TotalPageCountInFile;

#ifdef DEBUG
	s32 SplitCount;
#endif

	// NOTE(ariel) The database does not dedicate a root node to items of feeds
	// because it only accesses those items from a feed.
	db_page_cache PageCache;
	u8 Header[DB_PAGE_SIZE];
};

static void DB_Open(void);
static void DB_Close(void);

// NOTE(ariel) These procedures write to the datbase. Only the main thread may
// call these.
static void DB_AddFeed(String FeedLink);
static void DB_AddItems(String FeedLink, RSS_Tree_Node *ItemNode);
static void DB_DeleteFeed(String FeedLink);

// NOTE(ariel) These procedure updates already written fields in the database.
static void DB_UpdateFeedTitle(String FeedLink, String FeedTitle);
static void DB_MarkItemRead(String FeedLink, String ItemLink);
static void DB_MarkAllFeedItemsRead(String FeedLink);

typedef struct db_feed db_feed;
struct db_feed
{
	db_feed *Next;
	String Link;
	String Title;
};

typedef struct db_item db_item;
struct db_item
{
	db_item *Next;
	String Link;
	String Title;
	b32 Unread;
};

typedef struct db_feed_list db_feed_list;
struct db_feed_list
{
	db_feed *First;
	db_feed *Last;
};

typedef struct db_item_list db_item_list;
struct db_item_list
{
	db_item *First;
	db_item *Last;
};

// NOTE(ariel) These procedures strictly read from the database.
static db_feed_list DB_GetAllFeeds(Arena *PersistentArena);
static db_item_list DB_GetAllFeedItems(String FeedLink);

#endif

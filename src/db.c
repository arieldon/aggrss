/* ---
 * Serialization & Deserialization
 * ---
 */

static void
s8Serialize(u8 *Address, u8 Value)
{
	Address[0] = Value;
}

static void
s16Serialize(u8 *Address, s16 Value)
{
	Address[0] = (u8)(Value >> 0x00);
	Address[1] = (u8)(Value >> 0x08);
}

static void
s32Serialize(u8 *Address, s32 Value)
{
	Address[0] = (u8)(Value >> 0x00);
	Address[1] = (u8)(Value >> 0x08);
	Address[2] = (u8)(Value >> 0x10);
	Address[3] = (u8)(Value >> 0x18);
}

static s8
s8Deserialize(u8 *Address)
{
	s8 Value = Address[0];
	return Value;
}

static s16
s16Deserialize(u8 *Address)
{
	s16 Value = 0;
	Value |= (s16)(Address[0] << 0x00);
	Value |= (s16)(Address[1] << 0x08);
	return Value;
}

static s32
s32Deserialize(u8 *Address)
{
	s32 Value = 0;
	Value |= (s32)(Address[0] << 0x00);
	Value |= (s32)(Address[1] << 0x08);
	Value |= (s32)(Address[2] << 0x10);
	Value |= (s32)(Address[3] << 0x18);
	return Value;
}


/* ---
 * Page Cache
 * ---
 */

static inline void
UpdateMostRecentlyUsedPage(page_cache *PageCache, s32 PageNumber)
{
	PageCache->Pages[PageCache->MostRecentlyUsedPageNumber].MoreRecentlyUsedPage = PageNumber;
	PageCache->Pages[PageNumber].MoreRecentlyUsedPage = -1;
	PageCache->MostRecentlyUsedPageNumber = PageNumber;
}

static void
WritePageToDisk(page_cache *PageCache, s32 PageNumberInCache)
{
	s32 PageNumberInFile = PageCache->CacheToFilePageNumberMap[PageNumberInCache];
	AssertAlways(PageNumberInFile > -1);

	s32 Offset = PageCache->PageSize * PageNumberInFile;
	ssize BytesWrittenCount = pwrite(
		PageCache->DatabaseFileDescriptor, PageCache->Pages[PageNumberInCache].Data,
		PageCache->PageSize, Offset);
	AssertAlways(BytesWrittenCount == PageCache->PageSize);

	// TODO(ariel) How does Linux VFS implement fsync() for ext4?
	s32 SyncStatus = fsync(PageCache->DatabaseFileDescriptor);
	AssertAlways(SyncStatus == 0);

	PageCache->Pages[PageNumberInCache].Dirty = false;
	UpdateMostRecentlyUsedPage(PageCache, PageNumberInCache);
}

static s32
AllocatePage(page_cache *PageCache)
{
	s32 PageNumber = PageCache->LeastRecentlyUsedPageNumber;
	PageCache->LeastRecentlyUsedPageNumber = PageCache->Pages[PageNumber].MoreRecentlyUsedPage;

	UpdateMostRecentlyUsedPage(PageCache, PageNumber);

	// TODO(ariel) Flush more than one page at a time. Reference Chapter 8 The
	// Disk Block Cache in Practical File System Design for more details.
	if(PageCache->Pages[PageNumber].Dirty)
	{
		WritePageToDisk(PageCache, PageNumber);
		memset(PageCache->Pages[PageNumber].Data, 0, PageCache->PageSize);
		PageCache->Pages[PageNumber].Dirty = false;
		PageCache->CacheToFilePageNumberMap[PageNumber] = -1;
	}

	PageCache->PageCountInMemory += 1;
	AssertAlways(PageNumber > -1);
	return PageNumber;
}

static s32
ReadPageFromDisk(page_cache *PageCache, s32 PageNumberInFile)
{
	s32 PageNumberInCache = AllocatePage(PageCache);
	s32 Offset = PageCache->PageSize * PageNumberInFile;
	PageCache->CacheToFilePageNumberMap[PageNumberInCache] = PageNumberInFile;
	ssize BytesReadCount = pread(
		PageCache->DatabaseFileDescriptor, PageCache->Pages[PageNumberInCache].Data,
		PageCache->PageSize, Offset);
	AssertAlways(BytesReadCount == PageCache->PageSize); // TODO(ariel) How can I make this more resilient?
	return PageNumberInCache;
}

static void
InitializePages(page_cache *PageCache)
{
	PageCache->LeastRecentlyUsedPageNumber = 0;
	PageCache->MostRecentlyUsedPageNumber = DB_PAGE_COUNT_IN_CACHE - 1;

	for(s32 Index = PageCache->LeastRecentlyUsedPageNumber; Index < PageCache->MostRecentlyUsedPageNumber; Index += 1)
	{
		PageCache->Pages[Index].MoreRecentlyUsedPage = Index + 1;
		PageCache->Pages[Index].Dirty = false;
		memset(PageCache->Pages[Index].Data, 0, DB_PAGE_SIZE);
	}

	for(s32 Index = PageCache->LeastRecentlyUsedPageNumber; Index < PageCache->MostRecentlyUsedPageNumber; Index += 1)
	{
		PageCache->CacheToFilePageNumberMap[Index] = -1;
	}
}


/* ---
 * B+Tree
 * ---
 */

enum
{
	// NOTE(ariel) Internal nodes must allocate space for `RightPageNumber`.
	DB_OFFSET_TO_CELLS_FOR_INTERNAL_NODES = 12,
	DB_OFFSET_TO_CELLS_FOR_LEAF_NODES = 8,
};

static btree_node
ReadNodeFromDisk(Database *DB, s32 PageNumberInFile)
{
	btree_node DatabaseNode;

	DatabaseNode.PageNumberInCache = ReadPageFromDisk(&DB->PageCache, PageNumberInFile);
	page *Page = &DB->PageCache.Pages[DatabaseNode.PageNumberInCache];

	DatabaseNode.Type = s8Deserialize(&Page->Data[0]); AssertAlways(DatabaseNode.Type == DB_NODE_TYPE_INTERNAL || DatabaseNode.Type == DB_NODE_TYPE_LEAF);
	DatabaseNode.OffsetToFirstFreeBlock = s16Deserialize(&Page->Data[1]);
	DatabaseNode.CellCount = s16Deserialize(&Page->Data[3]);
	DatabaseNode.OffsetToCells = s16Deserialize(&Page->Data[5]);
	DatabaseNode.FragmentedBytesCount = s8Deserialize(&Page->Data[7]);
	DatabaseNode.RightPageNumber = DatabaseNode.Type == DB_NODE_TYPE_INTERNAL ? s32Deserialize(&Page->Data[8]) : -1;

	return DatabaseNode;
}

static void
WriteNodeToDisk(Database *DB, btree_node DatabaseNode)
{
	page *Page = &DB->PageCache.Pages[DatabaseNode.PageNumberInCache];

	s8Serialize(&Page->Data[0], DatabaseNode.Type);
	s16Serialize(&Page->Data[1], DatabaseNode.OffsetToFirstFreeBlock);
	s16Serialize(&Page->Data[3], DatabaseNode.CellCount);
	s16Serialize(&Page->Data[5], DatabaseNode.OffsetToCells);
	s8Serialize(&Page->Data[7], DatabaseNode.FragmentedBytesCount);
	if(DatabaseNode.Type == DB_NODE_TYPE_INTERNAL)
	{
		Assert(DatabaseNode.RightPageNumber != -1);
		s32Serialize(&Page->Data[8], DatabaseNode.RightPageNumber);
	}
	else
	{
		Assert(DatabaseNode.RightPageNumber == -1);
	}

	WritePageToDisk(&DB->PageCache, DatabaseNode.PageNumberInCache);
}

static btree_node
InitializeNewNode(Database *DB, node_type NodeType)
{
	btree_node DatabaseNode;
	DatabaseNode.PageNumberInCache = AllocatePage(&DB->PageCache);
	DatabaseNode.CellCount = 0;
	DatabaseNode.OffsetToCells = DB->PageCache.PageSize;
	DatabaseNode.Type = NodeType;

	if(NodeType == DB_NODE_TYPE_INTERNAL)
	{
		DatabaseNode.RightPageNumber = 0;
		DatabaseNode.OffsetToFirstFreeBlock = DB_OFFSET_TO_CELLS_FOR_INTERNAL_NODES;
	}
	else if(NodeType == DB_NODE_TYPE_LEAF)
	{
		DatabaseNode.RightPageNumber = -1;
		DatabaseNode.OffsetToFirstFreeBlock = DB_OFFSET_TO_CELLS_FOR_LEAF_NODES;
	}
	else
	{
		Assert(!"unreachable");
	}

	DB->PageCache.CacheToFilePageNumberMap[DatabaseNode.PageNumberInCache] = DB->TotalPageCountInFile;
	DB->TotalPageCountInFile += 1;

	WriteNodeToDisk(DB, DatabaseNode);
	return DatabaseNode;
}


/* ---
 * Database
 * ---
 */

// TODO(ariel) Update interface of initialization procedure. Match the SQLite
// version for now though.
static void
db_init(Database **db)
{
	enum { HEADER_PAGE_NUMBER_IN_FILE = 0 };

	static char HeaderMagicSequence[] = "aggrss db format";
	static database DB;

	// TODO(ariel) Use environment variables XDG to get this path to respect
	// user's existing configuration.
#define DATABASE_FILE_PATH CONFIG_DIRECTORY_PATH "/aggrss.db"
	int OpenFlags = O_RDWR | O_CREAT;
	mode_t Mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
	DB.PageCache.DatabaseFileDescriptor = open(DATABASE_FILE_PATH, OpenFlags, Mode);
	AssertAlways(DB.PageCache.DatabaseFileDescriptor != -1);
#undef DATABASE_FILE_PATH

	// NOTE(ariel) Initialize free list for page cache.
	InitializePages(&DB.PageCache);

	ssize BytesReadCount = read(DB.PageCache.DatabaseFileDescriptor, DB.Header, DB_HEADER_SIZE);
	if(BytesReadCount == DB_HEADER_SIZE)
	{
		b32 HeaderIsConsistent = memcmp(DB.Header, HeaderMagicSequence, sizeof(HeaderMagicSequence)) == 0;
		AssertAlways(HeaderIsConsistent);

		DB.FileFormatVersion = s16Deserialize(&DB.Header[16]);
		DB.PageCache.PageSize = s16Deserialize(&DB.Header[18]); Assert(DB.PageCache.PageSize == DB_PAGE_SIZE);
		DB.TotalPageCountInFile = s32Deserialize(&DB.Header[20]);
		s32 FeedsRootPageNumberInFile = s32Deserialize(&DB.Header[24]); Assert(FeedsRootPageNumberInFile == 1);
		s32 TagsRootPageNumberInFile = s32Deserialize(&DB.Header[28]); Assert(TagsRootPageNumberInFile == 2);

		// NOTE(ariel) Use this as another check for consistency.
		u8 RemainingBytesShouldBeZero = 0;
		for(s32 Index = 32; Index < DB_HEADER_SIZE; Index += 1)
		{
			RemainingBytesShouldBeZero |= DB.Header[Index];
		}
		AssertAlways(RemainingBytesShouldBeZero == 0);

		// ReadPageFromDisk(&DB.PageCache, HEADER_PAGE_NUMBER_IN_FILE);
		DB.FeedsRoot = ReadNodeFromDisk(&DB, FeedsRootPageNumberInFile);
		DB.TagsRoot = ReadNodeFromDisk(&DB, TagsRootPageNumberInFile);
	}
	else if(BytesReadCount == 0)
	{
		// TODO(ariel) Store head (or root) of free list?
		DB.FileFormatVersion = 0;
		DB.PageCache.PageSize = DB_PAGE_SIZE; // TODO(ariel) Query this value dynamically from the system.
		DB.TotalPageCountInFile = 1; // NOTE(ariel) Count this header page.

		s32 HeaderPageNumberInCache = AllocatePage(&DB.PageCache); Assert(HeaderPageNumberInCache == 0);
		DB.FeedsRoot = InitializeNewNode(&DB, DB_NODE_TYPE_LEAF); Assert(DB.FeedsRoot.PageNumberInCache == 1);
		DB.TagsRoot = InitializeNewNode(&DB, DB_NODE_TYPE_LEAF); Assert(DB.TagsRoot.PageNumberInCache == 2);

		memcpy(DB.Header, HeaderMagicSequence, sizeof(HeaderMagicSequence));
		s16Serialize(&DB.Header[16], DB.FileFormatVersion);
		s16Serialize(&DB.Header[18], DB.PageCache.PageSize);
		s32Serialize(&DB.Header[20], DB.TotalPageCountInFile);
		s32Serialize(&DB.Header[24], DB.PageCache.CacheToFilePageNumberMap[DB.FeedsRoot.PageNumberInCache]);
		s32Serialize(&DB.Header[28], DB.PageCache.CacheToFilePageNumberMap[DB.TagsRoot.PageNumberInCache]);
		memset(&DB.Header[32], 0, DB_PAGE_SIZE - 32);

		// memcpy(DB.PageCache.Pages[HeaderPageNumberInCache].Data, DB.Header, DB_HEADER_SIZE);
		// WritePageToDisk(&DB.PageCache, HeaderPageNumberInCache);

		AssertAlways(DB.TotalPageCountInFile == 3);
	}
	else
	{
		AssertAlways(!"failed to read database file");
	}

	*db = &DB;
}

static void
db_free(Database *db)
{
	// TODO(ariel) Flush to disk?
	close(db->PageCache.DatabaseFileDescriptor);
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

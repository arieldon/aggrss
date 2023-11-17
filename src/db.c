/* ---
 * Hash
 * ---
 */

static u32
DBHash(String Value)
{
	// NOTE(ariel) DBHash() uses 32-bit FNV-1a.
#define HASH_OFFSET 2166136261
#define HASH_PRIME 16777619
	u32 Hash = HASH_OFFSET;
	for (s32 Index = 0; Index < Value.len; Index += 1)
	{
		Hash = (Hash ^ Value.str[Index]) * HASH_PRIME;
	}
	return Hash;
#undef HASH_PRIME
#undef HASH_OFFSET
}


/* ---
 * Serialization & Deserialization
 * ---
 */

static void
s8Serialize(u8 *Address, u8 Value)
{
	*Address = Value;
}

static void
s16Serialize(u8 *Address, s16 Value)
{
	Address[0] = Value >> 0x00;
	Address[1] = Value >> 0x08;
}

static void
s32Serialize(u8 *Address, s32 Value)
{
	Address[0] = Value >> 0x00;
	Address[1] = Value >> 0x08;
	Address[2] = Value >> 0x10;
	Address[3] = Value >> 0x18;
}

static void
StringSerialize(u8 *Address, String Value)
{
	s32Serialize(Address, Value.len);
	memcpy(Address + sizeof(s32), Value.str, Value.len);
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
	u32 Intermediate = 0;
	Intermediate |= (u32)Address[0] << 0x00;
	Intermediate |= (u32)Address[1] << 0x08;
	s16 Value = Intermediate;
	return Value;
}

static s32
s32Deserialize(u8 *Address)
{
	u64 Intermediate = 0;
	Intermediate |= ((u64)Address[0] << 0x00);
	Intermediate |= ((u64)Address[1] << 0x08);
	Intermediate |= ((u64)Address[2] << 0x10);
	Intermediate |= ((u64)Address[3] << 0x18);
	s32 Value = Intermediate;
	return Value;
}

static String
StringDeserialize(u8 *Address)
{
	String Value = {0};
	Value.len = s32Deserialize(Address);
	Value.str = (char *)Address + sizeof(s32);
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
	s32 PageNumberInCache = -1;

	// TODO(ariel) If number of pages in caches increases drastically, consider
	// using a hash table instead of a plain array for this map.
	for(s32 Index = 0; Index < DB_PAGE_COUNT_IN_CACHE; Index += 1)
	{
		if(PageNumberInFile == PageCache->CacheToFilePageNumberMap[Index])
		{
			PageNumberInCache = Index;
			break;
		}
	}

	if(PageNumberInCache == -1)
	{
		PageNumberInCache = AllocatePage(PageCache);
		s32 Offset = PageCache->PageSize * PageNumberInFile;
		PageCache->CacheToFilePageNumberMap[PageNumberInCache] = PageNumberInFile;
		ssize BytesReadCount = pread(
			PageCache->DatabaseFileDescriptor, PageCache->Pages[PageNumberInCache].Data,
			PageCache->PageSize, Offset);
		AssertAlways(BytesReadCount == PageCache->PageSize); // TODO(ariel) How can I make this more resilient?
	}

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
	DB_OFFSET_TO_CELL_POSITIONS_FOR_INTERNAL_NODES = 12,
	DB_OFFSET_TO_CELL_POSITIONS_FOR_LEAF_NODES = 8,
};

static btree_node
ReadNodeFromDisk(Database *DB, s32 PageNumberInFile)
{
	btree_node DatabaseNode;

	DatabaseNode.PageNumberInFile = PageNumberInFile;
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
WriteNodeToDisk(Database *DB, btree_node *DatabaseNode)
{
	page *Page = &DB->PageCache.Pages[DatabaseNode->PageNumberInCache];

	s8Serialize(&Page->Data[0], DatabaseNode->Type);
	s16Serialize(&Page->Data[1], DatabaseNode->OffsetToFirstFreeBlock);
	s16Serialize(&Page->Data[3], DatabaseNode->CellCount);
	s16Serialize(&Page->Data[5], DatabaseNode->OffsetToCells);
	s8Serialize(&Page->Data[7], DatabaseNode->FragmentedBytesCount);
	if(DatabaseNode->Type == DB_NODE_TYPE_INTERNAL)
	{
		Assert(DatabaseNode->RightPageNumber != -1);
		s32Serialize(&Page->Data[8], DatabaseNode->RightPageNumber);
	}
	else
	{
		Assert(DatabaseNode->RightPageNumber == -1);
	}

	WritePageToDisk(&DB->PageCache, DatabaseNode->PageNumberInCache);
}

enum { DB_LAST_FREE_BLOCK = 0 };

static btree_node
InitializeNewNode(Database *DB, node_type NodeType)
{
	btree_node DatabaseNode;
	DatabaseNode.PageNumberInFile = DB->TotalPageCountInFile;
	DatabaseNode.PageNumberInCache = AllocatePage(&DB->PageCache);
	DatabaseNode.OffsetToFirstFreeBlock = DB->PageCache.PageSize - 4;
	DatabaseNode.CellCount = 0;
	DatabaseNode.FragmentedBytesCount = 0;
	DatabaseNode.OffsetToCells = DB->PageCache.PageSize;
	DatabaseNode.Type = NodeType;

	s16 AvailableSpace = DB->PageCache.PageSize;
	if(NodeType == DB_NODE_TYPE_INTERNAL)
	{
		DatabaseNode.RightPageNumber = 0;
		AvailableSpace -= DB_OFFSET_TO_CELL_POSITIONS_FOR_INTERNAL_NODES;
	}
	else if(NodeType == DB_NODE_TYPE_LEAF)
	{
		DatabaseNode.RightPageNumber = -1;
		AvailableSpace -= DB_OFFSET_TO_CELL_POSITIONS_FOR_LEAF_NODES;
	}
	else
	{
		Assert(!"unreachable");
	}

	// NOTE(ariel) Together these two fields form the header of free list in
	// page.
	page *Page = &DB->PageCache.Pages[DatabaseNode.PageNumberInCache];
	u8 *NextFreeBlock = &Page->Data[DatabaseNode.OffsetToFirstFreeBlock + 0];
	u8 *FreeBlockSize = &Page->Data[DatabaseNode.OffsetToFirstFreeBlock + 2];
	s16Serialize(NextFreeBlock, DB_LAST_FREE_BLOCK);
	s16Serialize(FreeBlockSize, AvailableSpace);

	DB->PageCache.CacheToFilePageNumberMap[DatabaseNode.PageNumberInCache] = DatabaseNode.PageNumberInFile;
	DB->TotalPageCountInFile += 1;

	WriteNodeToDisk(DB, &DatabaseNode);
	return DatabaseNode;
}

static s32
InitializeNewItemPage(Database *DB)
{
	s32 PageNumberInFile = DB->TotalPageCountInFile;
	DB->TotalPageCountInFile += 1;
	return PageNumberInFile;
}

enum { DB_CELL_OFFSET_SLOT = sizeof(u16) };

typedef struct feed_cell feed_cell;
struct feed_cell
{
	u32 ID;
	union
	{
		struct // NOTE(ariel) Leaf nodes store links and titles.
		{
			String Link;
			String Title;
			s32 ItemsPage;
		};
		struct // NOTE(ariel) Internal nodes store pointers to children.
		{
			s32 ChildPage;
		};
	};
};

typedef struct tag_cell tag_cell;
struct tag_cell
{
};

static feed_cell
ParseFeedCell(Database *DB, btree_node DatabaseNode, s32 CellNumber)
{
	feed_cell Cell = {0};

	// NOTE(ariel) Confirm absence of funny business in cache. It's highly
	// unlikely the cache purges this page from memory in so little time.
	AssertAlways(DatabaseNode.PageNumberInFile == DB->PageCache.CacheToFilePageNumberMap[DatabaseNode.PageNumberInCache]);

	AssertAlways(CellNumber < DatabaseNode.CellCount);
	page *Page = &DB->PageCache.Pages[DatabaseNode.PageNumberInCache];

	s32 OffsetToCellPositions =
		DatabaseNode.Type == DB_NODE_TYPE_INTERNAL
		? DB_OFFSET_TO_CELL_POSITIONS_FOR_INTERNAL_NODES
		: DB_OFFSET_TO_CELL_POSITIONS_FOR_LEAF_NODES;
	u8 *CellPositions = &Page->Data[OffsetToCellPositions];
	s16 CellPosition = s16Deserialize(CellPositions + 2*CellNumber); Assert(CellPosition > 0);
	u8 *RawCell = &Page->Data[CellPosition];

	// TODO(ariel) Move into function FeedCellDeserialize();
	Cell.ID = s32Deserialize(RawCell);
	if(DatabaseNode.Type == DB_NODE_TYPE_INTERNAL)
	{
		s16 ChildPagePosition = CellPosition + sizeof(Cell.ID);
		AssertAlways(ChildPagePosition < DB->PageCache.PageSize);
		u8 *ChildPage = &Page->Data[ChildPagePosition];
		Cell.ChildPage = s32Deserialize(ChildPage);
	}
	else if(DatabaseNode.Type == DB_NODE_TYPE_LEAF)
	{
		s16 LinkPosition = CellPosition + sizeof(Cell.ID);
		AssertAlways(LinkPosition < DB->PageCache.PageSize);
		u8 *Link = &Page->Data[LinkPosition];
		Cell.Link = StringDeserialize(Link);

		s16 TitlePosition = LinkPosition + sizeof(Cell.Link.len) + Cell.Link.len;
		AssertAlways(TitlePosition < DB->PageCache.PageSize);
		u8 *Title = &Page->Data[TitlePosition];
		Cell.Title = StringDeserialize(Title);

		s32 ItemsPagePosition = TitlePosition + sizeof(Cell.Title.len) + Cell.Title.len;
		AssertAlways(ItemsPagePosition < DB->PageCache.PageSize);
		u8 *ItemsPage = &Page->Data[ItemsPagePosition];
		Cell.ItemsPage = s32Deserialize(ItemsPage);
	}
	else
	{
		Assert(!"unreachable");
	}

	return Cell;
}

typedef struct search_result search_result;
struct search_result
{
	b32 Found;
	s32 CellNumber;
	btree_node *StackTraceToNode;
	btree_node Node;
};

static search_result
SearchForFeed(Database *DB, feed_cell Cell)
{
	search_result Result = {0};

	s32 PageNumber = DB->PageCache.CacheToFilePageNumberMap[DB->FeedsRoot.PageNumberInCache];
	for(;;)
	{
		btree_node DatabaseNode = ReadNodeFromDisk(DB, PageNumber);

		feed_cell CellOnDisk = {0};
		for(; Result.CellNumber < DatabaseNode.CellCount; Result.CellNumber += 1)
		{
			CellOnDisk = ParseFeedCell(DB, DatabaseNode, Result.CellNumber);
			if(Cell.ID <= CellOnDisk.ID)
			{
				break;
			}
		}

		if(DatabaseNode.Type == DB_NODE_TYPE_INTERNAL)
		{
			PageNumber = CellOnDisk.ChildPage;
		}
		else if(DatabaseNode.Type == DB_NODE_TYPE_LEAF)
		{
			Result.Found = CellOnDisk.ID == Cell.ID;
			Result.Node = DatabaseNode;
			break;
		}
		else
		{
			Assert(!"unreachable");
		}

		// TODO(ariel) Push node onto path.
		;
	}

	return Result;
}

static btree_node
SearchForTag(btree_node *Root, tag_cell Cell)
{
}

static void
SerializeFreeBlockHeader(u8 *Address, s16 NextFreeBlockPosition, s16 BlockSize)
{
	s16Serialize(Address, NextFreeBlockPosition);
	s16Serialize(Address + 2, BlockSize);
}

static void
InsertFeed(Database *DB, btree_node *DatabaseNode, feed_cell Cell)
{
	// FIXME(ariel) Confirm the node still maps to the correct page on disk.
	// There should be a gaurantee about this of some sort. Pull a page into the
	// cache if the page cache already flushed it.
	AssertAlways(DatabaseNode->PageNumberInFile == DB->PageCache.CacheToFilePageNumberMap[DatabaseNode->PageNumberInCache]);
	page *Page = &DB->PageCache.Pages[DatabaseNode->PageNumberInCache];

	// FIXME(ariel) Calculate conditionally based on type of cell.
	s32 RequiredSpace =
		(s32)DB_CELL_OFFSET_SLOT +
		(s32)sizeof(Cell.ID) +
		(s32)sizeof(Cell.Link.len) + Cell.Link.len +
		(s32)sizeof(Cell.Title.len) + Cell.Title.len +
		(s32)sizeof(Cell.ItemsPage);
	Assert(RequiredSpace <= UINT16_MAX);
#if 0
	s32 RemainingSpace = 0;
	b32 RootIsFull = RequiredSpace > RemainingSpace;
	if(RootIsFull)
	{
		// FIXME(ariel) Assume root is not full at first.
		;
	}
#endif

	// TODO(ariel) Only insert inside root node for now.
	// u8 *NextFreeBlockPositionAddress = &Page->Data[DatabaseNode->OffsetToFirstFreeBlock];
	u8 *FreeBlockSizeAddress = &Page->Data[DatabaseNode->OffsetToFirstFreeBlock + sizeof(u16)];
	// FIXME(ariel) Assume first block provides enough space for now.
	// s16 NextBlockPosition = s16Deserialize(NextFreeBlockPosition);
	s16 BlockSize = s16Deserialize(FreeBlockSizeAddress);
	Assert(BlockSize >= RequiredSpace);

	u8 *EndOfFreeBlockSizeAddress = FreeBlockSizeAddress + 1;
	u8 *CellStart = EndOfFreeBlockSizeAddress - RequiredSpace;
	u8 *CellID = CellStart;
	u8 *CellLink = CellID + sizeof(Cell.ID);
	u8 *CellTitle = CellLink + sizeof(Cell.Link.len) + Cell.Link.len;
	u8 *CellItemsPage = CellTitle + sizeof(Cell.Title.len) + Cell.Title.len; AssertAlways(CellItemsPage - Page->Data >= 0);
	s32Serialize(CellID, Cell.ID);
	StringSerialize(CellLink, Cell.Link);
	StringSerialize(CellTitle, Cell.Title);
	s32Serialize(CellItemsPage, Cell.ItemsPage);

	s16 RemainingBlockSize = BlockSize - (s16)RequiredSpace;
	SerializeFreeBlockHeader(CellStart - 4, DB_LAST_FREE_BLOCK, RemainingBlockSize);
	DatabaseNode->OffsetToFirstFreeBlock = CellStart - 4 - Page->Data;

	// NOTE(ariel) Serialize new cell offset.
	s32 OffsetToCellPositions = DatabaseNode->Type == DB_NODE_TYPE_INTERNAL
		? DB_OFFSET_TO_CELL_POSITIONS_FOR_INTERNAL_NODES
		: DB_OFFSET_TO_CELL_POSITIONS_FOR_LEAF_NODES;
	u8 *CellPositions = &Page->Data[OffsetToCellPositions];

	// NOTE(ariel) `CellPositions` default to last slot in array of cell
	// positions.
	s32 CellNumber = 0;
	u8 *CellPosition = CellPositions + 2*DatabaseNode->CellCount;
	while(CellNumber < DatabaseNode->CellCount)
	{
		u8 *CellOffset = CellPositions + 2*CellNumber;
		s16 CellForComparisonOffset = s16Deserialize(CellOffset);
		u32 CellIDForComparison = s32Deserialize(&Page->Data[CellForComparisonOffset]);
		if(Cell.ID < CellIDForComparison)
		{
			Assert(CellPosition > CellPositions + 2*CellNumber);
			CellPosition = CellPositions + 2*CellNumber;
			break;
		}
		CellNumber += 1;
	}
	ssize BytesToMoveCount = 2*(DatabaseNode->CellCount - CellNumber); Assert(BytesToMoveCount <= 2*DatabaseNode->CellCount);
	memmove(CellPositions + 2*(CellNumber+1), CellPositions + 2*CellNumber, BytesToMoveCount);
	u16 CellOffset = CellStart - Page->Data; Assert(CellOffset > 0);
	s16Serialize(CellPosition, CellOffset);

	DatabaseNode->CellCount += 1;
	WriteNodeToDisk(DB, DatabaseNode);
}


/* ---
 * Database
 * ---
 */

static char HeaderMagicSequence[] = "aggrss db format";

static void
WriteHeader(Database *DB)
{
	memcpy(DB->Header, HeaderMagicSequence, sizeof(HeaderMagicSequence));
	s16Serialize(&DB->Header[16], DB->FileFormatVersion);
	s16Serialize(&DB->Header[18], DB->PageCache.PageSize);
	s32Serialize(&DB->Header[20], DB->TotalPageCountInFile);
	s32Serialize(&DB->Header[24], DB->PageCache.CacheToFilePageNumberMap[DB->FeedsRoot.PageNumberInCache]);
	s32Serialize(&DB->Header[28], DB->PageCache.CacheToFilePageNumberMap[DB->TagsRoot.PageNumberInCache]);
	memset(&DB->Header[32], 0, DB->PageCache.PageSize - 32);
	write(DB->PageCache.DatabaseFileDescriptor, DB->Header, DB_HEADER_SIZE);
}

// TODO(ariel) Update interface of initialization procedure. Match the SQLite
// version for now though.
static void
db_init(Database **db)
{
	enum { HEADER_PAGE_NUMBER_IN_FILE = 0 };

	static database DB;

	arena_init(&DB.arena);

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

		DB.FeedsRoot = ReadNodeFromDisk(&DB, FeedsRootPageNumberInFile);
		DB.TagsRoot = ReadNodeFromDisk(&DB, TagsRootPageNumberInFile);
	}
	else if(BytesReadCount == 0)
	{
		// TODO(ariel) Store head (or root) of free list?
		DB.FileFormatVersion = 0;
		DB.PageCache.PageSize = DB_PAGE_SIZE; // TODO(ariel) Query this value dynamically from the system.
		DB.TotalPageCountInFile = 1; // NOTE(ariel) Count header as page.

		DB.FeedsRoot = InitializeNewNode(&DB, DB_NODE_TYPE_LEAF); Assert(DB.FeedsRoot.PageNumberInCache == 0);
		DB.TagsRoot = InitializeNewNode(&DB, DB_NODE_TYPE_LEAF); Assert(DB.TagsRoot.PageNumberInCache == 1);

		WriteHeader(&DB);
		AssertAlways(DB.TotalPageCountInFile == 3);
	}
	else
	{
		AssertAlways(!"failed to read database file");
	}

	*db = &DB;
}

static void
db_free(Database *DB)
{
	WriteHeader(DB);
#if 0 // TODO(ariel) Flush pages?
	for(s32 PageNumber = 0; PageNumber < DB_PAGE_COUNT_IN_CACHE; PageNumber += 1)
	{
		WritePageToDisk(&DB->PageCache, PageNumber);
	}
#endif
	close(DB->PageCache.DatabaseFileDescriptor);
}

static void
db_add_feed(Database *DB, String FeedLink, String FeedTitle)
{
	feed_cell Cell;
	Cell.ID = DBHash(FeedLink);
	Cell.Link = FeedLink;
	Cell.Title = FeedTitle;
	Cell.ItemsPage = -1;

	Arena_Checkpoint Checkpoint = arena_checkpoint_set(&DB->arena);
	{
		// NOTE(ariel) Silently ignore duplicates -- no unique constraint.
		search_result Result = SearchForFeed(DB, Cell);
		if(!Result.Found)
		{
			// FIXME(ariel) No stack trace of the path to the node this way.
			btree_node *Leaf = &Result.Node;
			AssertAlways(Leaf->Type == DB_NODE_TYPE_LEAF);
			Cell.ItemsPage = InitializeNewItemPage(DB);
			InsertFeed(DB, Leaf, Cell);
		}
	}
	arena_checkpoint_restore(Checkpoint);
}

static void
db_add_or_update_feed(Database *DB, String FeedLink, String FeedTitle)
{
}

enum
{
	DB_ITEM_PAGE_NEXT_PAGE = 0,
	DB_ITEM_PAGE_STRING_SLOT = 2,
};

static inline String
GetContentFromNode(RSS_Tree_Node *ItemNode, String TagName, String DefaultValue)
{
	String Result = DefaultValue;

	RSS_Tree_Node *RSSNode = find_item_child_node(ItemNode, TagName);
	if(RSSNode)
	{
		Result = RSSNode->content;
	}

	return Result;
}

static inline String
GetDateFromNode(RSS_Tree_Node *ItemNode)
{
	String Result = {0};
	Result = GetContentFromNode(ItemNode, string_literal("pubDate"), Result);
	Result = GetContentFromNode(ItemNode, string_literal("updated"), Result);
	return Result;
}

static inline u64
GetUnixTimestamp(String FeedLink, String DateTime)
{
	Timestamp Result = parse_date_time(DateTime);
	if (Result.error.str)
	{
		fprintf(stderr, "[DB ERROR] failed to parse date %.*s for %.*s: %.*s\n",
			DateTime.len, DateTime.str,
			FeedLink.len, FeedLink.str,
			Result.error.len, Result.error.str);
	}
	assert(Result.unix_format < UINT32_MAX);
	return Result.unix_format;
}

static inline s32
GetStringSize(String Value)
{
	s32 TotalSize = sizeof(Value.len) + Value.len;
	return TotalSize;
}

static void
db_add_item(Database *DB, String FeedLink, RSS_Tree_Node *ItemNode)
{
	feed_cell Cell =
	{
		.ID = DBHash(FeedLink),
		.Link = FeedLink,
	};
	search_result SearchResult = SearchForFeed(DB, Cell);

	AssertAlways(SearchResult.Found);
	Cell = ParseFeedCell(DB, SearchResult.Node, SearchResult.CellNumber);

	s32 PageNumberInCache = ReadPageFromDisk(&DB->PageCache, Cell.ItemsPage);
	page *Page = &DB->PageCache.Pages[PageNumberInCache];

	// FIXME(ariel) Traverse pages too.
	s16 NextPage = s16Deserialize(&Page->Data[DB_ITEM_PAGE_NEXT_PAGE]);
	s16 FirstUnusedByte = s16Deserialize(&Page->Data[DB_ITEM_PAGE_STRING_SLOT]);
	{
		String Link = find_link(ItemNode);
		String Title = GetContentFromNode(ItemNode, string_literal("title"), string_literal(""));
		u64 Date = GetUnixTimestamp(FeedLink, GetDateFromNode(ItemNode));
		b32 Unread = true;

		s32 ItemSize =
			GetStringSize(Link) +
			GetStringSize(Title) +
			sizeof(Date) +
			sizeof(Unread);
		s32 StartingPosition = FirstUnusedByte - ItemSize;
		AssertAlways(StartingPosition > 4);

		s32 LinkPosition = StartingPosition;
		s32 TitlePosition = LinkPosition + GetStringSize(Link);
		s32 DatePosition = TitlePosition + GetStringSize(Title);
		s32 UnreadPosition = DatePosition + sizeof(Date);
		StringSerialize(&Page->Data[LinkPosition], Link);
		StringSerialize(&Page->Data[TitlePosition], Title);
		s32Serialize(&Page->Data[DatePosition], Date);
		s32Serialize(&Page->Data[UnreadPosition], Unread);

		FirstUnusedByte = StartingPosition - 1;
	}
	s16Serialize(&Page->Data[DB_ITEM_PAGE_NEXT_PAGE], NextPage);
	s16Serialize(&Page->Data[DB_ITEM_PAGE_STRING_SLOT], FirstUnusedByte);
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
db_filter_feeds_by_tag(Database *DB, String *FeedLink, String *FeedTitle, String_List Tags)
{
	return db_iterate_feeds(DB, FeedLink, FeedTitle);
}

static b32
db_iterate_feeds(Database *DB, String *FeedLink, String *FeedTitle)
{
	static feed_cell Cell;
	static s32 CellNumber;
	static s32 PageNumber;

	b32 FeedExists = false;
	PageNumber = DB->PageCache.CacheToFilePageNumberMap[DB->FeedsRoot.PageNumberInCache];
	for(;;)
	{
		btree_node DatabaseNode = ReadNodeFromDisk(DB, PageNumber);

		if(DatabaseNode.Type == DB_NODE_TYPE_INTERNAL)
		{
			// TODO(ariel) Recurse down to leaf nodes when root isn't a leaf itself.
			Assert(!"unimplemented");
		}
		else if(DatabaseNode.Type == DB_NODE_TYPE_LEAF)
		{
			if(CellNumber < DatabaseNode.CellCount)
			{
				Cell = ParseFeedCell(DB, DatabaseNode, CellNumber);
				CellNumber += 1;

				FeedExists = true;
				*FeedLink = Cell.Link;
				*FeedTitle = Cell.Title;
				break;
			}
			else
			{
				CellNumber = 0;
				break;
			}
		}
		else
		{
			Assert(!"unreachable");
		}
	}

	return FeedExists;
}

static b32
db_iterate_items(Database *DB, String FeedLink, DB_Item *Item)
{
	// TODO(ariel) Retrieve page of items associated with feed.
	return false;
}

static b32
db_iterate_tags(Database *db, String *tag)
{
	return false;
}

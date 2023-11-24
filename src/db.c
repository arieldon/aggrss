static database DB;


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

enum { DB_OOM_PAGE = 0 };

static void
DB_WritePageToDisk(s32 PageNumberInCache)
{
	if(PageNumberInCache != DB_OOM_PAGE)
	{
		s32 PageNumberInFile = DB.PageCache.CacheToFilePageNumberMap[PageNumberInCache];
		AssertAlways(PageNumberInFile > -1);

		s32 Offset = DB.PageCache.PageSize * PageNumberInFile;
		ssize BytesWrittenCount = pwrite(
			DB.PageCache.FileDescriptor, DB.PageCache.Pages[PageNumberInCache].Data,
			DB.PageCache.PageSize, Offset);
		AssertAlways(BytesWrittenCount == DB.PageCache.PageSize);

		// TODO(ariel) How does Linux VFS implement fdatasync() for ext4?
		s32 SyncStatus = fdatasync(DB.PageCache.FileDescriptor);
		AssertAlways(SyncStatus == 0);
	}
}

static s32
DB_AllocatePage(void)
{
	s32 PageNumber = DB_OOM_PAGE;

	for(s32 Index = 1; Index < DB_PAGE_COUNT_IN_CACHE; Index += 1)
	{
		if(!DB.PageCache.CacheToFilePageNumberMap[Index])
		{
			// NOTE(ariel) Allocate page without mapping correctly. Caller must map
			// to correct page number in file.
			DB.PageCache.CacheToFilePageNumberMap[Index] = INT32_MAX;
			PageNumber = Index;
			break;
		}
	}

	if(PageNumber == DB_OOM_PAGE)
	{
		fprintf(stderr, "TODO(ariel) set error state\n");
	}

	return PageNumber;
}

static s32
DB_ReadPageFromDisk(s32 PageNumberInFile)
{
	s32 PageNumberInCache = 0;

	// TODO(ariel) If number of pages in caches increases drastically, consider
	// using a hash table instead of a plain array for this map.
	for(s32 Index = 1; Index < DB_PAGE_COUNT_IN_CACHE; Index += 1)
	{
		if(PageNumberInFile == DB.PageCache.CacheToFilePageNumberMap[Index])
		{
			PageNumberInCache = Index;
			break;
		}
	}

	if(!PageNumberInCache)
	{
		PageNumberInCache = DB_AllocatePage();
		DB.PageCache.CacheToFilePageNumberMap[PageNumberInCache] = PageNumberInFile;
		s32 Offset = DB.PageCache.PageSize * PageNumberInFile;
		ssize BytesReadCount = pread(
			DB.PageCache.FileDescriptor, DB.PageCache.Pages[PageNumberInCache].Data,
			DB.PageCache.PageSize, Offset);
		AssertAlways(BytesReadCount == DB.PageCache.PageSize); // TODO(ariel) How can I make this more resilient?
	}

	return PageNumberInCache;
}


/* ---
 * B+Tree
 * ---
 */

enum
{
	DB_PAGE_TYPE = 0,
	DB_PAGE_OFFSET_TO_FIRST_FREE_BLOCK = 1,
	DB_PAGE_CELL_COUNT = 3,
	DB_PAGE_FRAGMENTED_BYTES_COUNT = 5,
	DB_PAGE_RIGHT_PAGE_NUMBER = 6,

	// NOTE(ariel) Internal nodes must allocate space for `RightPageNumber`.
	DB_PAGE_INTERNAL_CELL_POSITIONS = 10,
	DB_PAGE_LEAF_CELL_POSITIONS = 6,

	DB_ROOT_PAGE_IN_FILE = 1,
};

enum
{
	DB_CHUNK_TERMINATOR = 0, // NOTE(ariel) Indicate first and last chunk in free list of chunks in page.
	DB_CHUNK_SIZE_ON_DISK = 2*sizeof(s16),
};

typedef struct db_chunk_header db_chunk_header;
struct db_chunk_header
{
	s16 Position;
	s16 PreviousChunkPosition;

	/* NOTE(ariel) The database tracks fields above only in memory. */
	/* NOTE(ariel) The database serializes fields below to disk. */

	s16 NextChunkPosition;
	s16 BytesCount;
};

static inline ssize
DB_GetLeafNodeSize(db_feed_cell Cell)
{
	ssize Result =
		sizeof(Cell.ID) +
		sizeof(Cell.Link.len) + Cell.Link.len +
		sizeof(Cell.Title.len) + Cell.Title.len +
		sizeof(Cell.ItemsPage);
	Assert(Result < DB_PAGE_SIZE);
	return Result;
}

static inline ssize
DB_GetInternalNodeSize(db_feed_cell Cell)
{
	enum { INTERNAL_CELL_SIZE = sizeof(Cell.ID) + sizeof(Cell.ChildPage) }; StaticAssert(INTERNAL_CELL_SIZE == 8);
	ssize Result = INTERNAL_CELL_SIZE;
	return Result;
}

// TODO(ariel) This read-write pair must use ZII to handle edge cases where
// next and previous point to terminators.
static db_chunk_header
DB_ReadChunkHeaderFromNode(db_btree_node *Node, s16 HeaderPosition)
{
	db_chunk_header Result = {0};
	
	if(HeaderPosition != DB_CHUNK_TERMINATOR)
	{
		Assert(HeaderPosition > (Node->Type == DB_NODE_TYPE_INTERNAL ? DB_PAGE_INTERNAL_CELL_POSITIONS : DB_PAGE_LEAF_CELL_POSITIONS));
		db_page *Page = &DB.PageCache.Pages[Node->PageNumberInCache];
		Result.Position = HeaderPosition;
		Result.NextChunkPosition = s16Deserialize(&Page->Data[HeaderPosition + 0]);
		Result.BytesCount = s16Deserialize(&Page->Data[HeaderPosition + 2]);
	}

	return Result;
}

static void
DB_WriteChunkHeaderToNode(db_btree_node *Node, db_chunk_header Header)
{
	if(Header.Position != DB_CHUNK_TERMINATOR)
	{
		Assert(Header.Position > (Node->Type == DB_NODE_TYPE_INTERNAL ? DB_PAGE_INTERNAL_CELL_POSITIONS : DB_PAGE_LEAF_CELL_POSITIONS));
		db_page *Page = &DB.PageCache.Pages[Node->PageNumberInCache];
		s16Serialize(&Page->Data[Header.Position + 0], Header.NextChunkPosition);
		s16Serialize(&Page->Data[Header.Position + 2], Header.BytesCount);
	}
	else
	{
		Node->OffsetToFirstFreeBlock = Header.NextChunkPosition;
	}
}

static void
DB_UpdateFreeChunkListAfterInsertion(db_btree_node *Node, db_chunk_header UsedChunk, s16 UsedBytesCount)
{
	// NOTE(ariel) Update chunk position and number of free bytes.
	{
		s16 MinimumCellSize = Node->Type == DB_NODE_TYPE_INTERNAL ? 8 : 16;
		s16 RemainingBytesCount = UsedChunk.BytesCount - UsedBytesCount;
		db_chunk_header PreviousChunk = DB_ReadChunkHeaderFromNode(Node, UsedChunk.PreviousChunkPosition);

		if(RemainingBytesCount >= MinimumCellSize)
		{
			s16 CellPosition = UsedChunk.Position + DB_CHUNK_SIZE_ON_DISK - UsedBytesCount;
			db_chunk_header UpdatedChunk =
			{
				.Position = (CellPosition-1) - DB_CHUNK_SIZE_ON_DISK,
				.NextChunkPosition = UsedChunk.NextChunkPosition,
				.BytesCount = RemainingBytesCount,
			};
			DB_WriteChunkHeaderToNode(Node, UpdatedChunk);
			PreviousChunk.NextChunkPosition = UpdatedChunk.Position;
		}
		else
		{
			// TODO(ariel) Defragment node if this value reaches some threshold.
			// SQLite places this threshold at 60 bytes.
			Node->FragmentedBytesCount += RemainingBytesCount;
			PreviousChunk.NextChunkPosition = UsedChunk.NextChunkPosition;
		}

		DB_WriteChunkHeaderToNode(Node, PreviousChunk);
	}

	// NOTE(ariel) Find free chunk header that contains bytes bordering list of
	// cell position entries and reduce its size by size of new cell position
	// entry.
	{
		s16 CellPositionEntriesBase = Node->Type == DB_NODE_TYPE_INTERNAL
			? DB_PAGE_INTERNAL_CELL_POSITIONS
			: DB_PAGE_LEAF_CELL_POSITIONS;
		s16 FreeSpaceStart = CellPositionEntriesBase + sizeof(s16)*Node->CellCount;

		db_chunk_header Chunk = DB_ReadChunkHeaderFromNode(Node, Node->OffsetToFirstFreeBlock);
		while(Chunk.BytesCount)
		{
			// NOTE(ariel) There may not exist a chunk that borders cell position
			// entries because a cell might sit there.
			if(Chunk.Position - Chunk.BytesCount < FreeSpaceStart)
			{
				Chunk.BytesCount -= sizeof(s16);
				DB_WriteChunkHeaderToNode(Node, Chunk);
				break;
			}
			Chunk = DB_ReadChunkHeaderFromNode(Node, Chunk.NextChunkPosition);
		}
	}

	// TODO(ariel) Merge bordering chunks.
	;
}

static db_chunk_header
DB_FindChunkBigEnough(db_btree_node *Node, db_feed_cell Cell)
{
	db_chunk_header Result = {0};

	s16 PreviousChunkPosition = DB_CHUNK_TERMINATOR;
	s16 ChunkPosition = Node->OffsetToFirstFreeBlock;
	s16 RequiredBytesCount = Node->Type == DB_NODE_TYPE_INTERNAL
		? DB_GetInternalNodeSize(Cell)
		: DB_GetLeafNodeSize(Cell);
	while(ChunkPosition != DB_CHUNK_TERMINATOR)
	{
		db_chunk_header Header = DB_ReadChunkHeaderFromNode(Node, ChunkPosition);

		if(Header.BytesCount >= RequiredBytesCount)
		{
			Result = Header;
			break;
		}

		PreviousChunkPosition = ChunkPosition;
		ChunkPosition = Header.NextChunkPosition;
	}

	if(Result.BytesCount)
	{
		Result.Position = ChunkPosition;
		Result.PreviousChunkPosition = PreviousChunkPosition;
	}

	return Result;
}

static db_btree_node
DB_ReadNodeFromDisk(s32 PageNumberInFile)
{
	db_btree_node Node = {0};

	Node.PageNumberInCache = DB_ReadPageFromDisk(PageNumberInFile);
	db_page *Page = &DB.PageCache.Pages[Node.PageNumberInCache];

	Node.Type = s8Deserialize(&Page->Data[DB_PAGE_TYPE]); AssertAlways(Node.Type == DB_NODE_TYPE_INTERNAL || Node.Type == DB_NODE_TYPE_LEAF);
	Node.OffsetToFirstFreeBlock = s16Deserialize(&Page->Data[DB_PAGE_OFFSET_TO_FIRST_FREE_BLOCK]);
	Node.CellCount = s16Deserialize(&Page->Data[DB_PAGE_CELL_COUNT]);
	Node.FragmentedBytesCount = s8Deserialize(&Page->Data[DB_PAGE_FRAGMENTED_BYTES_COUNT]);
	Node.RightPageNumber = Node.Type == DB_NODE_TYPE_INTERNAL ? s32Deserialize(&Page->Data[DB_PAGE_RIGHT_PAGE_NUMBER]) : -1;

	return Node;
}

static void
DB_WriteNodeToDisk(db_btree_node *Node)
{
	// TODO(ariel) Create a function to get page and check it remains valid. This
	// falls hand-in-hand with the idea to use separate caches for readers and
	// writers I think?
	db_page *Page = &DB.PageCache.Pages[Node->PageNumberInCache];

	s8Serialize(&Page->Data[DB_PAGE_TYPE], Node->Type);
	s16Serialize(&Page->Data[DB_PAGE_OFFSET_TO_FIRST_FREE_BLOCK], Node->OffsetToFirstFreeBlock);
	s16Serialize(&Page->Data[DB_PAGE_CELL_COUNT], Node->CellCount);
	s8Serialize(&Page->Data[DB_PAGE_FRAGMENTED_BYTES_COUNT], Node->FragmentedBytesCount);
	if(Node->Type == DB_NODE_TYPE_INTERNAL)
	{
		Assert(Node->RightPageNumber != -1);
		s32Serialize(&Page->Data[DB_PAGE_RIGHT_PAGE_NUMBER], Node->RightPageNumber);
	}
	else
	{
		Assert(Node->RightPageNumber == -1);
	}

	DB_WritePageToDisk(Node->PageNumberInCache);
}

static db_btree_node
DB_InitializeNewNode(db_node_type NodeType)
{
	db_btree_node Node =
	{
		.PageNumberInCache = DB_AllocatePage(),
		.OffsetToFirstFreeBlock = DB.PageCache.PageSize - DB_CHUNK_SIZE_ON_DISK,
		.Type = NodeType,
	};

	s16 AvailableBytes = DB.PageCache.PageSize;
	if(NodeType == DB_NODE_TYPE_INTERNAL)
	{
		Node.RightPageNumber = 0;
		AvailableBytes -= DB_PAGE_INTERNAL_CELL_POSITIONS;
	}
	else if(NodeType == DB_NODE_TYPE_LEAF)
	{
		Node.RightPageNumber = -1;
		AvailableBytes -= DB_PAGE_LEAF_CELL_POSITIONS;
	}
	else
	{
		Assert(!"unreachable");
	}

	db_chunk_header ChunkHeader =
	{
		.Position = Node.OffsetToFirstFreeBlock,
		.NextChunkPosition = DB_CHUNK_TERMINATOR,
		.BytesCount = AvailableBytes,
	};
	DB_WriteChunkHeaderToNode(&Node, ChunkHeader);

	DB.PageCache.CacheToFilePageNumberMap[Node.PageNumberInCache] = DB.TotalPageCountInFile;
	DB.TotalPageCountInFile += 1;

	DB_WriteNodeToDisk(&Node);
	return Node;
}

static db_feed_cell
DB_ReadFeedCell(db_btree_node *Node, s32 CellIndex)
{
	db_feed_cell Result = {0};

	Assert(CellIndex < Node->CellCount);
	db_page *Page = &DB.PageCache.Pages[Node->PageNumberInCache];
	s16 OffsetToCellPositionEntries = Node->Type == DB_NODE_TYPE_INTERNAL
		? DB_PAGE_INTERNAL_CELL_POSITIONS
		: DB_PAGE_LEAF_CELL_POSITIONS;
	s16 OffsetToCellPositionEntry = OffsetToCellPositionEntries + 2*CellIndex;
	s16 CellPosition = s16Deserialize(&Page->Data[OffsetToCellPositionEntry]);

	Result.ID = s32Deserialize(&Page->Data[CellPosition]);
	if(Node->Type == DB_NODE_TYPE_INTERNAL)
	{
		Result.ChildPage = s32Deserialize(&Page->Data[CellPosition + sizeof(Result.ID)]);
	}
	else if(Node->Type == DB_NODE_TYPE_LEAF)
	{
		s16 LinkPosition = CellPosition + sizeof(Result.ID);
		Result.Link = StringDeserialize(&Page->Data[LinkPosition]);

		s16 TitlePosition = LinkPosition + sizeof(Result.Link.len) + Result.Link.len;
		Result.Title = StringDeserialize(&Page->Data[TitlePosition]);

		s16 ItemPagePosition = TitlePosition + sizeof(Result.Title.len) + Result.Title.len;
		Result.ItemsPage = s32Deserialize(&Page->Data[ItemPagePosition]);

		Assert(LinkPosition < DB_PAGE_SIZE);
		Assert(TitlePosition < DB_PAGE_SIZE);
		Assert(ItemPagePosition < DB_PAGE_SIZE);
	}
	else
	{
		Assert(!"unreachable");
	}

	return Result;
}


/* ---
 * Database
 * ---
 */

static char HeaderMagicSequence[] = "aggrss db format";

static void
DB_WriteHeader(void)
{
	memcpy(DB.Header, HeaderMagicSequence, sizeof(HeaderMagicSequence));
	s16Serialize(&DB.Header[16], DB.FileFormatVersion);
	s16Serialize(&DB.Header[18], DB.PageCache.PageSize);
	s32Serialize(&DB.Header[20], DB.TotalPageCountInFile);
	s32Serialize(&DB.Header[24], DB_ROOT_PAGE_IN_FILE);
	memset(&DB.Header[28], 0, DB.PageCache.PageSize - 28);
	pwrite(DB.PageCache.FileDescriptor, DB.Header, DB_HEADER_SIZE, 0);
}

static void
DB_Open(void)
{
	enum { HEADER_PAGE_NUMBER_IN_FILE = 0 };

	arena_init(&DB.arena);

	// TODO(ariel) Use environment variables XDG to get this path to respect
	// user's existing configuration.
#define DATABASE_FILE_PATH CONFIG_DIRECTORY_PATH "/aggrss.db"
	int OpenFlags = O_RDWR | O_CREAT;
	mode_t Mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
	DB.PageCache.FileDescriptor = open(DATABASE_FILE_PATH, OpenFlags, Mode);
	AssertAlways(DB.PageCache.FileDescriptor != -1);
#undef DATABASE_FILE_PATH

	ssize BytesReadCount = read(DB.PageCache.FileDescriptor, DB.Header, DB_HEADER_SIZE);
	if(BytesReadCount == DB_HEADER_SIZE)
	{
		b32 HeaderIntact = !memcmp(DB.Header, HeaderMagicSequence, sizeof(HeaderMagicSequence));

		DB.FileFormatVersion = s16Deserialize(&DB.Header[16]);
		DB.PageCache.PageSize = s16Deserialize(&DB.Header[18]); HeaderIntact &= DB.PageCache.PageSize == DB_PAGE_SIZE;
		DB.TotalPageCountInFile = s32Deserialize(&DB.Header[20]); HeaderIntact &= DB.TotalPageCountInFile >= 2;
		s32 RootPageNumberInFile = s32Deserialize(&DB.Header[24]); HeaderIntact &= RootPageNumberInFile == DB_ROOT_PAGE_IN_FILE;

		for(s32 Index = 28; Index < DB_HEADER_SIZE; Index += 1)
		{
			HeaderIntact &= !DB.Header[Index];
		}

		if(HeaderIntact)
		{
			DB_ReadNodeFromDisk(DB_ROOT_PAGE_IN_FILE);
		}
		else
		{
			AssertAlways(!"database corrupt");
		}
	}
	else if(BytesReadCount == 0)
	{
		// TODO(ariel) Store head (or root) of free list of pages?
		DB.FileFormatVersion = 0;
		DB.PageCache.PageSize = DB_PAGE_SIZE;
		DB.TotalPageCountInFile = 1; // NOTE(ariel) Count header as page.
		DB_InitializeNewNode(DB_NODE_TYPE_LEAF);
		DB_WriteHeader();
	}
	else
	{
		AssertAlways(!"failed to read database file");
	}
}

static void
DB_Close(void)
{
	DB_WriteHeader();
	close(DB.PageCache.FileDescriptor);
}

static void
DB_BeginTransaction(void)
{
}

static void
DB_EndTransaction(void)
{
}

// NOTE(ariel) This procedure uses 32-bit FNV-1a.
static u32
DBHash(String Value)
{
	enum
	{
		HASH_OFFSET = 2166136261,
		HASH_PRIME = 16777619,
	};

	u32 Hash = HASH_OFFSET;
	for (s32 Index = 0; Index < Value.len; Index += 1)
	{
		Hash = (Hash ^ Value.str[Index]) * HASH_PRIME;
	}
	return Hash;
}

static void
DB_SplitNodes(void)
{
}

static void
DB_InsertLeafCell(db_btree_node *Node, db_feed_cell Cell, db_chunk_header FreeChunk)
{
	db_page *Page = &DB.PageCache.Pages[Node->PageNumberInCache];

	// NOTE(ariel) Find sorted cell entry position.
	b32 Unique = true;
	s32 CellEntryIndex = 0;
	u8 *CellPositionEntries = &Page->Data[DB_PAGE_LEAF_CELL_POSITIONS];
	while(CellEntryIndex < Node->CellCount)
	{
		s16 ExistingCellPosition = s16Deserialize(CellPositionEntries + 2*CellEntryIndex);
		u32 ExistingCellID = s32Deserialize(&Page->Data[ExistingCellPosition]);
		if(Cell.ID <= ExistingCellID)
		{
			Unique = Cell.ID != ExistingCellID;
			break;
		}
		CellEntryIndex += 1;
	}

	if(Unique)
	{
		s16 RequiredBytesCount = DB_GetLeafNodeSize(Cell);
		s16 CellPosition = FreeChunk.Position + DB_CHUNK_SIZE_ON_DISK - RequiredBytesCount;
		Assert(RequiredBytesCount <= FreeChunk.BytesCount);
		Assert(CellPosition > DB_PAGE_LEAF_CELL_POSITIONS + 2*Node->CellCount);
		Assert(CellPosition < DB_PAGE_SIZE);

		// TODO(ariel) Allocate a unique items page for a unique cell. :)
		;

		// NOTE(ariel) Serialize cell.
		u8 *CellID = &Page->Data[CellPosition];
		u8 *CellLink = CellID + sizeof(Cell.ID);
		u8 *CellTitle = CellLink + sizeof(Cell.Link.len) + Cell.Link.len;
		u8 *CellItemsPage = CellTitle + sizeof(Cell.Title.len) + Cell.Title.len;
		s32Serialize(CellID, Cell.ID);
		StringSerialize(CellLink, Cell.Link);
		StringSerialize(CellTitle, Cell.Title);
		s32Serialize(CellItemsPage, Cell.ItemsPage);

		// NOTE(ariel) Serialize cell entry position.
		u8 *CellPositionEntry = CellPositionEntries + 2*CellEntryIndex;
		u8 *NextCellPositionEntry = CellPositionEntries + 2*(CellEntryIndex+1);
		ssize BytesToMoveCount = 2*(Node->CellCount - CellEntryIndex);
		memmove(NextCellPositionEntry, CellPositionEntry, BytesToMoveCount);
		s16Serialize(CellPositionEntry, CellPosition);

		Node->CellCount += 1;
		Assert(CellEntryIndex < Node->CellCount);

		DB_UpdateFreeChunkListAfterInsertion(Node, FreeChunk, RequiredBytesCount);
	}
}

static void
DB_AddFeedIntoNode(db_btree_node *Node, db_feed_cell Cell, db_chunk_header FreeChunk)
{
	switch(Node->Type)
	{
		case DB_NODE_TYPE_INTERNAL:
		{
			// TODO(ariel) Recursively search tree to find leaf node in which to
			// insert.
			break;
		}
		case DB_NODE_TYPE_LEAF:
		{
			DB_InsertLeafCell(Node, Cell, FreeChunk);
			DB_WriteNodeToDisk(Node);
			break;
		}
		default:
		{
			// TODO(ariel) Report database corruption.
			break;
		}
	}
}

static void
DB_AddFeed(String FeedLink)
{
	DB_BeginTransaction();

	char BlankTitle[32] = {0}; // NOTE(ariel) Preallocate 32 bytes for title upon update.
	db_feed_cell Cell =
	{
		.ID = DBHash(FeedLink),
		.Link = FeedLink,
		.Title.str = BlankTitle,
		.Title.len = sizeof(BlankTitle),
	};
	db_btree_node Root = DB_ReadNodeFromDisk(DB_ROOT_PAGE_IN_FILE);

	// TODO(ariel) Split root if it doesn't contain enough space for this cell.
	db_chunk_header ChunkHeader = DB_FindChunkBigEnough(&Root, Cell);
	if(ChunkHeader.BytesCount == 0)
	{
		fprintf(stderr, "TODO(ariel) implement splits and internal nodes since root at capacity\n");
		return;
	}

	DB_AddFeedIntoNode(&Root, Cell, ChunkHeader);

	DB_EndTransaction();
}

static void
DB_AddItems(String FeedLink, RSS_Tree_Node *ItemNode)
{
}

static void
DB_DeleteFeed(String FeedLink)
{
}

static void
DB_UpdateFeedTitle(String FeedLink, String FeedTitle)
{
}

static void
DB_MarkItemRead(String FeedLink, String ItemLink)
{
}

static void
DB_MarkAllFeedItemsRead(String FeedLink)
{
}

static db_feed_list
DB_GetAllFeeds(void)
{
	db_feed_list List = {0};

	// TODO(ariel) Recurse from internal nodes to leaf nodes.
	db_btree_node Root = DB_ReadNodeFromDisk(DB_ROOT_PAGE_IN_FILE);
	if(Root.Type == DB_NODE_TYPE_LEAF)
	{
		for(s32 CellIndex = 0; CellIndex < Root.CellCount; CellIndex += 1)
		{
			db_feed_cell Cell = DB_ReadFeedCell(&Root, CellIndex);

			db_feed *Feed = arena_alloc(&DB.arena, sizeof(db_feed_cell));
			Feed->Next = 0;
			Feed->Link = Cell.Link;
			if(Cell.Title.str[0])
			{
				// NOTE(ariel) Clear title in memory if only padding currently exists
				// in database. This case occurs when user first adds some link or if
				// this link remains untitled.
				Feed->Title = Cell.Title;
			}

			if(!List.First)
			{
				List.First = Feed;
			}
			else if(!List.Last)
			{
				List.First->Next = List.Last = Feed;
			}
			else
			{
				List.Last = List.Last->Next = Feed;
			}
		}
	}

	return List;
}

static db_item_list
DB_GetAllFeedItems(String FeedLink)
{
	db_item_list List = {0};
	return List;
}

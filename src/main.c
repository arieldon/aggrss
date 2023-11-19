#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <curl/curl.h>
#include <sqlite3.h>

#include "SDL.h"
#include "SDL_opengl.h"

#include "base.h"
#include "arena.h"
#include "date_time.h"
#include "str.h"
#include "rss.h"
#include "db.h"
#include "err.h"
#include "font.h"
#include "linalg.h"
#include "load_opengl.h"
#include "pool.h"
#include "ui.h"
#include "renderer.h"
#include "string_table.h"
#include "multithreading.h"

#include "arena.c"
#include "date_time.c"
#include "str.c"
#include "rss.c"
#include "db.c"
#include "err.c"
#include "font.c"
#include "linalg.c"
#include "load_opengl.c"
#include "pool.c"
#include "ui.c"
#include "renderer.c"
#include "string_table.c"
#include "multithreading.c"

enum { FPS = 60 };
global u32 delta_ms = 1000 / FPS;

global Arena g_arena;
global pool LinkPool;
global pool MessagePool;

global sqlite3 *db;

global char mouse_button_map[] =
{
	[SDL_BUTTON_LEFT  & 0xff] = UI_MOUSE_BUTTON_LEFT,
	[SDL_BUTTON_RIGHT & 0xff] = UI_MOUSE_BUTTON_RIGHT,
};

global char modifier_key_map[256] =
{
	[SDLK_RETURN    & 0xff] = UI_KEY_RETURN,
	[SDLK_BACKSPACE & 0xff] = UI_KEY_BACKSPACE,
	[SDLK_LCTRL     & 0xff] = UI_KEY_CONTROL,
	[SDLK_RCTRL     & 0xff] = UI_KEY_CONTROL,
	[SDLK_ESCAPE    & 0xff] = UI_KEY_ESCAPE,
	[SDLK_PAGEUP    & 0xff] = UI_KEY_PAGE_UP,
	[SDLK_PAGEDOWN  & 0xff] = UI_KEY_PAGE_DOWN,
};

global task_queue TaskQueue = { .MaxTaskCount = 64 };

typedef struct link_to_query link_to_query;
struct link_to_query
{
	String Link;
	char Buffer[64];
};

typedef struct message_stack message_stack;
struct message_stack
{
	String_Node *_Atomic FirstMessage;
};
global message_stack MessageStack;

static void
PushMessage(String Message)
{
	// TODO(ariel) Move these variables into `message_stack`.
	local_persist String_Table Table;
	local_persist pthread_mutex_t TableLock = PTHREAD_MUTEX_INITIALIZER;

	String_Node *NewMessage = AllocatePoolSlot(&MessagePool);

	pthread_mutex_lock(&TableLock);
	{
		NewMessage->string = intern(&Table, Message);
	}
	pthread_mutex_unlock(&TableLock);

	for(;;)
	{
		String_Node *OldFirstMessage = MessageStack.FirstMessage;
		NewMessage->next = OldFirstMessage;
		if(atomic_compare_exchange_weak(&MessageStack.FirstMessage, &OldFirstMessage, NewMessage))
		{
			break;
		}
	}
}

typedef struct curl_response curl_response;
struct curl_response
{
	thread_info *Thread;
	String Data;
};

static ssize
StoreResponseFromCurl(void *Data, size_t Size, size_t Count, void *CustomUserData)
{
	ssize TotalBytes = Size * Count;

	curl_response *Response = CustomUserData;
	if(Response->Data.str)
	{
		Response->Data.str = arena_realloc(&Response->Thread->ScratchArena, Response->Data.len + TotalBytes);
	}
	else
	{
		Response->Data.str = arena_alloc(&Response->Thread->ScratchArena, TotalBytes);
	}

	assert(TotalBytes <= INT32_MAX);
	memcpy(Response->Data.str + Response->Data.len, Data, TotalBytes);
	Response->Data.len += (s32)TotalBytes;

	return TotalBytes;
}

static void
ParseFeed(s32 ThreadID, void *Data)
{
	thread_info *Thread = &TaskQueue.ThreadInfo[ThreadID];
	String Link = *(String *)Data;

	char *NullTerminatedLink = string_terminate(&Thread->ScratchArena, Link);
	curl_response Resource = { .Thread = Thread };
	curl_easy_setopt(Thread->CurlHandle, CURLOPT_URL, NullTerminatedLink);
	curl_easy_setopt(Thread->CurlHandle, CURLOPT_WRITEFUNCTION, StoreResponseFromCurl);
	curl_easy_setopt(Thread->CurlHandle, CURLOPT_WRITEDATA, &Resource);

	// NOTE(ariel) libcurl creates its own thread.
	CURLcode CurlResult = curl_easy_perform(Thread->CurlHandle);
	if(CurlResult != CURLE_OK)
	{
		char *NullTerminatedCurlErrorMessage = (char *)curl_easy_strerror(CurlResult);
		String CurlErrorMessage =
		{
			.str = NullTerminatedCurlErrorMessage,
			.len = (s32)strlen(NullTerminatedCurlErrorMessage),
		};
		String Strings[] = { Link, string_literal(" "), CurlErrorMessage };
		String FormattedMessage = concat_strings(&Thread->ScratchArena, ARRAY_COUNT(Strings), Strings);
		PushMessage(FormattedMessage);
		curl_easy_reset(Thread->CurlHandle);
		return;
	}

	long HTTPResponseCode;
	curl_easy_getinfo(Thread->CurlHandle, CURLINFO_RESPONSE_CODE, &HTTPResponseCode);
	if(HTTPResponseCode != 200)
	{
		String Strings[] = { string_literal("response code for "), Link, string_literal(" != 200") };
		String FormattedMessage = concat_strings(&Thread->ScratchArena, ARRAY_COUNT(Strings), Strings);
		PushMessage(FormattedMessage);
		curl_easy_reset(Thread->CurlHandle);
		return;
	}

	curl_easy_reset(Thread->CurlHandle);

	String RSS = Resource.Data;
	RSS_Tree *Feed = parse_rss(&Thread->PersistentArena, RSS);

	if(Feed->errors.first)
	{
		String_List List = {0};
		string_list_push_string(&Thread->ScratchArena, &List, string_literal("failed to parse "));
		string_list_push_string(&Thread->ScratchArena, &List, Link);

		RSS_Error *Error = Feed->errors.first;
		while(Error)
		{
			string_list_push_string(&Thread->ScratchArena, &List, string_literal("\t"));
			string_list_push_string(&Thread->ScratchArena, &List, Error->text);
			Error = Error->next;
		}

		String FormattedMessage = string_list_concat(&Thread->ScratchArena, List);
		PushMessage(FormattedMessage);
		return;
	}

	if(Feed->root)
	{
		Feed->feed_title = find_feed_title(&Thread->ScratchArena, Feed->root);
		if(!Feed->feed_title)
		{
			// NOTE(ariel) Invalidate feeds without a title tag.
			String Strings[] = { string_literal("failed to parse title of "), Link };
			String FormattedMessage = concat_strings(&Thread->ScratchArena, ARRAY_COUNT(Strings), Strings);
			PushMessage(FormattedMessage);
			return;
		}

		// NOTE(ariel) Feeds don't necessarily need to be filled; that is, empty
		// feeds are valid.
		Feed->first_item = find_item_node(&Thread->ScratchArena, Feed->root);
		db_add_or_update_feed(db, Link, Feed->feed_title->content);
		for (RSS_Tree_Node *Item = Feed->first_item; Item; Item = Item->next_sibling)
		{
			db_add_item(db, Link, Item);
		}

		String Strings[] = { string_literal("successfully parsed "), Feed->feed_title->content };
		String FormattedMessage = concat_strings(&Thread->ScratchArena, ARRAY_COUNT(Strings), Strings);
		PushMessage(FormattedMessage);
	}
	else
	{
		// TODO(ariel) All this formatted should be done in push_message() -- make
		// it a variadic function.
		String Strings[] = { string_literal("failed to parse title of %.*s\n"), Link };
		String FormattedMessage = concat_strings(&Thread->ScratchArena, ARRAY_COUNT(Strings), Strings);
		PushMessage(FormattedMessage);
	}

	if(Link.len > 64)
	{
		free(Link.str);
	}
	ReleasePoolSlot(&LinkPool, Data);
	arena_clear(&Thread->ScratchArena);
	arena_clear(&Thread->PersistentArena);
}

static void
EnqueueLinkToParse(String Link)
{
	link_to_query *LinkToQuery = AllocatePoolSlot(&LinkPool);
	LinkToQuery->Link.len = Link.len;
	LinkToQuery->Link.str = Link.len <= (ssize)sizeof(LinkToQuery->Buffer)
		? LinkToQuery->Buffer
		: calloc(Link.len, sizeof(char)); // TODO(ariel) Use table of interned strings instead of calloc().
	memcpy(LinkToQuery->Link.str, Link.str, Link.len);
	AddTaskToQueue(&TaskQueue, ParseFeed, LinkToQuery);
}

// NOTE(ariel) Most if not all URLs in the database are less than 64 characters
// in length. Default to the short statically allocated buffer to avoid dynamic
// allocations if possible, prioritizing the common case as a result.
global String feed_to_tag;
global char short_feed_to_tag_buffer[64];

static inline void
free_feed_to_tag(void)
{
	if (feed_to_tag.str != short_feed_to_tag_buffer)
	{
		free(feed_to_tag.str);
	}
	feed_to_tag.str = 0;
	feed_to_tag.len = 0;
}

static void
set_feed_to_tag(String feed_link)
{
	if (feed_to_tag.str)
	{
		free_feed_to_tag();
	}

	feed_to_tag.len = feed_link.len;
	if (feed_to_tag.len <= (s32)sizeof(short_feed_to_tag_buffer))
	{
		feed_to_tag.str = short_feed_to_tag_buffer;
	}
	else
	{
		feed_to_tag.str = calloc(feed_to_tag.len, sizeof(char));
	}
	memcpy(feed_to_tag.str, feed_link.str, feed_link.len);
}

static void
tag_feed(void)
{
	local_persist char tag_name_input[1024];
	local_persist Buffer tag_name =
	{
		.data.str = tag_name_input,
		.cap = sizeof(tag_name_input),
	};

	assert(feed_to_tag.str);
	switch (ui_prompt(string_literal("Tag Name"), &tag_name))
	{
		case UI_PROMPT_SUBMIT:
		{
			db_tag_feed(db, tag_name.data, feed_to_tag);
			free_feed_to_tag();
		} break;
		case UI_PROMPT_CANCEL:
		{
			free_feed_to_tag();
		} break;
	}
}

static void
process_frame(void)
{
	ui_begin();

	local_persist char new_feed_input[1024];
	local_persist Buffer new_feed =
	{
		.data.str = new_feed_input,
		.cap = sizeof(new_feed_input),
	};
	b32 submit_new_feed = false;
	submit_new_feed |= ui_textbox(&new_feed, string_literal("URL of Feed"));
	submit_new_feed |= ui_button(string_literal("Add Feed"));
	if (submit_new_feed)
	{
		db_add_feed(db, new_feed.data, string_literal(""));
		new_feed.data.len = 0;
	}

	if (ui_button(string_literal("Reload All Feeds")))
	{
		String feed_link = {0};
		String feed_title = {0};
		while (db_iterate_feeds(db, &feed_link, &feed_title))
		{
			EnqueueLinkToParse(feed_link);
		}
	}

	String_List tags = {0};
	if (ui_header(string_literal("Tags"), 0))
	{
		String tag = {0};
		while (db_iterate_tags(db, &tag))
		{
			if (ui_toggle(tag))
			{
				String tagdup = string_duplicate(&g_arena, tag);
				string_list_push_string(&g_arena, &tags, tagdup);
			}
		}
	}

	ui_separator();

	String feed_link = {0};
	String feed_title = {0};
	while (db_filter_feeds_by_tag(db, &feed_link, &feed_title, tags))
	{
		String display_name = feed_title.len ? feed_title : feed_link;
		s32 header_state = ui_header(display_name, UI_HEADER_SHOW_X_BUTTON);
		if (ui_header_deleted(header_state))
		{
			db_del_feed(db, feed_link);
		}
		if (ui_header_expanded(header_state))
		{
			DB_Item item = {0};
			while (db_iterate_items(db, feed_link, &item))
			{
				if (ui_link(item.title, item.unread))
				{
					if (item.link.len > 0)
					{
						pid_t pid = fork();
						if (pid == 0)
						{
							char *terminated_link = string_terminate(&g_arena, item.link);
							char *args[] = { "xdg-open", terminated_link, 0 };
							execvp("xdg-open", args);
							exit(1);
						}
						db_mark_item_read(db, item.link);
					}
				}
			}
		}
		if (ui_header_optionized(header_state))
		{
			local_persist String options[] =
			{
				static_string_literal("Mark All as Read"),
				static_string_literal("Reload"),
				static_string_literal("Tag"),
				static_string_literal("Delete"),
			};

			UI_Option_List option_list = {0};
			option_list.names = options;
			option_list.count = ARRAY_COUNT(options);

			s32 selection = ui_popup_menu(option_list);
			switch (selection)
			{
				case 0:
				{
					db_mark_all_read(db, feed_link);
				} break;
				case 1:
				{
					EnqueueLinkToParse(feed_link);
				} break;
				case 2:
				{
					set_feed_to_tag(feed_link);
				} break;
				case 3:
				{
					db_del_feed(db, feed_link);
				} break;
			}
		}
	}

	if (feed_to_tag.str)
	{
		tag_feed();
	}

	ui_separator();

	if (ui_header(string_literal("Messages"), 0))
	{
		String_Node *message = atomic_load(&MessageStack.FirstMessage);
		while (message)
		{
			ui_text(message->string);
			message = message->next;
		}
	}

	ui_end();
}

int
main(void)
{
	arena_init(&g_arena);

	// TODO(ariel) Set these sizes dynamically if there are more than 64 feeds in
	// database?
	LinkPool.SlotSize = sizeof(link_to_query);
	LinkPool.Capacity = 64*LinkPool.SlotSize;
	LinkPool.Buffer = arena_alloc(&g_arena, LinkPool.Capacity);
	InitializePool(&LinkPool);

	MessagePool.SlotSize = sizeof(String_Node);
	MessagePool.Capacity = 64*MessagePool.SlotSize;;
	MessagePool.Buffer = arena_alloc(&g_arena, MessagePool.Capacity);
	InitializePool(&MessagePool);

	curl_global_init(CURL_GLOBAL_DEFAULT);

	// NOTE(ariel) Initialize work queue.
	{
		InitializeThreads(&g_arena, &TaskQueue);
		for(s32 ThreadNumber = 0; ThreadNumber < TaskQueue.AdditionalThreadCount; ThreadNumber += 1)
		{
			thread_info *Info = &TaskQueue.ThreadInfo[ThreadNumber];
			Info->CurlHandle = curl_easy_init(); AssertAlways(Info->CurlHandle);
			arena_init(&Info->ScratchArena);
			arena_init(&Info->PersistentArena);
		}
	}

	SDL_Init(SDL_INIT_VIDEO);
	r_init(&g_arena);
	ui_init();
	db_init(&db);

	Arena_Checkpoint checkpoint = arena_checkpoint_set(&g_arena);
	for (;;)
	{
		u32 start = SDL_GetTicks();

		SDL_Event e = {0};
		while (SDL_PollEvent(&e))
		{
			switch (e.type)
			{
				case SDL_QUIT: goto exit;

				case SDL_MOUSEMOTION: ui_input_mouse_move(e.motion.x, e.motion.y); break;
				case SDL_MOUSEWHEEL:  ui_input_mouse_scroll(0, e.wheel.y); break;
				case SDL_MOUSEBUTTONDOWN:
				{
					s32 mouse_button = mouse_button_map[e.button.button & 0xff];
					ui_input_mouse_down(e.button.x, e.button.y, mouse_button);
				} break;
				case SDL_MOUSEBUTTONUP:
				{
					s32 mouse_button = mouse_button_map[e.button.button & 0xff];
					ui_input_mouse_up(e.button.x, e.button.y, mouse_button);
				} break;

				case SDL_KEYDOWN:
				{
					s32 modifier_key = modifier_key_map[e.key.keysym.sym & 0xff];
					if (modifier_key)
					{
						ui_input_key(modifier_key);
					}
					else
					{
						b32 ctrl = SDL_GetModState() & KMOD_CTRL;
						b32 v = e.key.keysym.sym == SDLK_v;
						b32 clipboard = SDL_HasClipboardText();
						if (ctrl && v && clipboard)
						{
							char *text = SDL_GetClipboardText();
							ui_input_text(text);
							SDL_free(text);
						}
					}
				} break;
				case SDL_TEXTINPUT: ui_input_text(e.text.text); break;
			}
		}

		process_frame();

		local_persist Color background = { 50, 50, 50, 255 };
		r_clear(background);
		r_present();

		arena_checkpoint_restore(checkpoint);

		// NOTE(ariel) Cap frames per second.
		u32 duration = SDL_GetTicks() - start;
		if (duration < delta_ms)
		{
			SDL_Delay(delta_ms - duration);
		}
	}

exit:
	for (s8 ThreadNumber = 0; ThreadNumber < TaskQueue.AdditionalThreadCount; ThreadNumber += 1)
	{
		thread_info *Info = &TaskQueue.ThreadInfo[ThreadNumber];
		curl_easy_cleanup(Info->CurlHandle);
	}
	curl_global_cleanup();
	db_free(db);
	return 0;
}

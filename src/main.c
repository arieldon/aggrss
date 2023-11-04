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

enum { FPS = 60 };
global u32 delta_ms = 1000 / FPS;

global Arena g_arena;
global Pool g_entry_pool;
global Pool g_error_pool;

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

typedef struct Worker Worker;
struct Worker
{
	pthread_t thread_id;

	CURL *curl_handle;

	// NOTE(ariel) The scratch arena stores temporary data for the thread -- data
	// unnecessary for the main thread's use, or any later use for that matter.
	Arena scratch_arena;

	// NOTE(ariel) The work arena stores persistent data for the program -- the
	// main thread uses these results later.
	Arena persistent_arena;
};

typedef struct Work_Entry Work_Entry;
struct Work_Entry
{
	Work_Entry *next;
	String url;
	char short_url_buffer[64];
};

// NOTE(ariel) This queue models the single producer, multiple consumer
// problem.
typedef struct Work_Queue Work_Queue;
struct Work_Queue
{
	sem_t semaphore;
	pthread_spinlock_t big_lock;
	Work_Entry *head;
	Work_Entry *tail;
};

global Worker *workers;
global Work_Queue work_queue;

typedef struct Message_Stack Message_Stack;
struct Message_Stack
{
	pthread_mutex_t lock;
	String_Node *first_message;
};

global Message_Stack g_message_stack = { .lock = PTHREAD_MUTEX_INITIALIZER };

static void
push_message(String message)
{
	local_persist String_Table table;
	local_persist pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

	if (!g_error_pool.buffer)
	{
		g_error_pool.slot_size = sizeof(String_Node);
		g_error_pool.page_size = g_error_pool.slot_size * 16;
		init_pool(&g_error_pool);
	}

	String_Node *new_message = get_slot(&g_error_pool);

	pthread_mutex_lock(&lock);
	{
		new_message->string = intern(&table, message);
	}
	pthread_mutex_unlock(&lock);

	String_Node *expected = atomic_load(&g_message_stack.first_message);
	do new_message->next = expected;
	while (!atomic_compare_exchange_weak(&g_message_stack.first_message, &expected, new_message));
}

typedef struct Response Response;
struct Response
{
	Worker *worker;
	String data;
};

static size_t
store_response_from_curl(void *data, size_t size, size_t nmemb, void *userp)
{
	size_t new_size = size * nmemb;

	Response *response = (Response *)userp;
	if (response->data.str)
	{
		response->data.str = arena_realloc(&response->worker->scratch_arena, response->data.len + new_size);
	}
	else
	{
		response->data.str = arena_alloc(&response->worker->scratch_arena, new_size);
	}
	memcpy(response->data.str + response->data.len, data, new_size);
	response->data.len += new_size;

	return new_size;
}

static void
parse_feed(Worker *worker, String url)
{
	Response resource = { .worker = worker };
	curl_easy_setopt(worker->curl_handle, CURLOPT_URL, url);
	curl_easy_setopt(worker->curl_handle, CURLOPT_WRITEFUNCTION, store_response_from_curl);
	curl_easy_setopt(worker->curl_handle, CURLOPT_WRITEDATA, &resource);

	CURLcode curl_result = curl_easy_perform(worker->curl_handle);
	if (curl_result != CURLE_OK)
	{
		char *curl_error = (char *)curl_easy_strerror(curl_result);
		String length_based_curl_error =
		{
			.str = curl_error,
			.len = strlen(curl_error),
		};
		String strings[] = { url, string_literal(" "), length_based_curl_error };
		String message = concat_strings(&worker->scratch_arena, ARRAY_COUNT(strings), strings);
		push_message(message);
		curl_easy_reset(worker->curl_handle);
		return;
	}

	long http_response_code;
	curl_easy_getinfo(worker->curl_handle, CURLINFO_RESPONSE_CODE, &http_response_code);
	if (http_response_code != 200)
	{
		String strings[] = { string_literal("response code for "), url, string_literal(" != 200") };
		String message = concat_strings(&worker->scratch_arena, ARRAY_COUNT(strings), strings);
		push_message(message);
		curl_easy_reset(worker->curl_handle);
		return;
	}

	curl_easy_reset(worker->curl_handle);

	String rss = resource.data;
	RSS_Tree *feed = parse_rss(&worker->persistent_arena, rss);

	if (feed->errors.first)
	{
		String_List ls = {0};
		string_list_push_string(&worker->scratch_arena, &ls, string_literal("failed to parse "));
		string_list_push_string(&worker->scratch_arena, &ls, url);

		RSS_Error *error = feed->errors.first;
		while (error)
		{
			string_list_push_string(&worker->scratch_arena, &ls, string_literal("\t"));
			string_list_push_string(&worker->scratch_arena, &ls, error->text);
			error = error->next;
		}

		String message = string_list_concat(&worker->scratch_arena, ls);
		push_message(message);
		return;
	}

	if (feed->root)
	{
		feed->feed_title = find_feed_title(&worker->scratch_arena, feed->root);
		if (!feed->feed_title)
		{
			// NOTE(ariel) Invalidate feeds without a title tag.
			String strings[] = { string_literal("failed to parse title of "), url };
			String message = concat_strings(&worker->scratch_arena, ARRAY_COUNT(strings), strings);
			push_message(message);
			return;
		}

		// NOTE(ariel) Feeds don't necessarily need to be filled; that is, empty
		// feeds are valid.
		feed->first_item = find_item_node(&worker->scratch_arena, feed->root);
		db_add_or_update_feed(db, url, feed->feed_title->content);
		for (RSS_Tree_Node *item = feed->first_item; item; item = item->next_sibling)
		{
			db_add_item(db, url, item);
		}

		String strings[] = { string_literal("successfully parsed "), feed->feed_title->content };
		String message = concat_strings(&worker->scratch_arena, ARRAY_COUNT(strings), strings);
		push_message(message);
	}
	else
	{
		String strings[] = { string_literal("failed to parse title of %.*s\n"), url };
		String message = concat_strings(&worker->scratch_arena, ARRAY_COUNT(strings), strings);
		push_message(message);
	}
}

static inline Work_Entry *
init_work_entry(String url)
{
	Work_Entry *entry = get_slot(&g_entry_pool);
	entry->next = 0;
	entry->url.len = url.len;
	if (url.len < (isize)sizeof(entry->short_url_buffer))
	{
		entry->url.str = entry->short_url_buffer;
	}
	else
	{
		entry->url.str = calloc(url.len, sizeof(char));
	}
	memcpy(entry->url.str, url.str, url.len);
	return entry;
}

static inline void
free_work_entry(Work_Entry *entry)
{
	if (entry->url.str != entry->short_url_buffer)
	{
		free(entry->url.str);
	}
	return_slot(&g_entry_pool, entry);
}

static void *
get_work_entry(void *arg)
{
	// NOTE(ariel) This routine serves as a consumer. Several threads process
	// (different) work from the queue simultaneously.
	Worker *worker = arg;

	for (;;)
	{
		Work_Entry *entry = 0;
		sem_wait(&work_queue.semaphore);

		pthread_spin_lock(&work_queue.big_lock);
		{
			entry = work_queue.head;
			work_queue.head = work_queue.head->next;
			if (work_queue.head == work_queue.tail)
			{
				work_queue.tail = 0;
			}
		}
		pthread_spin_unlock(&work_queue.big_lock);

		parse_feed(worker, entry->url);
		free_work_entry(entry);
		arena_clear(&worker->scratch_arena);
		arena_clear(&worker->persistent_arena);
	}

	return 0;
}

static void
add_work_entry(String url)
{
	// NOTE(ariel) This routine serves as a producer. Only a single thread of
	// execution adds entries to the work queue.
	Work_Entry *entry = init_work_entry(url);

	pthread_spin_lock(&work_queue.big_lock);
	{
		if (!work_queue.head)
		{
			work_queue.head = entry;
		}
		else if (!work_queue.tail)
		{
			work_queue.head->next = work_queue.tail = entry;
		}
		else
		{
			work_queue.tail = work_queue.tail->next = entry;
		}
	}
	pthread_spin_unlock(&work_queue.big_lock);

	sem_post(&work_queue.semaphore);
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
	if (feed_to_tag.len <= (i32)sizeof(short_feed_to_tag_buffer))
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
		// TODO(ariel) Prevent the user from reloading all feeds over and over. The
		// previous set of reloads must finish before the user may reload again.
		String feed_link = {0};
		String feed_title = {0};
		while (db_iterate_feeds(db, &feed_link, &feed_title))
		{
			add_work_entry(feed_link);
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
		i32 header_state = ui_header(display_name, UI_HEADER_SHOW_X_BUTTON);
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

			i32 selection = ui_popup_menu(option_list);
			switch (selection)
			{
				case 0:
				{
					db_mark_all_read(db, feed_link);
				} break;
				case 1:
				{
					add_work_entry(feed_link);
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
		String_Node *message = atomic_load(&g_message_stack.first_message);
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

	g_entry_pool.slot_size = sizeof(Work_Entry);
	g_entry_pool.page_size = g_entry_pool.slot_size * 16;
	init_pool(&g_entry_pool);

	curl_global_init(CURL_GLOBAL_DEFAULT);

	// NOTE(ariel) Initialize work queue.
	i32 n_logical_cpu_cores = SDL_GetCPUCount();
	i32 n_workers = n_logical_cpu_cores - 1;
	{
		workers = arena_alloc(&g_arena, n_workers * sizeof(Worker));

		if (sem_init(&work_queue.semaphore, 0, 0) == -1)
		{
			err_exit("failed to initialize semaphore for workers");
		}
		if (pthread_spin_init(&work_queue.big_lock, PTHREAD_PROCESS_PRIVATE))
		{
			err_exit("failed to initialize spin lock for workers");
		}
		for (i8 i = 0; i < n_workers; ++i)
		{
			Worker *worker = &workers[i];

			if (pthread_create(&worker->thread_id, 0, get_work_entry, worker))
			{
				err_exit("failed to launch thread");
			}
			if (pthread_detach(worker->thread_id))
			{
				err_exit("failed to mark thread %d as detached", i);
			}

			worker->curl_handle = curl_easy_init();
			if (!worker->curl_handle)
			{
				err_exit("failed to initialize CURL handle for thread %d", i);
			}

			arena_init(&worker->scratch_arena);
			arena_init(&worker->persistent_arena);
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
					i32 mouse_button = mouse_button_map[e.button.button & 0xff];
					ui_input_mouse_down(e.button.x, e.button.y, mouse_button);
				} break;
				case SDL_MOUSEBUTTONUP:
				{
					i32 mouse_button = mouse_button_map[e.button.button & 0xff];
					ui_input_mouse_up(e.button.x, e.button.y, mouse_button);
				} break;

				case SDL_KEYDOWN:
				{
					i32 modifier_key = modifier_key_map[e.key.keysym.sym & 0xff];
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
	for (i8 i = 0; i < n_workers; ++i)
	{
		Worker *worker = &workers[i];
		curl_easy_cleanup(worker->curl_handle);
	}
	curl_global_cleanup();
	db_free(db);
	free_pool(&g_entry_pool);
	return 0;
}

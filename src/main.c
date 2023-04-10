#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <unistd.h>

#include "SDL.h"
#include "renderer.h"
#include "ui.h"

#include "db.h"

#include "arena.h"
#include "err.h"
#include "request.h"
#include "rss.h"
#include "str.h"

enum { FPS = 60 };
global u32 delta_ms = 1000 / FPS;

global Arena g_arena;
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
};

// NOTE(ariel) The Intel Core i5-6500 processor in this machine has four total
// (physical and logical) cores. The main thread requires one core, which
// leaves three for other work.
// TODO(ariel) Is it feasible to run more than three workers? I _assume_ (no
// tests) that I/O rather than processing bounds this workload, so more workers
// than three may improve throughput.
enum { N_WORKERS = 3 };

typedef struct Worker Worker;
struct Worker
{
	pthread_t thread_id;

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
	i32 ncompletions;
	i32 nfails;

	Work_Entry *entries;
};

global Worker workers[N_WORKERS];
global Work_Queue work_queue;

internal void
parse_feed(Worker *worker, String url)
{
	Resource resource = download_resource(&worker->persistent_arena, &worker->scratch_arena, url);
	if (resource.error.len > 0)
	{
		// TODO(ariel) Push error on global RSS tree instead of or in addition to
		// logging a message here.
		++work_queue.nfails;
		fprintf(stderr, "failed to download %.*s: %.*s\n",
			url.len, url.str, resource.error.len, resource.error.str);
		return;
	}

	String rss = resource.result;
	RSS_Tree *feed = parse_rss(&worker->persistent_arena, rss);

	if (feed->errors.first)
	{
		fprintf(stderr, "failed to parse %.*s\n", url.len, url.str);
		RSS_Error *error = feed->errors.first;
		while (error)
		{
			fprintf(stderr, "\t%.*s\n", error->text.len, error->text.str);
			error = error->next;
		}
		++work_queue.nfails;
		return;
	}

	if (feed->root)
	{
		feed->feed_title = find_feed_title(&worker->scratch_arena, feed->root);
		if (!feed->feed_title)
		{
			// NOTE(ariel) Invalidate feeds without a title tag.
			++work_queue.nfails;
			fprintf(stderr, "failed to parse title of %.*s\n", url.len, url.str);
			return;
		}

		// NOTE(ariel) Feeds don't necessarily need to be filled; that is, empty
		// feeds are valid.
		feed->first_item = find_item_node(&worker->scratch_arena, feed->root);
	}

	db_add_feed(db, url, feed->feed_title->content);
	for (RSS_Tree_Node *item = feed->first_item; item; item = item->next_sibling)
	{
		db_add_item(db, url, item);
	}

	++work_queue.ncompletions;
}

internal void *
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
		arena_clear(&worker->scratch_arena);
		free(entry->url.str);
		free(entry);
	}

	return 0;
}

internal void
add_work_entry(String url)
{
	// NOTE(ariel) This routine serves as a producer. Only a single thread of
	// execution adds entries to the work queue.
	// TODO(ariel) Create a custom pool allocator for the work queue?
	Work_Entry *entry = calloc(1, sizeof(Work_Entry));
	entry->next = 0;
	entry->url.len = url.len;
	entry->url.str = calloc(url.len, sizeof(char));
	memcpy(entry->url.str, url.str, url.len);

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

internal String
format_complete_message(i32 nrows)
{
	local_persist char success_message[32] = {0};

	String message = {0};
	message.len = snprintf(success_message, sizeof(success_message),
		"%d of %d success", work_queue.ncompletions, nrows);
	message.str = success_message;

	return message;
}

internal String
format_fail_message(i32 nrows)
{
	local_persist char fail_message[32] = {0};

	String message = {0};
	message.len = snprintf(fail_message, sizeof(fail_message),
		"%d of %d fail", work_queue.nfails, nrows);
	message.str = fail_message;

	return message;
}

internal void
process_frame(void)
{
	ui_begin();

	i32 nrows = db_count_rows(db);
	String complete_message = format_complete_message(nrows);
	String fail_message = format_fail_message(nrows);
	ui_layout_row(1);
	ui_text(complete_message);
	ui_layout_row(1);
	ui_text(fail_message);

	local_persist char new_feed_input[1024];
	local_persist Buffer new_feed =
	{
		.data.str = new_feed_input,
		.cap = sizeof(new_feed_input),
	};
	ui_textbox(&new_feed);
	if (ui_button(string_literal("Add Feed")))
	{
		// TODO(ariel) Confirm user input represents URL. Get feed at URL.
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

	String feed_link = {0};
	String feed_title = {0};
	while (db_iterate_feeds(db, &feed_link, &feed_title))
	{
		String display_name = feed_title.len ? feed_title : feed_link;
		i32 header_state = ui_header(display_name);
		if (ui_header_expanded(header_state))
		{
			String item_link = {0};
			String item_title = {0};
			while (db_iterate_items(db, feed_link, &item_link, &item_title))
			{
				if (ui_link(item_title))
				{
					if (item_link.len > 0)
					{
						pid_t pid = fork();
						if (pid == 0)
						{
							char *terminated_link = string_terminate(&g_arena, item_link);
							char *args[] = { "xdg-open", terminated_link, 0 };
							execvp("xdg-open", args);
							exit(1);
						}
					}
				}
			}
		}
		else if (ui_header_deleted(header_state))
		{
			db_del_feed(db, feed_link);
		}
	}

	ui_end();
}

int
main(void)
{
	arena_init(&g_arena);

	// NOTE(ariel) Initialize work queue.
	{
		if (sem_init(&work_queue.semaphore, 0, 0) == -1)
		{
			err_exit("failed to initialize semaphore for workers");
		}
		if (pthread_spin_init(&work_queue.big_lock, PTHREAD_PROCESS_PRIVATE))
		{
			err_exit("failed to initialize spin lock for workers");
		}
		for (i8 i = 0; i < N_WORKERS; ++i)
		{
			Worker *worker = &workers[i];
			if (pthread_create(&worker->thread_id, 0, get_work_entry, worker))
			{
				err_exit("failed to launch thread");
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
	db_free(db);
	arena_release(&g_arena);
	return 0;
}

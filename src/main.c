#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <semaphore.h>

#include "SDL.h"
#include "renderer.h"
#include "ui.h"

#include "arena.h"
#include "err.h"
#include "request.h"
#include "rss.h"
#include "str.h"

enum { FPS = 60 };
global u32 delta_ms = 1000 / FPS;

global Arena g_arena;

global char modifier_key_map[256] = {
	[SDLK_RETURN    & 0xff] = UI_KEY_RETURN,
	[SDLK_BACKSPACE & 0xff] = UI_KEY_BACKSPACE,
};

// NOTE(ariel) The Intel Core i5-6500 processor in this machine has four total
// (physical and logical) cores. The main thread requires one core, which
// leaves three for other work.
// TODO(ariel) Is it feasible to run more than three workers? I _assume_ (no
// tests) that I/O rather than processing bounds this workload, so more workers
// than three may improve throughput.
enum { N_WORKERS = 3 };

typedef struct {
	pthread_t thread_id;

	// NOTE(ariel) The scratch arena stores temporary data for the thread -- data
	// unnecessary for the main thread's use, or any later use for that matter.
	Arena scratch_arena;

	// NOTE(ariel) The work arena stores persistent data for the program -- the
	// main thread uses these results later.
	Arena persistent_arena;
} Worker;

typedef struct {
	String url;
} Work_Entry;

// NOTE(ariel) This queue models the single producer, multiple consumer
// problem.
typedef struct {
	sem_t semaphore;

	_Atomic(i32) next_entry_to_read;
	_Atomic(i32) next_entry_to_write;
	_Atomic(i32) ncompletions;
	_Atomic(i32) nfails;

	i32 n_max_entries;
	Work_Entry *entries;
} Work_Queue;

global Worker workers[N_WORKERS];
global Work_Queue work_queue;

typedef struct {
	pthread_spinlock_t lock;
	RSS_Tree_List list;
} Feed_List;

global Feed_List feeds;

internal String
load_file(Arena *arena, FILE *file)
{
	String contents = {0};

	fseek(file, 0, SEEK_END);
	contents.len = ftell(file);
	rewind(file);
	contents.str = arena_alloc(arena, contents.len);
	fread(contents.str, contents.len, sizeof(char), file);
	fclose(file);

	return contents;
}

internal void
parse_feed(Worker *worker, String url)
{
#if NO_NETWORK
	char *domain = string_terminate(&worker->scratch_arena, parse_http_url(url).domain);
	FILE *file = fopen(domain, "rb");
	if (!file) {
		++work_queue.nfails;
		return;
	}
	String rss = load_file(&worker->persistent_arena, file);
#else
	String rss = download_resource(&worker->persistent_arena, &worker->scratch_arena, url);
	if (!rss.len) {
		// TODO(ariel) Push error on global RSS tree instead of or in addition to
		// logging a message here.
		++work_queue.nfails;
		return;
	}
#endif

	RSS_Token_List tokens = tokenize_rss(&worker->persistent_arena, rss);
	RSS_Tree *feed = parse_rss(&worker->persistent_arena, tokens);

	if (feed->root) {
		feed->feed_title = find_feed_title(&worker->scratch_arena, feed->root);
		if (!feed->feed_title) {
			// NOTE(ariel) Invalidate feeds without a title tag.
			++work_queue.nfails;
			return;
		}

		// NOTE(ariel) Feeds don't necessarily need to be filled; that is, empty
		// feeds are valid.
		feed->first_item = find_item_node(&worker->scratch_arena, feed->root);
	}

	// NOTE(ariel) Push results in persistent arena to global tree.
	pthread_spin_lock(&feeds.lock);
	{
		if (!feeds.list.first) {
			feeds.list.first = feeds.list.last = feed;
		} else {
			feeds.list.last->next = feed;
			feeds.list.last = feed;
			feed->next = 0;
		}
	}
	pthread_spin_unlock(&feeds.lock);

	++work_queue.ncompletions;
}

internal void *
get_work_entry(void *arg)
{
	// NOTE(ariel) This routine serves as a consumer. Several threads process
	// (different) work from the queue simultaneously.
	Worker *worker = arg;

	for (;;) {
		sem_wait(&work_queue.semaphore);
		assert(work_queue.next_entry_to_read < work_queue.n_max_entries);
		Work_Entry entry = work_queue.entries[work_queue.next_entry_to_read++];
		parse_feed(worker, entry.url);
		arena_clear(&worker->scratch_arena);
	}

	return 0;
}

internal void
add_work_entry(String url)
{
	// NOTE(ariel) This routine serves as a producer. Only a single thread of
	// execution adds entries to the work queue.
	Work_Entry entry = {url};
	assert(work_queue.next_entry_to_write < work_queue.n_max_entries);
	work_queue.entries[work_queue.next_entry_to_write++] = entry;
	sem_post(&work_queue.semaphore);
}

internal void
read_feeds(String feeds)
{
	String_List urls = string_split(&g_arena, feeds, '\n');
	work_queue.n_max_entries = urls.list_size;
	work_queue.entries = arena_alloc(&g_arena, work_queue.n_max_entries * sizeof(Work_Entry));

	String_Node *url = urls.head;
	while (url) {
		add_work_entry(url->string);
		url = url->next;
	}
}

internal String
format_complete_message(void)
{
	local_persist char success_message[32] = {0};

	String message = {0};
	message.len = snprintf(success_message, sizeof(success_message),
		"%d of %d success", work_queue.ncompletions, work_queue.n_max_entries);
	message.str = success_message;

	return message;
}

internal String
format_fail_message(void)
{
	local_persist char fail_message[32] = {0};

	String message = {0};
	message.len = snprintf(fail_message, sizeof(fail_message),
		"%d of %d fail", work_queue.nfails, work_queue.n_max_entries);
	message.str = fail_message;

	return message;
}

internal void
process_frame(void)
{
	ui_begin();

	String complete_message = format_complete_message();
	String fail_message = format_fail_message();
	ui_layout_row(1);
	ui_text(complete_message);
	ui_layout_row(1);
	ui_text(fail_message);

	pthread_spin_lock(&feeds.lock);
	{
		RSS_Tree *feed = feeds.list.first;
		while (feed) {
			RSS_Tree_Node *item_node = feed->first_item;
			if (ui_header(feed->feed_title->content)) {
				while (item_node) {
					RSS_Tree_Node *item_title_node = find_item_title(item_node);
					if (item_title_node) {
						ui_label(item_title_node->content);
					}
					item_node = item_node->next_sibling;
				}
			}
			feed = feed->next;
		}
	}
	pthread_spin_unlock(&feeds.lock);

	ui_end();
}

int
main(void)
{
	arena_init(&g_arena);

	// NOTE(ariel) Initialize tree that stores parsed RSS feeds.
	if (pthread_spin_init(&feeds.lock, PTHREAD_PROCESS_PRIVATE)) {
		err_exit("failed to initialize spin lock for list of parsed RSS feeds");
	}

	// NOTE(ariel) Initialize work queue.
	{
		if (sem_init(&work_queue.semaphore, 0, 0) == -1) {
			err_exit("failed to initialize semaphore for workers");
		}
		for (i8 i = 0; i < N_WORKERS; ++i) {
			Worker *worker = &workers[i];
			if (pthread_create(&worker->thread_id, 0, get_work_entry, worker)) {
				err_exit("failed to launch thread");
			}
			arena_init(&worker->scratch_arena);
			arena_init(&worker->persistent_arena);
		}
	}

	SDL_Init(SDL_INIT_VIDEO);
	r_init(&g_arena);
	ui_init();

	FILE *file = fopen("./feeds", "rb");
	if (!file) err_exit("failed to open feeds file");

	String feeds = load_file(&g_arena, file);
	read_feeds(feeds);

	Arena_Checkpoint checkpoint = arena_checkpoint_set(&g_arena);
	for (;;) {
		u32 start = SDL_GetTicks();

		SDL_Event e = {0};
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_QUIT: exit(EXIT_SUCCESS); break;

			case SDL_MOUSEMOTION: ui_input_mouse_move(e.motion.x, e.motion.y); break;
			case SDL_MOUSEWHEEL:  ui_input_mouse_scroll(0, e.wheel.y); break;
			case SDL_MOUSEBUTTONDOWN: ui_input_mouse_down(e.button.x, e.button.y, 1); break;
			case SDL_MOUSEBUTTONUP: ui_input_mouse_up(e.button.x, e.button.y, 1); break;

			case SDL_TEXTINPUT: ui_input_text(e.text.text); break;
			case SDL_KEYDOWN: {
				int modifier_key = modifier_key_map[e.key.keysym.sym & 0xff];
				if (modifier_key) {
					ui_input_key(modifier_key);
				}
			} break;
			}
		}

		process_frame();

		local_persist Color background = { 50, 50, 50, 255 };
		r_clear(background);
		r_present();

		arena_checkpoint_restore(checkpoint);

		// NOTE(ariel) Cap frames per second.
		u32 duration = SDL_GetTicks() - start;
		if (duration < delta_ms) {
			SDL_Delay(delta_ms - duration);
		}
	}

	arena_release(&g_arena);
	return 0;
}

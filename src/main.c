#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <semaphore.h>

#include "SDL.h"
#include "renderer.h"
#include "microui.h"

#include "arena.h"
#include "err.h"
#include "request.h"
#include "rss.h"
#include "str.h"

global Arena g_arena;

global const char button_map[256] = {
	[SDL_BUTTON_LEFT   & 0xff] = MU_MOUSE_LEFT,
	[SDL_BUTTON_RIGHT  & 0xff] = MU_MOUSE_RIGHT,
	[SDL_BUTTON_MIDDLE & 0xff] = MU_MOUSE_MIDDLE,
};
global const char key_map[256] = {
	[SDLK_LSHIFT    & 0xff] = MU_KEY_SHIFT,
	[SDLK_RSHIFT    & 0xff] = MU_KEY_SHIFT,
	[SDLK_LCTRL     & 0xff] = MU_KEY_CTRL,
	[SDLK_RCTRL     & 0xff] = MU_KEY_CTRL,
	[SDLK_LALT      & 0xff] = MU_KEY_ALT,
	[SDLK_RALT      & 0xff] = MU_KEY_ALT,
	[SDLK_RETURN    & 0xff] = MU_KEY_RETURN,
	[SDLK_BACKSPACE & 0xff] = MU_KEY_BACKSPACE,
};
global float background[3] = { 255, 255, 255 };

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

	i32 n_max_work_entries;
	Work_Entry *entries;
} Work_Queue;

global Worker workers[N_WORKERS];
global Work_Queue work_queue;

typedef struct {
	pthread_spinlock_t lock;
	RSS_Tree_List list;
} Feed_List;

global Feed_List feeds;

internal void
parse_feed(Worker *worker, String url)
{
	String rss = download_resource(&worker->persistent_arena, &worker->scratch_arena, url);
	if (!rss.len) {
		// TODO(ariel) Push error on global RSS tree instead of or in addition to
		// logging a message here.
		++work_queue.nfails;
		fprintf(stderr, "failed to receive valid response for %.*s\n", url.len, url.str);
		return;
	}

	RSS_Token_List tokens = tokenize_rss(&worker->persistent_arena, rss);
	RSS_Tree *feed = parse_rss(&worker->persistent_arena, tokens);

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
	fprintf(stderr, "succcess %.*s\n", url.len, url.str);
}

internal void *
get_work_entry(void *arg)
{
	// NOTE(ariel) This routine serves as a consumer. Several threads process
	// (different) work from the queue simultaneously.
	Worker *worker = arg;

	for (;;) {
		sem_wait(&work_queue.semaphore);
		if (work_queue.next_entry_to_read != work_queue.next_entry_to_write) {
			Work_Entry entry = work_queue.entries[work_queue.next_entry_to_read];
			work_queue.next_entry_to_read =
				(work_queue.next_entry_to_read + 1) % work_queue.n_max_work_entries;
			parse_feed(worker, entry.url);
			arena_clear(&worker->scratch_arena);
		}
	}

	return 0;
}

internal void
add_work_entry(String url)
{
	// NOTE(ariel) This routine serves as a producer. Only a single thread of
	// execution adds entries to the work queue.
	i32 new_next_entry_to_write = (work_queue.next_entry_to_write + 1) % work_queue.n_max_work_entries;
	assert(new_next_entry_to_write != work_queue.next_entry_to_read);

	Work_Entry entry = {url};
	work_queue.entries[work_queue.next_entry_to_write] = entry;
	work_queue.next_entry_to_write = new_next_entry_to_write;

	sem_post(&work_queue.semaphore);
}

internal void
read_feeds(String feeds)
{
	String_List urls = string_split(&g_arena, feeds, '\n');
	work_queue.n_max_work_entries = urls.list_size;
	work_queue.entries = arena_alloc(&g_arena, work_queue.n_max_work_entries * sizeof(Work_Entry));

	String_Node *url = urls.head;
	while (url) {
		add_work_entry(url->string);
		url = url->next;
	}
}

internal int
text_width(mu_Font font, const char *text, int len)
{
	(void)font;
	if (len == -1) len = strlen(text);
	return r_get_text_width(text, len);
}

internal int
text_height(mu_Font font)
{
	(void)font;
	return r_get_text_height();
}

internal char *
format_complete_message(void)
{
	local_persist char success_message[] = "XX of XX succeeded";
	snprintf(success_message, sizeof(success_message),
		"%d of %d success", work_queue.ncompletions, work_queue.n_max_work_entries);
	return success_message;
}

internal char *
format_fail_message(void)
{
	local_persist char fail_message[] = "XX of XX failed";
	snprintf(fail_message, sizeof(fail_message),
		"%d of %d fail", work_queue.nfails, work_queue.n_max_work_entries);
	return fail_message;
}

internal void
process_frame(mu_Context *ctx)
{
	mu_begin(ctx);

	i32 winopts = MU_OPT_NORESIZE | MU_OPT_NOCLOSE;
	if (mu_begin_window_ex(ctx, "RSS", mu_rect(0, 0, 800, 600), winopts)) {
		char *complete_message = format_complete_message();
		char *fail_message = format_fail_message();
		mu_layout_row(ctx, 1, (int[]){-1}, 0);
		mu_text(ctx, complete_message);
		mu_layout_row(ctx, 1, (int[]){-1}, 0);
		mu_text(ctx, fail_message);

		pthread_spin_lock(&feeds.lock);
		{
			RSS_Tree *feed = feeds.list.first;
			while (feed) {
				RSS_Tree_Node *root = feed->root;
				if (mu_header_ex(ctx, string_terminate(&g_arena, root->token->text), 0)) {
					// TODO(ariel) Insert a list of posts from the corresponding feed
					// here.
					//
					// TODO(ariel) Create nodes for content in the tree first. The tree,
					// for now, only contains the tag names, and those provide little
					// value to the user (me) in the graphical interface.
					;
				}
				feed = feed->next;
			}
		}
		pthread_spin_unlock(&feeds.lock);
		mu_end_window(ctx);
	}

	mu_end(ctx);
}

internal String
load_file(FILE *file)
{
	String contents = {0};

	fseek(file, 0, SEEK_END);
	contents.len = ftell(file);
	rewind(file);
	contents.str = arena_alloc(&g_arena, contents.len + 1);
	fread(contents.str, contents.len, sizeof(char), file);
	contents.str[contents.len] = 0;
	fclose(file);

	return contents;
}

int
main(void)
{
	arena_init(&g_arena);

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

	// NOTE(ariel) Initialize tree that stores parsed RSS feeds.
	if (pthread_spin_init(&feeds.lock, PTHREAD_PROCESS_PRIVATE)) {
		err_exit("failed to initialize spin lock for tree of parsed RSS feeds");
	}

	SDL_Init(SDL_INIT_VIDEO);
	r_init();

	mu_Context *ctx = arena_alloc(&g_arena, sizeof(mu_Context));
	mu_init(ctx);
	ctx->text_width = text_width;
	ctx->text_height = text_height;

	FILE *file = fopen("./feeds", "rb");
	if (!file) err_exit("failed to open feeds file");

	String feeds = load_file(file);
	read_feeds(feeds);

	Arena_Checkpoint checkpoint = arena_checkpoint_set(&g_arena);
	for (;;) {
		SDL_Event e = {0};
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_QUIT: exit(EXIT_SUCCESS); break;

			case SDL_MOUSEMOTION: mu_input_mousemove(ctx, e.motion.x, e.motion.y); break;
			case SDL_MOUSEWHEEL:  mu_input_scroll(ctx, 0, e.wheel.y * -30); break;
			case SDL_TEXTINPUT:   mu_input_text(ctx, e.text.text); break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP: {
				int b = button_map[e.button.button & 0xff];
				if (b && e.type == SDL_MOUSEBUTTONDOWN) mu_input_mousedown(ctx, e.button.x, e.button.y, b);
				if (b && e.type == SDL_MOUSEBUTTONUP)   mu_input_mouseup(ctx, e.button.x, e.button.y, b);
				break;
			}

			case SDL_KEYDOWN:
			case SDL_KEYUP: {
				int c = key_map[e.key.keysym.sym & 0xff];
				if (c && e.type == SDL_KEYDOWN) mu_input_keydown(ctx, c);
				if (c && e.type == SDL_KEYUP)   mu_input_keyup(ctx, c);
				break;
			}
			}
		}

		process_frame(ctx);

		r_clear(mu_color(background[0], background[1], background[2], 255));
		mu_Command *cmd = NULL;
		while (mu_next_command(ctx, &cmd)) {
			switch (cmd->type) {
			case MU_COMMAND_TEXT: r_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color); break;
			case MU_COMMAND_RECT: r_draw_rect(cmd->rect.rect, cmd->rect.color); break;
			case MU_COMMAND_ICON: r_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color); break;
			case MU_COMMAND_CLIP: r_set_clip_rect(cmd->clip.rect); break;
			}
		}
		r_present();

		arena_checkpoint_restore(checkpoint);
	}

	arena_release(&g_arena);
	return 0;
}

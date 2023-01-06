#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include "renderer.h"
#include "microui.h"

#include "arena.h"
#include "err.h"
#include "rss.h"

global Arena g_arena;
global Arena g_rss_arena;

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
terminate_string(String s)
{
	char *t = arena_alloc(&g_arena, s.len + 1);
	memcpy(t, s.str, s.len);
	t[s.len] = 0;
	return t;
}

internal void
process_frame(mu_Context *ctx, RSS_Tree tree)
{
	(void)tree;
	mu_begin(ctx);

	i32 winopts = MU_OPT_NORESIZE | MU_OPT_NOCLOSE;
	if (mu_begin_window_ex(ctx, "RSS", mu_rect(0, 0, 800, 600), winopts)) {
		// TODO(ariel) Iterate through all different feeds and draw a drop-down
		// header for them.
		RSS_Tree_Node *feed = 0;
		while (feed) {
			if (mu_header_ex(ctx, terminate_string(feed->token->text), 0)) {
				// TODO(ariel) Insert a list of posts from the corresponding feed here.
				;
			}
			feed = feed->next_sibling;
		}

		mu_end_window(ctx);
	}

	mu_end(ctx);
}

internal String
load_file(FILE *file)
{
	String contents;

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
	arena_init(&g_rss_arena);

	SDL_Init(SDL_INIT_EVERYTHING);
	r_init();

	mu_Context *ctx = arena_alloc(&g_arena, sizeof(mu_Context));
	mu_init(ctx);
	ctx->text_width = text_width;
	ctx->text_height = text_height;

	FILE *file = fopen("./sample-rss-2.xml", "rb");
	if (!file) err_exit("failed to open RSS file");

	String source = load_file(file);
	RSS_Token_List tokens = tokenize_rss(&g_rss_arena, source);
	RSS_Tree tree = parse_rss(&g_rss_arena, tokens);

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

		process_frame(ctx, tree);

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

	arena_release(&g_rss_arena);
	arena_release(&g_arena);
	return 0;
}

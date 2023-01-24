/*
** Copyright (c) 2020 rxi
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to
** deal in the Software without restriction, including without limitation the
** rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
** sell copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
** IN THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "microui.h"

#define unused(x) ((void)(x))

#define expect(x) do { \
		if (!(x)) { \
			fprintf(stderr, "Fatal error: %s:%d: assertion '%s' failed\n", \
				__FILE__, __LINE__, #x); \
			abort(); \
		} \
	} while (0)

#define push(item, stack) do { \
		expect((stack).index < (i32)(sizeof((stack).items) / sizeof(*(stack).items))); \
		(stack).items[(stack).index++] = (item); \
	} while (0)

#define pop(stack) do { \
		expect((stack).index > 0); \
		--(stack).index; \
	} while (0)


global Rectangle unclipped_rect = { 0, 0, 0x1000000, 0x1000000 };

global UI_Style default_style = {
	/* font | size | padding | spacing | indent */
	NULL, { 68, 10 }, 5, 4, 24,
	/* title_height | scrollbar_size | thumb_size */
	24, 12, 8,
	{
		{ 230, 230, 230, 255 }, /* UI_COLOR_TEXT */
		{ 25,  25,  25,  255 }, /* UI_COLOR_BORDER */
		{ 50,  50,  50,  255 }, /* UI_COLOR_WINDOWBG */
		{ 25,  25,  25,  255 }, /* UI_COLOR_TITLEBG */
		{ 240, 240, 240, 255 }, /* UI_COLOR_TITLETEXT */
		{ 0,   0,   0,   0   }, /* UI_COLOR_PANELBG */
		{ 75,  75,  75,  255 }, /* UI_COLOR_BUTTON */
		{ 95,  95,  95,  255 }, /* UI_COLOR_BUTTONHOVER */
		{ 115, 115, 115, 255 }, /* UI_COLOR_BUTTONFOCUS */
		{ 30,  30,  30,  255 }, /* UI_COLOR_BASE */
		{ 35,  35,  35,  255 }, /* UI_COLOR_BASEHOVER */
		{ 40,  40,  40,  255 }, /* UI_COLOR_BASEFOCUS */
		{ 43,  43,  43,  255 }, /* UI_COLOR_SCROLLBASE */
		{ 30,  30,  30,  255 }  /* UI_COLOR_SCROLLTHUMB */
	}
};

internal Rectangle
expand_rect(Rectangle rect, i32 n)
{
	return (Rectangle){ rect.x - n, rect.y - n, rect.w + n * 2, rect.h + n * 2 };
}

internal Rectangle
intersect_rects(Rectangle r1, Rectangle r2)
{
	i32 x1 = MAX(r1.x, r2.x);
	i32 y1 = MAX(r1.y, r2.y);
	i32 x2 = MIN(r1.x + r1.w, r2.x + r2.w);
	i32 y2 = MIN(r1.y + r1.h, r2.y + r2.h);
	if (x2 < x1) { x2 = x1; }
	if (y2 < y1) { y2 = y1; }
	return (Rectangle){ x1, y1, x2 - x1, y2 - y1 };
}

internal bool
rect_overlaps_vec2(Rectangle r, Vector2 p)
{
	return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

internal void
draw_window_frame(UI_Context *ctx, Rectangle rect, int colorid)
{
	ui_draw_rect(ctx, rect, ctx->style->colors[colorid]);
	if (colorid == UI_COLOR_SCROLLBASE  ||
			colorid == UI_COLOR_SCROLLTHUMB ||
			colorid == UI_COLOR_TITLEBG) { return; }
	/* draw border */
	if (ctx->style->colors[UI_COLOR_BORDER].a) {
		ui_draw_box(ctx, expand_rect(rect, 1), ctx->style->colors[UI_COLOR_BORDER]);
	}
}

void
ui_init(UI_Context *ctx)
{
	MEM_ZERO_STRUCT(ctx);
	ctx->draw_window_frame = draw_window_frame;
	ctx->_style = default_style;
	ctx->style = &ctx->_style;
}

void
ui_begin(UI_Context *ctx)
{
	expect(ctx->text_width && ctx->text_height);
	ctx->command_list.index = 0;
	ctx->root_list.index = 0;
	ctx->scroll_target = NULL;
	ctx->hover_root = ctx->next_hover_root;
	ctx->next_hover_root = NULL;
	ctx->mouse_delta.x = ctx->mouse_pos.x - ctx->last_mouse_pos.x;
	ctx->mouse_delta.y = ctx->mouse_pos.y - ctx->last_mouse_pos.y;
	ctx->frame++;
}

internal i32
compare_zindex(const void *a, const void *b)
{
	return (*(UI_Container **)a)->zindex - (*(UI_Container **)b)->zindex;
}

void
ui_end(UI_Context *ctx)
{
	i32 i, n;
	/* check stacks */
	expect(ctx->container_stack.index == 0);
	expect(ctx->clip_stack.index      == 0);
	expect(ctx->id_stack.index        == 0);
	expect(ctx->layout_stack.index    == 0);

	/* handle scroll input */
	if (ctx->scroll_target) {
		ctx->scroll_target->scroll.x += ctx->scroll_delta.x;
		ctx->scroll_target->scroll.y += ctx->scroll_delta.y;
	}

	/* unset focus if focus id was not touched this frame */
	if (!ctx->updated_focus) { ctx->focus = 0; }
	ctx->updated_focus = 0;

	/* bring hover root to front if mouse was pressed */
	if (ctx->mouse_pressed && ctx->next_hover_root &&
			ctx->next_hover_root->zindex < ctx->last_zindex &&
			ctx->next_hover_root->zindex >= 0
	) {
		ui_bring_to_front(ctx, ctx->next_hover_root);
	}

	/* reset input state */
	ctx->key_pressed = 0;
	ctx->input_text[0] = '\0';
	ctx->mouse_pressed = 0;
	ctx->scroll_delta = (Vector2){ 0, 0 };
	ctx->last_mouse_pos = ctx->mouse_pos;

	/* sort root containers by zindex */
	n = ctx->root_list.index;
	qsort(ctx->root_list.items, n, sizeof(UI_Container *), compare_zindex);

	/* set root container jump commands */
	for (i = 0; i < n; i++) {
		UI_Container *cnt = ctx->root_list.items[i];
		/* if this is the first container then make the first command jump to it.
		** otherwise set the previous container's tail to jump to this one */
		if (i == 0) {
			UI_Command *cmd = (UI_Command *)ctx->command_list.items;
			cmd->jump.dst = (char *)cnt->head + sizeof(UI_Jump_Command);
		} else {
			UI_Container *prev = ctx->root_list.items[i - 1];
			prev->tail->jump.dst = (char *)cnt->head + sizeof(UI_Jump_Command);
		}
		/* make the last container's tail jump to the end of command list */
		if (i == n - 1) {
			cnt->tail->jump.dst = ctx->command_list.items + ctx->command_list.index;
		}
	}
}

void
ui_set_focus(UI_Context *ctx, UI_ID id)
{
	ctx->focus = id;
	ctx->updated_focus = 1;
}

/* 32bit fnv-1a hash */
#define HASH_INITIAL 2166136261

internal void
hash(UI_ID *hash, const void *data, i32 size)
{
	const unsigned char *p = data;
	while (size--) {
		*hash = (*hash ^ *p++) * 16777619;
	}
}

UI_ID
ui_get_id(UI_Context *ctx, const void *data, i32 size)
{
	i32 index = ctx->id_stack.index;
	UI_ID res = (index > 0) ? ctx->id_stack.items[index - 1] : HASH_INITIAL;
	hash(&res, data, size);
	ctx->last_id = res;
	return res;
}

void
ui_push_id(UI_Context *ctx, const void *data, i32 size)
{
	push(ui_get_id(ctx, data, size), ctx->id_stack);
}

void
ui_pop_id(UI_Context *ctx)
{
	pop(ctx->id_stack);
}

void
ui_push_clip_rect(UI_Context *ctx, Rectangle rect)
{
	Rectangle last = ui_get_clip_rect(ctx);
	push(intersect_rects(rect, last), ctx->clip_stack);
}

void
ui_pop_clip_rect(UI_Context *ctx)
{
	pop(ctx->clip_stack);
}

Rectangle
ui_get_clip_rect(UI_Context *ctx)
{
	expect(ctx->clip_stack.index > 0);
	return ctx->clip_stack.items[ctx->clip_stack.index - 1];
}

UI_Clip_Amount
ui_check_clip(UI_Context *ctx, Rectangle r)
{
	Rectangle cr = ui_get_clip_rect(ctx);
	if (r.x > cr.x + cr.w || r.x + r.w < cr.x ||
			r.y > cr.y + cr.h || r.y + r.h < cr.y   ) { return UI_CLIP_ALL; }
	if (r.x >= cr.x && r.x + r.w <= cr.x + cr.w &&
			r.y >= cr.y && r.y + r.h <= cr.y + cr.h ) { return 0; }
	return UI_CLIP_PART;
}

internal void
push_layout(UI_Context *ctx, Rectangle body, Vector2 scroll)
{
	UI_Layout layout;
	i32 width = 0;
	MEM_ZERO_STRUCT(&layout);
	layout.body = (Rectangle){ body.x - scroll.x, body.y - scroll.y, body.w, body.h };
	layout.max = (Vector2){ -0x1000000, -0x1000000 };
	push(layout, ctx->layout_stack);
	ui_layout_row(ctx, 1, &width, 0);
}

internal UI_Layout *
get_layout(UI_Context *ctx)
{
	return &ctx->layout_stack.items[ctx->layout_stack.index - 1];
}

internal void
pop_container(UI_Context *ctx)
{
	UI_Container *cnt = ui_get_current_container(ctx);
	UI_Layout *layout = get_layout(ctx);
	cnt->content_size.x = layout->max.x - layout->body.x;
	cnt->content_size.y = layout->max.y - layout->body.y;
	/* pop container, layout and id */
	pop(ctx->container_stack);
	pop(ctx->layout_stack);
	ui_pop_id(ctx);
}

UI_Container *
ui_get_current_container(UI_Context *ctx)
{
	expect(ctx->container_stack.index > 0);
	return ctx->container_stack.items[ ctx->container_stack.index - 1 ];
}

internal UI_Container *
get_container(UI_Context *ctx, UI_ID id, int opt)
{
	UI_Container *cnt;
	/* try to get existing container from pool */
	i32 index = ui_pool_get(ctx, ctx->container_pool, UI_CONTAINERPOOL_SIZE, id);
	if (index >= 0) {
		if (ctx->containers[index].open || ~opt & UI_OPT_CLOSED) {
			ui_pool_update(ctx, ctx->container_pool, index);
		}
		return &ctx->containers[index];
	}
	if (opt & UI_OPT_CLOSED) { return NULL; }
	/* container not found in pool: init new container */
	index = ui_pool_init(ctx, ctx->container_pool, UI_CONTAINERPOOL_SIZE, id);
	cnt = &ctx->containers[index];
	memset(cnt, 0, sizeof(*cnt));
	cnt->open = 1;
	ui_bring_to_front(ctx, cnt);
	return cnt;
}

UI_Container *
ui_get_container(UI_Context *ctx, const char *name)
{
	UI_ID id = ui_get_id(ctx, name, strlen(name));
	return get_container(ctx, id, 0);
}

void
ui_bring_to_front(UI_Context *ctx, UI_Container *cnt)
{
	cnt->zindex = ++ctx->last_zindex;
}

/*============================================================================
** pool
**============================================================================*/

i32
ui_pool_init(UI_Context *ctx, UI_Pool_Item *items, i32 len, UI_ID id)
{
	i32 i, n = -1, f = ctx->frame;
	for (i = 0; i < len; i++) {
		if (items[i].last_update < f) {
			f = items[i].last_update;
			n = i;
		}
	}
	expect(n > -1);
	items[n].id = id;
	ui_pool_update(ctx, items, n);
	return n;
}

i32
ui_pool_get(UI_Context *ctx, UI_Pool_Item *items, i32 len, UI_ID id)
{
	i32 i;
	unused(ctx);
	for (i = 0; i < len; i++) {
		if (items[i].id == id) { return i; }
	}
	return -1;
}

void
ui_pool_update(UI_Context *ctx, UI_Pool_Item *items, i32 index)
{
	items[index].last_update = ctx->frame;
}

/*============================================================================
** input handlers
**============================================================================*/

void
ui_input_mousemove(UI_Context *ctx, i32 x, i32 y)
{
	ctx->mouse_pos = (Vector2){ x, y };
}

void
ui_input_mousedown(UI_Context *ctx, i32 x, i32 y, int btn)
{
	ui_input_mousemove(ctx, x, y);
	ctx->mouse_down |= btn;
	ctx->mouse_pressed |= btn;
}

void
ui_input_mouseup(UI_Context *ctx, i32 x, i32 y, int btn)
{
	ui_input_mousemove(ctx, x, y);
	ctx->mouse_down &= ~btn;
}

void
ui_input_scroll(UI_Context *ctx, i32 x, i32 y)
{
	ctx->scroll_delta.x += x;
	ctx->scroll_delta.y += y;
}

void
ui_input_keydown(UI_Context *ctx, i32 key)
{
	ctx->key_pressed |= key;
	ctx->key_down |= key;
}

void
ui_input_keyup(UI_Context *ctx, i32 key)
{
	ctx->key_down &= ~key;
}

void
ui_input_text(UI_Context *ctx, const char *text)
{
	i32 len = strlen(ctx->input_text);
	i32 size = strlen(text) + 1;
	expect(len + size <= (i32)sizeof(ctx->input_text));
	memcpy(ctx->input_text + len, text, size);
}

/*============================================================================
** commandlist
**============================================================================*/

UI_Command *
ui_push_command(UI_Context *ctx, UI_Command_Type type, i32 size)
{
	UI_Command *cmd = (UI_Command *)(ctx->command_list.items + ctx->command_list.index);
	expect(ctx->command_list.index + size < UI_COMMANDLIST_SIZE);
	cmd->base.type = type;
	cmd->base.size = size;
	ctx->command_list.index += size;
	return cmd;
}

int
ui_next_command(UI_Context *ctx, UI_Command **cmd)
{
	if (*cmd) {
		*cmd = (UI_Command *)(((char *)*cmd) + (*cmd)->base.size);
	} else {
		*cmd = (UI_Command *)ctx->command_list.items;
	}
	while ((char *)*cmd != ctx->command_list.items + ctx->command_list.index) {
		if ((*cmd)->type != UI_COMMAND_JUMP) { return 1; }
		*cmd = (*cmd)->jump.dst;
	}
	return 0;
}

internal UI_Command *
push_jump(UI_Context *ctx, UI_Command *dst)
{
	UI_Command *cmd;
	cmd = ui_push_command(ctx, UI_COMMAND_JUMP, sizeof(UI_Jump_Command));
	cmd->jump.dst = dst;
	return cmd;
}

void
ui_set_clip(UI_Context *ctx, Rectangle rect)
{
	UI_Command *cmd;
	cmd = ui_push_command(ctx, UI_COMMAND_CLIP, sizeof(UI_Clip_Command));
	cmd->clip.rect = rect;
}

void
ui_draw_rect(UI_Context *ctx, Rectangle rect, Color color)
{
	UI_Command *cmd;
	rect = intersect_rects(rect, ui_get_clip_rect(ctx));
	if (rect.w > 0 && rect.h > 0) {
		cmd = ui_push_command(ctx, UI_COMMAND_RECT, sizeof(UI_Rect_Command));
		cmd->rect.rect = rect;
		cmd->rect.color = color;
	}
}

void
ui_draw_box(UI_Context *ctx, Rectangle rect, Color color)
{
	ui_draw_rect(ctx, (Rectangle){ rect.x + 1, rect.y, rect.w - 2, 1 }, color);
	ui_draw_rect(ctx, (Rectangle){ rect.x + 1, rect.y + rect.h - 1, rect.w - 2, 1 }, color);
	ui_draw_rect(ctx, (Rectangle){ rect.x, rect.y, 1, rect.h }, color);
	ui_draw_rect(ctx, (Rectangle){ rect.x + rect.w - 1, rect.y, 1, rect.h }, color);
}

void
ui_draw_text(UI_Context *ctx, UI_Font font, const char *str, i32 len, Vector2 pos, Color color)
{
	UI_Command *cmd;
	Rectangle rect = (Rectangle){
		pos.x, pos.y,
		ctx->text_width(font, str, len), ctx->text_height(font),
	};
	UI_Clip_Amount clipped = ui_check_clip(ctx, rect);
	if (clipped == UI_CLIP_ALL ) { return; }
	if (clipped == UI_CLIP_PART) { ui_set_clip(ctx, ui_get_clip_rect(ctx)); }
	/* add command */
	if (len < 0) { len = strlen(str); }
	cmd = ui_push_command(ctx, UI_COMMAND_TEXT, sizeof(UI_Text_Command) + len);
	memcpy(cmd->text.str, str, len);
	cmd->text.str[len] = '\0';
	cmd->text.pos = pos;
	cmd->text.color = color;
	cmd->text.font = font;
	/* reset clipping if it was set */
	if (clipped) { ui_set_clip(ctx, unclipped_rect); }
}

void
ui_draw_icon(UI_Context *ctx, int id, Rectangle rect, Color color)
{
	UI_Command *cmd;
	/* do clip command if the rect isn't fully contained within the cliprect */
	UI_Clip_Amount clipped = ui_check_clip(ctx, rect);
	if (clipped == UI_CLIP_ALL ) { return; }
	if (clipped == UI_CLIP_PART) { ui_set_clip(ctx, ui_get_clip_rect(ctx)); }
	/* do icon command */
	cmd = ui_push_command(ctx, UI_COMMAND_ICON, sizeof(UI_Icon_Command));
	cmd->icon.id = id;
	cmd->icon.rect = rect;
	cmd->icon.color = color;
	/* reset clipping if it was set */
	if (clipped) { ui_set_clip(ctx, unclipped_rect); }
}

/*============================================================================
** layout
**============================================================================*/

enum { RELATIVE = 1, ABSOLUTE = 2 };

void
ui_layout_begin_column(UI_Context *ctx)
{
	push_layout(ctx, ui_layout_next(ctx), (Vector2){ 0, 0 });
}

void
ui_layout_end_column(UI_Context *ctx)
{
	UI_Layout *a, *b;
	b = get_layout(ctx);
	pop(ctx->layout_stack);
	/* inherit position/next_row/max from child layout if they are greater */
	a = get_layout(ctx);
	a->position.x = MAX(a->position.x, b->position.x + b->body.x - a->body.x);
	a->next_row = MAX(a->next_row, b->next_row + b->body.y - a->body.y);
	a->max.x = MAX(a->max.x, b->max.x);
	a->max.y = MAX(a->max.y, b->max.y);
}

void
ui_layout_row(UI_Context *ctx, i32 items, const i32 *widths, i32 height)
{
	UI_Layout *layout = get_layout(ctx);
	if (widths) {
		expect(items <= UI_MAX_WIDTHS);
		memcpy(layout->widths, widths, items * sizeof(widths[0]));
	}
	layout->items = items;
	layout->position = (Vector2){ layout->indent, layout->next_row };
	layout->size.y = height;
	layout->item_index = 0;
}

void
ui_layout_width(UI_Context *ctx, i32 width)
{
	get_layout(ctx)->size.x = width;
}

void
ui_layout_height(UI_Context *ctx, i32 height)
{
	get_layout(ctx)->size.y = height;
}

void
ui_layout_set_next(UI_Context *ctx, Rectangle r, int relative)
{
	UI_Layout *layout = get_layout(ctx);
	layout->next = r;
	layout->next_type = relative ? RELATIVE : ABSOLUTE;
}

Rectangle
ui_layout_next(UI_Context *ctx)
{
	UI_Layout *layout = get_layout(ctx);
	UI_Style *style = ctx->style;
	Rectangle res;

	if (layout->next_type) {
		/* handle rect set by `ui_layout_set_next` */
		int type = layout->next_type;
		layout->next_type = 0;
		res = layout->next;
		if (type == ABSOLUTE) { return (ctx->last_rect = res); }

	} else {
		/* handle next row */
		if (layout->item_index == layout->items) {
			ui_layout_row(ctx, layout->items, NULL, layout->size.y);
		}

		/* position */
		res.x = layout->position.x;
		res.y = layout->position.y;

		/* size */
		res.w = layout->items > 0 ? layout->widths[layout->item_index] : layout->size.x;
		res.h = layout->size.y;
		if (res.w == 0) { res.w = style->size.x + style->padding * 2; }
		if (res.h == 0) { res.h = style->size.y + style->padding * 2; }
		if (res.w <  0) { res.w += layout->body.w - res.x + 1; }
		if (res.h <  0) { res.h += layout->body.h - res.y + 1; }

		layout->item_index++;
	}

	/* update position */
	layout->position.x += res.w + style->spacing;
	layout->next_row = MAX(layout->next_row, res.y + res.h + style->spacing);

	/* apply body offset */
	res.x += layout->body.x;
	res.y += layout->body.y;

	/* update max position */
	layout->max.x = MAX(layout->max.x, res.x + res.w);
	layout->max.y = MAX(layout->max.y, res.y + res.h);

	return (ctx->last_rect = res);
}


/*============================================================================
** controls
**============================================================================*/

internal bool
in_hover_root(UI_Context *ctx)
{
	i32 i = ctx->container_stack.index;
	while (i--) {
		if (ctx->container_stack.items[i] == ctx->hover_root) { return true; }
		/* only root containers have their `head` field set; stop searching if we've
		** reached the current root container */
		if (ctx->container_stack.items[i]->head) { break; }
	}
	return false;
}

void
ui_draw_control_frame(UI_Context *ctx, UI_ID id, Rectangle rect, int colorid, int opt)
{
	if (opt & UI_OPT_NOFRAME) { return; }
	colorid += (ctx->focus == id) ? 2 : (ctx->hover == id) ? 1 : 0;
	ctx->draw_window_frame(ctx, rect, colorid);
}

void
ui_draw_control_text(UI_Context *ctx, const char *str, Rectangle rect, int colorid, int opt)
{
	Vector2 pos;
	UI_Font font = ctx->style->font;
	i32 tw = ctx->text_width(font, str, -1);
	ui_push_clip_rect(ctx, rect);
	pos.y = rect.y + (rect.h - ctx->text_height(font)) / 2;
	if (opt & UI_OPT_ALIGNCENTER) {
		pos.x = rect.x + (rect.w - tw) / 2;
	} else if (opt & UI_OPT_ALIGNRIGHT) {
		pos.x = rect.x + rect.w - tw - ctx->style->padding;
	} else {
		pos.x = rect.x + ctx->style->padding;
	}
	ui_draw_text(ctx, font, str, -1, pos, ctx->style->colors[colorid]);
	ui_pop_clip_rect(ctx);
}

bool
ui_mouse_over(UI_Context *ctx, Rectangle rect)
{
	return rect_overlaps_vec2(rect, ctx->mouse_pos) &&
		rect_overlaps_vec2(ui_get_clip_rect(ctx), ctx->mouse_pos) &&
		in_hover_root(ctx);
}

void
ui_update_control(UI_Context *ctx, UI_ID id, Rectangle rect, int opt)
{
	bool mouseover = ui_mouse_over(ctx, rect);

	if (ctx->focus == id) { ctx->updated_focus = 1; }
	if (opt & UI_OPT_NOINTERACT) { return; }
	if (mouseover && !ctx->mouse_down) { ctx->hover = id; }

	if (ctx->focus == id) {
		if (ctx->mouse_pressed && !mouseover) { ui_set_focus(ctx, 0); }
		if (!ctx->mouse_down && ~opt & UI_OPT_HOLDFOCUS) { ui_set_focus(ctx, 0); }
	}

	if (ctx->hover == id) {
		if (ctx->mouse_pressed) {
			ui_set_focus(ctx, id);
		} else if (!mouseover) {
			ctx->hover = 0;
		}
	}
}

void
ui_text(UI_Context *ctx, const char *text)
{
	const char *start, *end, *p = text;
	i32 width = -1;
	UI_Font font = ctx->style->font;
	Color color = ctx->style->colors[UI_COLOR_TEXT];
	ui_layout_begin_column(ctx);
	ui_layout_row(ctx, 1, &width, ctx->text_height(font));
	do {
		Rectangle r = ui_layout_next(ctx);
		i32 w = 0;
		start = end = p;
		do {
			const char *word = p;
			while (*p && *p != ' ' && *p != '\n') { p++; }
			w += ctx->text_width(font, word, p - word);
			if (w > r.w && end != start) { break; }
			w += ctx->text_width(font, p, 1);
			end = p++;
		} while (*end && *end != '\n');
		ui_draw_text(ctx, font, start, end - start, (Vector2){ r.x, r.y }, color);
		p = end + 1;
	} while (*end);
	ui_layout_end_column(ctx);
}

void
ui_label(UI_Context *ctx, const char *text)
{
	ui_draw_control_text(ctx, text, ui_layout_next(ctx), UI_COLOR_TEXT, 0);
}

int
ui_button_ex(UI_Context *ctx, const char *label, int icon, int opt)
{
	int res = 0;
	UI_ID id = label ? ui_get_id(ctx, label, strlen(label)) : ui_get_id(ctx, &icon, sizeof(icon));
	Rectangle r = ui_layout_next(ctx);
	ui_update_control(ctx, id, r, opt);
	/* handle click */
	if (ctx->mouse_pressed == UI_MOUSE_LEFT && ctx->focus == id) {
		res |= UI_RES_SUBMIT;
	}
	/* draw */
	ui_draw_control_frame(ctx, id, r, UI_COLOR_BUTTON, opt);
	if (label) { ui_draw_control_text(ctx, label, r, UI_COLOR_TEXT, opt); }
	if (icon) { ui_draw_icon(ctx, icon, r, ctx->style->colors[UI_COLOR_TEXT]); }
	return res;
}

int
ui_checkbox(UI_Context *ctx, const char *label, int *state)
{
	int res = 0;
	UI_ID id = ui_get_id(ctx, &state, sizeof(state));
	Rectangle r = ui_layout_next(ctx);
	Rectangle box = (Rectangle){ r.x, r.y, r.h, r.h };
	ui_update_control(ctx, id, r, 0);
	/* handle click */
	if (ctx->mouse_pressed == UI_MOUSE_LEFT && ctx->focus == id) {
		res |= UI_RES_CHANGE;
		*state = !*state;
	}
	/* draw */
	ui_draw_control_frame(ctx, id, box, UI_COLOR_BASE, 0);
	if (*state) {
		ui_draw_icon(ctx, UI_ICON_CHECK, box, ctx->style->colors[UI_COLOR_TEXT]);
	}
	r = (Rectangle){ r.x + box.w, r.y, r.w - box.w, r.h };
	ui_draw_control_text(ctx, label, r, UI_COLOR_TEXT, 0);
	return res;
}


internal int
header(UI_Context *ctx, const char *label, int istreenode, int opt)
{
	Rectangle r;
	int active, expanded;
	UI_ID id = ui_get_id(ctx, label, strlen(label));
	i32 index = ui_pool_get(ctx, ctx->treenode_pool, UI_TREENODEPOOL_SIZE, id);
	i32 width = -1;
	ui_layout_row(ctx, 1, &width, 0);

	active = (index >= 0);
	expanded = (opt & UI_OPT_EXPANDED) ? !active : active;
	r = ui_layout_next(ctx);
	ui_update_control(ctx, id, r, 0);

	/* handle click */
	active ^= (ctx->mouse_pressed == UI_MOUSE_LEFT && ctx->focus == id);

	/* update pool ref */
	if (index >= 0) {
		if (active) {
			ui_pool_update(ctx, ctx->treenode_pool, index);
		} else {
			memset(&ctx->treenode_pool[index], 0, sizeof(UI_Pool_Item));
		}
	} else if (active) {
		ui_pool_init(ctx, ctx->treenode_pool, UI_TREENODEPOOL_SIZE, id);
	}

	/* draw */
	if (istreenode) {
		if (ctx->hover == id) { ctx->draw_window_frame(ctx, r, UI_COLOR_BUTTONHOVER); }
	} else {
		ui_draw_control_frame(ctx, id, r, UI_COLOR_BUTTON, 0);
	}
	ui_draw_icon(
		ctx, expanded ? UI_ICON_EXPANDED : UI_ICON_COLLAPSED,
		(Rectangle){ r.x, r.y, r.h, r.h }, ctx->style->colors[UI_COLOR_TEXT]);
	r.x += r.h - ctx->style->padding;
	r.w -= r.h - ctx->style->padding;
	ui_draw_control_text(ctx, label, r, UI_COLOR_TEXT, 0);

	return expanded ? UI_RES_ACTIVE : 0;
}

int
ui_header_ex(UI_Context *ctx, const char *label, int opt)
{
	return header(ctx, label, 0, opt);
}

int
ui_begin_treenode_ex(UI_Context *ctx, const char *label, int opt)
{
	int res = header(ctx, label, 1, opt);
	if (res & UI_RES_ACTIVE) {
		get_layout(ctx)->indent += ctx->style->indent;
		push(ctx->last_id, ctx->id_stack);
	}
	return res;
}

void
ui_end_treenode(UI_Context *ctx)
{
	get_layout(ctx)->indent -= ctx->style->indent;
	ui_pop_id(ctx);
}

// TODO(ariel) Replace this with two functions: one for vertical, one for
// horizontal.
#define scrollbar(ctx, cnt, b, cs, x, y, w, h) \
	do { \
		/* only add scrollbar if content size is larger than body */            \
		int maxscroll = cs.y - b->h;                                            \
		if (maxscroll > 0 && b->h > 0) {                                        \
			Rectangle base, thumb;                                                \
			UI_ID id = ui_get_id(ctx, "!scrollbar" #y, 11);                       \
			/* get sizing / positioning */                                        \
			base = *b;                                                            \
			base.x = b->x + b->w;                                                 \
			base.w = ctx->style->scrollbar_size;                                  \
			/* handle input */                                                    \
			ui_update_control(ctx, id, base, 0);                                  \
			if (ctx->focus == id && ctx->mouse_down == UI_MOUSE_LEFT) {           \
				cnt->scroll.y += ctx->mouse_delta.y * cs.y / base.h;                \
			}                                                                     \
			/* clamp scroll to limits */                                          \
			cnt->scroll.y = CLAMP(cnt->scroll.y, 0, maxscroll);                   \
			/* draw base and thumb */                                             \
			ctx->draw_window_frame(ctx, base, UI_COLOR_SCROLLBASE);               \
			thumb = base;                                                         \
			thumb.h = MAX(ctx->style->thumb_size, base.h * b->h / cs.y);          \
			thumb.y += cnt->scroll.y * (base.h - thumb.h) / maxscroll;            \
			ctx->draw_window_frame(ctx, thumb, UI_COLOR_SCROLLTHUMB);             \
			/* set this as the scroll_target (will get scrolled on mousewheel) */ \
			/* if the mouse is over it */                                         \
			if (ui_mouse_over(ctx, *b)) { ctx->scroll_target = cnt; }             \
		} else {                                                                \
			cnt->scroll.y = 0;                                                    \
		}                                                                       \
	} while (0)

internal void
scrollbars(UI_Context *ctx, UI_Container *cnt, Rectangle *body)
{
	i32 sz = ctx->style->scrollbar_size;
	Vector2 cs = cnt->content_size;
	cs.x += ctx->style->padding * 2;
	cs.y += ctx->style->padding * 2;

	ui_push_clip_rect(ctx, *body);
	{
		/* resize body to make room for scrollbars */
		if (cs.y > cnt->body.h) { body->w -= sz; }
		if (cs.x > cnt->body.w) { body->h -= sz; }

		/* to create a horizontal or vertical scrollbar almost-identical code is
		** used; only the references to `x|y` `w|h` need to be switched */
		scrollbar(ctx, cnt, body, cs, x, y, w, h);
		scrollbar(ctx, cnt, body, cs, y, x, h, w);
	}
	ui_pop_clip_rect(ctx);
}

internal void
push_container_body(UI_Context *ctx, UI_Container *cnt, Rectangle body, int opt)
{
	if (~opt & UI_OPT_NOSCROLL) { scrollbars(ctx, cnt, &body); }
	push_layout(ctx, expand_rect(body, -ctx->style->padding), cnt->scroll);
	cnt->body = body;
}

internal void
begin_root_container(UI_Context *ctx, UI_Container *cnt)
{
	push(cnt, ctx->container_stack);
	/* push container to roots list and push head command */
	push(cnt, ctx->root_list);
	cnt->head = push_jump(ctx, NULL);
	/* set as hover root if the mouse is overlapping this container and it has a
	** higher zindex than the current hover root */
	if (rect_overlaps_vec2(cnt->rect, ctx->mouse_pos) &&
			(!ctx->next_hover_root || cnt->zindex > ctx->next_hover_root->zindex)
	) {
		ctx->next_hover_root = cnt;
	}
	/* clipping is reset here in case a root-container is made within
	** another root-containers's begin/end block; this prevents the inner
	** root-container being clipped to the outer */
	push(unclipped_rect, ctx->clip_stack);
}

internal void
end_root_container(UI_Context *ctx)
{
	/* push tail 'goto' jump command and set head 'skip' command. the final steps
	** on initing these are done in ui_end() */
	UI_Container *cnt = ui_get_current_container(ctx);
	cnt->tail = push_jump(ctx, NULL);
	cnt->head->jump.dst = ctx->command_list.items + ctx->command_list.index;
	/* pop base clip rect and container */
	ui_pop_clip_rect(ctx);
	pop_container(ctx);
}

int
ui_begin_window_ex(UI_Context *ctx, const char *title, Rectangle rect, int opt)
{
	Rectangle body;
	UI_ID id = ui_get_id(ctx, title, strlen(title));
	UI_Container *cnt = get_container(ctx, id, opt);
	if (!cnt || !cnt->open) { return 0; }
	push(id, ctx->id_stack);

	if (cnt->rect.w == 0) { cnt->rect = rect; }
	begin_root_container(ctx, cnt);
	rect = body = cnt->rect;

	/* draw frame */
	if (~opt & UI_OPT_NOFRAME) {
		ctx->draw_window_frame(ctx, rect, UI_COLOR_WINDOWBG);
	}

	/* do title bar */
	if (~opt & UI_OPT_NOTITLE) {
		Rectangle tr = rect;
		tr.h = ctx->style->title_height;
		ctx->draw_window_frame(ctx, tr, UI_COLOR_TITLEBG);

		/* do title text */
		if (~opt & UI_OPT_NOTITLE) {
			UI_ID id = ui_get_id(ctx, "!title", 6);
			ui_update_control(ctx, id, tr, opt);
			ui_draw_control_text(ctx, title, tr, UI_COLOR_TITLETEXT, opt);
			if (id == ctx->focus && ctx->mouse_down == UI_MOUSE_LEFT) {
				cnt->rect.x += ctx->mouse_delta.x;
				cnt->rect.y += ctx->mouse_delta.y;
			}
			body.y += tr.h;
			body.h -= tr.h;
		}

		/* do `close` button */
		if (~opt & UI_OPT_NOCLOSE) {
			UI_ID id = ui_get_id(ctx, "!close", 6);
			Rectangle r = (Rectangle){ tr.x + tr.w - tr.h, tr.y, tr.h, tr.h };
			tr.w -= r.w;
			ui_draw_icon(ctx, UI_ICON_CLOSE, r, ctx->style->colors[UI_COLOR_TITLETEXT]);
			ui_update_control(ctx, id, r, opt);
			if (ctx->mouse_pressed == UI_MOUSE_LEFT && id == ctx->focus) {
				cnt->open = 0;
			}
		}
	}

	push_container_body(ctx, cnt, body, opt);

	/* do `resize` handle */
	if (~opt & UI_OPT_NORESIZE) {
		int sz = ctx->style->title_height;
		UI_ID id = ui_get_id(ctx, "!resize", 7);
		Rectangle r = (Rectangle){ rect.x + rect.w - sz, rect.y + rect.h - sz, sz, sz };
		ui_update_control(ctx, id, r, opt);
		if (id == ctx->focus && ctx->mouse_down == UI_MOUSE_LEFT) {
			cnt->rect.w = MAX(96, cnt->rect.w + ctx->mouse_delta.x);
			cnt->rect.h = MAX(64, cnt->rect.h + ctx->mouse_delta.y);
		}
	}

	/* resize to content size */
	if (opt & UI_OPT_AUTOSIZE) {
		Rectangle r = get_layout(ctx)->body;
		cnt->rect.w = cnt->content_size.x + (cnt->rect.w - r.w);
		cnt->rect.h = cnt->content_size.y + (cnt->rect.h - r.h);
	}

	/* close if this is a popup window and elsewhere was clicked */
	if (opt & UI_OPT_POPUP && ctx->mouse_pressed && ctx->hover_root != cnt) {
		cnt->open = 0;
	}

	ui_push_clip_rect(ctx, cnt->body);
	return UI_RES_ACTIVE;
}

void
ui_end_window(UI_Context *ctx)
{
	ui_pop_clip_rect(ctx);
	end_root_container(ctx);
}

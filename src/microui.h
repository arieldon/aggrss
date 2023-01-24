/*
** Copyright (c) 2020 rxi
**
** This library is free software; you can redistribute it and/or modify it
** under the terms of the MIT license. See `microui.c` for details.
*/

#ifndef MICROUI_H
#define MICROUI_H

#include "base.h"

enum {
	UI_COMMANDLIST_SIZE     = 256 * 1024,
	UI_ROOTLIST_SIZE        = 32,
	UI_CONTAINERSTACK_SIZE  = 32,
	UI_CLIPSTACK_SIZE       = 32,
	UI_IDSTACK_SIZE         = 32,
	UI_LAYOUTSTACK_SIZE     = 16,
	UI_CONTAINERPOOL_SIZE   = 48,
	UI_TREENODEPOOL_SIZE    = 48,
	UI_MAX_WIDTHS           = 16,
	UI_MAX_FMT              = 127,
};

typedef enum {
	UI_CLIP_PART = 1,
	UI_CLIP_ALL
} UI_Clip_Amount;

typedef enum {
	UI_COMMAND_JUMP = 1,
	UI_COMMAND_CLIP,
	UI_COMMAND_RECT,
	UI_COMMAND_TEXT,
	UI_COMMAND_ICON,
	UI_COMMAND_MAX
} UI_Command_Type;

enum {
	UI_COLOR_TEXT,
	UI_COLOR_BORDER,
	UI_COLOR_WINDOWBG,
	UI_COLOR_TITLEBG,
	UI_COLOR_TITLETEXT,
	UI_COLOR_PANELBG,
	UI_COLOR_BUTTON,
	UI_COLOR_BUTTONHOVER,
	UI_COLOR_BUTTONFOCUS,
	UI_COLOR_BASE,
	UI_COLOR_BASEHOVER,
	UI_COLOR_BASEFOCUS,
	UI_COLOR_SCROLLBASE,
	UI_COLOR_SCROLLTHUMB,
	UI_COLOR_MAX
};

enum {
	UI_ICON_CLOSE = 1,
	UI_ICON_CHECK,
	UI_ICON_COLLAPSED,
	UI_ICON_EXPANDED,
	UI_ICON_MAX
};

enum {
	UI_RES_ACTIVE = (1 << 0),
	UI_RES_SUBMIT = (1 << 1),
	UI_RES_CHANGE = (1 << 2)
};

enum {
	UI_OPT_ALIGNCENTER = (1 << 0),
	UI_OPT_ALIGNRIGHT  = (1 << 1),
	UI_OPT_NOINTERACT  = (1 << 2),
	UI_OPT_NOFRAME     = (1 << 3),
	UI_OPT_NORESIZE    = (1 << 4),
	UI_OPT_NOSCROLL    = (1 << 5),
	UI_OPT_NOCLOSE     = (1 << 6),
	UI_OPT_NOTITLE     = (1 << 7),
	UI_OPT_HOLDFOCUS   = (1 << 8),
	UI_OPT_AUTOSIZE    = (1 << 9),
	UI_OPT_POPUP       = (1 << 10),
	UI_OPT_CLOSED      = (1 << 11),
	UI_OPT_EXPANDED    = (1 << 12)
};

enum {
	UI_MOUSE_LEFT       = (1 << 0),
	UI_MOUSE_RIGHT      = (1 << 1),
	UI_MOUSE_MIDDLE     = (1 << 2)
};

enum {
	UI_KEY_SHIFT        = (1 << 0),
	UI_KEY_CTRL         = (1 << 1),
	UI_KEY_ALT          = (1 << 2),
	UI_KEY_BACKSPACE    = (1 << 3),
	UI_KEY_RETURN       = (1 << 4)
};

typedef struct UI_Context UI_Context;
typedef u32 UI_ID;
typedef void * UI_Font;

typedef struct {
	i32 x, y;
} Vector2;

typedef struct {
	i32 x, y, w, h;
} Rectangle;

typedef struct {
	u8 r, g, b, a;
} Color;

typedef struct {
	UI_ID id;
	i32 last_update;
} UI_Pool_Item;

typedef struct {
	UI_Command_Type type;
	i32 size;
} UI_Base_Command;

typedef struct {
	UI_Base_Command base;
	void *dst;
} UI_Jump_Command;

typedef struct {
	UI_Base_Command base;
	Rectangle rect;
} UI_Clip_Command;

typedef struct {
	UI_Base_Command base;
	Rectangle rect;
	Color color;
} UI_Rect_Command;

typedef struct {
	UI_Base_Command base;
	UI_Font font;
	Vector2 pos;
	Color color;
	char str[1];
} UI_Text_Command;

typedef struct {
	UI_Base_Command base;
	Rectangle rect;
	i32 id;
	Color color;
} UI_Icon_Command;

typedef union {
	int type;
	UI_Base_Command base;
	UI_Jump_Command jump;
	UI_Clip_Command clip;
	UI_Rect_Command rect;
	UI_Text_Command text;
	UI_Icon_Command icon;
} UI_Command;

typedef struct {
	Rectangle body;
	Rectangle next;
	Vector2 position;
	Vector2 size;
	Vector2 max;
	i32 widths[UI_MAX_WIDTHS];
	i32 items;
	i32 item_index;
	i32 next_row;
	i32 next_type;
	i32 indent;
} UI_Layout;

typedef struct {
	UI_Command *head, *tail;
	Rectangle rect;
	Rectangle body;
	Vector2 content_size;
	Vector2 scroll;
	i32 zindex;
	i32 open;
} UI_Container;

typedef struct {
	UI_Font font;
	Vector2 size;
	i32 padding;
	i32 spacing;
	i32 indent;
	i32 title_height;
	i32 scrollbar_size;
	i32 thumb_size;
	Color colors[UI_COLOR_MAX];
} UI_Style;

struct UI_Context {
	/* callbacks */
	int (*text_width)(UI_Font font, const char *str, int len);
	int (*text_height)(UI_Font font);
	void (*draw_window_frame)(UI_Context *ctx, Rectangle rect, int colorid);

	/* core state */
	UI_Style _style;
	UI_Style *style;
	UI_ID hover;
	UI_ID focus;
	UI_ID last_id;
	Rectangle last_rect;
	i32 last_zindex;
	i32 updated_focus;
	i32 frame;
	UI_Container *hover_root;
	UI_Container *next_hover_root;
	UI_Container *scroll_target;

#define STACK(type, size) struct { i32 index; type items[size]; }
	STACK(char, UI_COMMANDLIST_SIZE) command_list;
	STACK(UI_Container *, UI_ROOTLIST_SIZE) root_list;
	STACK(UI_Container *, UI_CONTAINERSTACK_SIZE) container_stack;
	STACK(Rectangle, UI_CLIPSTACK_SIZE) clip_stack;
	STACK(UI_ID, UI_IDSTACK_SIZE) id_stack;
	STACK(UI_Layout, UI_LAYOUTSTACK_SIZE) layout_stack;
#undef STACK

	/* retained state pools */
	UI_Pool_Item container_pool[UI_CONTAINERPOOL_SIZE];
	UI_Container containers[UI_CONTAINERPOOL_SIZE];
	UI_Pool_Item treenode_pool[UI_TREENODEPOOL_SIZE];

	/* input state */
	Vector2 mouse_pos;
	Vector2 last_mouse_pos;
	Vector2 mouse_delta;
	Vector2 scroll_delta;
	int mouse_down;
	int mouse_pressed;
	int key_down;
	int key_pressed;
	char input_text[32];
};

void ui_init(UI_Context *ctx);
void ui_begin(UI_Context *ctx);
void ui_end(UI_Context *ctx);
void ui_set_focus(UI_Context *ctx, UI_ID id);
UI_ID ui_get_id(UI_Context *ctx, const void *data, int size);
void ui_push_id(UI_Context *ctx, const void *data, int size);
void ui_pop_id(UI_Context *ctx);
void ui_push_clip_rect(UI_Context *ctx, Rectangle rect);
void ui_pop_clip_rect(UI_Context *ctx);
Rectangle ui_get_clip_rect(UI_Context *ctx);
UI_Clip_Amount ui_check_clip(UI_Context *ctx, Rectangle r);
UI_Container *ui_get_current_container(UI_Context *ctx);
UI_Container *ui_get_container(UI_Context *ctx, const char *name);
void ui_bring_to_front(UI_Context *ctx, UI_Container *cnt);

int ui_pool_init(UI_Context *ctx, UI_Pool_Item *items, int len, UI_ID id);
int ui_pool_get(UI_Context *ctx, UI_Pool_Item *items, int len, UI_ID id);
void ui_pool_update(UI_Context *ctx, UI_Pool_Item *items, int idx);

void ui_input_mousemove(UI_Context *ctx, int x, int y);
void ui_input_mousedown(UI_Context *ctx, int x, int y, int btn);
void ui_input_mouseup(UI_Context *ctx, int x, int y, int btn);
void ui_input_scroll(UI_Context *ctx, int x, int y);
void ui_input_keydown(UI_Context *ctx, int key);
void ui_input_keyup(UI_Context *ctx, int key);
void ui_input_text(UI_Context *ctx, const char *text);

UI_Command *ui_push_command(UI_Context *ctx, UI_Command_Type type, int size);
int ui_next_command(UI_Context *ctx, UI_Command **cmd);
void ui_set_clip(UI_Context *ctx, Rectangle rect);
void ui_draw_rect(UI_Context *ctx, Rectangle rect, Color color);
void ui_draw_box(UI_Context *ctx, Rectangle rect, Color color);
void ui_draw_text(UI_Context *ctx, UI_Font font, const char *str, int len, Vector2 pos, Color color);
void ui_draw_icon(UI_Context *ctx, int id, Rectangle rect, Color color);

void ui_layout_row(UI_Context *ctx, int items, const int *widths, int height);
void ui_layout_width(UI_Context *ctx, int width);
void ui_layout_height(UI_Context *ctx, int height);
void ui_layout_begin_column(UI_Context *ctx);
void ui_layout_end_column(UI_Context *ctx);
void ui_layout_set_next(UI_Context *ctx, Rectangle r, int relative);
Rectangle ui_layout_next(UI_Context *ctx);

void ui_draw_control_frame(UI_Context *ctx, UI_ID id, Rectangle rect, int colorid, int opt);
void ui_draw_control_text(UI_Context *ctx, const char *str, Rectangle rect, int colorid, int opt);
bool ui_mouse_over(UI_Context *ctx, Rectangle rect);
void ui_update_control(UI_Context *ctx, UI_ID id, Rectangle rect, int opt);

#define ui_button(ctx, label)             ui_button_ex(ctx, label, 0, UI_OPT_ALIGNCENTER)
#define ui_header(ctx, label)             ui_header_ex(ctx, label, 0)
#define ui_begin_treenode(ctx, label)     ui_begin_treenode_ex(ctx, label, 0)
#define ui_begin_window(ctx, title, rect) ui_begin_window_ex(ctx, title, rect, 0)

void ui_text(UI_Context *ctx, const char *text);
void ui_label(UI_Context *ctx, const char *text);
int ui_button_ex(UI_Context *ctx, const char *label, int icon, int opt);
int ui_checkbox(UI_Context *ctx, const char *label, int *state);
int ui_header_ex(UI_Context *ctx, const char *label, int opt);
int ui_begin_treenode_ex(UI_Context *ctx, const char *label, int opt);
void ui_end_treenode(UI_Context *ctx);
int ui_begin_window_ex(UI_Context *ctx, const char *title, Rectangle rect, int opt);
void ui_end_window(UI_Context *ctx);

#endif

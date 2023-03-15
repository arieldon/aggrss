#ifndef UI_H
#define UI_H

#include "arena.h"
#include "base.h"
#include "str.h"

/* ---
 * Primitive UI Types
 * ---
 */

typedef u32 UI_ID;

typedef struct {
	f32 x, y, w, h;
} Quad;

typedef struct {
	u8 r, g, b, a;
} Color;

typedef union {
	struct { i32 x, y; };
	struct { i32 w, h; };
} Vector2;

typedef struct {
	String data;
	i32 cap;
} Buffer;


/* ---
 * UI Elements
 * ---
 */

typedef enum {
	UI_ICON_CLOSE = 0,
	UI_ICON_CHECK,
	UI_ICON_EXPANDED,
	UI_ICON_COLLAPSED,
	UI_ICON_MAX,
} UI_Icon;

enum {
	UI_KEY_BACKSPACE = 1 << 0,
	UI_KEY_RETURN    = 1 << 1,
};

typedef struct {
	struct {
		i32 current_block;
		i32 total_blocks;
	} current_row;

	i32 width;
	i32 height;
	i32 row_height;
	i32 x;
	i32 y;
} UI_Layout;


/* ---
 * UI Context
 * ---
 */

enum { N_MAX_BLOCKS  = 64 };

typedef struct {
	UI_ID id;
	b32 expanded;
	i32 last_frame_updated;
} UI_Block;

typedef struct {
	i32 index;
	UI_Block blocks[N_MAX_BLOCKS];
} UI_Block_Pool;

typedef struct {
	u64 frame;

	i32 mouse_x;
	i32 mouse_y;
	b32 mouse_down;

	i32 scroll_y;
	i32 scroll_delta_y;

	i32 key_press;
	Buffer input_text;

	UI_Layout layout;

	// NOTE(ariel) The user hovers the cursor over hot items (about to interact)
	// and clicks active item (currently interacting).
	UI_ID hot_block;
	UI_ID active_block;
	UI_ID active_keyboard_block;

	// NOTE(ariel) Store some state for each UI block between frames.
	UI_Block_Pool block_pool;
} UI_Context;


/* ---
 * NOTE(ariel) The programmer using this library must define the following
 * function calls to connect the renderer to the UI library.
 * ---
 */

extern void r_draw_rect(Quad dimensions, Color color);
extern void r_draw_text(String text, Vector2 position, Color color);
extern void r_draw_icon(UI_Icon icon_index, Quad dimensions, Color color);

extern i32 r_get_text_width(String text);
extern i32 r_get_text_height(String text);


/* ---
 * Interface
 * ---
 */

void ui_init(void);
void ui_begin(void);
void ui_end(void);

void ui_layout_row(i32 total_blocks);

b32 ui_button(String label);
b32 ui_header(String label);
b32 ui_textbox(Buffer *buffer);
b32 ui_link(String text);
void ui_text(String text);
void ui_label(String text);

void ui_input_mouse_move(i32 x, i32 y);
void ui_input_mouse_down(i32 x, i32 y, i32 mouse_button);
void ui_input_mouse_up(i32 x, i32 y, i32 mouse_button);
void ui_input_mouse_scroll(i32 x, i32 y);
void ui_input_text(char *text);
void ui_input_key(i32 key);

#endif

#ifndef UI_H
#define UI_H

/* ---
 * Primitive UI Types
 * ---
 */

typedef u32 UI_ID;

typedef struct Quad Quad;
struct Quad
{
	f32 x, y, w, h;
};

typedef struct Color Color;
struct Color
{
	u8 r, g, b, a;
};

typedef union Vector2 Vector2;
union Vector2
{
	struct { s32 x, y; };
	struct { s32 w, h; };
};

typedef struct Buffer Buffer;
struct Buffer
{
	String data;
	s32 cap;
};


/* ---
 * UI Elements
 * ---
 */

typedef enum
{
	UI_ICON_CLOSE = 0,
	UI_ICON_CHECK,
	UI_ICON_EXPANDED,
	UI_ICON_COLLAPSED,
	UI_ICON_DOT,
	UI_ICON_MAX,
} UI_Icon;

enum
{
	UI_MOUSE_BUTTON_LEFT  = 1 << 0,
	UI_MOUSE_BUTTON_RIGHT = 1 << 1,
};

enum
{
	UI_KEY_BACKSPACE = 1 << 0,
	UI_KEY_RETURN    = 1 << 1,
	UI_KEY_CONTROL   = 1 << 2,
	UI_KEY_ESCAPE    = 1 << 3,
	UI_KEY_PAGE_UP   = 1 << 4,
	UI_KEY_PAGE_DOWN = 1 << 5,
};

enum
{
	UI_HEADER_EXPANDED   = 1 << 0,
	UI_HEADER_DELETED    = 1 << 1,
	UI_HEADER_OPTIONIZED = 1 << 2,

	UI_HEADER_SHOW_X_BUTTON = 1 << 3,
};

enum
{
	UI_PROMPT_SUBMIT = 1 << 0,
	UI_PROMPT_CANCEL = 1 << 1,
};

typedef struct UI_Layout UI_Layout;
struct UI_Layout
{
	struct
	{
		s32 current_block;
		s32 total_blocks;
	} current_row;

	s32 width;
	s32 height;
	s32 row_height;
	s32 x;
	s32 y;
};

typedef struct UI_Option_List UI_Option_List;
struct UI_Option_List
{
	String *names;
	s32 count;
};

typedef struct UI_Popup_Menu UI_Popup_Menu;
struct UI_Popup_Menu
{
	Quad target;
	UI_Option_List options;

	// NOTE(ariel) The ID on which the selected action in popup menu acts.
	UI_ID id;
};

typedef struct UI_Prompt_Block UI_Prompt_Block;
struct UI_Prompt_Block
{
	Quad target;
	String prompt;
	Buffer *input_buffer;
	UI_ID textbox_id;
};


/* ---
 * UI Context
 * ---
 */

enum { N_MAX_BLOCKS = 256 };

typedef struct UI_Block UI_Block;
struct UI_Block
{
	UI_ID id;
	union
	{
		b32 expanded;
		b32 enabled;
	};
	s32 last_frame_updated;
};

typedef struct UI_Block_Pool UI_Block_Pool;
struct UI_Block_Pool
{
	s32 index;
	UI_Block blocks[N_MAX_BLOCKS];
};

typedef struct UI_Context UI_Context;
struct UI_Context
{
	u64 frame;

	s32 mouse_x;
	s32 mouse_y;
	s32 mouse_down;
	s32 previous_mouse_down;

	s32 scroll_y;
	s32 scroll_delta_y;

	s32 key_press;
	Buffer input_text;

	UI_Layout layout;
	UI_Popup_Menu popup_menu;
	UI_Prompt_Block prompt_block;

	// NOTE(ariel) The user hovers the cursor over hot items (about to interact)
	// and clicks active item (currently interacting).
	UI_ID hot_block;
	UI_ID active_block;
	UI_ID active_keyboard_block;

	// NOTE(ariel) Store some state for each UI block that demands some sort of
	// persistence between frames.
	UI_Block_Pool block_pool;
};


/* ---
 * NOTE(ariel) The programmer using this library must define the following
 * function calls to connect the renderer to the UI library.
 * ---
 */

static void r_draw_rect(Quad dimensions, Color color);
static void r_draw_text(String text, Vector2 position, Color color);
static void r_draw_icon(UI_Icon icon_index, Quad dimensions, Color color);

static void r_set_clip_quad(Quad dimensions);

static s32 r_get_text_width(String text);
static s32 r_get_text_height(String text);


/* ---
 * Interface
 * ---
 */

static void ui_init(void);
static void ui_begin(void);
static void ui_end(void);

static void ui_layout_row(s32 total_blocks);

static b32 ui_button(String label);
static b32 ui_toggle(String label);
static s32 ui_header(String label, s32 options);
static b32 ui_header_expanded(s32 header_state);
static b32 ui_header_deleted(s32 header_state);
static b32 ui_header_optionized(s32 header_state);
static s32 ui_popup_menu(UI_Option_List options);
static b32 ui_textbox(Buffer *buffer, String placeholder);
static b32 ui_link(String text, b32 unread);
static s32 ui_prompt(String prompt, Buffer *input_buffer);
static void ui_text(String text);
static void ui_label(String text);
static void ui_separator(void);

static void ui_input_mouse_move(s32 x, s32 y);
static void ui_input_mouse_down(s32 x, s32 y, s32 mouse_button);
static void ui_input_mouse_up(s32 x, s32 y, s32 mouse_button);
static void ui_input_mouse_scroll(s32 x, s32 y);
static void ui_input_text(char *text);
static void ui_input_key(s32 key);

#endif

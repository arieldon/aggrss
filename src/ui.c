#include "arena.h"
#include "base.h"
#include "str.h"
#include "ui.h"

global Color blank_color = {75, 75, 75, 255};
global Color hot_color = {95, 95, 95, 255};
global Color active_color = {115, 115, 115, 255};
global Color text_color = {230, 230, 230, 255};

global UI_Context ui;

void
ui_init(void)
{
	ui.frame = 0;
	ui.layout.row_height = r_get_text_height(string_literal("")) * 1.2f;

	local_persist char input_text[32];
	ui.input_text.data.str = input_text;
	ui.input_text.data.len = 0;
	ui.input_text.cap = 32;
}

// TODO(ariel) Consolidate where variables of the UI context are reset.
void
ui_begin(void)
{
	ui.hot_block = 0;
	ui.layout.width = 800;
	ui.layout.height = 600;
	ui.layout.x = 0;
	ui.layout.y = -ui.layout.row_height + ui.scroll_y;
}

void
ui_end(void)
{
	// TODO(ariel) Handle if mouse leaves window frame?
	// NOTE(ariel) Reset active block if left untouched by user.
	if (!ui.mouse_down)
	{
		ui.active_block = 0;
	}
	else if (ui.active_block == 0)
	{
		ui.active_block = -1;
	}

	// FIXME(ariel) I don't understand why this works -- review both upward and
	// downward scrolling behavior.
	i32 scroll = ui.scroll_y + ui.scroll_delta_y;
	if (ui.scroll_delta_y > 0)
	{
		ui.scroll_y = CLAMP(ui.scroll_y, 0, scroll);
	}
	else if (ui.scroll_delta_y < 0)
	{
		if (ui.layout.y - ui.scroll_delta_y > ui.layout.height)
		{
			ui.scroll_y = scroll;
		}
	}
	ui.scroll_delta_y = 0;

	ui.input_text.data.len = 0;
	ui.key_press = 0;

	++ui.frame;
}

void
ui_layout_row(i32 total_blocks)
{
	assert(total_blocks > 0);
	ui.layout.x = 0;
	ui.layout.y += ui.layout.row_height;
	ui.layout.current_row.current_block = 0;
	ui.layout.current_row.total_blocks = total_blocks;
}

internal Quad
ui_layout_next_block(void)
{
	Quad next_block = {0};

	// NOTE(ariel) Automagically create a new row for this block if the previous
	// call exhausted the current row.
	if (ui.layout.current_row.current_block == ui.layout.current_row.total_blocks)
	{
		ui_layout_row(1);
	}

	next_block.x = ui.layout.x;
	next_block.y = ui.layout.y;
	next_block.w = ui.layout.width / ui.layout.current_row.total_blocks;
	next_block.h = ui.layout.row_height;

	ui.layout.x += next_block.w;
	++ui.layout.current_row.current_block;

	return next_block;
}

internal inline b32
ui_mouse_overlaps(Quad target)
{
	b32 overlaps =
	(
		ui.mouse_x >= target.x &&
		ui.mouse_x <= target.x + target.w &&
		ui.mouse_y >= target.y &&
		ui.mouse_y <= target.y + target.h
	);
	return overlaps;
}

internal inline b32
ui_click(UI_ID id)
{
	b32 clicked = !ui.mouse_down && id == ui.hot_block && id == ui.active_block;
	return clicked;
}

internal inline void
ui_update_control(UI_ID id, Quad dimensions)
{
	if (ui_mouse_overlaps(dimensions))
	{
		ui.hot_block = id;
		if (ui.active_block == 0 && ui.mouse_down)
		{
			ui.active_block = id;
			ui.active_keyboard_block = 0;
		}
	}
}

internal UI_ID
get_id(String s)
{
	UI_ID hash = 2166136261;
	for (i32 i = 0; i < s.len; ++i)
	{
		hash = (hash ^ s.str[i]) * 16777619;
	}
	return hash;
}

internal i32
find_block(UI_ID id)
{
	for (i32 i = 0; i < N_MAX_BLOCKS; ++i)
	{
		if (id == ui.block_pool.blocks[i].id)
		{
			return i;
		}
	}
	return -1;
}

internal i32
alloc_block(UI_ID id)
{
	i32 block_index = -1;

	if (ui.block_pool.index < N_MAX_BLOCKS)
	{
		block_index = ui.block_pool.index++;
	}
	else
	{
		i32 least_recently_updated_index = 0;
		for (i32 i = 0; i < N_MAX_BLOCKS; ++i)
		{
			least_recently_updated_index = MIN(
				ui.block_pool.blocks[i].last_frame_updated, least_recently_updated_index);
		}
		block_index = least_recently_updated_index;
	}

	assert(block_index != -1);
	ui.block_pool.blocks[block_index].id = id;
	return block_index;
}

internal inline Color
color_block(UI_ID id)
{
	Color color = {0};

	if (id == ui.hot_block)
	{
		if (id == ui.active_block)
		{
			color = active_color;
		}
		else
		{
			color = hot_color;
		}
	}
	else
	{
		color = blank_color;
	}

	return color;
}

internal Vector2
get_text_dimensions(String text)
{
	Vector2 text_dimensions =
	{
		.w = r_get_text_width(text),
		.h = r_get_text_height(text),
	};
	return text_dimensions;
}

b32
ui_button(String label)
{
	UI_ID id = get_id(label);
	Quad target = ui_layout_next_block();

	ui_update_control(id, target);
	Color button_color = color_block(id);

	Vector2 text_dimensions = get_text_dimensions(label);
	Vector2 text_position =
	{
		.x = target.x + (target.w - text_dimensions.w) / 2,
		.y = target.y + (target.h - text_dimensions.h) / 2,
	};
	r_draw_rect(target, button_color);
	r_draw_text(label, text_position, text_color);

	b32 clicked = ui_click(id);
	return clicked;
}

b32
ui_header(String label)
{
	UI_ID id = get_id(label);

	i32 block_index = find_block(id);
	if (block_index == -1)
	{
		block_index = alloc_block(id);
	}
	UI_Block *persistent_block = &ui.block_pool.blocks[block_index];

	ui_layout_row(1);
	Quad target = ui_layout_next_block();

	ui_update_control(id, target);
	Color header_color = color_block(id);
	r_draw_rect(target, header_color);

	i32 icon_index = persistent_block->expanded ? UI_ICON_EXPANDED : UI_ICON_COLLAPSED;
	Quad icon_dimensions =
	{
		.x = target.x,
		.y = target.y,
		.w = 18,
		.h = 18,
	};
	r_draw_icon(icon_index, icon_dimensions, text_color);

	Vector2 text_position =
	{
		.x = target.x + 18 * 1.2,
		.y = target.y,
	};
	r_draw_text(label, text_position, text_color);

	b32 clicked = ui_click(id);
	persistent_block->expanded ^= clicked;
	persistent_block->last_frame_updated = ui.frame;

	return persistent_block->expanded;
}

void
ui_label(String text)
{
	Quad target = ui_layout_next_block();
	Vector2 text_position = (Vector2){
		.x = target.x,
		.y = target.y,
	};
	r_draw_text(text, text_position, text_color);
}

b32
ui_textbox(Buffer *buffer)
{
	b32 submit_text = false;

	// NOTE(ariel) Use the address of the buffer as the unique ID instead of
	// hashing the contents of the buffer since its contents changes as the user
	// types.
	UI_ID id = (uintptr_t)buffer;

	ui_layout_row(1);
	Quad target = ui_layout_next_block();

	ui_update_control(id, target);
	if (!ui.active_keyboard_block)
	{
		ui.active_keyboard_block = ui.active_block;
	}

	if (id == ui.active_keyboard_block)
	{
		i32 n = MIN(buffer->cap - buffer->data.len, ui.input_text.data.len);
		if (n > 0)
		{
			memcpy(buffer->data.str + buffer->data.len, ui.input_text.data.str, n);
			buffer->data.len += n;
		}

		if (ui.key_press & UI_KEY_BACKSPACE && buffer->data.len > 0)
		{
			--buffer->data.len;
		}

		submit_text = ui.key_press & UI_KEY_RETURN;
	}

	if (id == ui.active_keyboard_block)
	{
		Quad cursor =
		{
			.x = r_get_text_width(buffer->data),
			.y = target.y,
			.w = r_get_text_height(buffer->data) / 2,
			.h = r_get_text_height(buffer->data),
		};
		r_draw_rect(target, active_color);
		r_draw_rect(cursor, text_color);
	}
	else
	{
		r_draw_rect(target, blank_color);
	}
	Vector2 text_position =
	{
		.x = target.x,
		.y = target.y,
	};
	r_draw_text(buffer->data, text_position, text_color);

	return submit_text;
}

void
ui_text(String text)
{
	i32 max_width = ui.layout.width - 30;
	String substr = text;

	i32 offset = 0;
	i32 previous_offset = 0;
	for (; offset < text.len; ++offset)
	{
		char c = text.str[offset];
		if (c == ' ' || c == '\n')
		{
			i32 length = offset - previous_offset;
			substr = string_substr(text, previous_offset, length);
			i32 width = r_get_text_width(substr);
			if (width >= max_width)
			{
				Quad target = ui_layout_next_block();
				Vector2 text_position = (Vector2){
					.x = target.x,
					.y = target.y,
				};
				substr = string_trim_spaces(substr);
				r_draw_text(substr, text_position, text_color);

				MEM_ZERO_STRUCT(&substr);
				previous_offset = offset;
			}
		}
	}

	if (previous_offset != offset)
	{
		i32 length = text.len - previous_offset;
		substr = string_substr(text, previous_offset, length);
		Quad target = ui_layout_next_block();
		Vector2 text_position =
		{
			.x = target.x,
			.y = target.y,
		};
		substr = string_trim_spaces(substr);
		r_draw_text(substr, text_position, text_color);
	}
	else if (previous_offset == 0)
	{
		Quad target = ui_layout_next_block();
		Vector2 text_position =
		{
			.x = target.x,
			.y = target.y,
		};
		r_draw_text(text, text_position, text_color);
	}
}

b32
ui_link(String text)
{
	UI_ID id = get_id(text);
	Quad target = ui_layout_next_block();

	ui_update_control(id, target);

	Color text_color = {0};
	{
		Color dull_color = {200, 200, 200, 255};
		Color hot_color = {215, 215, 215, 255};
		Color active_color = {230, 230, 230, 255};
		if (id == ui.hot_block)
		{
			if (id == ui.active_block)
			{
				text_color = active_color;
			}
			else
			{
				text_color = hot_color;
			}
		}
		else
		{
			text_color = dull_color;
		}
	}

	Vector2 text_position =
	{
		.x = target.x,
		.y = target.y,
	};
	r_draw_text(text, text_position, text_color);

	b32 clicked = ui_click(id);
	return clicked;
}

void
ui_input_mouse_move(i32 x, i32 y)
{
	ui.mouse_x = x;
	ui.mouse_y = y;
}

void
ui_input_mouse_down(i32 x, i32 y, i32 mouse_button)
{
	ui_input_mouse_move(x, y);
	if (mouse_button == 1)
	{
		ui.mouse_down = true;
	}
}

void
ui_input_mouse_up(i32 x, i32 y, i32 mouse_button)
{
	ui_input_mouse_move(x, y);
	if (mouse_button == 1)
	{
		ui.mouse_down = false;
	}
}

void
ui_input_mouse_scroll(i32 x, i32 y)
{
	(void)x;
	ui.scroll_delta_y = y * ui.layout.row_height;
}

void
ui_input_text(char *text)
{
	i32 len = strlen(text);
	ui.input_text.data.len = MIN(ui.input_text.cap, len);
	memcpy(ui.input_text.data.str, text, ui.input_text.data.len);
}

void
ui_input_key(i32 key)
{
	ui.key_press |= key;
}

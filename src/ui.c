#include "arena.h"
#include "base.h"
#include "str.h"
#include "ui.h"

enum
{
	POPUP_MENU_LIGHT_PADDING =  5,
	POPUP_MENU_HEAVY_PADDING = 10,
};

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

	local_persist char input_text[128];
	ui.input_text.data.str = input_text;
	ui.input_text.data.len = 0;
	ui.input_text.cap = 128;
}

internal inline b32
ui_mouse_overlaps(Quad target)
{
	b32 overlaps =
		ui.mouse_x >= target.x &&
		ui.mouse_x <= target.x + target.w &&
		ui.mouse_y >= target.y &&
		ui.mouse_y <= target.y + target.h;
	return overlaps;
}

internal inline b32
is_popup_menu_blank(void)
{
	b32 x = !ui.popup_menu.target.x;
	b32 y = !ui.popup_menu.target.y;
	b32 w = !ui.popup_menu.target.w;
	b32 h = !ui.popup_menu.target.h;
	return x | y | w | h;
}

void
ui_begin(void)
{
	ui.hot_block = 0;
	ui.layout.width = 780;
	ui.layout.height = 600;
	ui.layout.x = 0;
	ui.layout.y = -ui.layout.row_height + ui.scroll_y;

	if (!is_popup_menu_blank())
	{
		// NOTE(ariel) If popup menu is open, close it before accepting any other
		// clicks.
		b32 click_outside_menu = ui.mouse_down && !ui_mouse_overlaps(ui.popup_menu.target);
		if (click_outside_menu)
		{
			MEM_ZERO_STRUCT(&ui.popup_menu);
			ui.mouse_down = 0;
		}
	}
}

internal void
ui_popup_menu_options(void)
{
	Color menu_color = {50, 50, 50, 255};
	r_draw_rect(ui.popup_menu.target, menu_color);

	i32 x = POPUP_MENU_LIGHT_PADDING + ui.popup_menu.target.x;
	i32 y = POPUP_MENU_LIGHT_PADDING + ui.popup_menu.target.y;
	for (i32 i = 0; i < ui.popup_menu.options.count; ++i)
	{
		Vector2 text_position =
		{
			.x = x,
			.y = y + ui.layout.row_height * i,
		};
		Quad option_target =
		{
			.x = ui.popup_menu.target.x,
			.y = text_position.y,
			.w = ui.popup_menu.target.w,
			.h = ui.layout.row_height,
		};

		if (ui_mouse_overlaps(option_target))
		{
			Color background_color = ui.mouse_down & UI_MOUSE_BUTTON_LEFT ? active_color : hot_color;
			r_draw_rect(option_target, background_color);
		}
		r_draw_text(ui.popup_menu.options.names[i], text_position, text_color);
	}
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

// NOTE(ariel) This is a close replica of ui_textbox(). If I improve the layout
// engine, I can compress a decent number of lines of code.
internal void
ui_prompt_block(void)
{
	// NOTE(ariel) Draw background.
	Vector2 text_dimensions = get_text_dimensions(ui.prompt_block.prompt);
	Vector2 text_position =
	{
		.x = ui.prompt_block.target.x + 5,
		.y = ui.prompt_block.target.y + text_dimensions.h / 2,
	};
	Color background_color = {30, 30, 30, 255};
	r_draw_rect(ui.prompt_block.target, background_color);
	r_draw_text(ui.prompt_block.prompt, text_position, text_color);

	// NOTE(ariel) Draw textbox.
	Buffer *input_buffer = ui.prompt_block.input_buffer;
	Quad textbox_target =
	{
		.x = text_position.x + text_dimensions.w + 5,
		.y = text_position.y,
		.w = ui.layout.width - textbox_target.x,
		.h = text_dimensions.h,
	};
	Vector2 input_text_dimensions = get_text_dimensions(input_buffer->data);
	Vector2 input_text_position =
	{
		.x = textbox_target.x + 3,
		.y = textbox_target.y,
	};

	if (ui_mouse_overlaps(textbox_target))
	{
		ui.hot_block = ui.prompt_block.textbox_id;
		if (ui.active_block == 0 && ui.mouse_down & UI_MOUSE_BUTTON_LEFT)
		{
			ui.active_block = ui.active_keyboard_block = ui.prompt_block.textbox_id;
		}
	}

	if (ui.prompt_block.textbox_id == ui.active_keyboard_block)
	{
		if (ui.input_text.data.len)
		{
			i32 n = MIN(input_buffer->cap - input_buffer->data.len, ui.input_text.data.len);
			if (n > 0)
			{
				memcpy(input_buffer->data.str + input_buffer->data.len, ui.input_text.data.str, n);
				input_buffer->data.len += n;
			}
		}
		else if (ui.key_press & UI_KEY_BACKSPACE && input_buffer->data.len > 0)
		{
			--input_buffer->data.len;
		}

		Quad cursor =
		{
			.x = input_text_position.x + input_text_dimensions.w,
			.y = textbox_target.y,
			.w = input_text_dimensions.h / 2,
			.h = input_text_dimensions.h,
		};
		r_draw_rect(textbox_target, active_color);
		r_draw_rect(cursor, text_color);
	}
	else
	{
		r_draw_rect(textbox_target, blank_color);
	}
	r_draw_text(input_buffer->data, input_text_position, text_color);
}

void
ui_end(void)
{
	// NOTE(ariel) Draw popup menu and/or prompt entry lazily here so it sits on top
	// of all other blocks.
	if (!is_popup_menu_blank())
	{
		ui_popup_menu_options();
	}
	if (ui.prompt_block.prompt.str)
	{
		ui_prompt_block();
	}

	// NOTE(ariel) Reset active block if left untouched by user.
	if (!(ui.mouse_down & UI_MOUSE_BUTTON_LEFT))
	{
		ui.active_block = 0;
	}
	else if (ui.active_block == 0)
	{
		ui.active_block = -1;
	}

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

	ui.previous_mouse_down = ui.mouse_down;

	++ui.frame;
}

void
ui_layout_row(i32 total_blocks)
{
	assert(total_blocks > 0);
	ui.layout.x = 10;
	ui.layout.y += ui.layout.row_height * 1.3;
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
ui_register_left_click(UI_ID id)
{
	// NOTE(ariel) The user must _release_ the mouse button to complete a click.
	b32 hot = id == ui.hot_block;
	b32 active = id == ui.active_block;
	b32 pressed = ui.previous_mouse_down & UI_MOUSE_BUTTON_LEFT;
	b32 released = !(ui.mouse_down & UI_MOUSE_BUTTON_LEFT);
	b32 clicked = pressed && released && hot && active;
	return clicked;
}

internal inline b32
ui_register_right_click(UI_ID id)
{
	b32 hot = id == ui.hot_block;
	b32 previous_frame_button_state = ui.previous_mouse_down & UI_MOUSE_BUTTON_RIGHT;
	b32 current_frame_button_state = ui.mouse_down & UI_MOUSE_BUTTON_RIGHT;
	b32 clicked = hot && previous_frame_button_state && !current_frame_button_state;
	return clicked;
}

internal inline void
ui_update_control(UI_ID id, Quad dimensions)
{
	if (!ui_mouse_overlaps(ui.prompt_block.target))
	{
		if (ui_mouse_overlaps(dimensions) && !ui_mouse_overlaps(ui.popup_menu.target))
		{
			ui.hot_block = id;
			if (ui.active_block == 0 && ui.mouse_down & UI_MOUSE_BUTTON_LEFT)
			{
				ui.active_block = id;
				ui.active_keyboard_block = 0;
			}
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
	ui.block_pool.blocks[block_index].expanded = false;
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
		.y = target.y + (target.h - text_dimensions.h) / 2 + 2,
	};
	r_draw_rect(target, button_color);
	r_draw_text(label, text_position, text_color);

	b32 clicked = ui_register_left_click(id);
	return clicked;
}

b32
ui_toggle(String label)
{
	UI_ID id = get_id(label);
	Quad target = ui_layout_next_block();

	i32 block_index = find_block(id);
	if (block_index == -1)
	{
		block_index = alloc_block(id);
	}
	UI_Block *persistent_block = &ui.block_pool.blocks[block_index];

	ui_update_control(id, target);

	b32 clicked = ui_register_left_click(id);
	persistent_block->enabled ^= clicked;

	Color toggle_color = color_block(id);
	b32 hot = id == ui.hot_block;
	toggle_color.r += 15 * hot;
	toggle_color.g += 15 * hot;
	toggle_color.b += 15 * hot;
	toggle_color.r += 30 * persistent_block->enabled;
	toggle_color.g += 30 * persistent_block->enabled;
	toggle_color.b += 30 * persistent_block->enabled;

	Vector2 text_dimensions = get_text_dimensions(label);
	Vector2 text_position =
	{
		.x = target.x + (target.w - text_dimensions.w) / 2,
		.y = target.y + (target.h - text_dimensions.h) / 2 + 2,
	};
	r_draw_rect(target, toggle_color);
	r_draw_text(label, text_position, text_color);

	return persistent_block->enabled;
}

i32
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

	i32 chevron_icon_index = persistent_block->expanded ? UI_ICON_EXPANDED : UI_ICON_COLLAPSED;
	Quad chevron_icon_dimensions =
	{
		.x = target.x,
		.y = target.y,
		.w = 18,
		.h = 18,
	};
	r_draw_icon(chevron_icon_index, chevron_icon_dimensions, text_color);

	i32 delete_icon_index = UI_ICON_CLOSE;
	Quad delete_icon_dimensions =
	{
		.x = ui.layout.width - 18,
		.y = target.y,
		.w = 18,
		.h = 18,
	};
	r_draw_icon(delete_icon_index, delete_icon_dimensions, text_color);

	Vector2 text_position =
	{
		.x = target.x + 18 * 1.2,
		.y = target.y + 2,
	};
	r_draw_text(label, text_position, text_color);

	b32 left_clicked = ui_register_left_click(id);
	persistent_block->expanded ^= left_clicked;
	persistent_block->last_frame_updated = ui.frame;

	b32 right_clicked = ui_register_right_click(id);
	if (right_clicked)
	{
		ui.popup_menu.id = id;
		ui.popup_menu.target.x = ui.mouse_x;
		ui.popup_menu.target.y = ui.mouse_y;
	}

	i32 header_state = 0;
	i32 expanded = UI_HEADER_EXPANDED * persistent_block->expanded;
	i32 prompted = UI_HEADER_OPTIONIZED * (id == ui.popup_menu.id);
	i32 deleted = UI_HEADER_DELETED * (left_clicked && ui_mouse_overlaps(delete_icon_dimensions));
	header_state = expanded | deleted | prompted;

	return header_state;
}

inline b32
ui_header_expanded(i32 header_state)
{
	b32 expanded = header_state & UI_HEADER_EXPANDED;
	return expanded;
}

inline b32
ui_header_deleted(i32 header_state)
{
	b32 deleted = header_state & UI_HEADER_DELETED;
	return deleted;
}

inline b32
ui_header_optionized(i32 header_state)
{
	b32 prompted = header_state & UI_HEADER_OPTIONIZED;
	return prompted;
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

void
ui_separator(void)
{
	Quad target = ui_layout_next_block();
	target.h = 1;
	target.y += ui.layout.row_height / 2;
	Color gray = {255, 255, 255, 125};
	r_draw_rect(target, gray);
}

i32
ui_popup_menu(UI_Option_List options)
{
	// NOTE(ariel) Layout the popup menu without issuing any draw calls. Draw the
	// popup menu at the end to ensure it sits atop all other blocks.
	if (is_popup_menu_blank() && ui.popup_menu.id == ui.hot_block)
	{
		for (i32 i = 0; i < options.count; ++i)
		{
			Vector2 text_dimensions = get_text_dimensions(options.names[i]);
			ui.popup_menu.target.w = MAX(ui.popup_menu.target.w, text_dimensions.w);
			ui.popup_menu.target.h += ui.layout.row_height;
		}
		ui.popup_menu.target.w += POPUP_MENU_HEAVY_PADDING;
		ui.popup_menu.target.h += POPUP_MENU_HEAVY_PADDING;

		i32 menu_overflows_x = ui.popup_menu.target.x + ui.popup_menu.target.w > ui.layout.width;
		i32 menu_overflows_y = ui.popup_menu.target.y + ui.popup_menu.target.h > ui.layout.height;
		ui.popup_menu.target.x -= ui.popup_menu.target.w * menu_overflows_x;
		ui.popup_menu.target.y -= ui.popup_menu.target.h * menu_overflows_y;

		// NOTE(ariel) The given options must exist to draw later.
		ui.popup_menu.options = options;
	}
	else
	{
		b32 pressed = ui.previous_mouse_down & UI_MOUSE_BUTTON_LEFT;
		b32 released = !(ui.mouse_down & UI_MOUSE_BUTTON_LEFT);
		if (pressed && released)
		{
			for (i32 i = 0; i < options.count; ++i)
			{
				Quad target =
				{
					.x = ui.popup_menu.target.x,
					.y = ui.popup_menu.target.y + ui.layout.row_height * i,
					.w = ui.popup_menu.target.w,
					.h = ui.layout.row_height,
				};

				b32 overlaps = ui_mouse_overlaps(target);
				if (overlaps)
				{
					MEM_ZERO_STRUCT(&ui.popup_menu);
					return i;
				}
			}
		}
	}
	return -1;
}

b32
ui_textbox(Buffer *buffer, String placeholder)
{
	b32 submit_text = false;

	// NOTE(ariel) Use the address of the buffer as the unique ID instead of
	// hashing the contents of the buffer since its contents changes as the user
	// types.
	UI_ID id = (uintptr_t)buffer;

	ui_layout_row(1);
	Quad target = ui_layout_next_block();

	ui_update_control(id, target);
	if (id == ui.active_block)
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

	String text = {0};
	Color color = {0};
	Vector2 text_dimensions = {0};
	Vector2 text_position = {0};
	if (id == ui.active_keyboard_block || buffer->data.len)
	{
		text = buffer->data;
		color = text_color;
		text_dimensions = get_text_dimensions(text);
		text_position = (Vector2)
		{
			.x = target.x,
			.y = target.y,
		};
	}
	else
	{
		Color placeholder_color = {175, 175, 175, 255};
		text = placeholder;
		color = placeholder_color;
		text_dimensions = get_text_dimensions(text);
		text_position = (Vector2)
		{
			.x = target.x + (target.w - text_dimensions.w) / 2,
			.y = target.y + (target.h - text_dimensions.h) / 2 + 2,
		};
	}

	if (id == ui.active_keyboard_block)
	{
		Quad cursor =
		{
			.x = text_dimensions.w + 10,
			.y = target.y + 1,
			.w = text_dimensions.h / 2,
			.h = text_dimensions.h + 1,
		};
		r_draw_rect(target, active_color);
		r_draw_rect(cursor, text_color);
	}
	else
	{
		r_draw_rect(target, blank_color);
	}
	r_draw_text(text, text_position, color);

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
ui_link(String text, b32 unread)
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

	i32 side_length = r_get_text_height(text);
	Quad unread_marker_target =
	{
		.x = target.x,
		.y = target.y,
		.w = side_length,
		.h = side_length,
	};
	Color unread_marker_color = {245, 165, 65, 255 * unread};
	r_draw_icon(UI_ICON_DOT, unread_marker_target, unread_marker_color);

	Vector2 text_position =
	{
		.x = target.x + side_length,
		.y = target.y,
	};
	r_draw_text(text, text_position, text_color);

	b32 clicked = ui_register_left_click(id);
	return clicked;
}

internal inline b32
is_prompt_block_blank(void)
{
	b32 x = !ui.prompt_block.target.x;
	b32 y = !ui.prompt_block.target.y;
	b32 w = !ui.prompt_block.target.w;
	b32 h = !ui.prompt_block.target.h;
	return x & y & w & h;
}

i32
ui_prompt(String prompt, Buffer *input_buffer)
{
	u32 prompt_state = 0;

	// NOTE(ariel) `prompt` must exist to draw later. Likewise, `buffer` must
	// exist for writes later.
	if (is_prompt_block_blank())
	{
		ui.prompt_block.prompt = prompt;
		ui.prompt_block.input_buffer = input_buffer;
		ui.prompt_block.input_buffer->data.len = 0;

		ui.prompt_block.textbox_id = (uintptr_t)input_buffer;
		ui.active_block = ui.active_keyboard_block = ui.prompt_block.textbox_id;

		ui.prompt_block.target.w = 800;
		ui.prompt_block.target.h = ui.layout.row_height * 2;
		ui.prompt_block.target.x = 0;
		ui.prompt_block.target.y = ui.layout.height - ui.prompt_block.target.h;
	}
	else
	{
		b32 submit_input = UI_PROMPT_SUBMIT * (ui.key_press == UI_KEY_RETURN);
		b32 cancel_prompt = UI_PROMPT_CANCEL * (ui.key_press == UI_KEY_ESCAPE);
		assert((submit_input == 0) || (cancel_prompt == 0));
		prompt_state = submit_input | cancel_prompt;
	}

	if (prompt_state)
	{
		MEM_ZERO_STRUCT(&ui.prompt_block);
	}

	return prompt_state;
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
	ui.mouse_down |= mouse_button;
}

void
ui_input_mouse_up(i32 x, i32 y, i32 mouse_button)
{
	ui_input_mouse_move(x, y);
	ui.mouse_down &= ~mouse_button;
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

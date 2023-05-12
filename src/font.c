#include <stdio.h>
#include <stdlib.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "arena.h"
#include "base.h"
#include "font.h"

typedef struct First_Stage_Glyph First_Stage_Glyph;
struct First_Stage_Glyph
{
	First_Stage_Glyph *next;
	u8 *bitmap;
	u32 index;
	u32 width;
	u32 height;
	u32 x_advance;
	i32 x_offset;
	i32 y_offset;
};

typedef struct First_Stage_Glyph_List First_Stage_Glyph_List;
struct First_Stage_Glyph_List
{
	First_Stage_Glyph *first;
	First_Stage_Glyph *last;
};

typedef struct Font_Data Font_Data;
struct Font_Data
{
	u32 min_glyph_index;
	u32 max_glyph_index;
	Code_Point_Glyph_Index_List code_points;
	First_Stage_Glyph_List glyphs;
};

internal String
load_file(Arena *arena, FILE *file)
{
	String contents = {0};

	fseek(file, 0, SEEK_END);
	contents.len = ftell(file);
	rewind(file);
	contents.str = arena_alloc(arena, contents.len);
	isize len = fread(contents.str, contents.len, sizeof(char), file);
	if (!len)
	{
		contents.str = 0;
		contents.len = 0;
	}
	fclose(file);

	return contents;
}

// TODO(ariel) Use a list of errors as strings instead of printing directly to
// stderr and (intentionally) crashing?
internal Font_Data
parse_font_file(Arena *arena, char *font_file_path)
{
	Font_Data result = {0};

	FT_Library library = {0};
	if (FT_Init_FreeType(&library))
	{
		fprintf(stderr, "error: failed to initialize FreeType library\n");
		exit(EXIT_FAILURE);
	}

#ifdef DEBUG
	{
		i32 major = 0;
		i32 minor = 0;
		i32 patch = 0;
		FT_Library_Version(library, &major, &minor, &patch);
		fprintf(stderr, "FreeType INFO: Version %d.%d.%d\n", major, minor, patch);
	}
#endif

	FILE *font_file = fopen(font_file_path, "rb");
	String font_data = load_file(arena, font_file);

	FT_Face face = {0};
	FT_Open_Args face_args =
	{
		.flags = FT_OPEN_MEMORY,
		.memory_base = (const FT_Byte *)font_data.str,
		.memory_size = font_data.len,
	};
	if (FT_Open_Face(library, &face_args, 0, &face))
	{
		fprintf(stderr, "error: failed to initialize icons face %s\n", font_file_path);
		exit(EXIT_FAILURE);
	}
	if (FT_Set_Pixel_Sizes(face, 0, FONT_SIZE))
	{
		fprintf(stderr, "error: failed to set font size for %s\n\n", font_file_path);
		exit(EXIT_FAILURE);
	}

	// NOTE(ariel) Create a list that maps the code point of each character and
	// the index of its corresponding glyph in the font.
	Code_Point_Glyph_Index_List code_points = {0};
	u32 min_glyph_index = UINT32_MAX;
	u32 max_glyph_index = 0;
	{
		FT_UInt glyph_index = 0;
		FT_ULong char_code = FT_Get_First_Char(face, &glyph_index);
		do
		{
			Code_Point_Glyph_Index_Pair *pair = arena_alloc(arena, sizeof(Code_Point_Glyph_Index_Pair));
			pair->code_point = char_code;
			pair->glyph_index = glyph_index;

			if (!code_points.first)
			{
				code_points.first = pair;
			}
			else if (!code_points.last)
			{
				code_points.first->next = code_points.last = pair;
			}
			else
			{
				code_points.last = code_points.last->next = pair;
			}

			min_glyph_index = MIN(min_glyph_index, glyph_index);
			max_glyph_index = MAX(max_glyph_index, glyph_index);

			char_code = FT_Get_Next_Char(face, char_code, &glyph_index);
		} while (glyph_index);
	}

	// NOTE(ariel) Render each glyph in the font and store the bitmap.
	First_Stage_Glyph_List glyphs = {0};
	{
		FT_Error error = 0;
		for (u32 glyph_index = min_glyph_index; glyph_index <= max_glyph_index; ++glyph_index)
		{
			error = FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT);
			if (error)
			{
				fprintf(stderr, "error: failed to load glyph %u\n", glyph_index);
				continue;
			}

			error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
			if (error)
			{
				fprintf(stderr, "error: failed to render glyph %u\n", glyph_index);
				continue;
			}

			u8 *bitmap_clone = 0;
			FT_Bitmap *bitmap = &face->glyph->bitmap;
			if (bitmap->width && bitmap->rows)
			{
				if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY)
				{
					fprintf(stderr, "error: failed to handle pixel mode %d of bitmap for glyph %u\n",
						bitmap->pixel_mode, glyph_index);
					continue;
				}

				if (bitmap->num_grays != 256)
				{
					fprintf(stderr,
						"error: failed to handle number of gray levels %d in bitmap for glyph %u\n",
						bitmap->num_grays, glyph_index);
					continue;
				}

				// NOTE(ariel) Copy the bitmap if all goes accordingly.
				{
					bitmap_clone = arena_alloc(arena, bitmap->width * bitmap->rows);

					i32 pitch = bitmap->pitch;
					u8 *first_line = bitmap->buffer;
					if (pitch < 0)
					{
						first_line = bitmap->buffer + -pitch * (bitmap->rows - 1);
					}

					u8 *in_line = first_line;
					u8 *out_line = bitmap_clone;
					for (u32 y = 0; y < bitmap->rows; ++y)
					{
						memcpy(out_line, in_line, bitmap->width);
						in_line += pitch;
						out_line += bitmap->width;
					}
				}
			}

			// NOTE(ariel) Append new glyph to list of glyphs.
			{
				First_Stage_Glyph *glyph = arena_alloc(arena, sizeof(First_Stage_Glyph));

				glyph->bitmap = bitmap_clone;
				glyph->index = glyph_index;
				glyph->width = bitmap->width;
				glyph->height = bitmap->rows;
				glyph->x_advance = face->glyph->advance.x >> 6;
				glyph->x_offset = face->glyph->bitmap_left;
				glyph->y_offset = face->glyph->bitmap_top;

				if (!glyphs.first)
				{
					glyphs.first = glyph;
				}
				else if (!glyphs.last)
				{
					glyphs.first->next = glyphs.last = glyph;
				}
				else
				{
					glyphs.last = glyphs.last->next = glyph;
				}
			}
		}
	}

	FT_Done_Face(face);
	FT_Done_FreeType(library);

	assert(max_glyph_index > 0);

	result.min_glyph_index = min_glyph_index;
	result.max_glyph_index = max_glyph_index;
	result.code_points = code_points;
	result.glyphs = glyphs;

	return result;
}

enum { N_CODE_POINT_SLOTS = 128 };

Font_Atlas
bake_font(Arena *arena)
{
	Font_Atlas atlas = {0};

	Arena scratch_arena = {0};
	arena_init(&scratch_arena);

	Font_Data characters = parse_font_file(&scratch_arena, "./assets/RobotoMono-Medium.ttf");
	Font_Data icons = parse_font_file(&scratch_arena, "./assets/icons.ttf");

	// NOTE(ariel) Initialize a data structure to quickly map a code point to a
	// corresponding glyph index based on the results of the initial pass from
	// parse_font_file().
	atlas.code_points = arena_alloc(arena, sizeof(Code_Point_Glyph_Index_List) * N_CODE_POINT_SLOTS);
	for (Code_Point_Glyph_Index_Pair *pair = characters.code_points.first; pair; pair = pair->next)
	{
		Code_Point_Glyph_Index_Pair *pair_clone = arena_alloc(arena, sizeof(Code_Point_Glyph_Index_Pair));
		pair_clone->code_point = pair->code_point;
		pair_clone->glyph_index = pair->glyph_index - characters.min_glyph_index;

		u32 index = pair->code_point % N_CODE_POINT_SLOTS;
		Code_Point_Glyph_Index_List *list = &atlas.code_points[index];
		if (!list->first)
		{
			list->first = pair_clone;
		}
		else if (!list->last)
		{
			list->first->next = list->last = pair_clone;
		}
		else
		{
			list->last = list->last->next = pair_clone;
		}
	}

	// TODO(ariel) Pack rectangle for real.
	atlas.width = BLANK_BITMAP_WIDTH;
	atlas.height = BLANK_BITMAP_HEIGHT;

	memset(atlas.blank, 0xff, sizeof(atlas.blank));

	{
		atlas.n_icon_glyphs = icons.max_glyph_index - icons.min_glyph_index + 1;
		atlas.icon_glyphs = arena_alloc(arena, sizeof(Glyph) * atlas.n_icon_glyphs);
		for (First_Stage_Glyph *glyph = icons.glyphs.first; glyph; glyph = glyph->next)
		{
			u32 adjusted_glyph_index = glyph->index - icons.min_glyph_index;

			atlas.icon_glyphs[adjusted_glyph_index].top = glyph->y_offset;
			atlas.icon_glyphs[adjusted_glyph_index].width = glyph->width;
			atlas.icon_glyphs[adjusted_glyph_index].height = glyph->height;
			atlas.icon_glyphs[adjusted_glyph_index].x_advance = glyph->x_advance;
			atlas.icon_glyphs[adjusted_glyph_index].texture_offset = atlas.width;
			atlas.icon_glyphs[adjusted_glyph_index].bitmap = glyph->bitmap;

			atlas.width += glyph->width;
			atlas.height = MAX(atlas.height, glyph->height);
		}
	}

	{
		atlas.min_glyph_index = characters.min_glyph_index;
		atlas.max_glyph_index = characters.max_glyph_index;
		atlas.n_character_glyphs = atlas.max_glyph_index - atlas.min_glyph_index + 1;
		atlas.character_glyphs = arena_alloc(arena, sizeof(Glyph) * atlas.n_character_glyphs);
		for (First_Stage_Glyph *glyph = characters.glyphs.first; glyph; glyph = glyph->next)
		{
			u32 adjusted_glyph_index = glyph->index - characters.min_glyph_index;

			// TODO(ariel) Match fields by name in the different struct types?
			atlas.character_glyphs[adjusted_glyph_index].top = glyph->y_offset;
			atlas.character_glyphs[adjusted_glyph_index].width = glyph->width;
			atlas.character_glyphs[adjusted_glyph_index].height = glyph->height;
			atlas.character_glyphs[adjusted_glyph_index].x_advance = glyph->width;
			atlas.character_glyphs[adjusted_glyph_index].texture_offset = atlas.width;
			atlas.character_glyphs[adjusted_glyph_index].bitmap = glyph->bitmap;

			atlas.width += glyph->width;
			atlas.height = MAX(atlas.height, glyph->height);
		}

		// NOTE(ariel) Correct `x_advance` for space.
		u32 space_glyph_index = map_code_point_to_glyph_index(&atlas, ' ');
		atlas.character_glyphs[space_glyph_index].x_advance = 10;
	}

	arena_release(&scratch_arena);
	return atlas;
}

u32
map_code_point_to_glyph_index(Font_Atlas *atlas, u32 code_point)
{
	u32 glyph_index = 0;

	u32 index = code_point % N_CODE_POINT_SLOTS;
	Code_Point_Glyph_Index_List *list = &atlas->code_points[index];
	for (Code_Point_Glyph_Index_Pair *pair = list->first; pair; pair = pair->next)
	{
		if (pair->code_point == code_point)
		{
			glyph_index = pair->glyph_index;
			break;
		}
	}

	return glyph_index;
}

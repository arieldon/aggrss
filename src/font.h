#ifndef FONT_H
#define FONT_H

enum
{
	FONT_SIZE = 18,
	BLANK_BITMAP_WIDTH = 3,
	BLANK_BITMAP_HEIGHT = 3,
};

typedef struct Code_Point_Glyph_Index_Pair Code_Point_Glyph_Index_Pair;
struct Code_Point_Glyph_Index_Pair
{
	Code_Point_Glyph_Index_Pair *next;
	u32 code_point;
	u32 glyph_index;
};

typedef struct Code_Point_Glyph_Index_List Code_Point_Glyph_Index_List;
struct Code_Point_Glyph_Index_List
{
	Code_Point_Glyph_Index_Pair *first;
	Code_Point_Glyph_Index_Pair *last;
};

typedef struct Glyph Glyph;
struct Glyph
{
	s32 width;
	s32 height;
	s32 x_advance;
	s32 x_offset;
	s32 y_offset;
	s32 x_texture_offset;
	s32 y_texture_offset;
	u8 *bitmap;
};

typedef struct Font_Atlas Font_Atlas;
struct Font_Atlas
{
	s32 width;
	s32 height;

	u8 blank[BLANK_BITMAP_WIDTH * BLANK_BITMAP_HEIGHT];

	u32 n_character_glyphs;
	Glyph *character_glyphs;
	Code_Point_Glyph_Index_List *code_points;

	u32 n_icon_glyphs;
	Glyph *icon_glyphs;
};

static Font_Atlas bake_font(Arena *arena);
static u32 map_code_point_to_glyph_index(Font_Atlas *atlas, u32 code_point);

#endif

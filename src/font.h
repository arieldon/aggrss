#ifndef FONT_H
#define FONT_H

#include "arena.h"
#include "base.h"

enum { FONT_SIZE = 18 };

typedef struct Code_Point_Glyph_Index_Pair Code_Point_Glyph_Index_Pair;
struct Code_Point_Glyph_Index_Pair
{
	Code_Point_Glyph_Index_Pair *next;
	u32 code_point;
	u32 glyph_index;
};

// TODO(ariel) Use a macro to define this list and the others?
typedef struct Code_Point_Glyph_Index_List Code_Point_Glyph_Index_List;
struct Code_Point_Glyph_Index_List
{
	Code_Point_Glyph_Index_Pair *first;
	Code_Point_Glyph_Index_Pair *last;
};

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
	Code_Point_Glyph_Index_List code_points_to_glyph_indices;
	First_Stage_Glyph_List glyphs;
};

typedef struct Glyph Glyph;
struct Glyph
{
	u32 top;
	u32 width;
	u32 height;
	u32 x_advance;
	u32 texture_offset;
	u8 *bitmap;
};

typedef struct Font_Atlas Font_Atlas;
struct Font_Atlas
{
	u32 width;
	u32 height;
	u32 n_glyphs;
	u32 min_glyph_index;
	u32 max_glyph_index;
	u32 blank_glyph_texture_offset;
	Code_Point_Glyph_Index_List *code_points;
	Glyph *glyphs;
};

Font_Data parse_font_file(Arena *arena);
Font_Atlas bake_font(Arena *arena, Font_Data font_data);
u32 map_code_point_to_glyph_index(Font_Atlas *atlas, u32 code_point);

#endif

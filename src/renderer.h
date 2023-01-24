#ifndef RENDERER_H
#define RENDERER_H

#include "microui.h"

void r_init(void);
void r_draw_rect(Rectangle rect, Color color);
void r_draw_text(const char *text, Vector2 pos, Color color);
void r_draw_icon(int id, Rectangle rect, Color color);

int r_get_text_width(const char *text, int len);
int r_get_text_height(void);

void r_set_clip_rect(Rectangle rect);

void r_clear(Color color);
void r_present(void);

#endif


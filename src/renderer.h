#ifndef RENDERER_H
#define RENDERER_H

#include "arena.h"
#include "ui.h"

void r_init(Arena *arena);

int r_get_text_width(String text);
int r_get_text_height(String text);

void r_set_clip_rect(Quad rect);

void r_clear(Color color);
void r_present(void);

#endif


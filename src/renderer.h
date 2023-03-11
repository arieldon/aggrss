#ifndef RENDERER_H
#define RENDERER_H

#include "arena.h"
#include "ui.h"

void r_init(Arena *arena);
void r_clear(Color color);
void r_present(void);

#endif


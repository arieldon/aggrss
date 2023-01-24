#include "SDL.h"
#include "SDL_opengl.h"

#include "renderer.h"
#include "atlas.inl"

#include "base.h"

enum { BUFFER_SIZE = 16384 };

global GLfloat   tex_buf[BUFFER_SIZE *  8];
global GLfloat  vert_buf[BUFFER_SIZE *  8];
global GLubyte color_buf[BUFFER_SIZE * 16];
global GLuint  index_buf[BUFFER_SIZE *  6];

global i32 width  = 800;
global i32 height = 600;
global i32 buf_idx;

global SDL_Window *window;

void
r_init(void)
{
	/* init SDL window */
	window = SDL_CreateWindow(
		NULL, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		width, height, SDL_WINDOW_OPENGL);
	SDL_GL_CreateContext(window);

	/* init gl */
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);
	glEnable(GL_TEXTURE_2D);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	/* init texture */
	GLuint id;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, ATLAS_WIDTH, ATLAS_HEIGHT, 0,
		GL_ALPHA, GL_UNSIGNED_BYTE, atlas_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	assert(glGetError() == 0);
}

internal void
flush(void)
{
	if (buf_idx) {
		glViewport(0, 0, width, height);
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0.0f, width, height, 0.0f, -1.0f, +1.0f);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();

		glTexCoordPointer(2, GL_FLOAT, 0, tex_buf);
		glVertexPointer(2, GL_FLOAT, 0, vert_buf);
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, color_buf);
		glDrawElements(GL_TRIANGLES, buf_idx * 6, GL_UNSIGNED_INT, index_buf);

		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();

		buf_idx = 0;
	}
}

internal void
push_quad(Rectangle dst, Rectangle src, Color color)
{
	if (buf_idx == BUFFER_SIZE) {
		flush();
	}

	i32 texvert_idx = buf_idx *  8;
	i32   color_idx = buf_idx * 16;
	i32 element_idx = buf_idx *  4;
	i32   index_idx = buf_idx *  6;
	buf_idx++;

	/* update texture buffer */
	f32 x = src.x / (f32)ATLAS_WIDTH;
	f32 y = src.y / (f32)ATLAS_HEIGHT;
	f32 w = src.w / (f32)ATLAS_WIDTH;
	f32 h = src.h / (f32)ATLAS_HEIGHT;
	tex_buf[texvert_idx + 0] = x;
	tex_buf[texvert_idx + 1] = y;
	tex_buf[texvert_idx + 2] = x + w;
	tex_buf[texvert_idx + 3] = y;
	tex_buf[texvert_idx + 4] = x;
	tex_buf[texvert_idx + 5] = y + h;
	tex_buf[texvert_idx + 6] = x + w;
	tex_buf[texvert_idx + 7] = y + h;

	/* update vertex buffer */
	vert_buf[texvert_idx + 0] = dst.x;
	vert_buf[texvert_idx + 1] = dst.y;
	vert_buf[texvert_idx + 2] = dst.x + dst.w;
	vert_buf[texvert_idx + 3] = dst.y;
	vert_buf[texvert_idx + 4] = dst.x;
	vert_buf[texvert_idx + 5] = dst.y + dst.h;
	vert_buf[texvert_idx + 6] = dst.x + dst.w;
	vert_buf[texvert_idx + 7] = dst.y + dst.h;

	/* update color buffer */
	memcpy(color_buf + color_idx +  0, &color, 4);
	memcpy(color_buf + color_idx +  4, &color, 4);
	memcpy(color_buf + color_idx +  8, &color, 4);
	memcpy(color_buf + color_idx + 12, &color, 4);

	/* update index buffer */
	index_buf[index_idx + 0] = element_idx + 0;
	index_buf[index_idx + 1] = element_idx + 1;
	index_buf[index_idx + 2] = element_idx + 2;
	index_buf[index_idx + 3] = element_idx + 2;
	index_buf[index_idx + 4] = element_idx + 3;
	index_buf[index_idx + 5] = element_idx + 1;
}

void
r_draw_rect(Rectangle rect, Color color)
{
	push_quad(rect, atlas[ATLAS_WHITE], color);
}

void
r_draw_text(const char *text, Vector2 pos, Color color)
{
	Rectangle dst = { pos.x, pos.y, 0, 0 };
	for (const char *p = text; *p; p++) {
		if ((*p & 0xc0) == 0x80) {
			continue;
		}

		i32 chr = MIN((u8)*p, 127);
		Rectangle src = atlas[ATLAS_FONT + chr];
		dst.w = src.w;
		dst.h = src.h;
		push_quad(dst, src, color);
		dst.x += dst.w;
	}
}

void
r_draw_icon(int id, Rectangle rect, Color color)
{
	Rectangle src = atlas[id];
	Rectangle quad = {
		.x = rect.x + (rect.w - src.w) / 2,
		.y = rect.y + (rect.h - src.h) / 2,
		.w = src.w,
		.h = src.h,
	};
	push_quad(quad, src, color);
}

i32
r_get_text_width(const char *text, i32 len)
{
	i32 res = 0;
	for (const char *p = text; *p && len--; p++) {
		if ((*p & 0xc0) == 0x80) {
			continue;
		}
		i32 chr = MIN(*p, 127);
		res += atlas[ATLAS_FONT + chr].w;
	}
	return res;
}

i32
r_get_text_height(void)
{
	return 18;
}

void
r_set_clip_rect(Rectangle rect)
{
	flush();
	glScissor(rect.x, height - (rect.y + rect.h), rect.w, rect.h);
}

void
r_clear(Color clr)
{
	flush();
	glClearColor(clr.r / 255.0f, clr.g / 255.0f, clr.b / 255.0f, clr.a / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void
r_present(void)
{
	flush();
	SDL_GL_SwapWindow(window);
}

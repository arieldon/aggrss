#define GLEW_STATIC
#include "GL/glew.h"
#include "SDL.h"
#include "SDL_opengl.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include "arena.h"
#include "base.h"
#include "linalg.h"
#include "renderer.h"

global const char *vertex_shader_source =
"#version 330 core\n"
"uniform mat4 projection;\n"
"layout(location = 0) in vec2 position;\n"
"layout(location = 1) in vec4 color;\n"
"layout(location = 2) in vec2 uv;\n"
"out vec4 output_color;\n"
"out vec2 output_uv;\n"
"void main()\n"
"{\n"
	"gl_Position = projection * vec4(position, 0.0, 1.0);\n"
	"output_color = color;\n"
	"output_uv = uv;\n"
"}\n";
global const char *fragment_shader_source =
"#version 330 core\n"
"uniform sampler2D font;\n"
"in vec4 output_color;\n"
"in vec2 output_uv;\n"
"void main()\n"
"{\n"
	"gl_FragColor = texture(font, output_uv).r * output_color;\n"
"}\n";

enum {
	N_MAX_QUADS = 16384,

	WIDTH  = 800,
	HEIGHT = 600,

	FONT_SIZE = 18,

	BLANK_BITMAP_WIDTH  = 3,
	BLANK_BITMAP_HEIGHT = 3,

	UI_BLANK = UI_ICON_MAX,
};

global GLuint vao;
global GLuint ebo;
global GLuint vbo;
global GLuint shader_program;

global SDL_Window *window;

typedef enum {
	VERTEX_ATTRIB_POSITION = 0,
	VERTEX_ATTRIB_COLOR,
	VERTEX_ATTRIB_UV,
	N_VERTEX_ATTRIBS,
} Vertex_Attribs;

typedef struct {
	Vector2f position;
	Color color;
	Vector2f uv;
} Vertex;

// NOTE(ariel) 4 vertices define 1 quad, and 1 quad consists of 2 triangles or
// 6 total (not necessarily unique) points.
global i32 vertices_cursor;
global u32 indices[N_MAX_QUADS * 6];
global Vertex vertices[N_MAX_QUADS * 4];

#ifdef DEBUG
internal void
MessageCallback(
	GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar *message,
	const void *userParam)
{
	// TODO(ariel) Read debugging section on learnopengl.com to provide more
	// helpful error messages.
	(void)source;
	(void)id;
	(void)severity;
	(void)length;
	(void)userParam;
	if (type == GL_DEBUG_TYPE_ERROR) {
		fprintf(stderr, "GL ERROR: %s\n", message);
	} else {
		fprintf(stderr, "GL INFO: %s\n", message);
	}
}
#endif

typedef struct {
	GLuint handle;
	String error;
} Shader_Handle;

internal Shader_Handle
compile_shader(Arena *arena, const char *const *shader_source, GLenum shader_type)
{
	Shader_Handle result = {0};

	GLuint shader = glCreateShader(shader_type);
	glShaderSource(shader, 1, shader_source, 0);
	glCompileShader(shader);

	GLint compile_status = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
	if (!compile_status) {
		GLint info_log_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
		if (info_log_length > 0) {
			String error = {
				.str = arena_alloc(arena, info_log_length + 1),
				.len = 0,
			};
			glGetShaderInfoLog(shader, info_log_length + 1, &error.len, error.str);
			result.error = error;
		}

		glDeleteShader(shader);
		shader = 0;
	}

	result.handle = shader;
	return result;
}

internal Shader_Handle
link_shader_program(Arena *arena, GLuint *shaders, i32 n_shaders)
{
	Shader_Handle result = {0};

	GLuint program = glCreateProgram();
	for (i32 i = 0; i < n_shaders; ++i) {
		glAttachShader(program, shaders[i]);
	}
	glLinkProgram(program);

	GLint link_status = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (!link_status) {
		GLint info_log_length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
		if (info_log_length) {
			String error = {
				.str = arena_alloc(arena, info_log_length + 1),
				.len = 0,
			};
			glGetProgramInfoLog(program, info_log_length + 1, &error.len, error.str);
			result.error = error;
		}

		glDeleteProgram(program);
		program = 0;
	}

	result.handle = program;
	return result;
}

typedef struct {
	u32 left;
	u32 top;
	u32 width;
	u32 height;
	u32 advance_x;
	u32 texture_offset;
} Glyph;

typedef struct {
	u32 width;
	u32 height;
	Glyph glyphs[128];
} Font_Atlas;

global Font_Atlas atlas;

void
r_init(Arena *arena)
{
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

	window = SDL_CreateWindow(
		"RSS Reader", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		WIDTH, HEIGHT, SDL_WINDOW_OPENGL);
	SDL_GL_CreateContext(window);

	glewExperimental = GL_TRUE;
	glewInit();

#ifdef DEBUG
	{
		i32 major = 0;
		i32 minor = 0;
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
		fprintf(stderr, "GL INFO: Version %d.%d\n", major, minor);
	}

	if (GLEW_ARB_debug_output) {
		glEnable(GL_DEBUG_OUTPUT);
		glDebugMessageCallback(MessageCallback, 0);
	}
#endif

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glViewport(0, 0, WIDTH, HEIGHT);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);

	{
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);

		glGenBuffers(1, &ebo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), 0, GL_STREAM_DRAW);

		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), 0, GL_STREAM_DRAW);

		glVertexAttribPointer(VERTEX_ATTRIB_POSITION, 2, GL_FLOAT, GL_FALSE,
			sizeof(Vertex), (void *)offsetof(Vertex, position));
		glEnableVertexAttribArray(VERTEX_ATTRIB_POSITION);

		glVertexAttribPointer(VERTEX_ATTRIB_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE,
			sizeof(Vertex), (void *)offsetof(Vertex, color));
		glEnableVertexAttribArray(VERTEX_ATTRIB_COLOR);

		glVertexAttribPointer(VERTEX_ATTRIB_UV, 2, GL_FLOAT, GL_FALSE,
			sizeof(Vertex), (void *)offsetof(Vertex, uv));
		glEnableVertexAttribArray(VERTEX_ATTRIB_UV);

		glBindVertexArray(0);
	}

	{
		Shader_Handle vertex_shader = compile_shader(arena, &vertex_shader_source, GL_VERTEX_SHADER);
		if (vertex_shader.error.len) {
			fprintf(stderr, "VERTEX SHADER ERROR: %.*s", vertex_shader.error.len, vertex_shader.error.str);
			exit(EXIT_FAILURE);
		}
		Shader_Handle fragment_shader = compile_shader(arena, &fragment_shader_source, GL_FRAGMENT_SHADER);
		if (fragment_shader.error.len) {
			fprintf(stderr, "FRAGMENT SHADER ERROR: %.*s", fragment_shader.error.len, fragment_shader.error.str);
			exit(EXIT_FAILURE);
		}

		GLuint shaders[] = { vertex_shader.handle, fragment_shader.handle };
		Shader_Handle program = link_shader_program(arena, shaders, ARRAY_COUNT(shaders));
		if (program.error.len) {
			fprintf(stderr, "SHADER PROGRAM ERROR: %.*s", program.error.len, program.error.str);
			exit(EXIT_FAILURE);
		}

		// NOTE(ariel) Upon success, set the global shader variable. Crash
		// otherwise.
		shader_program = program.handle;
		glUseProgram(shader_program);

		glDeleteShader(fragment_shader.handle);
		glDeleteShader(vertex_shader.handle);
	}

	{
		Matrix4f projection = mat4_ortho(0.0f, WIDTH, HEIGHT, 0.0f, -1.0f, +1.0f);
		GLint location = glGetUniformLocation(shader_program, "projection");
		glUniformMatrix4fv(location, 1, GL_FALSE, projection.e[0]);
	}

	{
		GLuint texture = 0;
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		glActiveTexture(GL_TEXTURE0);
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		FT_Library library = {0};
		FT_Face face = {0};
		FT_Face icons = {0};
		const char *font_file_path = "./assets/LiberationSans-Regular.ttf";
		const char *icons_file_path = "./assets/icons.ttf";

		if (FT_Init_FreeType(&library)) {
			fprintf(stderr, "ERROR: failed to initialize FreeType library\n");
			exit(EXIT_FAILURE);
		}
		if (FT_New_Face(library, font_file_path, 0, &face)) {
			fprintf(stderr, "ERROR: failed to initialize font face %s\n", font_file_path);
			exit(EXIT_FAILURE);
		}
		if (FT_New_Face(library, icons_file_path, 0, &icons)) {
			fprintf(stderr, "ERROR: failed to initialize icons face %s\n", icons_file_path);
			exit(EXIT_FAILURE);
		}
		if (FT_Set_Pixel_Sizes(face, 0, FONT_SIZE)) {
			fprintf(stderr, "ERROR: failed to set size of %s\n", font_file_path);
			exit(EXIT_FAILURE);
		}
		if (FT_Set_Pixel_Sizes(icons, 0, FONT_SIZE)) {
			fprintf(stderr, "ERROR: failed to set size of %s\n", icons_file_path);
			exit(EXIT_FAILURE);
		}

		// NOTE(ariel) For simplicity, the font atlas will consist of a single row
		// of glyphs. Compute the dimensions of this atlas.
		{
			// NOTE(ariel) Include standard ASCII characters in dimensions of
			// texture.
			for (i32 i = 32; i < 128; ++i) {
				if (FT_Load_Char(face, i, FT_LOAD_RENDER)) {
					fprintf(stderr, "ERROR: failed to load glyph %d from font\n", i);
					continue;
				}
				atlas.width += face->glyph->bitmap.width;
				if (atlas.height < face->glyph->bitmap.rows) {
					atlas.height = face->glyph->bitmap.rows;
				}
			}

			// NOTE(ariel) Inlcude icons in dimensions of texture.
			FT_UInt glyph_index = 0;
			FT_ULong char_code = FT_Get_First_Char(icons, &glyph_index);
			while (glyph_index) {
				if (FT_Load_Glyph(icons, glyph_index, FT_LOAD_RENDER)) {
					fprintf(stderr, "ERROR: failed to load glyph %d from icons\n", glyph_index);
					goto next;
				}
				atlas.width += icons->glyph->bitmap.width;
				if (atlas.height < icons->glyph->bitmap.rows) {
					atlas.height = icons->glyph->bitmap.rows;
				}
next:
				char_code = FT_Get_Next_Char(icons, char_code, &glyph_index);
			}

			// NOTE(ariel) Include 3x3 blank entry in dimensions texture.
			atlas.width += BLANK_BITMAP_WIDTH;
		}

		// NOTE(ariel) Allocate memory for font atlas on GPU.
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlas.width, atlas.height, 0,
			GL_RED, GL_UNSIGNED_BYTE, 0);

		// NOTE(ariel) Render and copy glyphs into font atlas texture.
		{
			i32 index = 0;
			i32 x_offset = 0;

			// NOTE(ariel) Copy ASCII bitmaps from FreeType into OpenGL texture.
			for (index = 32; index < 128; ++index) {
				FT_Load_Char(face, index, FT_LOAD_RENDER);
				FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);

				atlas.glyphs[index].width  = face->glyph->bitmap.width;
				atlas.glyphs[index].height = face->glyph->bitmap.rows;
				atlas.glyphs[index].left = face->glyph->bitmap_left;
				atlas.glyphs[index].top  = face->glyph->bitmap_top;
				atlas.glyphs[index].advance_x = face->glyph->advance.x >> 6;
				atlas.glyphs[index].texture_offset = x_offset;

				glTexSubImage2D(GL_TEXTURE_2D, 0, x_offset, 0,
					face->glyph->bitmap.width, face->glyph->bitmap.rows,
					GL_RED, GL_UNSIGNED_BYTE, face->glyph->bitmap.buffer);

				x_offset += face->glyph->bitmap.width;
			}

			// NOTE(ariel) Copy icon bitmaps from FreeType into OpenGL texture.
			index = 0;
			FT_UInt glyph_index = 0;
			FT_ULong char_code = FT_Get_First_Char(icons, &glyph_index);
			while (glyph_index) {
				FT_Load_Char(icons, char_code, FT_LOAD_RENDER);
				FT_Render_Glyph(icons->glyph, FT_RENDER_MODE_NORMAL);

				atlas.glyphs[index].width  = icons->glyph->bitmap.width;
				atlas.glyphs[index].height = icons->glyph->bitmap.rows;
				atlas.glyphs[index].left = icons->glyph->bitmap_left;
				atlas.glyphs[index].top  = icons->glyph->bitmap_top;
				atlas.glyphs[index].advance_x = icons->glyph->advance.x >> 6;
				atlas.glyphs[index].texture_offset = x_offset;

				glTexSubImage2D(GL_TEXTURE_2D, 0, x_offset, 0,
					icons->glyph->bitmap.width, icons->glyph->bitmap.rows,
					GL_RED, GL_UNSIGNED_BYTE, icons->glyph->bitmap.buffer);

				++index;
				x_offset += icons->glyph->bitmap.width;
				char_code = FT_Get_Next_Char(icons, char_code, &glyph_index);
			}

			// NOTE(ariel) Confirm icons do not overwrite characters in ASCII range.
			assert(index < 32);

			// NOTE(ariel) Copy blank entry to OpenGL texture.
			u8 blank[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, };
			atlas.glyphs[index].width = BLANK_BITMAP_WIDTH;
			atlas.glyphs[index].height = BLANK_BITMAP_HEIGHT;
			atlas.glyphs[index].texture_offset = x_offset;
			glTexSubImage2D(GL_TEXTURE_2D, 0, x_offset, 0,
				BLANK_BITMAP_WIDTH, BLANK_BITMAP_HEIGHT,
				GL_RED, GL_UNSIGNED_BYTE, blank);
		}

		FT_Done_Face(icons);
		FT_Done_Face(face);
		FT_Done_FreeType(library);
	}

	glUseProgram(0);
	// TODO(ariel) Print a useful error message.
	assert(glGetError() == GL_NO_ERROR);
}

internal void
flush(void)
{
	if (vertices_cursor) {
		glUseProgram(shader_program);
		glBindVertexArray(vao);

		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(u32) * vertices_cursor * 6, indices);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Vertex) * vertices_cursor * 4, vertices);

		glDrawElements(GL_TRIANGLES, vertices_cursor * 6, GL_UNSIGNED_INT, 0);

		glBindVertexArray(0);
		glUseProgram(0);

		vertices_cursor = 0;
	}
}

internal void
push_quad(Rectangle dst, Rectangle src, Color color)
{
	if (vertices_cursor == N_MAX_QUADS) {
		flush();
	}

	i32 vertices_index = vertices_cursor * 4;
	i32 indices_index  = vertices_cursor * 6;

	{
		f32 x = src.x / (f32)atlas.width;
		f32 y = src.y / (f32)atlas.height;
		f32 w = src.w / (f32)atlas.width;
		f32 h = src.h / (f32)atlas.height;

		vertices[vertices_index + 0] = (Vertex){
			.position = { dst.x, dst.y },
			.color = color,
			.uv = { x, y },
		};
		vertices[vertices_index + 1] = (Vertex){
			.position = { dst.x + dst.w, dst.y },
			.color = color,
			.uv = { x + w, y },
		};
		vertices[vertices_index + 2] = (Vertex){
			.position = { dst.x, dst.y + dst.h },
			.color = color,
			.uv = { x, y + h },
		};
		vertices[vertices_index + 3] = (Vertex){
			.position = { dst.x + dst.w, dst.y + dst.h },
			.color = color,
			.uv = { x + w, y + h },
		};
	}

	{
		indices[indices_index + 0] = vertices_index + 0;
		indices[indices_index + 1] = vertices_index + 1;
		indices[indices_index + 2] = vertices_index + 2;
		indices[indices_index + 3] = vertices_index + 2;
		indices[indices_index + 4] = vertices_index + 3;
		indices[indices_index + 5] = vertices_index + 1;
	}

	++vertices_cursor;
}

void
r_draw_rect(Rectangle rect, Color color)
{
	Rectangle source = {
		.x = +(f32)atlas.glyphs[UI_BLANK].texture_offset,
		.y = +(f32)atlas.glyphs[UI_BLANK].height,
		.w = +(f32)atlas.glyphs[UI_BLANK].width,
		.h = -(f32)atlas.glyphs[UI_BLANK].height,
	};
	push_quad(rect, source, color);
}

void
r_draw_text(const char *text, Vector2 pos, Color color)
{
	for (const char *p = text; *p; ++p) {
		i32 glyph_index = MIN(*p, 127);
		Rectangle destination = {
			.x = pos.x,
			.y = pos.y + (atlas.glyphs[glyph_index].height - atlas.glyphs[glyph_index].top),
		};
		Rectangle source = {
			.x = +(f32)atlas.glyphs[glyph_index].texture_offset,
			.y = +(f32)atlas.glyphs[glyph_index].height,
			.w = +(f32)atlas.glyphs[glyph_index].width,
			.h = -(f32)atlas.glyphs[glyph_index].height,
		};
		destination.y += FONT_SIZE - FONT_SIZE / 4;
		destination.w = source.w;
		destination.h = source.h;
		push_quad(destination, source, color);
		pos.x += atlas.glyphs[glyph_index].advance_x;
	}
}

void
r_draw_icon(UI_Icon icon, Rectangle rect, Color color)
{
	assert(icon < UI_ICON_MAX);
	Rectangle source = {
		.x = +(f32)atlas.glyphs[icon].texture_offset,
		.y = +(f32)atlas.glyphs[icon].height,
		.w = +(f32)atlas.glyphs[icon].width,
		.h = -(f32)atlas.glyphs[icon].height,
	};
	Rectangle destination = {
		.x = rect.x + (rect.w - source.w) / 2,
		.y = rect.y + (rect.h - source.h) / 2,
		.w = source.w,
		.h = source.h,
	};
	push_quad(destination, source, color);
}

i32
r_get_text_width(const char *text, i32 len)
{
	i32 res = 0;
	for (const char *p = text; *p && len--; p++) {
		i32 glyph_index = MIN(*p, 127);
		res += atlas.glyphs[glyph_index].width;
	}
	return res;
}

i32
r_get_text_height(void)
{
	return FONT_SIZE;
}

void
r_set_clip_rect(Rectangle rect)
{
	// NOTE(ariel) The scissor test discards fragments outside the dimensions
	// specified by this rectangle, so only pixels within it can be modified.
	flush();
	glScissor(rect.x, HEIGHT - (rect.y + rect.h), rect.w, rect.h);
}

void
r_clear(Color color)
{
	flush();
	glClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void
r_present(void)
{
	flush();
	SDL_GL_SwapWindow(window);
}

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

enum
{
	N_MAX_QUADS = (1 << 16),

	WIDTH  = 800,
	HEIGHT = 600,
};

global Font_Atlas atlas;

global GLuint vao;
global GLuint ebo;
global GLuint vbo;
global GLuint shader_program;

global SDL_Window *window;

typedef enum
{
	VERTEX_ATTRIB_POSITION = 0,
	VERTEX_ATTRIB_COLOR,
	VERTEX_ATTRIB_UV,
	N_VERTEX_ATTRIBS,
} Vertex_Attribs;

typedef struct Vertex Vertex;
struct Vertex
{
	Vector2f position;
	Color color;
	Vector2f uv;
};

// NOTE(ariel) 4 vertices define 1 quad, and 1 quad consists of 2 triangles or
// 6 total (not necessarily unique) points.
global s32 vertices_cursor;
global u32 indices[N_MAX_QUADS * 6];
global Vertex vertices[N_MAX_QUADS * 4];

#ifdef DEBUG
static void
MessageCallback(
	GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar *message,
	const void *userParam)
{
	(void)source;
	(void)id;
	(void)severity;
	(void)length;
	(void)userParam;
	if (type == GL_DEBUG_TYPE_ERROR)
	{
		fprintf(stderr, "GL ERROR: %s\n", message);
	}
	else
	{
		fprintf(stderr, "GL INFO: %s\n", message);
	}
}
#endif

typedef struct Shader_Handle Shader_Handle;
struct Shader_Handle
{
	GLuint handle;
	string error;
};

static Shader_Handle
compile_shader(arena *Arena, const char *const *shader_source, GLenum shader_type)
{
	Shader_Handle result = {0};

	GLuint shader = glCreateShader(shader_type);
	glShaderSource(shader, 1, shader_source, 0);
	glCompileShader(shader);

	GLint compile_status = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
	if (!compile_status)
	{
		GLint info_log_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
		if (info_log_length > 0)
		{
			string error =
			{
				.str = PushBytesToArena(Arena, info_log_length + 1),
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

static Shader_Handle
link_shader_program(arena *Arena, GLuint *shaders, s32 n_shaders)
{
	Shader_Handle result = {0};

	GLuint program = glCreateProgram();
	for (s32 i = 0; i < n_shaders; ++i)
	{
		glAttachShader(program, shaders[i]);
	}
	glLinkProgram(program);

	GLint link_status = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (!link_status)
	{
		GLint info_log_length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
		if (info_log_length)
		{
			string error =
			{
				.str = PushBytesToArena(Arena, info_log_length + 1),
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

static void
r_init(arena *Arena)
{
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

	window = SDL_CreateWindow(
		"aggrss", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		WIDTH, HEIGHT, SDL_WINDOW_OPENGL);
	SDL_GL_CreateContext(window);

	if (!load_gl_procedures())
	{
		fprintf(stderr, "failed to load necessary OpenGL function(s)\n");
		exit(EXIT_FAILURE);
	}

#ifdef DEBUG
	{
		s32 major = 0;
		s32 minor = 0;
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
		fprintf(stderr, "GL INFO: Version %d.%d\n", major, minor);
	}

	if (confirm_gl_extension_support(string_literal("GL_ARB_debug_output")))
	{
		// NOTE(ariel) Enable all debug messages from OpenGL, and deliver them as
		// soon as they occur.
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(MessageCallback, 0);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE);
	}
	else
	{
		fprintf(stderr, "GL INFO: Unable to load debug output extension\n");
	}
#endif

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glViewport(0, 0, WIDTH, HEIGHT);
	glEnable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);
	glEnable(GL_MULTISAMPLE);

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
		Shader_Handle vertex_shader = compile_shader(Arena, &vertex_shader_source, GL_VERTEX_SHADER);
		if (vertex_shader.error.len)
		{
			fprintf(stderr, "VERTEX SHADER ERROR: %.*s", vertex_shader.error.len, vertex_shader.error.str);
			exit(EXIT_FAILURE);
		}
		Shader_Handle fragment_shader = compile_shader(Arena, &fragment_shader_source, GL_FRAGMENT_SHADER);
		if (fragment_shader.error.len)
		{
			fprintf(stderr, "FRAGMENT SHADER ERROR: %.*s", fragment_shader.error.len, fragment_shader.error.str);
			exit(EXIT_FAILURE);
		}

		GLuint shaders[] = { vertex_shader.handle, fragment_shader.handle };
		Shader_Handle program = link_shader_program(Arena, shaders, ARRAY_COUNT(shaders));
		if (program.error.len)
		{
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

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		// NOTE(ariel) Layout font atlas.
		atlas = bake_font(Arena);

		// NOTE(ariel) Render and copy glyphs into font atlas texture.
		{
			// NOTE(ariel) Allocate memory for font atlas on GPU.
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlas.width, atlas.height, 0,
				GL_RED, GL_UNSIGNED_BYTE, 0);

			Glyph *glyph = 0;
			for (u32 i = 0; i < atlas.n_character_glyphs; ++i)
			{
				glyph = &atlas.character_glyphs[i];
				glTexSubImage2D(GL_TEXTURE_2D, 0, glyph->x_texture_offset, glyph->y_texture_offset,
					glyph->width, glyph->height,
					GL_RED, GL_UNSIGNED_BYTE, glyph->bitmap);
			}

			glyph = 0;
			for (u32 i = 0; i < atlas.n_icon_glyphs; ++i)
			{
				glyph = &atlas.icon_glyphs[i];
				glTexSubImage2D(GL_TEXTURE_2D, 0, glyph->x_texture_offset, glyph->y_texture_offset,
					glyph->width, glyph->height,
					GL_RED, GL_UNSIGNED_BYTE, glyph->bitmap);
			}

			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BLANK_BITMAP_WIDTH, BLANK_BITMAP_HEIGHT,
				GL_RED, GL_UNSIGNED_BYTE, atlas.blank);
		}
	}

	glUseProgram(0);
	assert(glGetError() == GL_NO_ERROR);
}

static void
flush(void)
{
	if (vertices_cursor)
	{
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

static void
push_quad(Quad dst, Quad src, Color color)
{
	if (vertices_cursor == N_MAX_QUADS)
	{
		flush();
	}

	s32 vertices_index = vertices_cursor * 4;
	s32 indices_index  = vertices_cursor * 6;

	{
		f32 x = src.x / (f32)atlas.width;
		f32 y = src.y / (f32)atlas.height;
		f32 w = src.w / (f32)atlas.width;
		f32 h = src.h / (f32)atlas.height;

		vertices[vertices_index + 0].position = (Vector2f){ dst.x, dst.y };
		vertices[vertices_index + 0].color = color;
		vertices[vertices_index + 0].uv = (Vector2f){ x, y };

		vertices[vertices_index + 1].position = (Vector2f){ dst.x + dst.w, dst.y };
		vertices[vertices_index + 1].color = color;
		vertices[vertices_index + 1].uv = (Vector2f){ x + w, y };

		vertices[vertices_index + 2].position = (Vector2f){ dst.x, dst.y + dst.h };
		vertices[vertices_index + 2].color = color;
		vertices[vertices_index + 2].uv = (Vector2f){ x, y + h };

		vertices[vertices_index + 3].position = (Vector2f){ dst.x + dst.w, dst.y + dst.h };
		vertices[vertices_index + 3].color = color;
		vertices[vertices_index + 3].uv = (Vector2f){ x + w, y + h };
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

static void
r_draw_rect(Quad rect, Color color)
{
	Quad source = {0};
	push_quad(rect, source, color);
}

typedef struct UTF8_Result UTF8_Result;
struct UTF8_Result
{
	s32 code_point;
	s32 offset_increment;
};

static inline UTF8_Result
decode_utf8_code_point(string s, s32 offset)
{
	UTF8_Result result =
	{
		.code_point = -1,
		.offset_increment = 1,
	};

	if ((unsigned char)s.str[offset] < 0x80)
	{
		if (s.len - offset >= 1)
		{
			result.code_point = s.str[offset];
			result.offset_increment = 1;
		}
	}
	else if ((s.str[offset] & 0xe0) == 0xc0)
	{
		if (s.len - offset >= 2)
		{
			result.code_point =
				((s32)(s.str[offset + 0] & 0x1f) << 6) |
				((s32)(s.str[offset + 1] & 0x3f) << 0);
			result.offset_increment = 2;
		}
	}
	else if ((s.str[offset] & 0xf0) == 0xe0)
	{
		if (s.len - offset >= 3)
		{
			result.code_point =
				((s32)(s.str[offset + 0] & 0x0f) << 12) |
				((s32)(s.str[offset + 1] & 0x3f) <<  6) |
				((s32)(s.str[offset + 2] & 0x3f) <<  0);
			result.offset_increment = 3;
		}
	}
	else if ((s.str[offset] & 0xf8) == 0xf0 && (unsigned char)s.str[offset] <= 0xf4)
	{
		if (s.len - offset >= 4)
		{
			result.code_point =
				((s32)(s.str[offset + 0] & 0x07) << 18) |
				((s32)(s.str[offset + 1] & 0x3f) << 12) |
				((s32)(s.str[offset + 2] & 0x3f) <<  6) |
				((s32)(s.str[offset + 3] & 0x3f) <<  0);
			result.offset_increment = 4;
		}
	}

	if (result.code_point >= 0xd800 && result.code_point <= 0xdfff)
	{
		result.code_point = -1;
	}

	return result;
}

static void
r_draw_text(string text, Vector2 pos, Color color)
{
	UTF8_Result result = {0};
	for (s32 offset = 0; offset < text.len; offset += result.offset_increment)
	{
		result = decode_utf8_code_point(text, offset);
		if (result.code_point != -1)
		{
			u32 glyph_index = map_code_point_to_glyph_index(&atlas, result.code_point);
			Glyph *glyph = &atlas.character_glyphs[glyph_index];

			Quad destination =
			{
				.x = (f32)pos.x,
				.y = (f32)(pos.y - glyph->y_offset),
			};
			Quad source =
			{
				.x = (f32)glyph->x_texture_offset,
				.y = (f32)glyph->y_texture_offset,
				.w = (f32)glyph->width,
				.h = (f32)glyph->height,
			};
			destination.y += FONT_SIZE;
			destination.w = source.w;
			destination.h = source.h;
			push_quad(destination, source, color);

			pos.x += glyph->x_advance;
		}
	}
}

static void
r_draw_icon(UI_Icon icon, Quad rect, Color color)
{
	assert(icon < UI_ICON_MAX);
	Quad source =
	{
		.x = (f32)atlas.icon_glyphs[icon].x_texture_offset,
		.y = (f32)atlas.icon_glyphs[icon].y_texture_offset,
		.w = (f32)atlas.icon_glyphs[icon].width,
		.h = (f32)atlas.icon_glyphs[icon].height,
	};
	Quad destination =
	{
		.x = rect.x + (rect.w - source.w) / 2,
		.y = rect.y + (rect.h - source.h) / 2,
		.w = source.w,
		.h = source.h,
	};
	push_quad(destination, source, color);
}

static void
r_set_clip_quad(Quad dimensions)
{
  flush();
  glScissor((s32)dimensions.x, (s32)dimensions.y, (s32)dimensions.w, (s32)dimensions.h);
}

static s32
r_get_text_width(string text)
{
	s32 width = 0;

	UTF8_Result result = {0};
	for (s32 offset = 0; offset < text.len; offset += result.offset_increment)
	{
		result = decode_utf8_code_point(text, offset);
		if (result.code_point != -1)
		{
			u32 glyph_index = map_code_point_to_glyph_index(&atlas, result.code_point);
			Glyph *glyph = &atlas.character_glyphs[glyph_index];
			s32 glyph_width = MAX(glyph->width, glyph->x_advance);
			width += glyph_width;
		}
	}

	return width;
}

static s32
r_get_text_height(string text)
{
	(void)text;
	return FONT_SIZE;
}

static void
r_clear(Color color)
{
	glClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

static void
r_present(void)
{
	flush();
	SDL_GL_SwapWindow(window);
}

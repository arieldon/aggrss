#define DEFINE_OPENGL_PROCEDURE(type, name) \
	type name = 0;
#define OPENGL_PROCEDURE_TRANSFORM DEFINE_OPENGL_PROCEDURE
DESIRED_OPENGL_PROCEDURES
#undef OPENGL_PROCEDURE_TRANSFORM

typedef union Get_Function_Pointer Get_Function_Pointer;
union Get_Function_Pointer
{
	void *void_pointer;
	void (*function_pointer)(void);
};

static b32
load_gl_procedures(void)
{
	b32 success = true;

#define LOAD_OPENGL_PROCEDURE(type, name) \
	name = (type)(Get_Function_Pointer) \
	{ \
		.void_pointer = SDL_GL_GetProcAddress(#name) \
	} \
	.function_pointer;
#define OPENGL_PROCEDURE_TRANSFORM LOAD_OPENGL_PROCEDURE
	DESIRED_OPENGL_PROCEDURES
#undef OPENGL_PROCEDURE_TRANSFORM

#define VALIDATE_OPENGL_PROCEDURE(type, name) \
	if (!name) \
	{ \
		success = false; \
		fprintf(stderr, "failed to load gl procedure %s\n", #name); \
	}
#define OPENGL_PROCEDURE_TRANSFORM VALIDATE_OPENGL_PROCEDURE
	DESIRED_OPENGL_PROCEDURES
#undef OPENGL_PROCEDURE_TRANSFORM

	return success;
}

static b32
confirm_gl_extension_support(string desired_extension)
{
	b32 extension_support_exists = false;

	s32 nextensions = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &nextensions);
	for (s32 i = 0; i < nextensions; ++i)
	{
		char *raw_extension = (char *)glGetStringi(GL_EXTENSIONS, i);
		string extension =
		{
			.str = raw_extension,
			.len = (s32)strlen(raw_extension),
		};
		if (string_match(desired_extension, extension))
		{
			// NOTE(ariel) This function does _not_ confirm the extension is loaded.
			extension_support_exists = true;
			break;
		}
	}

	return extension_support_exists;
}

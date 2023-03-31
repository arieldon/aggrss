#ifndef LOAD_OPENGL_H
#define LOAD_OPENGL_H

#include "SDL.h"
#include "SDL_opengl.h"

#include "base.h"

#define DESIRED_OPENGL_PROCEDURES \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLCREATESHADERPROC, glCreateShader) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLDELETESHADERPROC, glDeleteShader) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLSHADERSOURCEPROC, glShaderSource) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLCOMPILESHADERPROC, glCompileShader) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLGETSHADERIVPROC, glGetShaderiv) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLATTACHSHADERPROC, glAttachShader) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLLINKPROGRAMPROC, glLinkProgram) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLGENBUFFERSPROC, glGenBuffers) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLBINDBUFFERPROC, glBindBuffer) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLBUFFERDATAPROC, glBufferData) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLVERTEXATTRIBPOINTERARBPROC, glVertexAttribPointer) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLUSEPROGRAMPROC, glUseProgram) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLBUFFERSUBDATAPROC, glBufferSubData) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLGETSTRINGIPROC, glGetStringi) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLDEBUGMESSAGECALLBACKPROC, glDebugMessageCallback) \
	OPENGL_PROCEDURE_TRANSFORM(PFNGLDEBUGMESSAGECONTROLPROC, glDebugMessageControl) \

#define DECLARE_OPENGL_PROCEDURE(type, name) \
	extern type name;
#define OPENGL_PROCEDURE_TRANSFORM DECLARE_OPENGL_PROCEDURE
DESIRED_OPENGL_PROCEDURES
#undef OPENGL_PROCEDURE_TRANSFORM

b32 load_gl_procedures(void);
b32 confirm_gl_extension_support(String desired_extension);

#endif
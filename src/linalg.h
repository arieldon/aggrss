#ifndef LINALG_H
#define LINALG_H

#include "base.h"

#define ONE_DEG_IN_RAD (( 2.0f * M_PI ) / 360.0f)

typedef struct {
	f32 x, y;
} Vector2f;

typedef struct {
	i32 x, y;
} Vector2i;

typedef union {
	struct { f32 x, y, z; };
	struct { f32 r, g, b; };
} Vector3f;

typedef union {
	struct { u8 x, y, z, w; };
	struct { u8 r, g, b, a; };
} Vector4u;

typedef union {
	struct { f32 x, y, z, w; };
	struct { f32 r, g, b, a; };
} Vector4f;

typedef struct { f32 e[4][4]; } Matrix4f;

f32 convert_degrees_to_radians(f32 degrees);
f32 convert_radians_to_degrees(f32 radians);

Matrix4f mat4_identity(void);
Matrix4f mat4_scale(f32 x, f32 y, f32 z);
Matrix4f mat4_translate(f32 x, f32 y, f32 z);
Matrix4f mat4_rotate_x(f32 degrees);
Matrix4f mat4_rotate_y(f32 degrees);
Matrix4f mat4_rotate_z(f32 degrees);
Matrix4f mat4_ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far);
Matrix4f mat4_mul_mat4(Matrix4f a, Matrix4f b);
Vector4f mat4_mul_vec4(Matrix4f m, Vector4f v);

#endif

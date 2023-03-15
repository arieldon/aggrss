#ifndef LINALG_H
#define LINALG_H

#include "base.h"

#define ONE_DEG_IN_RAD (( 2.0f * M_PI ) / 360.0f)

typedef struct Vector2f Vector2f;
struct Vector2f
{
	f32 x, y;
};

typedef struct Vector2i Vector2i;
struct Vector2i
{
	i32 x, y;
};

typedef union Vector3f Vector3f;
union Vector3f
{
	struct { f32 x, y, z; };
	struct { f32 r, g, b; };
};

typedef union Vector4u Vector4u;
union Vector4u
{
	struct { u8 x, y, z, w; };
	struct { u8 r, g, b, a; };
};

typedef union Vector4f Vector4f;
union Vector4f
{
	struct { f32 x, y, z, w; };
	struct { f32 r, g, b, a; };
};

typedef struct Matrix4f Matrix4f;
struct Matrix4f { f32 e[4][4]; };

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

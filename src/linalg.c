#include <math.h>

#include "base.h"
#include "linalg.h"

f32
convert_degrees_to_radians(f32 degrees)
{
	return degrees * (M_PI / 180.0f);
}

f32
convert_radians_to_degrees(f32 radians)
{
	return radians * (180.0f / M_PI);
}

Matrix4f
mat4_identity(void)
{
	Matrix4f identity =
	{
		.e =
		{
			{ 1, 0, 0, 0 },
			{ 0, 1, 0, 0 },
			{ 0, 0, 1, 0 },
			{ 0, 0, 0, 1 },
		}
	};
	return identity;
}

Matrix4f
mat4_scale(f32 x, f32 y, f32 z)
{
	Matrix4f result =
	{
		.e =\
		{
			{ x, 0, 0, 0 },
			{ 0, y, 0, 0 },
			{ 0, 0, z, 0 },
			{ 0, 0, 0, 1 },
		}
	};
	return result;
}

Matrix4f
mat4_translate(f32 x, f32 y, f32 z)
{
	Matrix4f result =
	{
		.e =
		{
			{ 1, 0, 0, 0 },
			{ 0, 1, 0, 0 },
			{ 0, 0, 1, 0 },
			{ x, y, z, 1 },
		}
	};
	return result;
}

Matrix4f
mat4_rotate_x(f32 degrees)
{
	f32 r = convert_degrees_to_radians(degrees);
	f32 c = cos(r);
	f32 s = sin(r);
	Matrix4f result =
	{
		.e =
		{
			{ 1, 0,  0, 0 },
			{ 0, c, -s, 0 },
			{ 0, s,  c, 0 },
			{ 0, 0,  0, 1 },
		}
	};
	return result;
}

Matrix4f
mat4_rotate_y(f32 degrees)
{
	f32 r = convert_degrees_to_radians(degrees);
	f32 c = cos(r);
	f32 s = sin(r);
	Matrix4f result =
	{
		.e =
		{
			{  c, 0, s, 0 },
			{  0, 1, 0, 0 },
			{ -s, 0, c, 0 },
			{  0, 0, 0, 1 },
		}
	};
	return result;
}

Matrix4f
mat4_rotate_z(f32 degrees)
{
	f32 r = convert_degrees_to_radians(degrees);
	f32 c = cos(r);
	f32 s = sin(r);
	Matrix4f result =
	{
		.e =
		{
			{ c, -s, 0, 0 },
			{ s,  c, 0, 0 },
			{ 0,  0, 1, 0 },
			{ 0,  0, 0, 1 },
		}
	};
	return result;
}

Matrix4f
mat4_ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far)
{
	f32 x  = +2.0f / (right - left);
	f32 y  = +2.0f / (top - bottom);
	f32 z  = -2.0f / (far - near);
	f32 tx = -(right + left) / (right - left);
	f32 ty = -(top + bottom) / (top - bottom);
	f32 tz = -(far + near) / (far - near);
	Matrix4f result =
	{
		.e =
		{
			{  x,  0,  0, 0 },
			{  0,  y,  0, 0 },
			{  0,  0,  z, 0 },
			{ tx, ty, tz, 1 },
		}
	};
	return result;
}

Matrix4f
mat4_mul_mat4(Matrix4f a, Matrix4f b)
{
	Matrix4f p = {0};

	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			for (int i = 0; i < 4; ++i)
			{
				p.e[r][c] += a.e[r][i] * b.e[i][c];
			}
		}
	}

	return p;
}

Vector4f
mat4_mul_vec4(Matrix4f m, Vector4f v)
{
	Vector4f result =
	{
		.x = m.e[0][0] * v.x + m.e[1][0] * v.y + m.e[2][0] * v.z + m.e[3][0] * v.w,
		.y = m.e[0][1] * v.x + m.e[1][1] * v.y + m.e[2][1] * v.z + m.e[3][1] * v.w,
		.z = m.e[0][2] * v.x + m.e[1][2] * v.y + m.e[2][2] * v.z + m.e[3][2] * v.w,
		.w = m.e[0][3] * v.x + m.e[1][3] * v.y + m.e[2][3] * v.z + m.e[3][3] * v.w,
	};
	return result;
}

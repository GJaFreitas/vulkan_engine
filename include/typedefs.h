#pragma once


#include <stdint.h>

typedef int8_t		i8;
typedef int16_t		i16;
typedef int32_t		i32;
typedef int64_t		i64;

typedef uint8_t		u8;
typedef uint16_t	u16;
typedef uint32_t	u32;
typedef uint64_t	u64;

typedef struct
{
	float	x,y;
}	vec2;

typedef struct
{
	float	x,y,z;
}	vec3;

typedef struct
{
	float	x,y,z,w;
}	vec4;

typedef struct
{
	float	data[4];
}	mat2;

typedef struct
{
	float	data[9];
}	mat3;

typedef struct
{
	float	data[16];
}	mat4;

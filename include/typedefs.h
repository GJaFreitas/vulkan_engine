#pragma once


#include <stdint.h>

#define sizeofarray(x) (sizeof(x) / sizeof(x[0]))

typedef int8_t		i8;
typedef int16_t		i16;
typedef int32_t		i32;
typedef int64_t		i64;

typedef uint8_t		u8;
typedef uint16_t	u16;
typedef uint32_t	u32;
typedef uint64_t	u64;

typedef float		float32;
typedef float		real32;

typedef double		float64;
typedef double		real64;

// This can represent binary data or typical string
// This structure OWNS memory. If it is deleted data must
// be freed.
typedef struct String
{
	u8	*data;
	u64	count;
}	String;


// The -1 is because the String type doesnt account for the \0 while sizeof() does
#define sizeofString(x) (sizeof(x) - 1)
#define STRING_LIT(x) ((String){(u8 *)(x), sizeofString(x)})

// This structure does not OWN memory, it points to another string
// to view that memory
typedef String StringView;



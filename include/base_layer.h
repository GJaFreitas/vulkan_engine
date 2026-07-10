#pragma once
#include "typedefs.h"
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <cglm/cglm.h>

#include "logs.h"

/* ---------------------- ALLOCATORS ---------------------- */ 
/* -------------------------------------------------------- */ 

#define DEFAULT_ALIGN	16

typedef struct Allocator Allocator;

typedef struct AllocationInfo
{
	Allocator	*allocator;
	u64		allocation_size;
	u64		alignment;
}	AllocationInfo;

// NOTE: I dont know if this is needed (24/06/26)
enum AllocatorType
{
	ALLOCATOR_ARENA,
	ALLOCATOR_HEAP,
	ALLOCATOR_POOL,
};

typedef void* (*FP_AllocationFunction)(Allocator *allocator, u64 size, u64 alignment);
typedef void (*FP_FreeFunction)(Allocator *allocator, void *mem);
typedef void* (*FP_ReallocFunction)(Allocator *allocator, void *original, u64 original_size, u64 size, u64 alignment);

typedef struct
{
	u64		size;
	u64		offset;
	u8		*mem;
}	ArenaAllocator;

typedef struct Allocator
{
	enum AllocatorType	type;
	Allocator		*parent;
	FP_AllocationFunction	fp_allocation;
	FP_FreeFunction		fp_free;
	FP_ReallocFunction	fp_reallocation;

	union
	{
		ArenaAllocator	arena;
	};

}	Allocator;


Allocator	newArenaAllocator(u64 size, Allocator *parent, u64 alignment);

/* -------------------------------------------------------- */ 
/* ---------------------- ALLOCATORS ---------------------- */ 

String	readFile(String filename);
u8	*readFileData(String filename, u64 *file_size);
// No allocations
StringView	getNextLine(String str, u64 *offset);
void	printString(const char *fmt, StringView str);
void	stringViewSkipChar(StringView *s, const char c);
void	stringViewJumpToChar(StringView *s, const char c);
u8	*stringViewPtrToChar(StringView s, const char c);
bool	stringIsEqual(String s1, String s2);
char	*cstrdup(const char *str, u64 *size, Allocator *allocator);
String	stringDup(StringView str, Allocator *allocator);
String	createString(const char *cstr);
String	stringCopy(const String str);
void	stringViewAdvance(StringView *s, u64 count);
char	*stringToCstr(String s, Allocator *allocator);


u64	queryTimer(void);
u64	getFrameDeltaNano(void);
double	getFrameDelta(void);



// Callbacks
// ---------

typedef void (*FP_HotloadCallback)(void *data);

void	start_hotload_callbacks(void);
void	register_callback(String filename, FP_HotloadCallback function, void *user_data);
void	do_callbacks(void);

#include "stb_sprintf.h"

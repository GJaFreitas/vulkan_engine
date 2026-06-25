#pragma once
#include "typedefs.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* ---------------------- ALLOCATORS ---------------------- */ 
/* -------------------------------------------------------- */ 

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

// The -1 is because the String type doesnt account for the \0 while sizeof() does
#define sizeofString(x) (sizeof(x) - 1)

// This can represent binary data or typical string
// This structure OWNS memory. If it is deleted data must
// be freed.
typedef struct String
{
	u8	*data;
	u64	count;
}	String;

// This structure does not OWN memory, it points to another string
// to view that memory
typedef String StringView;

String	readFile(const char *filename);
// No allocations
StringView	getNextLine(String str, u64 *offset);
void	printString(const char *fmt, StringView str);
void	stringViewSkipChar(StringView *s, const char c);
void	stringViewJumpToChar(StringView *s, const char c);
u8	*stringViewPtrToChar(StringView s, const char c);
bool	stringIsEqual(String s1, String s2);
char	*cstrdup(const char *str, u64 *size, Allocator *allocator);
String	stringDup(StringView str, Allocator *allocator);


//  ──────────────────────────────── LOGS ─────────────────────────────

void	start_logs(void);
void	print_logs(void);
void	engine_warn(const char *file, const char *fmt, ...);
void	engine_error(const char *file, const char *fmt, ...);
void	engine_log(const char *file, const char *fmt, ...);

//  ──────────────────────────────── LOGS ─────────────────────────────
#include "stb_sprintf.h"

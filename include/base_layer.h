#include "typedefs.h"

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

// This can represent binary data or typical string
typedef struct String
{
	u8	*data;
	u64	size;
}	String;

typedef String StringView;

String	readFile(const char *filename);
// Allocates memory
String	getNextLine(String str, u64 *offset, Allocator *allocator);
// No allocations
StringView	getNextLine_noMem(String str, u64 *offset);
void	printString(const char *fmt, StringView str);
void	stringViewSkipChar(StringView *s, const char c);
char	*cstrdup(const char *str, u64 *size, Allocator *allocator);


//  ──────────────────────────────── LOGS ─────────────────────────────

void	engine_log(const char *file, const char *fmt, ...);

//  ──────────────────────────────── LOGS ─────────────────────────────

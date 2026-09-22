#pragma once

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "typedefs.h"
#include "logging.h"

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
typedef void* (*FP_ReallocFunction)(Allocator *allocator, void *original, u64 original_size, u64 new_size, u64 alignment);
typedef void (*FP_Reset)(Allocator *allocator);

typedef struct
{
	u64		size;
	u64		offset;
	u8		*mem;
	void		*last;
}	ArenaAllocator;

typedef struct PoolAllocator
{
	u64	capacity;
	u64	elem_size;

	bool	*used;
	u8	*slots;

	u64	free_count;
	u64	hint;
}	PoolAllocator;

typedef struct HeapEntry
{
	bool	used;
	u32	entry_size;	// How many blocks this allocation occupies
}	HeapEntry;

// kind of like a pool but for 64k blocks. When more than 64k is asked for
// it returns 2 blocks
typedef struct HeapAllocator
{
	u32		num_slots;
	u8		*mem;
	HeapEntry	*blocks;

	u32		hint;
	u32		free_blocks;

}	HeapAllocator;

typedef struct Allocator
{
	enum AllocatorType	type;
	Allocator		*parent;
	FP_AllocationFunction	fp_allocation;
	FP_FreeFunction		fp_free;
	FP_ReallocFunction	fp_reallocation;
	FP_Reset		fp_reset;

	union
	{
		ArenaAllocator	arena;
		PoolAllocator	pool;
		HeapAllocator	heap;
	};

}	Allocator;

#define POOL_ALLOCATE(x) ((x)->fp_allocation((x), 0, 0))
#define POOL_FREE(pool, ptr) ((x)->fp_free((x), ptr))

void		*standard_alloc(u64 size, u64 alignment);
Allocator	newArenaAllocator(u64 size, Allocator *parent, u64 alignment);
Allocator	newPoolAllocator(u64 capacity, u64 elem_size, Allocator *parent, u64 alignment);
Allocator	newHeapAllocator(u64 max, Allocator *parent, u64 alignment);
u64		heapGetRealAllocationSize(Allocator *a, void *ptr);

void		testPool();
void		testHeapAllocator(void);


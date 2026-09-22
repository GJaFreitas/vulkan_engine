#include "allocators.h"
#include "str.h"

// TODO: This is wrong of course
void	*standard_alloc(u64 size, u64 alignment)
{
	(void)alignment;
	return (malloc(size));
}

static inline u64	align_up(u64 value, u64 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

void	*arenaAllocationFunction(Allocator *allocator, u64 size, u64 alignment)
{
	void	*mem;

	const u64	a_size = allocator->arena.size;
	const u8	*a_mem = allocator->arena.mem;
	const u64	a_offset = allocator->arena.offset;

	const u64	aligned_offset = align_up(a_offset, alignment);
	const u64	new_offset = aligned_offset + size;

	if (new_offset >= a_size) {
		engine_error(LOG_FILE, "Arena size too small for allocation, size: %ld alignment: %ld arena size: %ld\n", size, alignment, a_size);
		return NULL;
	}

	mem = (u8 *)a_mem + aligned_offset;
	allocator->arena.offset = new_offset;
	allocator->arena.last = mem;
	return mem;
}

void	arenaFreeFunction(Allocator *allocator, void *mem)
{
	ArenaAllocator	*a = &allocator->arena;

	if (a->last == mem) {
		const u64	last_offset = (u8 *)mem - a->mem;
		a->offset = last_offset;
	}
}

void	arenaResetFunction(Allocator *allocator)
{
	memset(allocator->arena.mem, 0, allocator->arena.offset);
	allocator->arena.offset = 0;
}

void	*dudReallocFunction(Allocator *allocator, void *original, u64 original_size, u64 size, u64 alignment)
{
	(void)allocator;
	(void)original;
	(void)original_size;
	(void)size;
	(void)alignment;
	return NULL;
}

Allocator	newArenaAllocator(u64 size, Allocator *parent, u64 alignment)
{
	Allocator	allocator = {0};
	ArenaAllocator	a = {0};

	a.size = size;
	a.offset = 0;
	if (parent)
		a.mem = parent->fp_allocation(parent, size, alignment);
	else
		a.mem = standard_alloc(size, DEFAULT_ALIGN);

	allocator.parent = parent;
	allocator.arena = a;
	allocator.type = ALLOCATOR_ARENA;
	allocator.fp_allocation = arenaAllocationFunction;
	allocator.fp_free = arenaFreeFunction;
	allocator.fp_reallocation = dudReallocFunction;
	allocator.fp_reset = arenaResetFunction;
	return allocator;
}

void	*poolAllocationFunction(Allocator *allocator, u64 size, u64 alignment)
{
	(void)size;
	(void)alignment;

	PoolAllocator	*pool = &allocator->pool;

	if (pool->free_count == 0) {
		engine_error(LOG_FILE, "Pool allocation requested but no free slots exist");
		return (NULL);
	}

	const u64	start = pool->hint;
	u64		i = start;

	do {
		if (!pool->used[i]) {
			pool->used[i] = 1;
			pool->free_count -= 1;

			// Wrapping increment:
			pool->hint = (i + 1) % pool->capacity;

			return (pool->slots + (i * pool->elem_size));
		}
		// Wrapping increment:
		i = (i + 1) % pool->capacity;
	} while (i != start);

	return (NULL);
}

void	poolFreeFunction(Allocator *allocator, void *mem)
{
	PoolAllocator	*pool = &allocator->pool;

	if (pool->free_count == pool->capacity) {
		return ;
	}

	const u64	offset = (u8 *)mem - pool->slots;
	const u64	index = offset / pool->elem_size;

	// After deliberating i realized that if the memory
	// passed was wrong or unused it probably means there
	// is a bug in the calling code. The kind of bug this
	// implies can be hard to catch so im just going to crash
	// the program right here to find it.
	if (offset % pool->elem_size != 0) {
		fprintf(stderr, "The memory passed to poolFreeFunction() does not match the size of the elements in the pool. Mem: %p Offset: %zu ElementSize: %zu\n", mem, offset, pool->elem_size);
		exit(1);
	}

	if (!pool->used[index]) {
		fprintf(stderr, "The memory passed to poolFreeFunction() was not used, this means a trash value was operated on. Mem: %p Offset: %zu ElementSize: %zu\n", mem, offset, pool->elem_size);
		exit(1);
	}

	pool->used[index] = 0;
	pool->free_count += 1;
	pool->hint = index;
}

void	poolResetFunction(Allocator *allocator)
{
	PoolAllocator	*pool = &allocator->pool;

	memset(pool->used, 0, pool->capacity);
	pool->free_count = pool->capacity;
	pool->hint = 0;
}

Allocator	newPoolAllocator(u64 capacity, u64 elem_size, Allocator *parent, u64 alignment)
{
	Allocator	allocator = {0};
	PoolAllocator	a = {0};

	a.capacity = capacity;
	a.elem_size = elem_size;
	a.free_count = capacity;

	if (parent) {
		a.used = parent->fp_allocation(parent, capacity, alignment);
		a.slots = parent->fp_allocation(parent, capacity * elem_size, alignment);
	} else {
		a.used = standard_alloc(capacity, alignment);
		a.slots = standard_alloc(capacity * elem_size, alignment);
	}

	allocator.pool = a;

	allocator.parent = parent;
	allocator.type = ALLOCATOR_POOL;
	allocator.fp_allocation = poolAllocationFunction;
	allocator.fp_free = poolFreeFunction;
	allocator.fp_reset = poolResetFunction;
	allocator.fp_reallocation = dudReallocFunction;
	return (allocator);
}

#define HEAP_LEFT_SHIFT 12
#define HEAP_BLK_SIZE	(1 << HEAP_LEFT_SHIFT)
static inline u64	round_up_heap(u64 n) {
	return ((n + HEAP_BLK_SIZE - 1) & ~(HEAP_BLK_SIZE - 1));
}

i32	_getNextEmptyHeapEntry(HeapAllocator *h, u64 block_count)
{
	const u32	start = h->hint;
	u32		i = start;

	do {
		HeapEntry	entry = h->blocks[i];

		if (entry.used == false) {
			bool		fits = true;
			const u32	run_start = i;
			if (run_start + block_count > h->num_slots) {
				i = 0;
			} else {
				for (u32 j = 0; j < block_count ; j++) {
					const u32	check_idx = run_start + j;
					if (check_idx == h->num_slots || h->blocks[check_idx].used == true || (check_idx == start && j > 0)) {
						// hit a used block, or wrapped all the way around without finding room
						fits = false;
						break ;
					}
				}
				if (fits) {
					for (u32 j = 0; j < block_count; j++) {
						u64	mark_idx = run_start + j;
						h->blocks[mark_idx].used = true;
					}
					h->hint = (run_start + block_count) % h->num_slots;
					h->blocks[run_start].entry_size = block_count; // only the first block records the run length
					return (run_start);
				}
				// didn't fit — fall through to advance i and keep scanning
				i = (i + 1) % h->num_slots;
			}
		} else {
			const u32 skip = entry.entry_size > 0 ? entry.entry_size : 1;
			i = (i + skip) % h->num_slots;
		}
	} while (i != start);
	return (-1);
}

void	*heapAllocationFunction(Allocator *a, u64 size, u64 alignment)
{
	(void)alignment;

	HeapAllocator	*heap = &a->heap;

	const u64	allocation_size = round_up_heap(size);
	const u32	block_count = allocation_size >> HEAP_LEFT_SHIFT;

	if (heap->free_blocks == 0) {
		engine_error(LOG_FILE, "Heap allocation requested but not enough memory is free, requested: %zu, available: %zu", allocation_size, heap->free_blocks << HEAP_LEFT_SHIFT);
		return (NULL);
	}


	i32	empty_idx = _getNextEmptyHeapEntry(heap, block_count);
	if (empty_idx == -1) {
		engine_error(LOG_FILE, "Heap allocation requested but not enough memory is free because of memory fragmentation");
		return (NULL);
	}

	heap->free_blocks -= block_count;
	return (&heap->mem[empty_idx << HEAP_LEFT_SHIFT]);
}

void	heapFreeFunction(Allocator *a, void *mem)
{
	HeapAllocator	*heap = &a->heap;

	const u8	*heap_mem = heap->mem;
	const u64	offset = (u8 *)mem - heap_mem;
	const u32	block_idx = offset >> HEAP_LEFT_SHIFT;

	if (offset % HEAP_BLK_SIZE != 0) {
		engine_error(LOG_FILE, "The memory passed to heapFreeFunction() does not match the size of the heap blocks. Mem: %p Offset: %zu Block Size: %zu\n", mem, offset, (u64)HEAP_BLK_SIZE);
		exit(1);
	}

	if (!heap->blocks[block_idx].used) {
		engine_error(LOG_FILE, "The memory passed to heapFreeFunction() was not used, this means a trash value was operated on. Mem: %p Offset: %zu BlockSize: %zu\n", mem, offset, (u64)HEAP_BLK_SIZE);
		exit(1);
	}

	const u32	blk_count = heap->blocks[block_idx].entry_size;
	for (u32 i = 0; i < blk_count; i++) {
		const u32	idx = block_idx + i;
		heap->blocks[idx].entry_size = 0;
		heap->blocks[idx].used = false;
	}
	heap->free_blocks += blk_count;
}

void	*heapReallocFunction(Allocator *a, void *original, u64 original_size, u64 new_size, u64 alignment)
{
	(void)original_size;
	HeapAllocator	*heap = &a->heap;

	const u8	*heap_mem = heap->mem;
	const u64	offset = (u8 *)original - heap_mem;
	const u32	orig_block_idx = offset >> HEAP_LEFT_SHIFT;

	if (offset % HEAP_BLK_SIZE != 0) {
		engine_error(LOG_FILE, "The memory passed to heapReallocFunction() does not match the size of the heap blocks. Mem: %p Offset: %zu Block Size: %zu\n", original, offset, (u64)HEAP_BLK_SIZE);
		exit(1);
	}

	if (!heap->blocks[orig_block_idx].used) {
		engine_error(LOG_FILE, "The memory passed to heapReallocFunction() was not used, this means a trash value was operated on. Mem: %p Offset: %zu BlockSize: %zu\n", original, offset, (u64)HEAP_BLK_SIZE);
		exit(1);
	}

	const HeapEntry	block = heap->blocks[orig_block_idx];
	const u64	original_allocation_size = block.entry_size << HEAP_LEFT_SHIFT;
	const u64	original_block_count = block.entry_size;

	const u64	requested_allocation_size = round_up_heap(new_size);
	const u32	requested_block_count = requested_allocation_size >> HEAP_LEFT_SHIFT;

	if (heap->free_blocks >= requested_block_count) {
		if (original_block_count >= requested_block_count) {
			const u32	gained_blocks = original_block_count - requested_block_count;
			heap->free_blocks += gained_blocks;
			heap->blocks[orig_block_idx].entry_size = requested_block_count;
			for (u32 i = requested_block_count; i < original_block_count; i++) {
				const u32	idx = orig_block_idx + i;
				heap->blocks[idx].used = false;
			}
			return (original);
		}
		u8	*dst = a->fp_allocation(a, new_size, alignment);
		if (!dst) {
			exit(1);
		}
		u8	*src = original;

		// TODO: May be worth to do a fragmentation check here to see if mem fits or not.
		// will only do it if error happens in real scenario
		// Ex: [ 0 0 0 1 1 1 0 0] with a request for reallocation of the 3 blocks to 4 blocks
		// clearly theres enough space but with fp_allocation it wont be found

		const u64	cpy_size = new_size > original_allocation_size ? original_allocation_size : new_size;
		memcpy(dst, src, cpy_size);
		a->fp_free(a, original);
		return (dst);
	}
	engine_error(LOG_FILE, "heapReallocFunction() failed because not enough blocks are free. Requested blocks: %u, Available blocks: %u", requested_block_count, heap->free_blocks);

	return (NULL);

	// TODO: Possible memory streaming?
	// Ex: [ 0 0 1 1 0 1 0 1 0] I want to reallocate those 2 1's into 3
	// blocks, i could copy the first 1 into the first slot and then copy
	// the second 1 into the 2nd slot and then have the 3rd marked as used.
}

void	heapResetFunction(Allocator *a)
{
	HeapAllocator	*heap = &a->heap;

	memset(heap->mem, 0, heap->num_slots << HEAP_LEFT_SHIFT);
	memset(heap->blocks, 0, heap->num_slots * sizeof(HeapEntry));
}

// Returns the actual size that was allocated for a given allocation
u64	heapGetRealAllocationSize(Allocator *a, void *ptr)
{
	HeapAllocator	*heap = &a->heap;

	const u8	*heap_mem = heap->mem;
	const u64	offset = (u8 *)ptr - heap_mem;
	const u32	orig_block_idx = offset >> HEAP_LEFT_SHIFT;

	HeapEntry	block = heap->blocks[orig_block_idx];
	return (block.entry_size << HEAP_LEFT_SHIFT);
}

Allocator	newHeapAllocator(u64 max, Allocator *parent, u64 alignment)
{
	Allocator	allocator = {0};
	HeapAllocator	a = {0};

	// Round max to the next 64k multiple
	const u64	size = round_up_heap(max);
	const u64	num_slots = size >> HEAP_LEFT_SHIFT;
	const u64	block_size = sizeof(HeapEntry) * num_slots;

	a.num_slots = num_slots;
	a.free_blocks = num_slots;

	if (parent) {
		a.blocks = parent->fp_allocation(parent, block_size, alignment);
		a.mem = parent->fp_allocation(parent, size, alignment);
	} else {
		a.blocks = standard_alloc(block_size, alignment);
		a.mem = standard_alloc(size, alignment);
	}
	memset(a.mem, 0, size);
	memset(a.blocks, 0, block_size);

	allocator.heap = a;

	allocator.parent = parent;
	allocator.type = ALLOCATOR_HEAP;
	allocator.fp_allocation = heapAllocationFunction;
	allocator.fp_free = heapFreeFunction;
	allocator.fp_reset = heapResetFunction;
	allocator.fp_reallocation = heapReallocFunction;
	return (allocator);
}

#include <assert.h>
#include <string.h>

void	testHeapAllocator(void)
{

	Allocator	heap_alloc = newHeapAllocator(HEAP_BLK_SIZE * 8, NULL, DEFAULT_ALIGN);
	HeapAllocator	*heap = &heap_alloc.heap;
	assert(heap->num_slots == 8);
	assert(heap->free_blocks == 8);

	u8	*ptrs[4];
	u64	sizes[4] = {HEAP_BLK_SIZE, HEAP_BLK_SIZE * 3, HEAP_BLK_SIZE * 2, HEAP_BLK_SIZE * 2};

	for (u32 i = 0; i < 4; i++) {
		ptrs[i] = heap_alloc.fp_allocation(&heap_alloc, sizes[i], DEFAULT_ALIGN);
		memset(ptrs[i], 'A' + i, sizes[i]);
	}
	assert(heap->free_blocks == 0);

	print("First print:\n");
	for (u32 i = 0; i < 4; i++) {
		print("Alloc[%u]: ptr=%p size=%zu first_byte=%c\n", i, (void *)ptrs[i], sizes[i], ptrs[i][0]);
	}

	// Free the middle allocation and reuse the space
	heap_alloc.fp_free(&heap_alloc, ptrs[1]);
	assert(heap->free_blocks == 3);

	ptrs[1] = heap_alloc.fp_allocation(&heap_alloc, HEAP_BLK_SIZE * 3, DEFAULT_ALIGN);
	memset(ptrs[1], 'Z', HEAP_BLK_SIZE * 3);
	assert(heap->free_blocks == 0);

	print("Second print:\n");
	for (u32 i = 0; i < 4; i++) {
		print("Alloc[%u]: ptr=%p first_byte=%c\n", i, (void *)ptrs[i], ptrs[i][0]);
	}

	// Realloc: grow, and confirm old data survived the copy
	u8	*grown = heap_alloc.fp_reallocation(&heap_alloc, ptrs[2], HEAP_BLK_SIZE * 2, HEAP_BLK_SIZE * 4, DEFAULT_ALIGN);
	assert(grown[0] == 'C');
	ptrs[2] = grown;

	for (u32 i = 0; i < 4; i++) {
		heap_alloc.fp_free(&heap_alloc, ptrs[i]);
	}
	assert(heap->free_blocks == heap->num_slots);

	print("Heap test passed\n");

}

void	testPool()
{
	Allocator	pool = newPoolAllocator(16, sizeof(u64), NULL, DEFAULT_ALIGN);

	u64	numbers[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	u64	more_numbers[] = {20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 220, 222, 222, 223, 224, 225};
	u64	*ptrs[16];

	for (u32 i = 0; i < 16; i++) {
		ptrs[i] = POOL_ALLOCATE(&pool);
		memcpy(ptrs[i], &numbers[i], sizeof(u64));
	}

	print("First print:\n");
	for (u32 i = 0; i < 16; i++) {
		print("Num[%u]: %zu\n", i, *ptrs[i]);
	}

	pool.fp_free(&pool, ptrs[7]);
	ptrs[7] = POOL_ALLOCATE(&pool);
	memcpy(ptrs[7], &more_numbers[7], sizeof(u64));

	print("Second print:\n");
	for (u32 i = 0; i < 16; i++) {
		print("Num[%u]: %zu\n", i, *ptrs[i]);
	}
}

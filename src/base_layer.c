#include "base_layer.h"
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"

// TODO: REMOVE THIS
#include <stdio.h>

// TODO: Allocators and file reading functions
// TODO: Allocators and string manipulation functions

// TODO: This is wrong of course
void	*standard_alloc(u64 size)
{
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
		// TODO: Create some actual logging
		fprintf(stderr, "[ERROR] Arena size too small for allocation, size: %ld alignment: %ld arena size: %ld\n", size, alignment, a_size);
		return NULL;
	}

	mem = (u8 *)a_mem + aligned_offset;
	allocator->arena.offset = new_offset;
	return mem;
}

void	arenaFreeFunction(Allocator *allocator, void *mem)
{
	(void)allocator;
	(void)mem;
	return ;
}

void	*arenaReallocFunction(Allocator *allocator, void *original, u64 original_size, u64 size, u64 alignment)
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
		a.mem = standard_alloc(size);

	allocator.parent = parent;
	allocator.arena = a;
	allocator.type = ALLOCATOR_ARENA;
	allocator.fp_allocation = arenaAllocationFunction;
	allocator.fp_free = arenaFreeFunction;
	allocator.fp_reallocation = arenaReallocFunction;
	return allocator;
}

String	readFile(const char *filename)
{
	int	fd = open(filename, O_RDONLY);

	if (fd == -1) {
		fprintf(stderr, "Failed to open file: %s\n", filename);
		return (String){NULL, 0};
	}

	struct stat	stats;
	fstat(fd, &stats);

	String	file;

	file.data = mmap(NULL, stats.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	file.size = stats.st_size;

	close(fd);

	if (file.data == MAP_FAILED) {
		fprintf(stderr, "Failed to map file: %s\n", filename);
		return (String){NULL, 0};
	}

	return file;
}


// Allocates memory
String	getNextLine(String str, u64 *offset, Allocator *allocator)
{
	String	line;

	u8	*start = (u8 *)str.data + (*offset);

	u64	i = 0;
	for (; line.data[i] != '\n' && i < (*offset) + str.size; i++) { }

	line.size = i + 1;
	line.data = allocator->fp_allocation(allocator, i, 64);

	memcpy(line.data, start, line.size);

	*offset += line.size;

	return line;
}

void	printString(const char *fmt, StringView str)
{
	u32	i = 0;
	while (fmt[i] != '%') { i++; }
	write(STDIN_FILENO, fmt, i);
	write(STDIN_FILENO, str.data, str.size);

	// Pass the '%'
	i++;
	u32	j = i;
	while (fmt[i] != 0) { i++; }
	write(STDIN_FILENO, fmt + j, i - j);
}

// No allocations
StringView	getNextLine_noMem(String str, u64 *offset)
{
	StringView	line;

	if (*offset >= str.size)
		return (StringView){NULL, 0};

	line.data = (u8 *)str.data + (*offset);

	u64	i = *offset;
	for (; i < str.size && str.data[i] != '\n'; i++) { }

	line.size = i - (*offset);

	if (i < str.size && str.data[i] == '\n')
		line.size++;

	*offset = i + (i < str.size);

	return line;
}

void	stringViewSkipChar(StringView *s, const char c)
{
	u64	i;

	i = 0;
	while (i < s->size)
	{
		if (s->data[i] != c)
			break ;
		i++;
	}

	s->size -= i;
	s->data += i;
}

char	*cstrdup(const char *str, u64 *size, Allocator *allocator)
{
	char	*new_str;

	u64	str_size = strlen(str);
	new_str = allocator->fp_allocation(allocator, str_size, 8);
	memcpy(new_str, str, str_size);
	*size = str_size;
	return (new_str);
}

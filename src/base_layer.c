#include "base_layer.h"
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"
#include <stdio.h>

u64	queryTimer(void)
{
	struct timespec	t;

	clock_gettime(CLOCK_MONOTONIC_RAW, &t);
	return (t.tv_nsec);
}

u64	getFrameDeltaNano(void)
{
	static u64	prev_time = 0;
	
	if (prev_time == 0) {
		prev_time = queryTimer();
		return 0;
	}
	u64	new_time = queryTimer();
	u64	time_elapsed = new_time - prev_time;
	prev_time = new_time;
	return (time_elapsed);
}

double	getFrameDelta(void)
{
	const u64	nano_delta = getFrameDeltaNano();
	const double	ms_delta = (double)nano_delta / 1000;
	return (ms_delta);
}

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
		engine_error("base_layer.c", "Arena size too small for allocation, size: %ld alignment: %ld arena size: %ld\n", size, alignment, a_size);
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
		a.mem = standard_alloc(size, DEFAULT_ALIGN);

	allocator.parent = parent;
	allocator.arena = a;
	allocator.type = ALLOCATOR_ARENA;
	allocator.fp_allocation = arenaAllocationFunction;
	allocator.fp_free = arenaFreeFunction;
	allocator.fp_reallocation = arenaReallocFunction;
	return allocator;
}

u8	*readFileData(String filename, u64 *file_size)
{
	char	filename_cstring[128];
	memcpy(filename_cstring, filename.data, filename.count);
	filename_cstring[filename.count] = 0;
	int	fd = open(filename_cstring, O_RDONLY);

	if (fd == -1) {
		fprintf(stderr, "Failed to open file: %S\n", filename);
		return NULL;
	}

	u8	*data;

	struct stat	stats;
	fstat(fd, &stats);

	data = mmap(NULL, stats.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

	close(fd);

	if (data == MAP_FAILED) {
		fprintf(stderr, "Failed to map file: %S\n", filename);
		return NULL;
	}
	if (file_size) {
		*file_size = stats.st_size;
	} else {
		fprintf(stderr, "Failed to get file size: %S\n", filename);
		munmap(data, stats.st_size);
		return NULL;
	}
	return (data);
}

String	readFile(String filename)
{
	String	file;

	file.data = readFileData(filename, &file.count);

	return file;
}

void	printString(const char *fmt, StringView str)
{
	u32	i = 0;
	while (fmt[i] != '%') { i++; }
	write(STDIN_FILENO, fmt, i);
	write(STDIN_FILENO, str.data, str.count);

	// Pass the '%'
	i++;
	u32	j = i;
	while (fmt[i] != 0) { i++; }
	write(STDIN_FILENO, fmt + j, i - j);
}

// No allocations
StringView	getNextLine(String str, u64 *offset)
{
	StringView	line;

	if (*offset >= str.count)
		return (StringView){NULL, 0};

	line.data = (u8 *)str.data + (*offset);

	u64	i = *offset;
	for (; i < str.count && str.data[i] != '\n'; i++) { }

	line.count = i - (*offset);

	if (i < str.count && str.data[i] == '\n')
		line.count++;

	*offset = i + (i < str.count);

	return line;
}

void	stringViewSkipChar(StringView *s, const char c)
{
	u64	i;

	i = 0;
	while (i < s->count)
	{
		if (s->data[i] != c)
			break ;
		i++;
	}

	s->count -= i;
	s->data += i;
}

u8	*stringViewPtrToChar(StringView s, const char c)
{
	u64	i;

	i = 0;
	while (i < s.count)
	{
		if (s.data[i] == c)
			return &s.data[i];
		i++;
	}
	return NULL;
}

void	stringViewJumpToChar(StringView *s, const char c)
{
	u64	i;

	i = 0;
	while (i < s->count)
	{
		if (s->data[i] == c)
			break ;
		i++;
	}

	s->count -= i;
	s->data += i;
}

void	stringViewAdvance(StringView *s, u64 count)
{
	s->data += count;
	s->count -= count;
}

bool	stringIsEqual(String s1, String s2)
{
	if (s1.count != s2.count)
		return false;

	for (u64 i = 0; i < s1.count; i++) {
		if (s1.data[i] != s2.data[i])
			return false;
	}
	return true;
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

String	stringDup(StringView str, Allocator *allocator)
{
	String	new_str;

	new_str.count = str.count;
	if (allocator)
		new_str.data = allocator->fp_allocation(allocator, new_str.count, 8);
	else
		new_str.data = standard_alloc(new_str.count, DEFAULT_ALIGN);
	memcpy(new_str.data, str.data, str.count);
	return (new_str);
}

String	createString(const char *cstr)
{
	String	s;

	u64	len = strlen(cstr);
	s.count = len;
	// TODO: Change allocators
	s.data = malloc(len);
	memcpy(s.data, cstr, len);
	return s;
}

String	stringCopy(const String str)
{
	String	s;

	s.count = str.count;
	// TODO: Change allocators
	s.data = malloc(s.count);
	memcpy(s.data, str.data, s.count);
	return s;
}

char	*stringToCstr(String s, Allocator *allocator)
{
	char	*cstring;

	if (allocator)
		cstring = allocator->fp_allocation(allocator, s.count + 1, 16);
	else
		cstring = standard_alloc(s.count + 1, DEFAULT_ALIGN);
	memcpy(cstring, s.data, s.count);
	cstring[s.count] = 0;
	return cstring;
}

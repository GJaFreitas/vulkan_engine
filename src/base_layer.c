#include "base_layer.h"
#include <stdio.h>

float32	randomFloat(float min, float max) {
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}


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

void	destroyFile(String file)
{
	munmap(file.data, file.count);
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

#include "base_layer.h"

typedef struct CallbackEntry
{
	bool			needs_change;
	i64			last_change;
	String			filename;
	FP_HotloadCallback	function;
	void			*user_data;
	struct CallbackEntry	*next;
}	CallbackEntry;

static CallbackEntry	callback_entries;
static CallbackEntry	*current_entry = &callback_entries;
static Allocator	callback_allocator;

void	start_hotload_callbacks(void)
{
	callback_allocator = newArenaAllocator(1 << 19, NULL, DEFAULT_ALIGN);
}

static i64	getLastUpdate(String filename)
{
	char	filename_cstring[128];
	memcpy(filename_cstring, filename.data, filename.count);
	filename_cstring[filename.count] = 0;
	i32	fd = open(filename_cstring, O_RDONLY);

	if (fd == -1) {
		engine_warn(__FILE__, "Unable to open file: %S", filename);
		return (-1);
	}

	struct stat	stats;
	fstat(fd, &stats);

	close(fd);
	return (stats.st_mtim.tv_sec);
}

void	do_callbacks(void)
{
	CallbackEntry	*cur = &callback_entries;

	while (cur->next != NULL) {
		i64	last_change = getLastUpdate(cur->filename);

		if (last_change != -1 && last_change != cur->last_change) {
			cur->last_change = last_change;
			cur->function(cur->user_data);
		}
		cur = cur->next;
	}
}

void	register_callback(String filename, FP_HotloadCallback function, void *user_data)
{
	current_entry->filename = stringDup(filename, &callback_allocator);
	current_entry->last_change = getLastUpdate(filename);
	current_entry->needs_change = 0;
	current_entry->function = function;
	current_entry->user_data = user_data;
	current_entry->next = callback_allocator.fp_allocation(&callback_allocator, sizeof(CallbackEntry), 16);
	current_entry = current_entry->next;
	current_entry->next = NULL;
}

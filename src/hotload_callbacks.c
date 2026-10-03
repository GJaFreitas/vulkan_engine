#include "base_layer.h"
#include <sys/stat.h>

typedef struct CallbackEntry
{
	bool			needs_change;
	i64			last_change;
	String			filename;
	FP_HotloadCallback	function;
	void			*user_data;
	struct CallbackEntry	*next;
} CallbackEntry;

static CallbackEntry	*callback_list = NULL;
static Allocator	callback_allocator;

void	start_hotload_callbacks(void)
{
	callback_allocator = newArenaAllocator(KB(32), NULL, DEFAULT_ALIGN);
}

static i64	getLastUpdate(String filename)
{
	char		filename_cstring[4096];
	struct stat	stats;

	// Prevent buffer overflow for long paths
	if (filename.count >= sizeof(filename_cstring)) {
		engine_warn(LOG_FILE, "Filename too long: %S", filename);
		return (-1);
	}

	memcpy(filename_cstring, filename.data, filename.count);
	filename_cstring[filename.count] = '\0';

	// Use stat instead of open/fstat/close to reduce syscalls by 66%
	if (stat(filename_cstring, &stats) == -1) {
		return (-1);
	}

	// Combine seconds and nanoseconds to catch rapid consecutive saves
	return ((i64)stats.st_mtim.tv_sec * 1000000000LL) + (i64)stats.st_mtim.tv_nsec;
}

void	do_callbacks(void)
{
	CallbackEntry	*cur;
	i64		last_change;

	cur = callback_list;

	while (cur != NULL) {
		last_change = getLastUpdate(cur->filename);

		if (last_change != -1 && last_change != cur->last_change) {
			cur->last_change = last_change;
			cur->function(cur->user_data);
		}
		cur = cur->next;
	}
}

void	register_callback(String filename, FP_HotloadCallback function, void *user_data)
{
	CallbackEntry	*new_entry;
	
	new_entry = callback_allocator.fp_allocation(&callback_allocator, sizeof(CallbackEntry), 16);

	new_entry->filename = strDup(filename, &callback_allocator);
	new_entry->last_change = getLastUpdate(filename);
	new_entry->needs_change = false;
	new_entry->function = function;
	new_entry->user_data = user_data;
	
	// Standard O(1) head insertion
	new_entry->next = callback_list;
	callback_list = new_entry;
}

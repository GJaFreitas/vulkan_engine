#include "base_layer.h"
#include <stdarg.h>

// None of this is async, be careful later

typedef struct LogEntry
{
	String	file_name;
	String	log;
	struct LogEntry	*next;
}	LogEntry;

LogEntry	logs;
LogEntry	*current_entry = &logs;
Allocator	log_allocator;

void	start_logs(void)
{
	log_allocator = newArenaAllocator(1 << 19, NULL, 8);
	logs.file_name.data = (u8 *)cstrdup("nil", &logs.file_name.count, &log_allocator);
	logs.log.data = (u8 *)cstrdup("Engine started", &logs.log.count, &log_allocator);
}

void	print_logs(void)
{
	LogEntry	*cur;

	const char	msg[] = "At file: ";
	const char	msg2[] = "[LOG] ";
	cur = &logs;
	while (cur)
	{
		write(STDOUT_FILENO, msg, sizeof(msg));
		write(STDOUT_FILENO, cur->file_name.data, cur->file_name.count);
		write(STDOUT_FILENO, "; ", 2);
		write(STDOUT_FILENO, msg2, sizeof(msg2));
		write(STDOUT_FILENO, cur->log.data, cur->log.count);
		write(STDOUT_FILENO, "\n", 1);
		cur = cur->next;
	}
}

static void	_add_log(LogEntry entry)
{
	current_entry->next = log_allocator.fp_allocation(&log_allocator, sizeof(LogEntry), 8);
	current_entry = current_entry->next;
	*current_entry = entry;
	current_entry->next = NULL;
}

void	engine_log(const char *file, const char *fmt, ...)
{
	LogEntry	log_entry;

	const u64	buffer_size = strlen(fmt) * 2;
	char	*buffer;

	log_entry.file_name.data = (u8 *)cstrdup(file, &log_entry.file_name.count, &log_allocator);
	buffer = log_allocator.fp_allocation(&log_allocator, buffer_size, 8);

	va_list	ap;
	va_start(ap, fmt);
	vsnprintf(buffer, buffer_size, fmt, ap);
	va_end(ap);

	const u64	actual_size = strlen(buffer);
	const u64	size_dif = buffer_size - actual_size;

	// BUG: Be very careful with this stuff, could bring about bugs. If the
	// logs are wrong check here
	log_allocator.arena.offset -= size_dif;
	log_entry.log.data = (u8 *)buffer;
	log_entry.log.count = actual_size;

	_add_log(log_entry);
}

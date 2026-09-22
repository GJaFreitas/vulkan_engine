#include "stb_sprintf.h"
#include "logging.h"
#include <stdarg.h>
#include "str.h"

// None of this is async, be careful later

typedef struct LogEntry
{
	enum LogLevel	level;
	String		file_name;
	String		log;
	struct LogEntry	*next;
}	LogEntry;

static LogEntry	logs;
static LogEntry	*current_entry = &logs;
static Allocator	log_allocator;
static enum LogLevel	print_severity = LOG_LOG;


void	start_logs(void)
{
	log_allocator = newArenaAllocator(1 << 19, NULL, DEFAULT_ALIGN);
}

void	set_log_severity(enum LogLevel level)
{
	print_severity = level;
}

void	print_single_log(LogEntry entry)
{
	if (entry.level < print_severity)
		return ;

	const char	log_string[] = "[LOG] ";
	const char	warn_string[] = "[WARN] ";
	const char	error_string[] = "[ERROR] ";
	const char	debug_string[] = "[DEBUG] ";

	switch (entry.level) {
		case LOG_LOG:
			write(STDOUT_FILENO, log_string, sizeof(log_string) - 1);
			break;
		case LOG_WARN:
			write(STDOUT_FILENO, warn_string, sizeof(warn_string) - 1);
			break;
		case LOG_ERROR:
			write(STDOUT_FILENO, error_string, sizeof(error_string) - 1);
			break;
		case LOG_DEBUG:
			write(STDOUT_FILENO, debug_string, sizeof(debug_string) - 1);
			break;
	}
	write(STDOUT_FILENO, entry.file_name.data, entry.file_name.count);
	write(STDOUT_FILENO, ": ", 2);
	write(STDOUT_FILENO, entry.log.data, entry.log.count);
	write(STDOUT_FILENO, "\n", 1);
}

void	print_logs(void)
{
	LogEntry	*cur;

	cur = &logs;
	while (cur)
	{
		print_single_log(*cur);
		cur = cur->next;
	}
}

static void	_add_log(LogEntry entry)
{
	if (current_entry->file_name.count == 0) {
		*current_entry = entry;
	} else {
		current_entry->next = log_allocator.fp_allocation(&log_allocator, sizeof(LogEntry), 8);
		current_entry = current_entry->next;
		*current_entry = entry;
		current_entry->next = NULL;
	}
	print_single_log(entry);
}

static void	create_log_entry(enum LogLevel level, String file, const char *fmt, va_list ap)
{
	LogEntry	log_entry;

	const u64	buffer_size = 512;
	char		*buffer;

	log_entry.file_name = strDup(file, &log_allocator);
	buffer = log_allocator.fp_allocation(&log_allocator, buffer_size, 8);

	const u64	actual_size = stbsp_vsnprintf(buffer, buffer_size, fmt, ap);
	const u64	size_dif = buffer_size - actual_size;

	// BUG: Be very careful with this stuff, could bring about bugs. If the
	// logs are wrong check here
	log_allocator.arena.offset -= size_dif;
	log_entry.log.data = (u8 *)buffer;
	log_entry.log.count = actual_size;
	log_entry.level = level;

	_add_log(log_entry);
}

void	engine_warn(String file, const char *fmt, ...)
{
	va_list	ap;
	va_start(ap, fmt);
	create_log_entry(LOG_WARN, file, fmt, ap);
	va_end(ap);
}

void	engine_error(String file, const char *fmt, ...)
{
	va_list	ap;
	va_start(ap, fmt);
	create_log_entry(LOG_ERROR, file, fmt, ap);
	va_end(ap);
}

void	engine_debug(String file, const char *fmt, ...)
{
	va_list	ap;
	va_start(ap, fmt);
	create_log_entry(LOG_DEBUG, file, fmt, ap);
	va_end(ap);
}

void	engine_log(String file, const char *fmt, ...)
{
	va_list	ap;
	va_start(ap, fmt);
	create_log_entry(LOG_LOG, file, fmt, ap);
	va_end(ap);
}

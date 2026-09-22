#pragma once

#include <unistd.h>

#include "typedefs.h"
#include "allocators.h"

enum LogLevel
{
	LOG_LOG,
	LOG_WARN,
	LOG_ERROR,
	LOG_DEBUG,
};

#define LOG_FILE (STRING_LIT(__FILE_NAME__))

void	start_logs(void);
void	set_log_severity(enum LogLevel level);
void	print_logs(void);
void	engine_warn(String file, const char *fmt, ...);
void	engine_error(String file, const char *fmt, ...);
void	engine_log(String file, const char *fmt, ...);
void	engine_debug(String file, const char *fmt, ...);

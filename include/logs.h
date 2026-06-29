#pragma once

//  ──────────────────────────────── LOGS ─────────────────────────────

enum LogLevel
{
	LOG_LOG,
	LOG_WARN,
	LOG_ERROR,
	LOG_DEBUG,
};

void	start_logs(void);
void	set_log_severity(enum LogLevel level);
void	print_logs(void);
void	engine_warn(const char *file, const char *fmt, ...);
void	engine_error(const char *file, const char *fmt, ...);
void	engine_log(const char *file, const char *fmt, ...);
void	engine_debug(const char *file, const char *fmt, ...);

//  ──────────────────────────────── LOGS ─────────────────────────────


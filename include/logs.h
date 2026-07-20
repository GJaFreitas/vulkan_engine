#pragma once

//  ──────────────────────────────── LOGS ─────────────────────────────

#define LOG_E(x) engine_error(__FILE__, (x))
#define LOG_L(x) engine_log(__FILE__, (x))
#define LOG_W(x) engine_warn(__FILE__, (x))
#define LOG_D(x) engine_debug(__FILE__, (x))

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


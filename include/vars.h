#pragma once

#include "base_layer.h"
#include <sys/stat.h>

// The -1 is because the String type doesnt account for the \0 while sizeof() does
#define RegisterField(var_name, var_type, settings_type) \
	{(String){(u8 *)(#var_name), sizeof(#var_name) - 1}, var_type, offsetof(settings_type, var_name)}

void	init_vars();
void	check_var_modify();
void	vars_callback(void *udata);

enum ConfigType
{
	CONFIG_BOOL,
	CONFIG_FLOAT,
	CONFIG_INT,
	CONFIG_STR,
};

typedef struct
{
	String			name;
	const enum ConfigType	type;
	u64			offset;
}	ConfigField;

typedef struct Audio
{
	float	mix_all;
	float	mix_sfx;
}	Audio;

typedef struct Dev
{
	bool	profiling;
}	Dev;

typedef struct Display
{
	bool	vsync;
}	Display;

typedef struct Settings
{
	Audio	audio;
	Dev	dev;
	Display	display;
}	Settings;

extern Settings	g_settings;

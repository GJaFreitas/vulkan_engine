#pragma once

#include "base_layer.h"

// The -1 is because the String type doesnt account for the \0 while sizeof() does
#define RegisterField(var_name, var_type, settings_type) \
	{(String){(u8 *)(#var_name), sizeof(#var_name) - 1}, var_type, offsetof(settings_type, var_name)}

void	init_vars();

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
	char	testing[32];
	char	testing_again[32];
	char	testing_once_again[32];
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

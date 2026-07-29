#pragma once
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <cglm/cglm.h>

#include "all.h"

String	readFile(String filename);
u8	*readFileData(String filename, u64 *file_size);
// No allocations
StringView	getNextLine(String str, u64 *offset);

u64	queryTimer(void);
u64	getFrameDeltaNano(void);
double	getFrameDelta(void);
float32	randomFloat(float min, float max);

// Callbacks
// ---------

typedef void (*FP_HotloadCallback)(void *data);

void	start_hotload_callbacks(void);
void	register_callback(String filename, FP_HotloadCallback function, void *user_data);
void	do_callbacks(void);

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
#define CGLM_ALL_UNALIGNED
#include <cglm/cglm.h>

#include "typedefs.h"

#include "all.h"

static inline void	setFlag32(u32 *bitset, u32 flag) {
	*bitset = *bitset | flag;
}
static inline void	unsetFlag32(u32 *bitset, u32 flag) {
	*bitset = *bitset & ~flag;
}
static inline bool	queryFlag32(u32 bitset, u32 flag) {
	return (bitset & flag) == flag;
}

// For cglm
#define X 0
#define Y 1
#define Z 2
#define W 3

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

String	readFile(String filename);
u8	*readFileData(String filename, u64 *file_size);
void	destroyFile(String file);

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

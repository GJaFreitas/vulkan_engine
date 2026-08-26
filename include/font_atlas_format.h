#pragma once
#include <stdint.h>

#pragma pack(push, 1)

typedef struct
{
	uint32_t magic;       // 'FATL' as a u32
	uint32_t version;
	uint32_t font_count;
}	FileHeader;

typedef struct
{
	char     path[64];        // font path, null-padded
	uint32_t atlas_width;
	uint32_t atlas_height;
	float    px_range;
	uint32_t glyph_count;
	// raw RGBA pixel data (atlas_width * atlas_height * 4 bytes) follows
	// immediately after the glyph_count GlyphInfo entries that follow this header
}	FontHeader;

typedef struct
{
	uint32_t index;
	float    uv_min[2];
	float    uv_max[2];
	float    plane_min[2];
	float    plane_max[2];
	float    advance;
}	GlyphInfo;

#pragma pack(pop)

#define FATL_MAGIC   0x4C544146u // "FATL"
#define FATL_VERSION 1u
// Channel count used by the distance fields. Meaning: how many fields exist
#define CHANNELS	4

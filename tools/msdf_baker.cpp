#include "/home/bag/git/msdf-atlas-gen/msdf-atlas-gen/msdf-atlas-gen.h"
#include "font_atlas_format.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>

// This program will prebake all font atlases that are needed by the engine and
// output them all into one file, at the begining of said file it will write
// how many fonts are baked and some information about them like their name it
// will also have the offset into the file where said font is at and how big it
// is so that they can be loaded threaded very easily.

typedef unsigned char	u8;
typedef unsigned short	u16;
typedef unsigned int	u32;

typedef float		f32;
typedef double		f64;

using namespace msdf_atlas;

int	main(int argc, char **argv)
{
	if (argc < 3) {
		printf("Usage: %s <font-path1.ttf> <font-path2.ttf> ... <outfile.bin>\n", argv[0]);
		return (1);
	}


	int		font_count = argc - 2;
	const char	**font_paths = (const char **)&argv[1];
	const char	*out_path = argv[argc - 1];

	FILE *out = fopen(out_path, "wb");

	msdfgen::FreetypeHandle *ft = msdfgen::initializeFreetype();
	if (!ft) { fprintf(stderr, "Failed to init FreeType\n"); return 1; }


	FileHeader file_header = { FATL_MAGIC, FATL_VERSION, (uint32_t)font_count };
	fwrite(&file_header, sizeof(file_header), 1, out);

	for (int fi = 0; fi < font_count; fi++) {
		std::vector<GlyphGeometry>	glyphs;
		FontGeometry	font_geometry(&glyphs);

		msdfgen::FontHandle *font = msdfgen::loadFont(ft, font_paths[fi]);
		if (!font) { fprintf(stderr, "Failed to load font\n"); return 1; }

		Charset	charset(charset.ASCII);
		font_geometry.loadCharset(font, 1.0, charset, msdfgen::FONT_SCALING_EM_NORMALIZED);

		const double	max_corner_angle = 3.0;
		for (GlyphGeometry &glyph : glyphs) {
			glyph.edgeColoring(msdfgen::edgeColoringInkTrap, max_corner_angle, 0);
		}

		TightAtlasPacker packer;
		packer.setDimensionsConstraint(DimensionsConstraint::SQUARE);
		packer.setMinimumScale(48.0);      // glyph size within the atlas — tune like your old bake size
		packer.setPixelRange(4.0);         // "pxrange" — analogous to your old SDF spread
		packer.setMiterLimit(1.0);
		packer.pack(glyphs.data(), (int)glyphs.size());

		int width, height;
		packer.getDimensions(width, height);

		ImmediateAtlasGenerator<float, CHANNELS, mtsdfGenerator, BitmapAtlasStorage<msdfgen::byte, CHANNELS>> generator(width, height);
		GeneratorAttributes attributes;
		generator.setAttributes(attributes);
		generator.setThreadCount(4);
		generator.generate(glyphs.data(), (int)glyphs.size());


		// derive a short name from the filename, e.g. strip path/extension
		FontHeader	font_header = {};
		snprintf(font_header.path, sizeof(font_header.path), "%s", font_paths[fi]);
		font_header.atlas_width = width;
		font_header.atlas_height = height;
		font_header.px_range = 4.0f;
		font_header.glyph_count = (uint32_t)glyphs.size();
		fwrite(&font_header, sizeof(FontHeader), 1, out);
		
		for (const GlyphGeometry &g : glyphs) {
			double al, ab, ar, at, pl, pb, pr, pt;
			g.getQuadAtlasBounds(al, ab, ar, at);
			g.getQuadPlaneBounds(pl, pb, pr, pt);

			GlyphInfo entry = {
				.index = (u32)g.getIndex(),
				.uv_min = { (float)(al / width), (float)(1.0 - ab / height) },
				.uv_max = { (float)(ar / width), (float)(1.0 - at / height) },
				.plane_min = { (float)pl, (float)pb },
				.plane_max = { (float)pr, (float)pt },
				.advance = (float)g.getAdvance(),
			};

			fwrite(&entry, sizeof(entry), 1, out);
		}

		// Raw RGBA pixels straight from the generator's in-memory bitmap —
		// no PNG encode/decode round-trip needed at all.
		msdfgen::Bitmap<unsigned char, CHANNELS>	bytes(width, height);
		generator.atlasStorage().get(0, 0, bytes);
		auto ref = msdfgen::BitmapConstRef<msdfgen::byte, CHANNELS>(bytes);
		const u8	*pixels = ref.pixels;
		fwrite(pixels, 1, (size_t)width * height * CHANNELS, out);

		printf("Baked font %d/%d: %s (%u glyphs, %ux%u)\n",
		       fi + 1, font_count, basename(font_header.path), font_header.glyph_count, width, height);
		msdfgen::destroyFont(font);
	}

	fclose(out);
	return 0;

}

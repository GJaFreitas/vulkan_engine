#include "font_atlas_format.h"
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <string>
#include <filesystem>

typedef unsigned char	u8;
typedef unsigned short	u16;
typedef unsigned int	u32;

typedef float		f32;
typedef double		f64;

std::vector<GlyphInfo>	readGlyphInfoCSV(std::string csv_file)
{
	std::vector<GlyphInfo>	glyph_infos;

	// CSV columns
	//
	// Character Unicode value or glyph index, depending on whether character set or glyph set mode is used.
	//
	// Horizontal advance in em's.
	//
	// The next 4 columns are the glyph quad's bounds in em's relative to
	// the baseline and cursor. Depending on the -yorigin setting, this is
	// either left, bottom, right, top (bottom-up Y) or left, top, right,
	// bottom (top-down Y).
	//
	// The last 4 columns the the glyph's bounds in the atlas in pixels.
	// Depending on the -yorigin setting, this is either left, bottom,
	// right, top (bottom-up Y) or left, top, right, bottom (top-down Y).
	//

	FILE	*csv = fopen(csv_file.c_str(), "r");

	return glyph_infos;
}

int	main(int argc, char **argv) {

	if (argc <= 3) {
		std::fprintf(stderr, "Usage: %s <font path1> <font path2> ... <output>\n", argv[0]);
		exit(1);
	}

	std::vector<std::string>	font_paths(argv + 1, argv + argc);
	const u32			font_count = argc - 2;

	FileHeader	file_header = {FATL_MAGIC, FATL_VERSION, font_count};

	for (u32 fi = 0; fi < font_count; fi++) {
		std::string	font_path = font_paths.at(fi);
		FontHeader	fheader = {};
		std::snprintf(fheader.path, 64, "%s", font_path.c_str());

		const std::string	font_name = std::filesystem::path(font_path).stem().string();
		const std::string	glyph_set = "tools/glyphset.txt";
		const std::string	rgba_output = font_name + ".rgba";
		const std::string	csv_output = font_name + ".csv";

		std::string	command = "tools/msdf-atlas-gen/build/bin/msdf-atlas-gen -glyphset" + glyph_set + " -font " + font_path + " -format rgba -imageout " + rgba_output + " -csv " + csv_output + " -type mtsdf -square -minsize 24 -emrange 4.0 -angle 3.0 -miterlimit 1 -threads 0 -yorigin top";

		int result = std::system(command.c_str());
		if (result != 0) { std::fprintf(stderr, "Command %s failed\n", command.c_str()); return 1;}

		std::vector<GlyphInfo>	glyph_infos = readGlyphInfoCSV(csv_output);
	}

	return 0;
}

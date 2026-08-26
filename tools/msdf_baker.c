#include "font_atlas_format.h"
#include "fonts.h"
#include "stb_sprintf.h"
#include "base_layer.h"

static inline u32	big_endian_u32(const u8* p) {
	return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] <<  8 | (u32) p[3] <<  0;
}

u32	readRGBAData(String rgba_file_path, String *data, Allocator *a)
{
	// RGBA data is like this:
	// RGBA magic bits, width, size and pixel data after. Uncompressed and unapolagetic:
	// https://github.com/bzotto/rgba_bitmap

	String	file = readFile(rgba_file_path);
	StringView	file_view = file;

	// Skip rgba
	strViewAdvance(&file_view, 4);
	u32	width = big_endian_u32(file_view.data);
	strViewAdvance(&file_view, sizeof(int));
	u32	height = big_endian_u32(file_view.data);
	strViewAdvance(&file_view, sizeof(int));

	if (width != height) { exit(2); }

	data->data = a->fp_allocation(a, width * width * 4, DEFAULT_ALIGN);
	data->count = width * width * 4;
	memcpy(data->data, file_view.data, width * width * 4);
	destroyFile(file);
	return width;
}

void	readGlyphInfoCSV(String csv_file_path, Vector *glyph_vec, u32 atlas_size, FT_Face face)
{
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

	String	csv = readFile(csv_file_path);
	u64	offset = 0;

	StringView	line = getNextLine(csv, &offset);
	for (; line.count > 0; line = getNextLine(csv, &offset)) {
		GlyphInfo	glyph = {};

		glyph.index = FT_Get_Char_Index(face, atoi((char *)line.data));
		strViewJumpToChar(&line, ',');
		strViewAdvance(&line, 1);

		glyph.advance = atof((char *)line.data);
		strViewJumpToChar(&line, ',');
		strViewAdvance(&line, 1);

		glyph.plane_min[0] = atof((char *)line.data);
		strViewJumpToChar(&line, ',');
		strViewAdvance(&line, 1);
		glyph.plane_min[1] = atof((char *)line.data);
		strViewJumpToChar(&line, ',');
		strViewAdvance(&line, 1);

		glyph.plane_max[0] = atof((char *)line.data);
		strViewJumpToChar(&line, ',');
		strViewAdvance(&line, 1);
		glyph.plane_max[1] = atof((char *)line.data);
		strViewJumpToChar(&line, ',');
		strViewAdvance(&line, 1);

		glyph.uv_min[0] = atof((char *)line.data) / atlas_size;
		strViewJumpToChar(&line, ',');
		strViewAdvance(&line, 1);
		glyph.uv_min[1] = atof((char *)line.data) / atlas_size;
		strViewJumpToChar(&line, ',');
		strViewAdvance(&line, 1);

		glyph.uv_max[0] = atof((char *)line.data) / atlas_size;
		strViewJumpToChar(&line, ',');
		strViewAdvance(&line, 1);
		glyph.uv_max[1] = atof((char *)line.data) / atlas_size;
		strViewJumpToChar(&line, ',');
		strViewAdvance(&line, 1);

		vectorAppend(glyph_vec, &glyph);
	}
	destroyFile(csv);
}

int	main(int argc, char **argv) {

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <font path1> <font path2> ... <output>\n", argv[0]);
		exit(1);
	}
	FT_Library ft_lib;
	FT_Init_FreeType(&ft_lib);

	Allocator	a = newArenaAllocator(MB(20), NULL, DEFAULT_ALIGN);
	Vector		*glyph_vec = vectorCreate(512, sizeof(GlyphInfo), &a);

	char		**font_paths = argv + 1;
	char		*out_path = argv[argc - 1];
	const u32	font_count = argc - 2;

	FileHeader	file_header = {FATL_MAGIC, FATL_VERSION, font_count};

	FILE	*outfile = fopen(out_path, "wb");

	fwrite(&file_header, sizeof(FileHeader), 1, outfile);

	for (u32 fi = 0; fi < font_count; fi++) {
		char		*font_path = font_paths[fi];
		FontHeader	fheader = {};
		snprintf(fheader.path, 64, "%s", font_path);
		FT_Face face;
		FT_New_Face(ft_lib, font_path, 0, &face);

		StringView	font_name = {.data = (u8*)font_path, .count = strlen(font_path)};
		u8	*last_bar = strViewPtrToCharR(font_name, '/');
		u32	last_bar_i = last_bar - font_name.data;
		strViewAdvance(&font_name, last_bar_i + 1);
		font_name.count -= sizeofString(".ttf");

		const String		char_set = STRING_LIT("tools/charset.txt");
		String			rgba_output = {.count = font_name.count + sizeofString(".rgba")};
		String			csv_output = {.count = font_name.count + sizeofString(".csv")};
		rgba_output.data = calloc(1, font_name.count + sizeofString(".rgba"));
		csv_output.data = calloc(1, font_name.count + sizeofString(".csv"));
		strCopy(rgba_output, font_name);
		strCopy(csv_output, font_name);
		StringView	sv = {rgba_output.data + font_name.count, sizeofString(".rgba")};
		strCopy(sv, STRING_LIT(".rgba"));
		sv = (StringView){csv_output.data + font_name.count, sizeofString(".csv")};
		strCopy(sv, STRING_LIT(".csv"));


		char	command[512];
		stbsp_snprintf(command, 512, "tools/msdf-atlas-gen/build/bin/msdf-atlas-gen -charset %S -font %s -format rgba -imageout %S -csv %S -type mtsdf -square -minsize 24 -pxrange 8.0 -angle 3.0 -miterlimit 1 -threads 0 -yorigin top", char_set, font_path, rgba_output, csv_output);

		{
			FILE	*pipe = popen(command, "r");
			if (!pipe) { fprintf(stderr, "Command failed\n"); return 1;}

			char		data[512];
			fgets(data, 512, pipe);
			fputs(data, stdout); // Still get the output printed in terminal

			// Parse the field from:
			// Atlas image file saved.
			// Glyph layout written into CSV file.
			// Loaded geometry of 113 out of 113 glyphs.
			// Atlas size: 1249x1249
			// Glyph count: 113
			//
			// DEcide if its not just better to use pxrange in the command itself

			int result = pclose(pipe);
			if (result != 0) { fprintf(stderr, "Command failed\n"); return 1; }
		}

		String	rgba_data;
		u32	atlas_size = readRGBAData(rgba_output, &rgba_data, &a);
		print("Atlas size: %ux%u\n", atlas_size, atlas_size);
		readGlyphInfoCSV(csv_output, glyph_vec, atlas_size, face);
		print("Glyph count: %zu\n", glyph_vec->used);

		fheader.glyph_count = glyph_vec->used;
		fheader.atlas_height = atlas_size;
		fheader.atlas_width = atlas_size;
		fheader.px_range = 4;
		memcpy(fheader.path, font_path, strlen(font_path));

		fwrite(&fheader, sizeof(FontHeader), 1, outfile);
		fwrite(glyph_vec->elems, sizeof(GlyphInfo), glyph_vec->used, outfile);
		fwrite(rgba_data.data, 1, rgba_data.count, outfile);

		vectorReset(glyph_vec);
	}

	return 0;
}

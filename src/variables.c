#include "vars.h"

Settings	g_settings;

ConfigField	audio_fields[] = {
	RegisterField(mix_all, CONFIG_FLOAT, Audio),
	RegisterField(mix_sfx, CONFIG_FLOAT, Audio),
};

ConfigField	dev_fields[] = {
	RegisterField(profiling, CONFIG_BOOL, Dev),
};

ConfigField	display_fields[] = {
	RegisterField(vsync, CONFIG_BOOL, Display),
};

static void	modify_field(ConfigField field, String field_value, void *section)
{
	char	c;
	char	*ptr;
	float val;

	switch (field.type) {
		case CONFIG_BOOL: 
			if (stringIsEqual(field_value, (String){(u8 *)"true", sizeof("true")})) {
				*(bool *)((u8 *)section + field.offset) = true;
			} else {
				*(bool *)((u8 *)section + field.offset) = false;
			}
		break ;
		case CONFIG_FLOAT:
			c = field_value.data[field_value.count];
			field_value.data[field_value.count] = 0;
			val = atof((char *)field_value.data);
			field_value.data[field_value.count] = c;
			*(float *)((u8 *)section + field.offset) = val;
		break ;
		case CONFIG_INT:
			c = field_value.data[field_value.count];
			field_value.data[field_value.count] = 0;
			val = atoi((char *)field_value.data);
			field_value.data[field_value.count] = c;
			*(float *)((u8 *)section + field.offset) = val;
		break ;
		case CONFIG_STR:
			ptr = *(char **)((u8 *)section + field.offset);
			memcpy(ptr, field_value.data, field_value.count);
		break ;
		default: engine_log("variables.c", "How did we get here?\n");
	}
}

void	assignField(String settings_type, String field_name, String field_value)
{
	// The -1 is because the String type doesnt account for the \0 while sizeof() does
	const String	audio = {(u8 *)"Audio", sizeof("audio") - 1};
	const String	dev = {(u8 *)"Dev", sizeof("dev") - 1};
	const String	display = {(u8 *)"Display", sizeof("display") - 1};


	if (stringIsEqual(settings_type, audio)) {
		for (u16 i = 0; i < sizeofarray(audio_fields); i++) {
			if (stringIsEqual(field_name, audio_fields[i].name)) {
				modify_field(audio_fields[i], field_value, &g_settings.audio);
			}
		}
	} else if (stringIsEqual(settings_type, dev)) {
		for (u16 i = 0; i < sizeofarray(dev_fields); i++) {
			if (stringIsEqual(field_name, dev_fields[i].name)) {
				modify_field(dev_fields[i], field_value, &g_settings.dev);
			}
		}
	} else if (stringIsEqual(settings_type, display)) {
		for (u16 i = 0; i < sizeofarray(display_fields); i++) {
			if (stringIsEqual(field_name, display_fields[i].name)) {
				modify_field(display_fields[i], field_value, &g_settings.display);
			}
		}
	}
}

void	init_vars()
{
	String	file_data = readFile("data/All.variables");

	StringView	subdir;
	u64	offset = 0;
	u16	line_nr = 1;
	while (true) {

		StringView	line = getNextLine(file_data, &offset);
		if (!line.data) break ;

		// Remove the \n
		line.count--;
		// Advance spaces
		stringViewSkipChar(&line, ' ');

		if (line.data[0] == ':') {

			if (line.count < 2) {
				engine_log(__FILE_NAME__, "variables file parsing error at line %u! Lines starting with ':' must have a '/' and a name afterwards.\n", line_nr);
			} else {
				if (line.data[1] != '/') {
					engine_log(__FILE_NAME__, "variables file parsing error at line %u! Expected a '/' after ':'.\n", line_nr);
				} else {
					StringView folder_name = line;
					folder_name.data += 2;
					folder_name.count -= 2;
					printString("Folder name '%'\n", folder_name);
					subdir = folder_name;
				}
			}


		} else if (line.data[0] == '#') {
			// printString("COMMENT: %\n", line);
		} else if (line.data[0] == '\n') {
			;
		} else {
			StringView	rhs = line;

			// rhs is at the space
			stringViewJumpToChar(&rhs, ' ');
			if (rhs.data[0] != ' ') {
				engine_log(__FILE_NAME__, "variables file parsing error at line %u! Expected a space after var name.\n", line_nr);
			} else {
				// rhs is after the space
				// rhs is now the value
				stringViewSkipChar(&rhs, ' ');

				// name is just the var name
				StringView name = line;
				// +1 for the space
				name.count -= rhs.count + 1;

				printString("Name: '%' ", name);
				printString("Value: '%'\n", rhs);

				assignField(subdir, name, rhs);
			}
		}

		line_nr++;
	}

	munmap(file_data.data, file_data.count);
}

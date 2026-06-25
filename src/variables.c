#include "vars.h"

// TODO: Make this hotload

Settings	g_settings;

ConfigField	audio_fields[] = {
	RegisterField(mix_all, CONFIG_FLOAT, Audio),
	RegisterField(mix_sfx, CONFIG_FLOAT, Audio),
};

ConfigField	dev_fields[] = {
	RegisterField(profiling, CONFIG_BOOL, Dev),
	RegisterField(testing, CONFIG_STR, Dev),
	RegisterField(testing_again, CONFIG_STR, Dev),
	RegisterField(testing_once_again, CONFIG_STR, Dev),
};

ConfigField	display_fields[] = {
	RegisterField(vsync, CONFIG_BOOL, Display),
};

enum assignFielddError {
	SUCCESS,
	NO_FIELD_NAME,
	NO_SUBDIR,
	STRING_QUOTE_START,
	STRING_QUOTE_END,
};

static enum assignFielddError 	modify_field(ConfigField field, String field_value, void *section)
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
			if (field_value.data[0] != '\"') {
				return STRING_QUOTE_START;
			}
			if (!stringViewPtrToChar(field_value, '\"')) {
				return STRING_QUOTE_END;
			}
			// Skip the quote
			field_value.data += 1;
			// Dont count the ending quote
			field_value.count -= 2;
			ptr = (char *)((u8 *)section + field.offset);
			printf("dereference ptr: %s\n", ptr);
			memcpy(ptr, field_value.data, field_value.count);
		break ;
		default: engine_log("variables", "How did we get here?\n");
	}
	return (SUCCESS);
}

static enum assignFielddError	assignField(String subdir, String field_name, String field_value)
{
	// The -1 is because the String type doesnt account for the \0 while sizeof() does
	const String	audio = {(u8 *)"Audio", sizeof("audio") - 1};
	const String	dev = {(u8 *)"Dev", sizeof("dev") - 1};
	const String	display = {(u8 *)"Display", sizeof("display") - 1};

	bool subdir_exists = false;

	if (stringIsEqual(subdir, audio)) {
		subdir_exists = true;
		for (u16 i = 0; i < sizeofarray(audio_fields); i++) {
			if (stringIsEqual(field_name, audio_fields[i].name)) {
				return modify_field(audio_fields[i], field_value, &g_settings.audio);
			}
		}
	} else if (stringIsEqual(subdir, dev)) {
		subdir_exists = true;
		for (u16 i = 0; i < sizeofarray(dev_fields); i++) {
			if (stringIsEqual(field_name, dev_fields[i].name)) {
				return modify_field(dev_fields[i], field_value, &g_settings.dev);
			}
		}
	} else if (stringIsEqual(subdir, display)) {
		subdir_exists = true;
		for (u16 i = 0; i < sizeofarray(display_fields); i++) {
			if (stringIsEqual(field_name, display_fields[i].name)) {
				return modify_field(display_fields[i], field_value, &g_settings.display);
			}
		}
	}
	if (!subdir_exists)
		return NO_SUBDIR;
	return NO_FIELD_NAME;
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
				engine_log("variables", "variables file parsing error at line %u! Lines starting with ':' must have a '/' and a name afterwards.\n", line_nr);
			} else {
				if (line.data[1] != '/') {
					engine_log("variables", "variables file parsing error at line %u! Expected a '/' after ':'.\n", line_nr);
				} else {
					subdir = line;
					subdir.data += 2;
					subdir.count -= 2;
					printString("Folder name '%'\n", subdir);
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

				enum assignFielddError error = assignField(subdir, name, rhs);
				switch (error) {
					case NO_FIELD_NAME:
						engine_log("variables", "Setting name doesnt match any known settings! line number: %u\n", line_nr);
					break;
					case NO_SUBDIR:
						engine_log("variables", "Subdir name doesnt match any known subdirs! line number: %u\n", line_nr);
					break;
					case STRING_QUOTE_END:
						engine_log("variables", "String must end with quote! line number: %u\n", line_nr);
					break;
					case STRING_QUOTE_START:
						engine_log("variables", "String must start with quote! line number: %u\n", line_nr);
					break;
					default:
					break;

				}
			}
		}

		line_nr++;
	}

	munmap(file_data.data, file_data.count);
}

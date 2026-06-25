#include "vars.h"
#include <errno.h>

// TODO: Make this hotload

Settings	g_settings;

ConfigField	audio_fields[] = {
	RegisterField(mix_all, CONFIG_FLOAT, Audio),
};

ConfigField	dev_fields[] = {
	RegisterField(profiling, CONFIG_BOOL, Dev),
};

ConfigField	display_fields[] = {
	RegisterField(vsync, CONFIG_BOOL, Display),
};

const String	audio_string = {(u8 *)"Audio", sizeofString("audio")};
const String	dev_string = {(u8 *)"Dev", sizeofString("dev")};
const String	display_string = {(u8 *)"Display", sizeofString("display")};

enum assignFielddError {
	SUCCESS,
	NO_FIELD_NAME,
	NO_SUBDIR,
	STRING_QUOTE_START,
	STRING_QUOTE_END,
};

static void	print_field(ConfigField fields[], u32 field_size, String subdir)
{
	const u32	printable_buffer_size = 256;
	char		printable_buffer[printable_buffer_size];

	const char	*true_string = "true";
	const char	*false_string = "false";

	void	*section;
	if (stringIsEqual(subdir, audio_string))
		section = &g_settings.audio;
	else if (stringIsEqual(subdir, dev_string))
		section = &g_settings.dev;
	else if (stringIsEqual(subdir, display_string))
		section = &g_settings.display;

	for (u32 i = 0; i < field_size; i++) {
		ConfigField	current_field = fields[i];
		void		*value = (void *)((u8 *)section + current_field.offset);

		switch (current_field.type) {
			case CONFIG_BOOL:
				if(*(bool *)value)
					stbsp_snprintf(printable_buffer, printable_buffer_size, "%S.%S = %s", subdir, current_field.name, true_string);
				else
					stbsp_snprintf(printable_buffer, printable_buffer_size, "%S.%S = %s", subdir, current_field.name, false_string);
			break;
			case CONFIG_STR:
				stbsp_snprintf(printable_buffer, printable_buffer_size, "%S.%S = %S", subdir, current_field.name, *(String *)value);
			break;
			case CONFIG_INT:
				stbsp_snprintf(printable_buffer, printable_buffer_size, "%S.%S = %i", subdir, current_field.name, *(int *)value);
			break;
			case CONFIG_FLOAT:
				stbsp_snprintf(printable_buffer, printable_buffer_size, "%S.%S = %.4f", subdir, current_field.name, *(float *)value);
			break;
		}
		printf("%s\n", printable_buffer);
	}
}

static void	print_variables()
{
	// MODIFY IF SETTINGS CHANGED: SUBDIRS
	print_field(audio_fields, sizeofarray(audio_fields), audio_string);
	print_field(dev_fields, sizeofarray(dev_fields), dev_string);
	print_field(display_fields, sizeofarray(display_fields), display_string);
}

static enum assignFielddError 	modify_field(ConfigField field, String field_value, void *section)
{
	char	c;
	float val;

	switch (field.type) {
		case CONFIG_BOOL: 
			if (stringIsEqual(field_value, (String){(u8 *)"true", sizeofString("true")})) {
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
			// NOTE: I am leaking this memory wilingly because it only happens in development
			// while i am hotloading variables. 25/06/26
			*(String *)((u8 *)section + field.offset) = stringDup(field_value, NULL);
		break ;
		default: engine_log("variables", "How did we get here?");
	}
	return (SUCCESS);
}

static enum assignFielddError	assignField(String subdir, String field_name, String field_value)
{

	bool subdir_exists = false;

	// MODIFY IF SETTINGS CHANGED: SUBDIRS
	if (stringIsEqual(subdir, audio_string)) {
		subdir_exists = true;
		for (u16 i = 0; i < sizeofarray(audio_fields); i++) {
			if (stringIsEqual(field_name, audio_fields[i].name)) {
				return modify_field(audio_fields[i], field_value, &g_settings.audio);
			}
		}
	} else if (stringIsEqual(subdir, dev_string)) {
		subdir_exists = true;
		for (u16 i = 0; i < sizeofarray(dev_fields); i++) {
			if (stringIsEqual(field_name, dev_fields[i].name)) {
				return modify_field(dev_fields[i], field_value, &g_settings.dev);
			}
		}
	} else if (stringIsEqual(subdir, display_string)) {
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

void	check_var_modify()
{
	static i64	last_mod = 0;

	struct stat	stats;
	stat("data/All.variables", &stats);

	if (last_mod == 0) {
		last_mod = stats.st_mtim.tv_sec;
	}

	if (last_mod != stats.st_mtim.tv_sec) {
		last_mod = stats.st_mtim.tv_sec;
		usleep(1000);
		engine_log("variables", "Variables changed, hotloading");
		print_variables();
		init_vars();
	}
}

void	init_vars()
{
	String	file_data = readFile("data/All.variables");
	if (file_data.data == NULL) {
		fprintf(stderr, "mmap error: %s\n", strerror(errno));
		engine_error("variables", "Failed to read file data/All.variables\n");
		return ;
	}

	u32	audio_fields_assigned = 0;
	u32	dev_fields_assigned = 0;
	u32	display_fields_assigned = 0;

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
				engine_log("variables", "variables file parsing error at line %u! Lines starting with ':' must have a '/' and a name afterwards.", line_nr);
			} else {
				if (line.data[1] != '/') {
					engine_log("variables", "variables file parsing error at line %u! Expected a '/' after ':'.", line_nr);
				} else {
					subdir = line;
					subdir.data += 2;
					subdir.count -= 2;
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
				engine_log(__FILE_NAME__, "variables file parsing error at line %u! Expected a space after var name.", line_nr);
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
						engine_log("variables", "Setting name doesnt match any known settings! line number: %u", line_nr);
					break;
					case NO_SUBDIR:
						engine_log("variables", "Subdir name doesnt match any known subdirs! line number: %u", line_nr);
					break;
					case STRING_QUOTE_END:
						engine_log("variables", "String must end with quote! line number: %u", line_nr);
					break;
					case STRING_QUOTE_START:
						engine_log("variables", "String must start with quote! line number: %u", line_nr);
					break;
					case SUCCESS:
						// MODIFY IF SETTINGS CHANGED: SUBDIRS
						if (stringIsEqual(subdir, audio_string))
							audio_fields_assigned++;
						else if (stringIsEqual(subdir, dev_string))
							dev_fields_assigned++;
						else if (stringIsEqual(subdir, display_string))
							display_fields_assigned++;
					break;

				}
			}
		}

		line_nr++;
	}

	if (audio_fields_assigned != sizeofarray(audio_fields))
		engine_log("variables", "Audio field registered but not in variables file!");
	if (dev_fields_assigned != sizeofarray(dev_fields))
		engine_log("variables", "dev field registered but not in variables file!");
	if (display_fields_assigned != sizeofarray(display_fields))
		engine_log("variables", "display field registered but not in variables file!");

	munmap(file_data.data, file_data.count);
}

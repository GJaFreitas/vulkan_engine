#include "vars.h"
#include <errno.h>

// TODO: Make this hotload

Settings	g_settings;

ConfigField	audio_fields[] = {
	RegisterField(mix_all, CONFIG_FLOAT, Audio),
};

ConfigField	dev_fields[] = {
	RegisterField(profiling, CONFIG_BOOL, Dev),
	RegisterField(grid_size, CONFIG_FLOAT, Dev),
	RegisterField(line_width, CONFIG_FLOAT, Dev),
	RegisterField(major_line_every, CONFIG_FLOAT, Dev),
	RegisterField(fade_distance, CONFIG_FLOAT, Dev),
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
	const char	*true_string = "true";
	const char	*false_string = "false";

	void	*section;
	if (strEq(subdir, audio_string)) {
		section = &g_settings.audio;
		fprintf(stdout, "AUDIO SETTINGS ----------\n");
	}
	else if (strEq(subdir, dev_string)) {
		section = &g_settings.dev;
		fprintf(stdout, "DEV SETTINGS ----------\n");
	}
	else if (strEq(subdir, display_string)) {
		section = &g_settings.display;
		fprintf(stdout, "DISPLAY SETTINGS ----------\n");
	}

	for (u32 i = 0; i < field_size; i++) {
		ConfigField	current_field = fields[i];
		void		*value = (void *)((u8 *)section + current_field.offset);

		switch (current_field.type) {
			case CONFIG_BOOL:
				if(*(bool *)value)
					print("%S.%S = %s\n", subdir, current_field.name, true_string);
				else
					print("%S.%S = %s\n", subdir, current_field.name, false_string);
			break;
			case CONFIG_STR:
				print("%S.%S = %S\n", subdir, current_field.name, *(String *)value);
			break;
			case CONFIG_INT:
				print("%S.%S = %i\n", subdir, current_field.name, *(int *)value);
			break;
			case CONFIG_FLOAT:
				print("%S.%S = %.4f\n", subdir, current_field.name, *(float *)value);
			break;
		}
	}
	fprintf(stdout, "\n");
}

static void	print_variables()
{
	// MODIFY IF SETTINGS CHANGED: SUBDIRS
	fprintf(stdout, "---------- VARIABLES ----------\n");
	print_field(audio_fields, sizeofarray(audio_fields), audio_string);
	print_field(dev_fields, sizeofarray(dev_fields), dev_string);
	print_field(display_fields, sizeofarray(display_fields), display_string);
	fprintf(stdout, "---------- DONE ----------\n");
}

static enum assignFielddError 	modify_field(ConfigField field, String field_value, void *section)
{
	char	c;
	float val;

	switch (field.type) {
		case CONFIG_BOOL: 
			if (strEq(field_value, (String){(u8 *)"true", sizeofString("true")})) {
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
			if (!strViewPtrToChar(field_value, '\"')) {
				return STRING_QUOTE_END;
			}
			// Skip the quote
			field_value.data += 1;
			// Dont count the ending quote
			field_value.count -= 2;
			// NOTE: I am leaking this memory wilingly because it only happens in development
			// while i am hotloading variables. 25/06/26
			*(String *)((u8 *)section + field.offset) = strDup(field_value, NULL);
		break ;
		default: engine_log(LOG_FILE, "How did we get here?");
	}
	return (SUCCESS);
}

static enum assignFielddError	assignField(String subdir, String field_name, String field_value)
{

	bool subdir_exists = false;

	// MODIFY IF SETTINGS CHANGED: SUBDIRS
	if (strEq(subdir, audio_string)) {
		subdir_exists = true;
		for (u16 i = 0; i < sizeofarray(audio_fields); i++) {
			if (strEq(field_name, audio_fields[i].name)) {
				return modify_field(audio_fields[i], field_value, &g_settings.audio);
			}
		}
	} else if (strEq(subdir, dev_string)) {
		subdir_exists = true;
		for (u16 i = 0; i < sizeofarray(dev_fields); i++) {
			if (strEq(field_name, dev_fields[i].name)) {
				return modify_field(dev_fields[i], field_value, &g_settings.dev);
			}
		}
	} else if (strEq(subdir, display_string)) {
		subdir_exists = true;
		for (u16 i = 0; i < sizeofarray(display_fields); i++) {
			if (strEq(field_name, display_fields[i].name)) {
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
		engine_log(LOG_FILE, "Variables changed, hotloading");
		print_variables();
		init_vars();
	}
}

void	vars_callback(void *udata)
{
	usleep(1000);
	engine_log(LOG_FILE, "Variables changed, hotloading");
	init_vars();
	print_variables();

	// TODO: Check if its worth logging which vars changed and then update things accordingly
	updateGridProperties(udata);
}

void	init_vars()
{
	String	file_data = readFile(STRING_LIT("data/All.variables"));
	if (file_data.data == NULL) {
		fprintf(stderr, "mmap error: %s\n", strerror(errno));
		engine_error(LOG_FILE, "Failed to read file data/All.variables\n");
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
		strViewSkipChar(&line, ' ');

		if (line.data[0] == ':') {

			if (line.count < 2) {
				engine_log(LOG_FILE, "variables file parsing error at line %u! Lines starting with ':' must have a '/' and a name afterwards.", line_nr);
			} else {
				if (line.data[1] != '/') {
					engine_log(LOG_FILE, "variables file parsing error at line %u! Expected a '/' after ':'.", line_nr);
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
			strViewJumpToChar(&rhs, ' ');
			if (rhs.data[0] != ' ') {
				engine_log(LOG_FILE, "variables file parsing error at line %u! Expected a space after var name.", line_nr);
			} else {
				// rhs is after the space
				// rhs is now the value
				strViewSkipChar(&rhs, ' ');

				// name is just the var name
				StringView name = line;
				// +1 for the space
				name.count -= rhs.count + 1;

				enum assignFielddError error = assignField(subdir, name, rhs);
				switch (error) {
					case NO_FIELD_NAME:
						engine_log(LOG_FILE, "Setting name doesnt match any known settings! line number: %u", line_nr);
					break;
					case NO_SUBDIR:
						engine_log(LOG_FILE, "Subdir name doesnt match any known subdirs! line number: %u", line_nr);
					break;
					case STRING_QUOTE_END:
						engine_log(LOG_FILE, "String must end with quote! line number: %u", line_nr);
					break;
					case STRING_QUOTE_START:
						engine_log(LOG_FILE, "String must start with quote! line number: %u", line_nr);
					break;
					case SUCCESS:
						// MODIFY IF SETTINGS CHANGED: SUBDIRS
						if (strEq(subdir, audio_string))
							audio_fields_assigned++;
						else if (strEq(subdir, dev_string))
							dev_fields_assigned++;
						else if (strEq(subdir, display_string))
							display_fields_assigned++;
					break;

				}
			}
		}

		line_nr++;
	}

	if (audio_fields_assigned != sizeofarray(audio_fields))
		engine_log(LOG_FILE, "Audio field registered but not in variables file!");
	if (dev_fields_assigned != sizeofarray(dev_fields))
		engine_log(LOG_FILE, "dev field registered but not in variables file!");
	if (display_fields_assigned != sizeofarray(display_fields))
		engine_log(LOG_FILE, "display field registered but not in variables file!");

	munmap(file_data.data, file_data.count);
}

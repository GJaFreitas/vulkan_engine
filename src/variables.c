#include "base_layer.h"

void	init_vars()
{
	String	file_data = readFile("data/All.variables");

	u64	offset = 0;
	u16	line_nr = 1;
	while (true) {

		StringView	line = getNextLine_noMem(file_data, &offset);
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
					engine_log(__FILE_NAME__, "variables file parsing error at line %u! Exepected a '/' after ':'.\n", line_nr);
				} else {
					StringView folder_name = line;
					folder_name.data += 2;
					folder_name.count -= 2;
					printString("Folder name %\n", folder_name);
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
				engine_log(__FILE_NAME__, "variables file parsing error at line %u! Exepected a space after var name.\n", line_nr);
			} else {
				// rhs is after the space
				// rhs is now the value
				stringViewSkipChar(&rhs, ' ');

				// name is just the var name
				StringView name = line;
				name.count -= rhs.count;

				printString("Name: %", name);
				printString("Value: %\n", rhs);
			}
		}

		line_nr++;
	}
}

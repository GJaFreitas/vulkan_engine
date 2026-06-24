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
		line.size--;
		// Advance spaces
		stringViewSkipChar(&line, ' ');

		if (line.data[0] == ':') {
			printString("SUBFOLDER: %\n", line);
		} else if (line.data[0] == '#') {
			printString("COMMENT: %\n", line);
		} else if (line.data[0] == '\n') {
			;
		} else {
			printString("VALUE: %\n", line);
		}

		line_nr++;
	}
}

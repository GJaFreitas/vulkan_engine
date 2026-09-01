#include "console.h"

// TODO: When disabling the console the '\' character stays in the input buffer

#define MAX_HISTORY	200
typedef struct ConsoleHistory
{
	u8	command[CONSOLE_MAX_INPUT_LEN];
	u16	command_len;
}	ConsoleCommand;
ConsoleCommand	commands[MAX_HISTORY];
u16		current_history_idx = 0;

typedef struct InputBuffer
{
	u8	input_buf[CONSOLE_MAX_INPUT_LEN];
	u16	cursor_pos;
	u16	strlen;
}	InputBuffer;

InputBuffer	input = {{0}, 0, 0};

void	consoleBackspace(void)
{
	if (input.cursor_pos > 0) {
		input.cursor_pos--;
		input.input_buf[input.cursor_pos] = '\0'; 
	}
}

void	consoleEnter(void)
{
	if (input.cursor_pos > 0) {
		// 1. Copy input_buffer to log history here
		if (current_history_idx >= MAX_HISTORY) {
			current_history_idx = 0;
		}
		memcpy(commands[current_history_idx].command, input.input_buf, input.strlen);
		commands[current_history_idx++].command_len = input.strlen;
		// --- //

		// 2. Dispatch command parser here

		// --- //

		// 3. Clear the active buffer
		memset(input.input_buf, 0, CONSOLE_MAX_INPUT_LEN);
		input.cursor_pos = 0;
		input.strlen = 0;
		// --- //
	}
}

void	consoleInputInsert(const char *text, u64 len)
{
	if (input.cursor_pos + len < CONSOLE_MAX_INPUT_LEN - 1) {
		memcpy(&input.input_buf[input.cursor_pos], text, len);
		input.cursor_pos += len;
		input.strlen += len;
	}
}

String	consoleHist(void)
{
	String	s = {commands[current_history_idx + 1].command, commands[current_history_idx + 1].command_len};
	return s;
}

String	consoleInput(void)
{
	String	out;
	out.data = input.input_buf;
	out.count = input.strlen;
	return out;
}

void	consoleLeftArrow(void)
{

}

void	consoleRightArrow(void)
{
	
}

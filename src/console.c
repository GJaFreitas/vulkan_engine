#include "console.h"

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
		// Basic ASCII backspace (Note: A true UTF-8 backspace 
		// needs to check if the deleted byte is a continuation byte)
		input.cursor_pos--;
		input.input_buf[input.cursor_pos] = '\0'; 
	}
}

void	consoleEnter(void)
{
	if (input.cursor_pos > 0) {
		// 1. Copy input_buffer to log history here
		// 2. Dispatch command parser here

		// 3. Clear the active buffer
		memset(input.input_buf, 0, CONSOLE_MAX_INPUT_LEN);
		input.cursor_pos = 0;
	}
}

void	consoleInputInsert(const char *text, u64 len)
{
	if (input.cursor_pos + len < CONSOLE_MAX_INPUT_LEN - 1) {
		memcpy(&input.input_buf[input.cursor_pos], text, len);
		input.cursor_pos += len;
	}
}

String	consoleHist(void)
{
	u8	data[] = "Placeholder";
	String	s = {data, sizeofString(data)};
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

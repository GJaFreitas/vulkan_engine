#pragma once

#include "base_layer.h"

#define CONSOLE_MAX_INPUT_LEN	256


#define MAX_HISTORY	200
typedef struct ConsoleHistory
{
	u8	command[CONSOLE_MAX_INPUT_LEN];
	u16	command_len;
}	ConsoleCommand;

void	consoleInputInsert(const char *text, u64 len);
void	consoleBackspace(void);
void	consoleEnter(void);
void	consoleLeftArrow(void);
void	consoleRightArrow(void);


ConsoleCommand	*consoleHist(u16 *start);
String	consoleInput(void);

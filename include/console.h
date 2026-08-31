#pragma once

#include "base_layer.h"

#define CONSOLE_MAX_INPUT_LEN	256

void	consoleInputInsert(const char *text, u64 len);
void	consoleBackspace(void);
void	consoleEnter(void);
void	consoleLeftArrow(void);
void	consoleRightArrow(void);


String	consoleHist(void);
String	consoleInput(void);

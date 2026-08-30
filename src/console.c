#include "base_layer.h"

u8	text[] = "Hi this text is in the console";
String	placeholder = {text, sizeofString(text)};

String	consoleBackend(void)
{
	return placeholder;
}

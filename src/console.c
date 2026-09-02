#include "console.h"
#include "stb_sprintf.h"
#include "world.h"

// TODO: When disabling the console the '\' character stays in the input buffer

ConsoleCommand	commands[MAX_HISTORY];
u16		current_history_idx = 0;

typedef struct InputBuffer
{
	u8	input_buf[CONSOLE_MAX_INPUT_LEN];
	u16	cursor_pos;
	u16	strlen;
}	InputBuffer;

static InputBuffer	input = {{0}, 0, 0};

// --- COMMAND DISPATCH --- //

static void	_toggle(int argc, String *argv)
{
	(void)argc;
	(void)argv;
	if (argc != 2) {
		consoleAppend("Usage: toggle [variable]");
	}
	if (strEq(argv[1], STRING_LIT("fps")))
		gameStateToggle(&game_state, ShowFps);
	else if (strEq(argv[1], STRING_LIT("grid")))
		gameStateToggle(&game_state, ShowGrid);
}

// A standard signature for console commands. 
// argc = number of arguments typed. argv = array of argument strings.
typedef void	(*ConsoleCmdFn)(int argc, String *argv);

typedef struct
{
	String name;
	ConsoleCmdFn func;
	String description;
} CCmd;

#define RegisterCmd(string_name, func_ptr, desc) \
	{(String){(u8 *)(string_name), sizeof(string_name) - 1}, func_ptr, {(u8*)(desc), sizeof(desc)-1}}

static const CCmd g_commands[] = {
	RegisterCmd("spawn", spawnCommand, "Spawns an entity"),
	RegisterCmd("toggle", _toggle, "Toggles fps counter"),
};
static const u32 g_cmd_count = sizeof(g_commands) / sizeof(g_commands[0]);

static String *tokenizeCommand(StringView command, Allocator *frame_allocator, u16 *out_argc)
{
	if (command.count == 0) {
		*out_argc = 0;
		return NULL;
	}

	u16 arg_count = 0;
	bool in_word = false;

	for (u16 i = 0; i < command.count; i++) {
		if (command.data[i] != ' ') {
			if (!in_word) {
				arg_count++;
				in_word = true;
			}
		} else {
			in_word = false;
		}
	}

	if (arg_count == 0) {
		*out_argc = 0;
		return NULL;
	}

	String *arguments = frame_allocator->fp_allocation(
		frame_allocator, 
		sizeof(String) * arg_count, 
		DEFAULT_ALIGN
	);

	u16 arg_idx = 0;
	u16 word_start = 0;
	in_word = false;

	for (u16 i = 0; i <= command.count; i++) {
		bool is_space_or_end = (i == command.count || command.data[i] == ' ');

		if (!is_space_or_end) {
			if (!in_word) {
				word_start = i; // Mark the start of a new word
				in_word = true;
			}
		} else {
			if (in_word) {
				// Slice directly into the original command buffer
				arguments[arg_idx].data = command.data + word_start;
				arguments[arg_idx].count = i - word_start;

				arg_idx++;
				in_word = false;
			}
		}
	}

	*out_argc = arg_count;
	return arguments;
}

static void	validateAndDispatch(InputBuffer ibuf, Allocator *frame_allocator)
{
	StringView	command = {ibuf.input_buf, ibuf.strlen};

	u16	argc;
	String	*argv = tokenizeCommand(command, frame_allocator, &argc);

	for (u32 i = 0; i < g_cmd_count; i++) {
		// TODO: Add functionality to mess with settings like:
		// vsync off
		if (strEq(argv[0], g_commands[i].name)) {
			g_commands[i].func(argc, argv);
		}
	}
}

// --- CONSOLE STUFF --- //

void	consoleBackspace(void)
{
	if (input.cursor_pos > 0) {
		input.cursor_pos--;
		input.input_buf[input.cursor_pos] = '\0'; 
	}
}

void	consoleEnter(Allocator *frame_allocator)
{
	if (input.cursor_pos > 0) {
		// 1. Copy input_buffer to log history
		if (current_history_idx >= MAX_HISTORY) {
			current_history_idx = 0;
		}
		memcpy(commands[current_history_idx].command, input.input_buf, input.strlen);
		commands[current_history_idx++].command_len = input.strlen;
		// --- //
		// 2. Dispatch command parser
		validateAndDispatch(input, frame_allocator);
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

void	consoleAppend(const char *fmt, ...)
{
	va_list	ap;
	va_start(ap, fmt);

	const u64	buf_size = CONSOLE_MAX_INPUT_LEN;
	u8		buf[buf_size];

	u64	written = stbsp_vsnprintf((char *)buf, buf_size, fmt, ap);
	va_end(ap);

	String	text = {buf, written};

	ConsoleCommand	new_command;
	if (text.count >= CONSOLE_MAX_INPUT_LEN) text.count = CONSOLE_MAX_INPUT_LEN - 1;
	memcpy(new_command.command, text.data, text.count);
	new_command.command_len = text.count;
	if (current_history_idx >= MAX_HISTORY) {
		current_history_idx = 0;
	}
	commands[current_history_idx++] = new_command;
}

ConsoleCommand	*consoleHist(u16 *start)
{
	*start = current_history_idx;
	return commands;
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



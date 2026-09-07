#include "console.h"
#include "stb_sprintf.h"
#include "world.h"

void	_trie_test(int argc, String *argv);

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
	RegisterCmd("trie", _trie_test, "Test the trie"),
};
static const u32 g_cmd_count = sizeof(g_commands) / sizeof(g_commands[0]);


// --- TRIE --- //

#define MAX_TRIE_NODES 2048

typedef struct
{
	u16    first_child, last_child, next_sibling;
	u8     is_end_of_word; // Added to differentiate prefixes vs actual commands
	String prefix;
}	Trie;

Trie    *trie;
u32     entry_count = 0;

static inline Trie    *trieGet(u16 idx)
{
	return &trie[idx];
}

static inline u16    allocateTrie(void)
{
	if (entry_count >= MAX_TRIE_NODES - 1) {
		engine_debug(LOG_FILE, "Trie node limit reached!");
		exit(1);
	}
	entry_count++;
	return entry_count;
}

// Helper: Safely allocate a string block inside the arena
static String    allocString(Allocator *perm_arena, String s)
{
	String res;
	res.count = s.count;
	res.data = perm_arena->fp_allocation(perm_arena, s.count, DEFAULT_ALIGN);
	memcpy(res.data, s.data, s.count);
	return res;
}

// Helper: Returns a substring covering indices [0, length)
static String    strPrefix(String s, u32 length)
{
	String res = s;
	res.count = length;
	return res;
}

static u32    getSharedChars(String a, String b)
{
	u32 i = 0;
	for (; i < a.count && i < b.count; i++) {
		if (a.data[i] != b.data[i]) break;
	}
	return i;
}

// Helper: Safely append a child and maintain the last_child pointer
static void    addChild(u16 parent_idx, u16 child_idx)
{
	Trie *parent = trieGet(parent_idx);
	if (parent->first_child == 0) {
		parent->first_child = child_idx;
		parent->last_child = child_idx;
	} else {
		Trie *last = trieGet(parent->last_child);
		last->next_sibling = child_idx;
		parent->last_child = child_idx;
	}
}

// Helper: Swap out a child when a node splits
static void    replaceChild(u16 parent_idx, u16 old_child, u16 new_child)
{
	Trie *parent = trieGet(parent_idx);
	Trie *new_node = trieGet(new_child);
	Trie *old_node = trieGet(old_child);

	new_node->next_sibling = old_node->next_sibling;
	old_node->next_sibling = 0;

	if (parent->first_child == old_child) {
		parent->first_child = new_child;
	} else {
		u16 cur = parent->first_child;
		while (cur != 0) {
			Trie *c = trieGet(cur);
			if (c->next_sibling == old_child) {
				c->next_sibling = new_child;
				break;
			}
			cur = c->next_sibling;
		}
	}
	if (parent->last_child == old_child) {
		parent->last_child = new_child;
	}
}

static u16    trieSearch(String s, u16 cur_idx)
{
	if (s.count == 0) return cur_idx;

	Trie *t = trieGet(cur_idx);
	u16 child_idx = t->first_child;

	while (child_idx != 0) {
		Trie *child = trieGet(child_idx);
		u32 shared = getSharedChars(child->prefix, s);

		if (shared > 0) {
			if (shared == s.count) {
				// The input is a full prefix of this child.
				// E.g., user typed "ap", and this node is "apple".
				return child_idx; 
			} else if (shared == child->prefix.count) {
				// The child is a prefix of the input. Traverse deeper.
				// E.g., user typed "apple", and this node is "app".
				return trieSearch(s, child_idx);
			}
			// Partial match that diverged (e.g. typing "apk" but finding "apple")
			return 0; 
		}
		child_idx = child->next_sibling;
	}
	return 0;
}

static void    trieAppend(String s, Allocator *perm_arena)
{
	u16 cur_idx = 0;

	while (1) {
		Trie *cur = trieGet(cur_idx);
		u16 child_idx = cur->first_child;

		u32 best_shared = 0;
		u16 best_child = 0;

		// Radix trees only have a maximum of 1 branch that shares a prefix
		while (child_idx != 0) {
			Trie *child = trieGet(child_idx);
			u32 shared = getSharedChars(child->prefix, s);
			if (shared > 0) {
				best_shared = shared;
				best_child = child_idx;
				break; 
			}
			child_idx = child->next_sibling;
		}

		// Case 1: No overlaps. Just add as a new leaf.
		if (best_shared == 0) {
			u16 new_idx = allocateTrie();
			Trie *new_node = trieGet(new_idx);
			new_node->prefix = allocString(perm_arena, s);
			new_node->is_end_of_word = 1;
			addChild(cur_idx, new_idx);
			return;
		}

		Trie *child = trieGet(best_child);

		// Case 2: Exact duplicate
		if (best_shared == child->prefix.count && best_shared == s.count) {
			child->is_end_of_word = 1;
			return;
		}

		// Case 3: The new string is a longer version of the child. Go deeper.
		if (best_shared == child->prefix.count) {
			cur_idx = best_child;
			continue;
		}

		// Case 4: Node Split (e.g. tree has "apple", we insert "apt")
		// We create an intermediate node "ap"
		u16 split_idx = allocateTrie();
		Trie *split_node = trieGet(split_idx);
		split_node->prefix = allocString(perm_arena, strPrefix(s, best_shared));
		split_node->is_end_of_word = 0; 

		// The existing child ("apple") becomes a child of the split node
		split_node->first_child = best_child;
		split_node->last_child = best_child;
		replaceChild(cur_idx, best_child, split_idx);

		// Add the new string ("apt") as the sibling, unless it IS the split node
		if (best_shared == s.count) {
			split_node->is_end_of_word = 1;
		} else {
			u16 new_leaf_idx = allocateTrie();
			Trie *new_leaf = trieGet(new_leaf_idx);
			new_leaf->prefix = allocString(perm_arena, s);
			new_leaf->is_end_of_word = 1;
			addChild(split_idx, new_leaf_idx);
		}
		return;
	}
}

// Drops down the tree until it finds a complete command
static u16    getFirstLeaf(u16 cur_idx)
{
	while (cur_idx != 0) {
		Trie *t = trieGet(cur_idx);
		if (t->is_end_of_word) return cur_idx;
		cur_idx = t->first_child;
	}
	return 0;
}

void    consoleInit(Allocator *perm_arena)
{
	trie = perm_arena->fp_allocation(perm_arena, MAX_TRIE_NODES * sizeof(Trie), DEFAULT_ALIGN);
	memset(trie, 0, MAX_TRIE_NODES * sizeof(Trie));
	entry_count = 0;
	for (u32 i = 0; i < g_cmd_count; i++) {
		trieAppend(g_commands[i].name, perm_arena);
	}
}

void	_trie_test(int argc, String *argv)
{
	if (argc < 2) return;

	u16    best_idx = trieSearch(argv[1], 0);

	// If we landed on an incomplete prefix node, dig down to a full command
	if (best_idx != 0 && !trieGet(best_idx)->is_end_of_word) {
		best_idx = getFirstLeaf(best_idx);
	}

	if (best_idx) {
		Trie *best_match = trieGet(best_idx);
		consoleAppend("Best match: %S", best_match->prefix);
	} else {
		consoleAppend("Best match: %S", STRING_LIT("none"));
	}
}

// --- COMMAND DISPATCH --- //

ConsoleCommand	commands[MAX_HISTORY];
u16		current_history_idx = 0;

typedef struct InputBuffer
{
	u8	input_buf[CONSOLE_MAX_INPUT_LEN];
	u16	cursor_pos;
	u16	strlen;
}	InputBuffer;

static InputBuffer	input = {{0}, 0, 0};

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
		input.strlen--;
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

	ConsoleCommand	new_command = {0};
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

void	consoleTab(void)
{
	String	cur_written = {input.input_buf, input.strlen};
	u16	best_match_idx = trieSearch(cur_written, 0);
	if (!best_match_idx) return ;

	Trie	*best_match = trieGet(best_match_idx);
	String	completion = best_match->prefix;

	memset(input.input_buf, 0, CONSOLE_MAX_INPUT_LEN);
	memcpy(input.input_buf, completion.data, completion.count);
	input.cursor_pos = completion.count + 1;
	input.input_buf[input.cursor_pos - 1] = ' ';
	input.strlen = completion.count + 1;
}

#pragma once

#include <string.h>
#include <stdarg.h>

#include "typedefs.h"
#include "allocators.h"

#define WHITESPACE " \n\t\r\v"

u64	cstrlen(const char *cs);
bool	strEq(StringView sv1, StringView sv2);
String	cstringToString(const char *cs, Allocator *a);
String	strDup(StringView sv, Allocator *a);
char	*strToCstring(StringView sv, Allocator *a);
u32	vsprint(char *buf, u64 buf_size, const char *fmt, ...);
u32	print(const char *fmt, ...);;
// Advances the ptr by 'count';
void	strViewAdvance(StringView *sv, u64 count);
// Returns true if char was found and false if end of string was reached;
bool	strViewJumpToChar(StringView *sv, const char c);
// Returns a pointer to the first instance of 'c';
u8	*strViewPtrToChar(StringView sv, const char c);
// Returns a pointer to the last instance of 'c';
u8	*strViewPtrToCharR(StringView sv, const char c);
void	strViewSkipChar(StringView *sv, const char c);
// Advances the ptr to after every char in 'skip';
bool	strViewSkip(StringView *sv, StringView skip);
i64	strToLong(StringView _sv);
// Basic implementation;
float32	strToFloat(StringView _sv);
void	strCopy(StringView dst, StringView src);
String	subStr(StringView sv, u64 size, Allocator *a);
StringView	strViewChr(String s, const char c);
void	strReadSize(StringView *sv, void *dest, u64 size);

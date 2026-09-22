#include "str.h"
#include "allocators.h"
#include "stb_sprintf.h"

bool	isNum(const char c)
{
	return (c >= '0' && c <= '9');
}

u32	vsprint(char *buf, u64 buf_size, const char *fmt, ...)
{
	va_list	ap;
	va_start(ap, fmt);

	u64	written = stbsp_vsnprintf(buf, buf_size, fmt, ap);

	va_end(ap);
	return (written);
}

u32	print(const char *fmt, ...)
{
	va_list	ap;
	va_start(ap, fmt);

	const u64	buf_size = 4096;
	char		buf[buf_size];

	u64	written = stbsp_vsnprintf(buf, buf_size, fmt, ap);
	write(1, buf, written);

	va_end(ap);
	return (written);
}

u64	cstrlen(const char *cs)
{
	u64	i = 0;

	if (cs == NULL) return (0);
	while (cs[i] != 0) i++;
	return (i);
}

bool	strEq(StringView sv1, StringView sv2)
{
	if (sv1.count != sv2.count)
		return false;
	for (u64 i = 0; i < sv1.count; i++) {
		if (sv1.data[i] != sv2.data[i])
			return false;
	}
	return true;
}

String	cstringToString(const char *cs, Allocator *a)
{
	String	s;

	s.count = cstrlen(cs);
	if (a)
		s.data = a->fp_allocation(a, s.count + 1, DEFAULT_ALIGN);
	else
		s.data = standard_alloc(s.count + 1, DEFAULT_ALIGN);
	memcpy(s.data, cs, s.count);
	return (s);
}

String	strDup(StringView sv, Allocator *a)
{
	String	s;

	s.count = sv.count;
	s.data = a->fp_allocation(a, s.count, DEFAULT_ALIGN);
	memcpy(s.data, sv.data, s.count);
	return (s);
}

char	*strToCstring(StringView sv, Allocator *a)
{
	char	*cs;

	cs = a->fp_allocation(a, sv.count + 1, DEFAULT_ALIGN);
	memcpy(cs, sv.data, sv.count);
	cs[sv.count] = 0;
	return (cs);
}

// Advances the ptr by 'count'
void	strViewAdvance(StringView *sv, u64 count)
{
	sv->data += count;
	sv->count -= count;
}

// Returns true if char was found and false if end of string was reached
bool	strViewJumpToChar(StringView *sv, const char c)
{
	bool	ret = false;
	u64	i;

	i = 0;
	while (i < sv->count)
	{
		if (sv->data[i] == c) {
			ret = true;
			break ;
		}
		i++;
	}
	sv->count -= i;
	sv->data += i;
	return (ret);
}

// Returns a pointer to the first instance of 'c'
u8	*strViewPtrToChar(StringView sv, const char c)
{
	u64	i;

	i = 0;
	while (i < sv.count)
	{
		if (sv.data[i] == c)
			return &sv.data[i];
		i++;
	}
	return NULL;
}

// Returns a pointer to the last instance of 'c'
u8	*strViewPtrToCharR(StringView sv, const char c)
{
	i64	i;

	i = sv.count - 1;
	while (i >= 0)
	{
		if (sv.data[i] == c)
			return &sv.data[i];
		i--;
	}
	return NULL;
}

void	strViewSkipChar(StringView *sv, const char c)
{
	u64	i;

	i = 0;
	while (i < sv->count)
	{
		if (sv->data[i] != c)
			break ;
		i++;
	}

	sv->count -= i;
	sv->data += i;
}

// Advances the ptr to after every char in 'skip'
bool	strViewSkip(StringView *sv, StringView skip)
{
	u64 i = 0;
	for (; i < sv->count; i++) {
		if (strViewPtrToChar(skip, sv->data[i]) == NULL) {
			break ;
		}
	}
	strViewAdvance(sv, i);
	return (i != sv->count);
}

i64	strToLong(StringView _sv)
{
	StringView	*sv = &_sv;
	i32	sign = 1;

	strViewSkip(sv, STRING_LIT(WHITESPACE));

	// End of string reached
	if (sv->count == 0) {
		return (0);
	}

	for (u64 i = 0; i < sv->count; i++) {
		if (sv->data[0] >= '0' && sv->data[0] <= '9') {
			break;
		} else if (sv->data[0] == '-') {
			sign = -1;
		}
		strViewAdvance(sv, 1);
	}

	i64	total = 0;
	for (u64 i = 0; i < sv->count && isNum(sv->data[i]); i++) {
		u8	num = sv->data[i] - '0';
		total *= 10;
		total += num;
	}
	total *= sign;
	return (total);
}

// Basic implementation
float32	strToFloat(StringView _sv)
{
	StringView	*sv = &_sv;
	i32	sign = 1;

	strViewSkip(sv, STRING_LIT(WHITESPACE));

	// End of string reached
	if (sv->count == 0) {
		return (0);
	}

	for (u64 i = 0; i < sv->count; i++) {
		if (sv->data[0] >= '0' && sv->data[0] <= '9') {
			break;
		} else if (sv->data[0] == '-') {
			sign = -1;
		}
		strViewAdvance(sv, 1);
	}

	float32	total = 0.0f;
	float32	frac = 1.0f;
	bool	is_decimal = false;

	for (u64 i = 0; i < sv->count; i++) {
		if (isNum(sv->data[i])) {
			total = total * 10.0f + (sv->data[i] - '0');
			if (is_decimal) {
				frac *= 10.0f;
			}
		} else if (sv->data[0] == '.') {
			if (is_decimal) {
				// Two '.', this is an error
				return 0.0f;
			}
			is_decimal = true;
		} else {
			break ;
		}
	}

	if (is_decimal) {
		total /= frac;
	}
	
	return (total * sign);
}

void	strCopy(StringView dst, StringView src)
{
	u64	size = dst.count < src.count ? dst.count : src.count;
	memcpy(dst.data, src.data, size);
}

String	subStr(StringView sv, u64 size, Allocator *a)
{
	sv.count = size;
	return (strDup(sv, a));
}

StringView	strViewChr(String s, const char c)
{
	StringView	sv = s;

	u64 i = 0;
	for (; i < s.count; i++) {
		if (s.data[i] == c) {
			break;
		}
	}
	sv.count = i;
	return sv;
}

// No checks here, segfault is welcome
void	strReadSize(StringView *sv, void *dest, u64 size)
{
	memcpy(dest, sv->data, size);
	strViewAdvance(sv, size);
}

// No allocations
StringView	getNextLine(String str, u64 *offset)
{
	StringView	line;

	if (*offset >= str.count)
		return (StringView){NULL, 0};

	line.data = (u8 *)str.data + (*offset);

	u64	i = *offset;
	for (; i < str.count && str.data[i] != '\n'; i++) { }

	line.count = i - (*offset);

	if (i < str.count && str.data[i] == '\n')
		line.count++;

	*offset = i + (i < str.count);

	return line;
}

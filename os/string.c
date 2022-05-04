#include "string.h"
#include "types.h"

static void *fast_memset(void *dst, int val, uint cnt)
{
	char *sp = (char *)dst; // start pos
	char *ep = sp + cnt;

	// if cnt is too large, fast copy; otherwise, trivial copy

	if (cnt > 32) {
		// align to 8 bytes
		uint64 r = (8 - (uint64)sp % 8) % 8;
		while (r != 0) {
			*sp++ = val;
			--r;
		}

		// fast copy
		uint64 val64 = val & 0xFF;
		val64 |= val64 << 8;
		val64 |= val64 << 16;
		val64 |= val64 << 32;

		r = (uint64)(ep - sp) / 8;

		int tmp = r % 8;
		for (int i = 0; i < tmp; ++i) {
			*(uint64 *)sp = val64;
			sp += 8;
		}
		r -= tmp;

		while (r > 0) {
			((uint64 *)sp)[0] = val64;
			((uint64 *)sp)[1] = val64;
			((uint64 *)sp)[2] = val64;
			((uint64 *)sp)[3] = val64;
			((uint64 *)sp)[4] = val64;
			((uint64 *)sp)[5] = val64;
			((uint64 *)sp)[6] = val64;
			((uint64 *)sp)[7] = val64;
			sp += 64, r -= 8;
		}
	}

	// copy others
	while (sp != ep) {
		*sp++ = val;
	}

	return dst;
}

void *memset(void *dst, int c, uint n)
{
	return fast_memset(dst, c, n);
}

int memcmp(const void *v1, const void *v2, uint n)
{
	const uchar *s1, *s2;

	s1 = v1;
	s2 = v2;
	while (n-- > 0) {
		if (*s1 != *s2)
			return *s1 - *s2;
		s1++, s2++;
	}

	return 0;
}

void *memmove(void *dst, const void *src, uint n)
{
	const char *s;
	char *d;

	s = src;
	d = dst;
	if (s < d && s + n > d) {
		s += n;
		d += n;
		while (n-- > 0)
			*--d = *--s;
	} else
		while (n-- > 0)
			*d++ = *s++;

	return dst;
}

// memcpy exists to placate GCC.  Use memmove.
void *memcpy(void *dst, const void *src, uint n)
{
	return memmove(dst, src, n);
}

int strncmp(const char *p, const char *q, uint n)
{
	while (n > 0 && *p && *p == *q)
		n--, p++, q++;
	if (n == 0)
		return 0;
	return (uchar)*p - (uchar)*q;
}

char *strncpy(char *s, const char *t, int n)
{
	char *os;

	os = s;
	while (n-- > 0 && (*s++ = *t++) != 0)
		;
	while (n-- > 0)
		*s++ = 0;
	return os;
}

// Like strncpy but guaranteed to NUL-terminate.
char *safestrcpy(char *s, const char *t, int n)
{
	char *os;

	os = s;
	if (n <= 0)
		return os;
	while (--n > 0 && (*s++ = *t++) != 0)
		;
	*s = 0;
	return os;
}

int strlen(const char *s)
{
	int n;

	for (n = 0; s[n]; n++)
		;
	return n;
}

void dummy(int _, ...)
{
}
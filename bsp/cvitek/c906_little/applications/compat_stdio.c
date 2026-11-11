/*
 * stdio compat layer on top of RT-Thread klibc.
 *
 * Both bsp and cvi_mw components call newlib's printf/sprintf family.
 * Without these definitions the linker pulls newlib's vfprintf/ldtoa
 * machinery from libc.a (~30KB of .text in small-core images).
 * Route them to rt_kprintf / rt_vsnprintf instead, so that cluster is
 * never linked.
 */
#include <stdio.h>
#include <stdarg.h>
#include <rtthread.h>

#define COMPAT_PRINTF_BUF 256

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	return rt_vsnprintf(buf, size, fmt, ap);
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = rt_vsnprintf(buf, size, fmt, ap);
	va_end(ap);

	return n;
}

int sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = rt_vsnprintf(buf, (size_t)-1, fmt, ap);
	va_end(ap);

	return n;
}

int printf(const char *fmt, ...)
{
	char buf[COMPAT_PRINTF_BUF];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = rt_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	rt_kprintf("%s", buf);

	return n;
}

int fprintf(FILE *stream, const char *fmt, ...)
{
	char buf[COMPAT_PRINTF_BUF];
	va_list ap;
	int n;

	(void)stream;
	va_start(ap, fmt);
	n = rt_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	rt_kprintf("%s", buf);

	return n;
}

int puts(const char *s)
{
	rt_kprintf("%s\n", s);

	return 0;
}
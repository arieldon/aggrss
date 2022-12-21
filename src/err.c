#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "err.h"

internal inline void
err_out(va_list args, char *fmt, int errcode)
{
	// TODO(ariel) Convert this function into a macro instead.
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, args);
	if (errcode) fprintf(stderr, " (%s)\n", strerror(errcode));
	else fprintf(stderr, "\n");
}

void
err_msg(char *fmt, ...)
{
	int errcode = errno;
	va_list args;
	va_start(args, fmt);
	err_out(args, fmt, errcode);
	va_end(args);
}

void
err_exit(char *fmt, ...)
{
	int errcode = errno;
	va_list args;
	va_start(args, fmt);
	err_out(args, fmt, errcode);
	va_end(args);
	exit(1);
}

static inline void
err_out(va_list args, char *fmt, int errcode)
{
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, args);
	if (errcode)
	{
		fprintf(stderr, " (%s)\n", strerror(errcode));
	}
	else fprintf(stderr, "\n");
}

static void
err_msg(char *fmt, ...)
{
	int errcode = errno;
	va_list args;
	va_start(args, fmt);
	err_out(args, fmt, errcode);
	va_end(args);
}

static void
err_exit(char *fmt, ...)
{
	int errcode = errno;
	va_list args;
	va_start(args, fmt);
	err_out(args, fmt, errcode);
	va_end(args);
	exit(1);
}

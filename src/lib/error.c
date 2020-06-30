/*
 * Copyright (C) 2009 John Kacur <jkacur@redhat.com>
 *
 * error routines, similar to those found in
 * Advanced Programming in the UNIX Environment 2nd ed.
 */
#include "error.h"
#include <stdlib.h>

/* print an error message and return */
void err_msg(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("ERROR: ", stderr);
	err_doit(0, fmt, ap);
	va_end(ap);
	return;
}

/* Print an error message, plus a message for err, and return */
void err_msg_n(int err, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("ERROR: ", stderr);
	err_doit(err, fmt, ap);
	va_end(ap);
	return;
}

/* print an error message and quit */
void err_exit(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	err_doit(0, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

/* Print an error message, plus a message for err and exit with error err */
void err_exit_n(int err, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("FATAL: ", stderr);
	err_doit(err, fmt, ap);
	va_end(ap);
	exit(err);
}

/* logging only, print messange DEBUG */
void debug(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fputs("DEBUG: ", stderr);
	err_doit(0, fmt, ap);
	va_end(ap);
}

/* logging only, print messange Continuation INFO */
void cont(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fputs("... ", stderr);
	err_doit(0, fmt, ap);
	va_end(ap);
}

/* logging only, print messange INFO */
void info(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fputs("INFO: ", stderr);
	err_doit(0, fmt, ap);
	va_end(ap);
}

/* logging only, print messange WARNING */
void warn(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fputs("WARN: ", stderr);
	err_doit(0, fmt, ap);
	va_end(ap);
}

/* FATAL error, print error and stop immediately */
void fatal(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("FATAL: ", stderr);
	err_doit(0, fmt, ap);
	va_end(ap);
	abort();
}

/* FATAL error, print error and error description, and stop immediately */
void fatal_n(int err, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("FATAL: ", stderr);
	err_doit(err, fmt, ap);
	va_end(ap);
	abort();
}

void err_doit(int err, const char *fmt, va_list ap)
{
	vfprintf(stderr, fmt, ap);
	if (err)
		fprintf(stderr, ": %s\n", strerror(err));
	else
		fprintf(stderr, "\n");
	fflush(stderr);
	return;
}

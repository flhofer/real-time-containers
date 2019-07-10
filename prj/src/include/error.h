#ifndef __ERROR_H
#define __ERROR_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#if DEBUG
	extern FILE * dbg_out; // debug output file, defined in main
	#define printDbg(...) (void)fprintf (dbg_out, __VA_ARGS__)
#else
	#define printDbg(...) //
#endif

// general log information
void debug(char *fmt, ...);
void cont(char *fmt, ...);
void info(char *fmt, ...);
void warn(char *fmt, ...);
// error only printing
void err_msg(char *fmt, ...);
void err_msg_n(int err, char *fmt, ...);
// normal exit on error
void err_quit(char *fmt, ...) __attribute__((noreturn));
void err_exit(int err, char *fmt, ...) __attribute__((noreturn));
// fatal errors, immediate exit
void fatal(char *fmt, ...) __attribute__((noreturn));
void fatal_n(int err, char *fmt, ...) __attribute__((noreturn));
// interal error print function
void err_doit(int err, const char *fmt, va_list ap);

// general default
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

#endif	/* __ERROR_H */

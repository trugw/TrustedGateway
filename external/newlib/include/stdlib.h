/*
 * stdlib.h
 *
 * Definitions for common types, variables, and functions.
 */

#ifndef _NEWLIB_STDLIB_H_
#define _NEWLIB_STDLIB_H_

#define __need_size_t
#define __need_wchar_t
#define __need_NULL
#include <stddef.h>

#include <sys/cdefs.h>

#ifndef _BEGIN_STD_C
#define _BEGIN_STD_C
#endif
#ifndef _END_STD_C
#define _END_STD_C
#endif

_BEGIN_STD_C
int	atoi (const char *__nptr);
long	strtol (const char *__restrict __n, char **__restrict __end_PTR, int __base);
_END_STD_C

#endif /* _NEWLIB_STDLIB_H_ */

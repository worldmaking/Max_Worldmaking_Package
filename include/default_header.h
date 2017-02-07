#ifndef INCLUDE_DEFAULT_HEADER
#define INCLUDE_DEFAULT_HEADER 1

#include <stddef.h>
#include <stdarg.h>
#include <float.h>
#include <stdint.h>
#include <limits.h>

extern "C" {

	void object_post(void *, char *, ...);
	void object_warn(void *, char *, ...);
	void object_error(void *, char *, ...);

}

#endif

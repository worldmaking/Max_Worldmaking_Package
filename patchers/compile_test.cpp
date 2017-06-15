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

typedef int (*testfun_t)(int);

extern "C" int test(int x) {
	object_post(NULL, "hi from jit %d\n", x);
	return x*2;
}


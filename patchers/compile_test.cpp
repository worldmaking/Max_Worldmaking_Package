#include <stddef.h>
#include <stdarg.h>
#include <float.h>
#include <stdint.h>
#include <limits.h>

// I can't get these C++ includes to play nice.
// Looks like I'll need to shell, rather than using Clang JIT
//#include <string>
//#include <iostream>
//#define GLM_FORCE_RADIANS
//#include "glm/glm.hpp"

#include <GLFW/glfw3.h>

//#include <pcl-1.8/pcl/io/pcd_io.h>
//#include <pcl-1.8/pcl/point_types.h>

extern "C" {

#ifdef C74_X64
	typedef double 		t_atom_float;
	typedef long long 	t_atom_long;
#else 
	typedef float 		t_atom_float;
	typedef long 		t_atom_long;
#endif 


struct t_object;

struct t_symbol {
	char *s_name;
	t_object *s_thing;
};

struct t_atom {
	short a_type;
	union {
		t_atom_long w_long;
		t_atom_float w_float;
		t_symbol *w_sym;
		t_object *w_obj;
	} a_w;
};

t_symbol *gensym(const char *s);

void object_post(void *, char *, ...);
void object_warn(void *, char *, ...);
void object_error(void *, char *, ...);

int compile_dlopen(t_object * x, t_symbol * libname);

}

typedef int (*testfun_t)(int);

extern "C" int init(t_object * host) {
	object_post(NULL, "init");
	
	int ok = GLFW_TRUE == glfwInit();
	if (!ok) return -1;
	object_post(NULL, "glfw inited");
	object_post(NULL, "glfw %s", glfwGetVersionString());
	return 0;
}

extern "C" void anything(t_object *x, t_symbol *s, long argc, t_atom *argv) {
	object_post(x, "%s(#%d)\n", s->s_name, argc);
	
	int ok = glfwInit();
	object_post(NULL, "glfw inited");
	object_post(NULL, "glfw %s", glfwGetVersionString());
}

extern "C" int test(int x) {
	object_post(NULL, "hi from jit %d\n", x);
	return x*2;
}


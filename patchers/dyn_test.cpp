#if defined(__x86_64__) || defined(_M_X64)
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

extern "C" {

	t_symbol *gensym(const char *s);

	extern void object_post(void *, const char *, ...);
	void object_warn(void *, const char *, ...);
	void object_error(void *, const char *, ...);

}

#include "al_math.h"

struct Foo {
	
	Foo() {
		object_post(0, "ctor");
	}
	~Foo() {
		object_post(0, "dtor");
	}
};

// yes, ctor/dtor work
Foo foo;

extern "C" {

	__declspec(dllexport) int init(t_object * host) {
		object_post(host, "init");
		
		return 0;	
	}

	__declspec(dllexport) int test(int x) {
		return (glm::linearRand(0, x-1));
	}

	__declspec(dllexport) void anything(t_object * x, t_symbol * s, long argc, t_atom * argv) {
		object_post(x, "%s(#%d)\n", s->s_name, argc);
	}
}


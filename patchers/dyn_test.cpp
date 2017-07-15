#include "al_max.h"
#include "al_math.h"


struct Instance {
	t_object * host = 0;
	void * out1 = 0;
	
	Instance(t_object * host) : host(host) {
		out1 = dynamic_outlet(host, gensym("out1"));
	
		object_post(host, "created dynamic instance %p", this);
	}
	
	~Instance() {
		object_post(host, "destroying dynamic instance %p", this);
	}
	
	void bang() {
		t_atom a[1];
		atom_setsym(a, gensym("hello"));
		outlet_anything(out1, gensym("boo"), 1, a);
	}
};

extern "C" {

	C74_EXPORT void * init(t_object * host) {
		return new Instance(host);
	}
	
	C74_EXPORT void quit(t_object * host, Instance * I) {
		if (I) delete I;
	}

	C74_EXPORT t_atom_long test(Instance * I, t_atom_long i) {
		return i*2;
	}

	C74_EXPORT void anything(Instance * I, t_symbol * s, long argc, t_atom * argv) {
		static t_symbol * ps_bang = gensym("bang");
		
		if (s == ps_bang) {
			I->bang();
		} else {
			object_post(I->host, "%s(#%d)", s->s_name, argc);
		}
	}
}


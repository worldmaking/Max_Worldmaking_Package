#include "al_max.h"
#include "al_math.h"

struct Instance {
	t_object * host = 0;
	
	Instance(t_object * host) : host(host) {
		object_post(host, "created dynamic instance %p", this);
	}
	
	~Instance() {
		object_post(host, "destroying dynamic instance %p", this);
	}
};

extern "C" {

	C74_EXPORT void * init(t_object * host) {
		return new Instance(host);
	}
	
	C74_EXPORT void quit(t_object * host, Instance * I) {
		delete I;
	}

	C74_EXPORT t_atom_long test(Instance * I, t_atom_long x) {
		return glm::linearRand((int)0, (int)x-1);
	}

	C74_EXPORT void anything(t_object * x, t_symbol * s, long argc, t_atom * argv) {
		object_post(x, "%s(#%d)\n", s->s_name, argc);
	}
}


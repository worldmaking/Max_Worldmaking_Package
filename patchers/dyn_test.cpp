#include <stdio.h>
#include "al_max.h"
#include "al_math.h"

#ifndef WIN_VERSION
#include <Carbon/Carbon.h> 
#endif 

// a struct to contain whatever dynamic content you want to host in [dyn]
struct Instance {
	// host is the [dyn] object that is hosting this dll/dylib
	t_object * host = 0;
	void * outlet1 = 0;

	int callcount=0;
	
	Instance(t_object * host) : host(host) {
		object_post(host, "created dynamic instance %p", this);
		// get a reference to an outlet in the host:
		outlet1 = dynamic_outlet(host, gensym("out1"));
	}
	
	~Instance() {
		object_post(host, "destroying dynamic instance %p", this);
	}

	void anything(t_symbol * s, long argc, t_atom * argv) {
		callcount++;
		//object_post(host, "%s (%d arguments), calls: %d\n", s->s_name, argc, callcount);

		// echo back to outlet:
		outlet_anything(outlet1, s, argc, argv);
	}
};

extern "C" {

	// called immediately after the dll/dylib is loaded
	// init must be defined, and must return a pointer to your dynamic object (the instance)
	// this pointer will be passed to all subsequent calls
	C74_EXPORT void * init(t_object * host) {
		return new Instance(host);
	}
	
	// called just before the dll/dylib is unloaded. 
	C74_EXPORT void quit(t_object * host, Instance * I) {
		delete I;
	}

	// this method must be defined
	// it is called whenever the host [dyn] receives a message
	C74_EXPORT void anything(Instance * I, t_symbol * s, long argc, t_atom * argv) {
		I->anything(s, argc, argv);
	}
}


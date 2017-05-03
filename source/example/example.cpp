#include "al_max.h"

#include <new> // for in-place constructor

static t_class * max_class = 0;

class Example {
public:
	t_object ob; // max objExamplet, must be first!
	
	Example() {
		
	}
	
	~Example() {
		
	}
};

void * example_new(t_symbol *s, long argc, t_atom *argv) {
	Example *x = NULL;
	if ((x = (Example *)object_alloc(max_class))) {
		
		x = new (x) Example();
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:
		
	}
	return (x);
}

void example_free(Example *x) {
	x->~Example();
}

void example_assist(Example *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "I am inlet %ld", a);
	}
	else {	// outlet
		sprintf(s, "I am outlet %ld", a);
	}
}

void ext_main(void *r)
{
	t_class *c;
	
	c = class_new("example", (method)example_new, (method)example_free, (long)sizeof(Example),
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)example_assist,			"assist",		A_CANT, 0);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

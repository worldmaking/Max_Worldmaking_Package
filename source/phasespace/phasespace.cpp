#include "al_max.h"

#include <new> // for in-place constructor

#include "owl.hpp"

static t_class * max_class = 0;

class phasespace {
public:
	t_object ob; 
	void * out1;

	t_symbol * address;
	t_atom_long connected = 0;

	OWL::Context owl; 
	OWL::Markers markers;
	OWL::Cameras cameras;
	
	phasespace() {
		address = gensym("localhost");
		out1 = outlet_new(&ob, 0);

	}
	
	~phasespace() {
		disconnect();
	}

	void connect() {
		if (connected) return;

		if (owl.open(address->s_name) <= 0 || owl.initialize() <= 0) {
			object_error(&ob, "couldn't connect to OWL");
			connected = 0;
			object_attr_touch(&ob, gensym("connected"));
		}
		else {
			\ 
			connected = 1;
			object_attr_touch(&ob, gensym("connected"));
			owl.streaming(1);
		}
	}

	void disconnect() {
		if (connected) {
			owl.done();
			owl.close();
			connected = 0;
			object_attr_touch(&ob, gensym("connected"));
		}
	}

	void getcameras() {
		post("getting cameras\n");
		if (owl.isOpen() && owl.property<int>("initialized")) {
			const OWL::Event *event = owl.nextEvent(0);
			while (event) {

				post("getting camera events\n");
				if (event->type_id() == OWL::Type::ERROR) {
					object_error(&ob, "OWL error: %s %s", event->name(), event->str().data());
					break;
				}
				else if (event->type_id() == OWL::Type::FRAME)
				{
					//cout << "time=" << event->time() << " " << event->type_name() << " " << event->name() << "=" << event->size<OWL::Event>() << ":" << endl;
					if (event->find("cameras", cameras) > 0)
					{
						//cout << " markers=" << markers.size() << ":" << endl;
						for (OWL::Cameras::iterator m = cameras.begin(); m != cameras.end(); m++) {

							/*
							uint32_t id; 
							uint32_t flags; 
							float    pose[7]; 
							float    cond;
							*/

							if (m->cond > 0) {

								t_atom a[8];
								atom_setlong(a + 0, m->id);
								atom_setfloat(a + 1, m->pose[0]); // pos x
								atom_setfloat(a + 2, m->pose[1]); // pos y
								atom_setfloat(a + 3, m->pose[2]); // pos z
								 
								atom_setfloat(a + 4, m->pose[4]); // quat x
								atom_setfloat(a + 5, m->pose[5]); // quat y
								atom_setfloat(a + 6, m->pose[6]); // quat z
								atom_setfloat(a + 7, m->pose[3]); // quat w
								outlet_anything(out1, gensym("camera"), 4, a);
							}
						}
					}
					else {

						post("couldn't find cameras\n");
					}
				}
				event = owl.nextEvent(0);
			}
		}
		else {
			disconnect();
		}
	}

	void bang() {
		if (owl.isOpen() && owl.property<int>("initialized")) {
			const OWL::Event *event = owl.nextEvent(0);
			while (event) {

				if (event->type_id() == OWL::Type::ERROR) {
					object_error(&ob, "OWL error: %s %s", event->name(), event->str().data());
					break;
				}
				else if (event->type_id() == OWL::Type::FRAME)
				{
					//cout << "time=" << event->time() << " " << event->type_name() << " " << event->name() << "=" << event->size<OWL::Event>() << ":" << endl;
					if (event->find("markers", markers) > 0)
					{
						//cout << " markers=" << markers.size() << ":" << endl;
						for (OWL::Markers::iterator m = markers.begin(); m != markers.end(); m++) {

							/*
							uint32_t id; uint32_t flags; 
							• 0x000F = Slot number 
							• 0x0010 = Predicted 
							• 0x0100 = 3D rejected int64_t  time; float    x, y, z; float    cond; 
							• Condition number of plane intersection matrix 
							• Low positive numbers are good condition values, negative numbers are not
							*/

							if (m->cond > 0) {
								
								t_atom a[4];
								atom_setlong(a + 0, m->id);
								atom_setfloat(a + 1, m->x);
								atom_setfloat(a + 2, m->y);
								atom_setfloat(a + 3, m->z);
								outlet_anything(out1, gensym("point"), 4, a);
							}
						}
					}
				}
				event = owl.nextEvent(0);
			}
		}
		else {
			disconnect();
		}
	}
};

void * phasespace_new(t_symbol *s, long argc, t_atom *argv) {
	phasespace *x = NULL;
	if ((x = (phasespace *)object_alloc(max_class))) {
		
		x = new (x) phasespace();
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:
		x->connect();
	}
	return (x);
}

void phasespace_free(phasespace *x) {
	x->~phasespace();
}

void phasespace_bang(phasespace * x) {
	x->bang();
}

void phasespace_getcameras(phasespace * x) {
	x->getcameras();
}

void phasespace_connect(phasespace * x) {
	x->connect();
}

void phasespace_disconnect(phasespace * x) {
	x->disconnect();
}

void phasespace_assist(phasespace *x, void *b, long m, long a, char *s)
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
	
	c = class_new("phasespace", (method)phasespace_new, (method)phasespace_free, (long)sizeof(phasespace),
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)phasespace_assist,			"assist",		A_CANT, 0);

	class_addmethod(c, (method)phasespace_bang, "bang", 0);
	class_addmethod(c, (method)phasespace_getcameras, "getcameras", 0);
	class_addmethod(c, (method)phasespace_connect, "connect", 0);
	class_addmethod(c, (method)phasespace_disconnect, "disconnect", 0);

	CLASS_ATTR_LONG(c, "connected", 0, phasespace, connected);
	CLASS_ATTR_SYM(c, "address", 0, phasespace, address);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

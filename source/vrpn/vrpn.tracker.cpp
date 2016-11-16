/*
 
 TODO: build libvrpn.a (including dependencies) for 32 and 64 bit fat library
 (ideally static...)
 
 */

#include "al_max.h"

#include "vrpn/vrpn_Connection.h"
#include "vrpn/vrpn_Tracker.h"

static t_class * max_class = 0;
static t_symbol * ps_quat;
static t_symbol * ps_position;
static t_symbol * ps_sensor;

class vrpn_tracker {
public:
	t_object ob; // max objvrpnt, must be first!
	void * outlet_msg;
	void * outlet_sensor;
	void * outlet_quat;
	void * outlet_position;
	
	// attrs:
	t_symbol * name;
	t_symbol * host;
	t_atom_long port;
	
	vrpn_Tracker_Remote * tkr;
	vrpn_TRACKERCB data;
	char name_str[4096];
	
	static void tracker_handler(void * u, const vrpn_TRACKERCB d) {
		vrpn_tracker * self = (vrpn_tracker *)u;
		memcpy(&self->data, &d, sizeof(vrpn_TRACKERCB));
	}
	
	vrpn_tracker() {
		outlet_msg = outlet_new(&ob, NULL);
		outlet_sensor = outlet_new(&ob, NULL);
		outlet_quat = outlet_new(&ob, "quat");
		outlet_position = outlet_new(&ob, "position");
		
		name = gensym("RigidBody1");
		port = 3883;
		host = gensym("127.0.0.1");
		
		tkr = 0;
	}
	
	void init() {
		
		snprintf(name_str, 4095, "%s@%s:%d", name->s_name, host->s_name, (int)port);
		
		tkr = new vrpn_Tracker_Remote(name_str);
		tkr->register_change_handler((void *)this, (vrpn_TRACKERWORKSPACECHANGEHANDLER)&tracker_handler);
		
		object_post(&ob, "created vrpn tracker %s", name_str);

	}
	
	void clear() {
		if (tkr) {
			delete tkr;
			tkr = 0;
		}
	}
	
	~vrpn_tracker() {
		if (tkr) delete tkr;
	}
	
	void bang() {
		if (!tkr) {
			init();
			if (!tkr) return;
		}
		
		t_atom list[4];
		tkr->mainloop();
		
		outlet_int(outlet_sensor, data.sensor);
		
		atom_setfloat(list+0, data.quat[0]);
		atom_setfloat(list+1, data.quat[1]);
		atom_setfloat(list+2, data.quat[2]);
		atom_setfloat(list+3, data.quat[3]);
		outlet_list(outlet_quat, NULL, 4, list);
		
		atom_setfloat(list+0, data.pos[0]);
		atom_setfloat(list+1, data.pos[1]);
		atom_setfloat(list+2, data.pos[2]);
		outlet_list(outlet_position, NULL, 3, list);
		
	}
};

void * vrpn_new(t_symbol *s, long argc, t_atom *argv) {
	vrpn_tracker *x = NULL;
	if ((x = (vrpn_tracker *)object_alloc(max_class))) {
		
		x = new (x) vrpn_tracker();
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:
		x->init();
		
	}
	return (x);
}

void vrpn_free(vrpn_tracker *x) {
	x->~vrpn_tracker();
}

void vrpn_bang(vrpn_tracker* x) { x->bang(); }
void vrpn_init(vrpn_tracker* x) { x->init(); }
void vrpn_clear(vrpn_tracker* x) { x->clear(); }

t_max_err vrpn_host_set(vrpn_tracker *x, t_object *attr, long argc, t_atom *argv) {
	x->host = atom_getsym(argv);
	x->clear();
	return 0;
}

t_max_err vrpn_name_set(vrpn_tracker *x, t_object *attr, long argc, t_atom *argv) {
	x->name = atom_getsym(argv);
	x->clear();
	return 0;
}

void vrpn_assist(vrpn_tracker *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "messages in");
	}
	else {
		switch(a) {
			case 0: sprintf(s, "position"); break;
			case 1: sprintf(s, "quat"); break;
			case 2: sprintf(s, "sensor ID"); break;
			default:sprintf(s, "messages"); break;
		}
	}
}


void ext_main(void *r)
{
	t_class *c;
	ps_quat = gensym("quat");
	ps_position = gensym("position");
	ps_sensor = gensym("sensor");

	c = class_new("vrpn.tracker", (method)vrpn_new, (method)vrpn_free, (long)sizeof(vrpn_tracker),
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)vrpn_assist,			"assist",		A_CANT, 0);
	
	class_addmethod(c, (method)vrpn_bang,			"bang",		0);
	class_addmethod(c, (method)vrpn_clear,			"clear",		0);
	class_addmethod(c, (method)vrpn_init,			"init",		0);
	
	CLASS_ATTR_LONG(c, "port", 0, vrpn_tracker, port);
	CLASS_ATTR_SYM(c, "host", 0, vrpn_tracker, host);
	CLASS_ATTR_ACCESSORS(c, "host", NULL, vrpn_host_set);
	CLASS_ATTR_SYM(c, "name", 0, vrpn_tracker, name);
	CLASS_ATTR_ACCESSORS(c, "name", NULL, vrpn_name_set);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

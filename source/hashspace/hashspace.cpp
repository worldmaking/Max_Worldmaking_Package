
#include "al_max.h"
#include "al_hashspace.h"

static t_class * max_class = 0;

class max_hashspace {
public:
	t_object ob; // max objvrpnt, must be first!
	void * outlet_msg;
	void * outlet_results;
	
	// attrs:
	glm::vec3 world_min = glm::vec3(0.f);
	glm::vec3 world_max = glm::vec3(1.f);
	
	// internal:
	Hashspace3D<> * space;
	
	max_hashspace() {
		outlet_msg = outlet_new(&ob, NULL);
		outlet_results = outlet_new(&ob, "list");
		
	}
	
	void init() {
		space = new Hashspace3D<>;
		clear();
	}
	
	void clear() {
		space->reset(world_min, world_max);
	}
	
	// accepts type=float32, planecount=3, dim=1 or 2
	void jit_matrix(void * in_mat) {
		t_jit_matrix_info in_info;
		char * in_bp;
		// lock it:
		long in_savelock = (long)jit_object_method(in_mat, _jit_sym_lock, 1);
		
		// ensure data exists:
		jit_object_method(in_mat, _jit_sym_getdata, &in_bp);
		if (!in_bp) {
			jit_error_code(&ob, JIT_ERR_INVALID_INPUT);
			return;
		}
		
		// ensure the type is correct:
		jit_object_method(in_mat, _jit_sym_getinfo, &in_info);
		if (in_info.type != _jit_sym_float32) {
			jit_error_code(&ob, JIT_ERR_MISMATCH_TYPE);
			return;
		}
		else if (in_info.planecount != 3) {
			jit_error_code(&ob, JIT_ERR_MISMATCH_PLANE);
			return;
		}
		else if (in_info.dimcount > 2) {
			jit_error_code(&ob, JIT_ERR_MISMATCH_DIM);
			return;
		}
		
		int rows = in_info.dim[0];
		// treat 1D matrices as 1xN 2D matrix, so we can use the same loop code:
		int cols = in_info.dimcount > 1 ? in_info.dim[1] : 1;
		
		for (int i = 0, y = 0; y<cols; y++) {
			glm::vec3 * cell = (glm::vec3 *)(in_bp + y*in_info.dimstride[1]);
			for (int x = 0; x<rows; x++, i++) {
				space->move(i, *cell++);
			}
		}
		
		// restore matrix lock state:
		jit_object_method(in_mat, _jit_sym_lock, in_savelock);
	}
	
	// args: x, y, z (position to query)
	// r radius
	// id (of query object, or -1 for no id)
	// maxresults to return
	void query(t_atom_long argc, t_atom * argv) {
		
		//post("pos %f %f %f", atom_getfloat(&argv[0]), atom_getfloat(&argv[1]), atom_getfloat(&argv[2]));
		
		
		glm::vec3 center = glm::vec3(atom_getfloat(&argv[0]), atom_getfloat(&argv[1]), atom_getfloat(&argv[2]));
		float radius = atom_getfloat(&argv[3]);
		int32_t id = atom_gettype(&argv[4]) == A_LONG ? atom_getlong(&argv[4]) : -1;
		int32_t maxresults = atom_gettype(&argv[5]) == A_LONG ? atom_getlong(&argv[5]) : 32;
		
		std::vector<int32_t> results;
		int nres = space->query(results, maxresults, center, id, radius, 0.f);
		
		t_atom list[nres];
		for (int i=0; i<nres; i++) {
			// note: query results will never include self.
			atom_setlong(&list[i], results[i]);
		}
		outlet_list(outlet_results, 0L, nres, list);
		
		t_atom a[1];
		atom_setlong(a, nres);
		outlet_anything(outlet_msg, gensym("found"), 1, a);
	}
	
	~max_hashspace() {
		delete space;
	}
};

void * hashspace_new(t_symbol *s, long argc, t_atom *argv) {
	max_hashspace *self = NULL;
	if ((self = (max_hashspace *)object_alloc(max_class))) {
		
		self = new (self) max_hashspace();
		
		// apply attrs:
		attr_args_process(self, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:
		self->init();
		
	}
	return self;
}

void hashspace_free(max_hashspace *self) {
	self->~max_hashspace();
}

void hashspace_clear(max_hashspace* self) { self->clear(); }

void hashspace_move(max_hashspace* self, t_atom_long id, float x, float y, float z) {
	self->space->move(id, glm::vec3(x, y, z));
}

void hashspace_move_jit_matrix(max_hashspace* self, t_symbol * name) {
	void * in_mat = jit_object_findregistered(name);
	if (!in_mat) {
		jit_error_code(self, JIT_ERR_INVALID_INPUT);
		return;
	}
	self->jit_matrix(in_mat);
}

void hashspace_remove(max_hashspace* self, t_atom_long id) {
	self->space->remove(id);
}

void hashspace_query(max_hashspace* self, t_symbol * name, t_atom_long argc, t_atom * argv) {
	//post("%p query %s %d", self, name->s_name, argc);
	self->query(argc, argv);
}

void hashspace_assist(max_hashspace *self, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "messages in");
	}
	else {
		switch(a) {
			case 0: sprintf(s, "query results"); break;
			default:sprintf(s, "messages"); break;
		}
	}
}


extern "C" void ext_main(void *r)
{
	t_class *c;

	c = class_new("hashspace", (method)hashspace_new, (method)hashspace_free, (long)sizeof(max_hashspace), 0L, A_GIMME, 0);
	
	class_addmethod(c, (method)hashspace_assist, "assist", A_CANT, 0);
	
	class_addmethod(c, (method)hashspace_clear,	"clear", 0);
	class_addmethod(c, (method)hashspace_move, "move", A_LONG, A_FLOAT, A_FLOAT, A_FLOAT, 0);
	class_addmethod(c, (method)hashspace_remove, "remove", A_LONG, 0);
	class_addmethod(c, (method)hashspace_query, "query", A_GIMME, 0);
	class_addmethod(c, (method)hashspace_move_jit_matrix, "jit_matrix", A_SYM, 0);
	
	//CLASS_ATTR_LONG(c, "port", 0, max_hashspace, port);
	//CLASS_ATTR_SYM(c, "host", 0, max_hashspace, host);
	//CLASS_ATTR_ACCESSORS(c, "host", NULL, hashspace_host_set);
	//CLASS_ATTR_SYM(c, "name", 0, max_hashspace, name);
	//CLASS_ATTR_ACCESSORS(c, "name", NULL, hashspace_name_set);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

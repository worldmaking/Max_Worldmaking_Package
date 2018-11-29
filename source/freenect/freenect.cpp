// a bunch of likely Max includes:
#include "al_max.h"

// used in ext_main
// a dummy call just to get jitter initialized, otherwise _jit_sym_* symbols etc. won't be populated
// (normally this isn't necessary because jit_class_new does it, but I"m not using jit_class_new)
void initialize_jitlib() {
	t_jit_matrix_info info;
	jit_matrix_info_default(&info);
	info.type = gensym("char");
	object_free(jit_object_new(gensym("jit_matrix"), &info));
}

static t_class * max_class = 0;


#ifdef __GNUC__
#include <stdint.h>
#else
#include "stdint.h"
#endif

#include <vector>
#include <algorithm>
#include <signal.h>
#include <thread>

#include "libfreenect.h"

#define KINECT_DEPTH_WIDTH 640
#define KINECT_DEPTH_HEIGHT 480

freenect_context * f_ctx = NULL;

class freenect {
public:
	t_object ob;
	void *		outlet_msg;
	
	// freenect:
	freenect_device  *device;
	
	freenect() {
		
		outlet_msg = outlet_new(&ob, 0);
	}
	
	~freenect() {
		close();
	}
	
	void open(t_symbol *s, long argc, t_atom * argv) {
		
		t_atom a[1];
		
	}
	
	void close() {
		
	}
	
	void bang() {
		
	}
	
	static void log_cb(freenect_context *dev, freenect_loglevel level, const char *msg) {
		object_post(NULL, msg);
	}
	
//	static void *capture_threadfunc(void *arg) {
//		freenect *x = (freenect *)arg;
//
//		capturing = 1;
//		object_post(NULL, "freenect starting processing");
//		while (capturing > 0) {
//			int err = freenect_process_events(f_ctx);
//			//int err = freenect_process_events_timeout(f_ctx);
//			if(err < 0){
//				object_error(NULL, "Freenect could not process events.");
//				break;
//			}
//			systhread_sleep(0);
//		}
//		object_post(NULL, "freenect finished processing");
//
//	out:
//		systhread_exit(NULL);
//		return NULL;
//	}
};


void freenect_open(freenect * x, t_symbol *s, long argc, t_atom * argv) {
	x->open(s, argc, argv);
}
void freenect_bang(freenect * x) { x->bang(); }
void freenect_close(freenect * x) { x->close(); }

void * freenect_new(t_symbol *s, long argc, t_atom *argv) {
	freenect *x = NULL;
	if ((x = (freenect *)object_alloc(max_class))) {
		
		x = new (x)freenect();
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:
		
	}
	return (x);
}

void freenect_free(freenect *x) {
	x->~freenect();
}

void freenect_assist(freenect *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "I am inlet %ld", a);
	}
	else {	// outlet
		switch (a) {
//			case 0: sprintf(s, "colour (jit_matrix)"); break;
//			case 1: sprintf(s, "3D cloud at colour dim (jit_matrix)"); break;
//			case 2: sprintf(s, "depth (jit_matrix)"); break;
//			case 3: sprintf(s, "player at depth dim (jit_matrix)"); break;
//			case 4: sprintf(s, "matrices for jit.gl.mesh (jit_matrix)"); break;
//			case 5: sprintf(s, "skeleton"); break;
			default: sprintf(s, "other messages"); break;
		}
	}
}

static void freenect_logger(freenect_context *dev, freenect_loglevel level, const char *msg) {
	object_post(NULL, msg);
}

void freenect_quit() {
	if (f_ctx) {
		freenect_shutdown(f_ctx);
		f_ctx = NULL;
	}
}

void freenect_init() {
	// create the freenect context:
	freenect_context *context = 0;
	if(!f_ctx){
		if (freenect_init(&context, NULL) < 0) {
			object_error(NULL, "freenect_init() failed");
			return;
		}
	}
	f_ctx = context;
	
	freenect_set_log_callback(f_ctx, freenect_logger);
	//		FREENECT_LOG_FATAL = 0,     /**< Log for crashing/non-recoverable errors */
	//		FREENECT_LOG_ERROR,         /**< Log for major errors */
	//		FREENECT_LOG_WARNING,       /**< Log for warning messages */
	//		FREENECT_LOG_NOTICE,        /**< Log for important messages */
	//		FREENECT_LOG_INFO,          /**< Log for normal messages */
	//		FREENECT_LOG_DEBUG,         /**< Log for useful development messages */
	//		FREENECT_LOG_SPEW,          /**< Log for slightly less useful messages */
	//		FREENECT_LOG_FLOOD,         /**< Log EVERYTHING. May slow performance. */
	freenect_set_log_level(f_ctx, FREENECT_LOG_WARNING);
	
	quittask_install((method)freenect_quit, NULL);
}

void ext_main(void *r)
{
//	initialize_jitlib();
//	common_symbols_init();
//	
	freenect_init();
	
//	ps_vertex_matrix = gensym("vertex_matrix");
//	ps_normal_matrix = gensym("normal_matrix");
//	ps_texcoord_matrix = gensym("texcoord_matrix");
//	ps_index_matrix = gensym("index_matrix");

	t_class *c;
	
	c = class_new("freenect", (method)freenect_new, (method)freenect_free, (long)sizeof(freenect), 0L, A_GIMME, 0);
	class_addmethod(c, (method)freenect_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)freenect_open, "open", A_GIMME, 0);
	class_addmethod(c, (method)freenect_bang, "bang", 0);
	class_addmethod(c, (method)freenect_close, "close", 0);
	
//	CLASS_ATTR_LONG(c, "unique", 0, freenect, unique);
//	CLASS_ATTR_STYLE(c, "unique", 0, "onoff");
//	CLASS_ATTR_LONG(c, "use_depth", 0, freenect, use_depth);
//	CLASS_ATTR_STYLE(c, "use_depth", 0, "onoff");
//	CLASS_ATTR_LONG(c, "use_colour", 0, freenect, use_colour);
//	CLASS_ATTR_STYLE(c, "use_colour", 0, "onoff");
//	CLASS_ATTR_LONG(c, "use_colour_cloud", 0, freenect, use_colour_cloud);
//	CLASS_ATTR_STYLE(c, "use_colour_cloud", 0, "onoff");
//	CLASS_ATTR_LONG(c, "face_negative_z", 0, freenect, face_negative_z);
//	CLASS_ATTR_STYLE(c, "face_negative_z", 0, "onoff");
//	CLASS_ATTR_FLOAT(c, "triangle_limit", 0, freenect, triangle_limit);
//	CLASS_ATTR_LONG(c, "stitch", 0, freenect, stitch);
	
	class_register(CLASS_BOX, c);
	max_class = c;
	
}

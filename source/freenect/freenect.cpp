// a bunch of likely Max includes:
#include "al_max.h"
#include "al_math.h"

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

t_symbol * resource_path = 0;


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
#include "libfreenect_registration.h"

#define KINECT_DEPTH_WIDTH 640
#define KINECT_DEPTH_HEIGHT 480

freenect_context * f_ctx = NULL;
t_systhread capture_thread;
int capturing = 0;

static void freenect_logger(freenect_context *dev, freenect_loglevel level, const char *msg) {
	object_post(NULL, msg);
}

void freenect_quit() {
	if (f_ctx) {
		freenect_shutdown(f_ctx);
		f_ctx = NULL;
	}
}


class freenect {
public:
	t_object ob;
	void *		outlet_msg;
	void *		outlet_cloud;
	void *		outlet_rgb_cloud;
	void *		outlet_depth;
	void *		outlet_rgb;
	
	// attrs:
    t_atom_long use_color = 1;
	t_atom_long clearnulls = 1;
	t_atom_long unique = 1;
    t_atom_long map_color_to_depth = 1;
    t_atom_long debug_level = 1;
	
	// generic:
	volatile char new_rgb_data;
	volatile char new_depth_data;
	volatile char new_cloud_data;
	
	// depth matrix for raw output:
	void *		depth_mat;
	void *		depth_mat_wrapper;
	t_atom		depth_name[1];
	float *	depth_back;
	
	// rgb matrix for raw output:
	void *		rgb_mat;
	void *		rgb_mat_wrapper;
	t_atom		rgb_name[1];
	glm::i8vec3 * 	rgb_back;
	
	// rgb matrix for cloud output:
	void *		rgb_cloud_mat;
	void *		rgb_cloud_mat_wrapper;
	t_atom		rgb_cloud_name[1];
	glm::i8vec3 *	rgb_cloud_back;
	
	// cloud matrix for output:
	void *		cloud_mat;
	void *		cloud_mat_wrapper;
	t_atom		cloud_name[1];
	glm::vec3 *		cloud_back;
	
//	float		depth_base, depth_offset;
//	glm::vec2	depth_focal;
//	glm::vec2	depth_center;
//	glm::vec2	rgb_focal;
//	glm::vec2	rgb_center;
	
	// freenect:
	freenect_device  *device;
	// internal data:
	uint16_t *	depth_data;
	
	
	
	freenect() {
		
		device = 0;
		// depth buffer doesn't use a jit_matrix, because uint16_t is not a Jitter type:
		depth_data = (uint16_t *)sysmem_newptr(KINECT_DEPTH_WIDTH*KINECT_DEPTH_HEIGHT * sizeof(uint16_t));
		
		new_rgb_data = new_depth_data = new_cloud_data = 0;
		
//		// attributes:
//		depth_base = 0.085f;
//		depth_offset = 0.0011f;
//		depth_center.x = 314.f;
//		depth_center.y = 241.f;
//		depth_focal.x = 597.f;
//		depth_focal.y = 597.f;
//		rgb_center.x = 320.f;
//		rgb_center.y = 240.f;
//		rgb_focal.x = 524.f;
//		rgb_focal.y = 524.f;
		
		t_jit_matrix_info info;
		
		depth_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
		depth_mat = jit_object_method(depth_mat_wrapper, _jit_sym_getmatrix);
		// create the internal data:
		jit_matrix_info_default(&info);
		info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
		info.planecount = 1;
		info.type = gensym("float32");
		info.dimcount = 2;
		info.dim[0] = KINECT_DEPTH_WIDTH;
		info.dim[1] = KINECT_DEPTH_HEIGHT;
		jit_object_method(depth_mat, _jit_sym_setinfo_ex, &info);
		jit_object_method(depth_mat, _jit_sym_clear);
		jit_object_method(depth_mat, _jit_sym_getdata, &depth_back);
		// cache name:
		atom_setsym(depth_name, jit_attr_getsym(depth_mat_wrapper, _jit_sym_name));
		
		rgb_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
		rgb_mat = jit_object_method(rgb_mat_wrapper, _jit_sym_getmatrix);
		// create the internal data:
		jit_matrix_info_default(&info);
		info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
		info.planecount = 3;
		info.type = gensym("char");
		info.dimcount = 2;
		info.dim[0] = KINECT_DEPTH_WIDTH;
		info.dim[1] = KINECT_DEPTH_HEIGHT;
		jit_object_method(rgb_mat, _jit_sym_setinfo_ex, &info);
		jit_object_method(rgb_mat, _jit_sym_clear);
		jit_object_method(rgb_mat, _jit_sym_getdata, &rgb_back);
		// cache name:
		atom_setsym(rgb_name, jit_attr_getsym(rgb_mat_wrapper, _jit_sym_name));
		
		rgb_cloud_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
		rgb_cloud_mat = jit_object_method(rgb_cloud_mat_wrapper, _jit_sym_getmatrix);
		// create the internal data:
		jit_matrix_info_default(&info);
		info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
		info.planecount = 3;
		info.type = gensym("char");
		info.dimcount = 2;
		info.dim[0] = KINECT_DEPTH_WIDTH;
		info.dim[1] = KINECT_DEPTH_HEIGHT;
		jit_object_method(rgb_cloud_mat, _jit_sym_setinfo_ex, &info);
		jit_object_method(rgb_cloud_mat, _jit_sym_clear);
		jit_object_method(rgb_cloud_mat, _jit_sym_getdata, &rgb_cloud_back);
		// cache name:
		atom_setsym(rgb_cloud_name, jit_attr_getsym(rgb_cloud_mat_wrapper, _jit_sym_name));
		
		cloud_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
		cloud_mat = jit_object_method(cloud_mat_wrapper, _jit_sym_getmatrix);
		// create the internal data:
		jit_matrix_info_default(&info);
		info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
		info.planecount = 3;
		info.type = gensym("float32");
		info.dimcount = 2;
		info.dim[0] = KINECT_DEPTH_WIDTH;
		info.dim[1] = KINECT_DEPTH_HEIGHT;
		jit_object_method(cloud_mat, _jit_sym_setinfo_ex, &info);
		jit_object_method(cloud_mat, _jit_sym_clear);
		jit_object_method(cloud_mat, _jit_sym_getdata, &cloud_back);
		// cache name:
		atom_setsym(cloud_name, jit_attr_getsym(cloud_mat_wrapper, _jit_sym_name));
		
		outlet_msg = outlet_new(&ob, 0);
        outlet_rgb_cloud = outlet_new(&ob, 0);
		outlet_cloud = outlet_new(&ob, 0);
        outlet_rgb = outlet_new(&ob, 0);
		outlet_depth = outlet_new(&ob, 0);
	}
	
	~freenect() {
		close();
		sysmem_freeptr(depth_data);
	}
    
    // TODO: a 'reopen()' message for when some attrs change
	
	void open(t_symbol *s, long argc, t_atom * argv) {
		if (device){
			object_post(&ob, "A device is already open.");
			return;
		}
		
		// mark one additional device:
		capturing++;
		if(!f_ctx){
			long priority = 0; // maybe increase?
			if (systhread_create((method)&capture_threadfunc, this, 0, priority, 0, &capture_thread)) {
				object_error(&ob, "Failed to create capture thread.");
				capturing = 0;
				return;
			}
			while(!f_ctx){
				systhread_sleep(0);
			}
		}
		
		getdevices();
		
		int ndevices = freenect_num_devices(f_ctx);
		if(!ndevices){
			object_post(&ob, "Could not find any connected kinect device. Are you sure the power cord is plugged-in?");
			capturing = 0;
			return;
		}
		
		if (argc > 0 && atom_gettype(argv) == A_SYM) {
			const char * serial = atom_getsym(argv)->s_name;
			object_post(&ob, "opening device %s", serial);
			if (freenect_open_device_by_camera_serial(f_ctx, &device, serial) < 0) {
				object_error(&ob, "failed to open device %s", serial);
				device = NULL;
			}
		} else {
			int devidx = 0;
			if (argc > 0 && atom_gettype(argv) == A_LONG) devidx = atom_getlong(argv);
			
			object_post(&ob, "opening device %d", devidx);
			if (freenect_open_device(f_ctx, &device, devidx) < 0) {
				object_error(&ob, "failed to open device %d", devidx);
				device = NULL;
			}
		}
		
		if (!device) {
			// failed to create device:
			capturing--;
			return;
		}
        
        //        FREENECT_LOG_FATAL = 0,     /**< Log for crashing/non-recoverable errors */
        //        FREENECT_LOG_ERROR,         /**< Log for major errors */
        //        FREENECT_LOG_WARNING,       /**< Log for warning messages */
        //        FREENECT_LOG_NOTICE,        /**< Log for important messages */
        //        FREENECT_LOG_INFO,          /**< Log for normal messages */
        //        FREENECT_LOG_DEBUG,         /**< Log for useful development messages */
        //        FREENECT_LOG_SPEW,          /**< Log for slightly less useful messages */
        //        FREENECT_LOG_FLOOD,         /**< Log EVERYTHING. May slow performance. */
        if (debug_level < 0) debug_level = 0;
        if (debug_level > 7) debug_level = 7;
        freenect_set_log_level(f_ctx, (freenect_loglevel)debug_level);
		
		freenect_set_user(device, this);
		freenect_set_depth_callback(device, depth_callback);
        freenect_set_depth_buffer(device, depth_data);
        if (map_color_to_depth) {
            freenect_set_depth_mode(device, freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_MM));
        } else {
            freenect_set_depth_mode(device, freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_REGISTERED));
        }
        
        if (use_color) {
            freenect_set_video_callback(device, rgb_callback);
            freenect_set_video_buffer(device, rgb_back);
            freenect_set_video_mode(device, freenect_find_video_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_VIDEO_RGB));
        }
        
        led(LED_OFF);
		freenect_start_depth(device);
		if (use_color) freenect_start_video(device);
	}
	
	void getdevices() {
		t_atom a[8];
		int i;
		struct freenect_device_attributes* attribute_list;
		struct freenect_device_attributes* attribute;
		
		// list devices:
		if (!f_ctx) return;
		
		int num_devices = freenect_list_device_attributes(f_ctx, &attribute_list);
		
		i = 0;
		attribute = attribute_list;
		while (attribute) {
			atom_setsym(a+i, gensym(attribute->camera_serial));
			attribute = attribute->next;
		}
		outlet_anything(outlet_msg, gensym("devices"), num_devices, a);
		
		freenect_free_device_attributes(attribute_list);
	}
	
	void close() {
		if(!device) return;
		
		led(LED_BLINK_GREEN);
        
		freenect_close_device(device);
		device = NULL;
		
		// mark one less device running:
		capturing--;
	}
	
	void accel() {
		t_atom a[3];
		double x, y, z;
		if (!device) return;
		
		freenect_update_tilt_state(device);
		freenect_raw_tilt_state * state = freenect_get_tilt_state(device);
		freenect_get_mks_accel(state, &x, &y, &z);
		atom_setfloat(a+0, x);
		atom_setfloat(a+1, y);
		atom_setfloat(a+2, z);
		outlet_anything(outlet_msg, gensym("accel"), 3, a);
	}
	
	void led(int option) {
		if (!device) return;
		
		//		LED_OFF              = 0, /**< Turn LED off */
		//		LED_GREEN            = 1, /**< Turn LED to Green */
		//		LED_RED              = 2, /**< Turn LED to Red */
		//		LED_YELLOW           = 3, /**< Turn LED to Yellow */
		//		LED_BLINK_GREEN      = 4, /**< Make LED blink Green */
		//		// 5 is same as 4, LED blink Green
		//		LED_BLINK_RED_YELLOW = 6, /**< Make LED blink Red/Yellow */
		freenect_set_led(device, (freenect_led_options)(option % 7));
	}
	
	static void rgb_callback(freenect_device *dev, void *pixels, uint32_t timestamp){
		freenect *x = (freenect *)freenect_get_user(dev);
		if(x) x->rgb_process();
	}
	
	static void depth_callback(freenect_device *dev, void *pixels, uint32_t timestamp){
		freenect *x = (freenect *)freenect_get_user(dev);
		if(x) x->depth_process();
	}
	
	
	void depth_process() {
		// for each cell:
		for (int i=0; i<KINECT_DEPTH_HEIGHT*KINECT_DEPTH_WIDTH; i++) {
			// cache raw, unrectified depth in output:
			// (casts uint16_t to uint32_t)
			depth_back[i] = (float)depth_data[i];
		}
		new_depth_data = 1;
	}
	
	void rgb_process() {
        if (!device) return;
		// helper function to map one FREENECT_VIDEO_RGB image to a FREENECT_DEPTH_MM
		// image (inverse mapping to FREENECT_DEPTH_REGISTERED, which is depth -> RGB)
		//FREENECTAPI void freenect_map_rgb_to_depth( freenect_device* dev, uint16_t* depth_mm, uint8_t* rgb_raw, uint8_t* rgb_registered );
        if (map_color_to_depth) {
            freenect_map_rgb_to_depth(device, depth_data, (uint8_t*)rgb_back, (uint8_t*)rgb_cloud_back);
        } else {
            // nothing to do
        }
		new_rgb_data = 1;
	}
	
	void cloud_process() {
        if (!device) return;
		int i = 0;
		for (int y=0; y<KINECT_DEPTH_HEIGHT; y++) {
			for (int x=0; x<KINECT_DEPTH_WIDTH; x++, i++) {
				glm::vec3& meters = cloud_back[i];
				int mmz = depth_back[i];
				if (mmz < 10000 && mmz > 300) {
					// convenience function to convert a single x-y coordinate pair from camera
					// to world coordinates
					//FREENECTAPI void freenect_camera_to_world(freenect_device* dev, int cx, int cy, int wz, double* wx, double* wy);
					double mmx, mmy;
					freenect_camera_to_world(device, x, y, mmz, &mmx, &mmy);
					meters.x = mmx * 0.001f;
					meters.y = mmy * -0.001f;
					meters.z = mmz * -0.001f;
				} else if (clearnulls) {
					meters.x = 0.f;
					meters.y = 0.f;
					meters.z = 0.f;
				}
			}
		}
	}
	
	void bang() {
        if (use_color) {
            if (new_rgb_data || !unique) {
                outlet_anything(outlet_rgb  , _jit_sym_jit_matrix, 1, rgb_name  );
                if (map_color_to_depth) {
                    outlet_anything(outlet_rgb_cloud  , _jit_sym_jit_matrix, 1, rgb_cloud_name  );
                } else {
                    outlet_anything(outlet_rgb_cloud  , _jit_sym_jit_matrix, 1, rgb_name  );
                }
                new_rgb_data = 0;
            }
        }
		if (new_depth_data || !unique) {
			outlet_anything(outlet_depth, _jit_sym_jit_matrix, 1, depth_name);
			cloud_process();
			outlet_anything(outlet_cloud, _jit_sym_jit_matrix, 1, cloud_name);
			new_depth_data = 0;
		}
	}
	
	static void *capture_threadfunc(void *arg) {
		//freenect *x = (freenect *)arg;
        setenv("LIBFREENECT_FIRMWARE_PATH", resource_path->s_name, 1);
//        char* envpath = getenv("LIBFREENECT_FIRMWARE_PATH");
//        post("env %s", envpath);
		
		// create the freenect context:
		freenect_context *context = 0;
		if(!f_ctx){
			if (freenect_init(&context, NULL) < 0) {
				object_error(NULL, "freenect_init() failed");
				goto out;
			}
		}
		f_ctx = context;
        
		
		freenect_set_log_callback(f_ctx, freenect_logger);
        freenect_set_log_level(f_ctx, FREENECT_LOG_ERROR);
		
		object_post(NULL, "freenect starting processing");
		while (capturing > 0) {
			int err = freenect_process_events(f_ctx);
			//int err = freenect_process_events_timeout(f_ctx);
			if(err < 0){
				object_error(NULL, "Freenect could not process events.");
				break;
			}
			systhread_sleep(0);
		}
		object_post(NULL, "freenect finished processing");
		
	out:
		freenect_quit();
		systhread_exit(NULL);
		return NULL;
	}
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

void freenect_accel(freenect *x) { x->accel(); }
void freenect_led(freenect *x, t_atom_long n) { x->led(n); }
void freenect_getdevices(freenect *x) { x->getdevices(); }

void freenect_assist(freenect *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "I am inlet %ld", a);
	}
	else {	// outlet
		switch (a) {
            case 0: sprintf(s, "depth in mm (jit_matrix)"); break;
			case 1: sprintf(s, "raw colour (jit_matrix)"); break;
                
            case 2: sprintf(s, "point cloud in meters (jit_matrix)"); break;
			case 3: sprintf(s, "colour for point cloud (jit_matrix)"); break;
//			case 4: sprintf(s, "matrices for jit.gl.mesh (jit_matrix)"); break;
//			case 5: sprintf(s, "skeleton"); break;
			default: sprintf(s, "other messages"); break;
		}
	}
}



void ext_main(void *r)
{
//	initialize_jitlib();
//	common_symbols_init();
//	
//	freenect_init();
	
	quittask_install((method)freenect_quit, NULL);
	
//	ps_vertex_matrix = gensym("vertex_matrix");
//	ps_normal_matrix = gensym("normal_matrix");
//	ps_texcoord_matrix = gensym("texcoord_matrix");
//	ps_index_matrix = gensym("index_matrix");
    
    
    if (resource_path == 0) {
        char filename[MAX_FILENAME_CHARS];
        char folderpath[MAX_FILENAME_CHARS];
        char systempath[MAX_FILENAME_CHARS];
        short outvol;
        t_fourcc outtype;
#ifdef WIN_VERSION
        strncpy_zero(filename, "freenect.mxe", MAX_FILENAME_CHARS);
#else
        strncpy_zero(filename, "freenect.mxo", MAX_FILENAME_CHARS);
#endif
        short result = locatefile_extended(filename, &outvol, &outtype, NULL, 0);
        if (result == 0
            && path_toabsolutesystempath(outvol, "../resources", folderpath) == 0
            && path_nameconform(folderpath, systempath, PATH_STYLE_SLASH, PATH_TYPE_BOOT) == 0) {
            resource_path = gensym(systempath);
        }
        else {
            object_error(0, "failed to locate resources");
            resource_path = gensym(".");
        }
    }

	t_class *c;
	
	c = class_new("freenect", (method)freenect_new, (method)freenect_free, (long)sizeof(freenect), 0L, A_GIMME, 0);
	class_addmethod(c, (method)freenect_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)freenect_open, "open", A_GIMME, 0);
	class_addmethod(c, (method)freenect_bang, "bang", 0);
	class_addmethod(c, (method)freenect_close, "close", 0);
	class_addmethod(c, (method)freenect_accel, "accel", 0);
	class_addmethod(c, (method)freenect_getdevices, "getdevices", 0);
	class_addmethod(c, (method)freenect_led, "led", A_LONG, 0);
	
	CLASS_ATTR_LONG(c, "unique", 0, freenect, unique);
	CLASS_ATTR_STYLE(c, "unique", 0, "onoff");
	CLASS_ATTR_LONG(c, "clearnulls", 0, freenect, clearnulls);
	CLASS_ATTR_STYLE(c, "clearnulls", 0, "onoff");
    CLASS_ATTR_LONG(c, "map_color_to_depth", 0, freenect, map_color_to_depth);
    CLASS_ATTR_STYLE(c, "map_color_to_depth", 0, "onoff");
    
//	CLASS_ATTR_LONG(c, "use_depth", 0, freenect, use_depth);
//	CLASS_ATTR_STYLE(c, "use_depth", 0, "onoff");
    
    
    CLASS_ATTR_LONG(c, "use_color", 0, freenect, use_color);
    CLASS_ATTR_STYLE(c, "use_color", 0, "onoff");
    
    
    CLASS_ATTR_LONG(c, "debug_level", 0, freenect, debug_level);
    
//	CLASS_ATTR_LONG(c, "face_negative_z", 0, freenect, face_negative_z);
//	CLASS_ATTR_STYLE(c, "face_negative_z", 0, "onoff");
//	CLASS_ATTR_FLOAT(c, "triangle_limit", 0, freenect, triangle_limit);
//	CLASS_ATTR_LONG(c, "stitch", 0, freenect, stitch);
	
	class_register(CLASS_BOX, c);
	max_class = c;
	
}

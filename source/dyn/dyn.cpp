/*
 
 // dynattr
 
 void *attr = object_attr_get(x, paramname);
 if (attr == 0) {
 // attr did not exist; create it:

 
 t_object *attrobj = attribute_new(paramname->s_name, sym_paramty, 0, 0, 0);
 err = object_addattr(x, attrobj);
 if (err == MAX_ERR_NONE) {
 
 err = object_attr_addattr_parse((t_object *)x, paramname->s_name, "dynamicattr", gensym("long"), 0, "1");
 
 err = object_attr_setvalueof(x, paramname, 1, argv);
 
 
 

 */


#include "al_max.h"

#include "ext.h"
#include "ext_path.h"
#include "ext_obex.h"

#include <string>
#ifdef WIN_VERSION
	// Windows
#else
	// OSX
	#include <dlfcn.h> // dlopen
	#define USE_UV 1
#endif

#ifdef USE_UV
#include "uv.h"
#endif

#define MULTILINE(...) #__VA_ARGS__

static t_class * max_class = 0;

static t_systhread;

class dyn {
public:
	
	typedef t_atom_long (*testfun_t)(void * instance, t_atom_long);
	typedef void * (*initfun_t)(void *);
	typedef void (*quitfun_t)(void *host, void * instance);
	typedef void (*gimmefun_t)(void * instance, t_symbol *, long, t_atom *);
	
	t_object ob;
	void * outlet_msg;
	void * outlet_out1;
	
	// attrs;
	t_symbol * file = _sym_nothing;
	t_atom_long autowatch = 1;
	
	void * filewatcher = 0;
#ifdef WIN_VERSION
	HMODULE lib_handle = 0;
#else
	void * lib_handle = 0;
#endif
	void * instance_handle = 0;
	testfun_t testfun = 0;
	initfun_t initfun = 0;
	quitfun_t quitfun = 0;
	gimmefun_t gimmefun = 0;
	
#ifdef USE_UV
	uv_loop_t uvloop;
#endif
	
	char systempath[MAX_FILENAME_CHARS];
	
	dyn() {
		outlet_msg = outlet_new(&ob, 0);
		
		// create at least one dynamic outlet:
		t_object * b;
		object_obex_lookup(&ob,gensym("#B"),(t_object **)&b);
		object_method(b, gensym("dynlet_begin"));
		outlet_out1 = outlet_append(&ob, 0, 0);
		object_method(b, gensym("dynlet_end"));
		object_obex_storeflags(&ob, gensym("out1"), (t_object *)outlet_out1, OBJ_FLAG_REF);
		
#ifdef USE_UV
		int err = uv_loop_init(&uvloop);
		if (err) object_error(&ob, "uv error %s", uv_strerror(err));
#endif
	}
	
	~dyn() {
		if (filewatcher) {
			filewatcher_stop(filewatcher);
			object_free((t_object *)filewatcher);
			filewatcher = 0;
		}
		
		unload();
#ifdef USE_UV
		uv_loop_close(&uvloop);
#endif
	}
	
#ifdef USE_UV
	// somehow need to pulse this
	// it should always be from the same thread
	// maybe a qelem
	void uvtick() {
		uv_run(&uvloop, UV_RUN_DEFAULT);
	}
#endif
	
	void unload() {
		
		if (lib_handle) {
			// call the dylib's close handler
			if (quitfun) {
				quitfun(this, instance_handle);
			}
			
#ifdef WIN_VERSION
			if (FreeLibrary(lib_handle) == 0) {
				char err[256];
				FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
							  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
				object_error(&ob, "%s", err);
				return;
			}
#else
			if (dlclose(lib_handle) != 0) {
				object_error(&ob, "%s", dlerror());
				return;
			}
			// check if it is already loaded?
			if (dlopen(systempath, RTLD_NOLOAD)) {
				object_warn(&ob, "library did not unload %s", dlerror());
				return;
			}
#endif
			lib_handle = 0;
		}
		testfun = 0;
		initfun = 0;
		quitfun = 0;
		gimmefun = 0;
		
		t_atom a[1];
		atom_setlong(a+0, 0);
		outlet_anything(outlet_msg, gensym("loaded"), 1, a);
	}
	
	void reload() {
		if (file) {
			
			if (filewatcher) {
				filewatcher_stop(filewatcher);
				object_free((t_object *)filewatcher);
				filewatcher = 0;
			}
			
			char libname[MAX_FILENAME_CHARS];
#ifdef WIN_VERSION

			
#ifdef C74_X64
			// Max 64-bit should look into a /x64 subfolder 
			snprintf_zero(libname, MAX_FILENAME_CHARS, "%s.x64.dll", file->s_name);
#else
			snprintf_zero(libname, MAX_FILENAME_CHARS, "%s.dll", file->s_name);
#endif

#else
			snprintf_zero(libname, MAX_FILENAME_CHARS, "%s.dylib", file->s_name);
#endif
			
			short vol;
			t_fourcc type;
			short res = locatefile_extended(libname, &vol, &type, NULL, 0);
			if (res != 0) {
				object_error(&ob, "couldn't find file %s", libname);
			} else {
			
				filewatcher = filewatcher_new(&ob,vol,libname);
				if (autowatch) filewatcher_start(filewatcher);
				
				char folderpath[MAX_FILENAME_CHARS];
				if (path_toabsolutesystempath(vol, libname, folderpath) == 0
					&& path_nameconform(folderpath, systempath, PATH_STYLE_SLASH, PATH_TYPE_BOOT) == 0) {
					
					object_post(&ob, "path %s", systempath);
					
					unload();
					
#ifdef WIN_VERSION
					lib_handle = LoadLibraryA(systempath);
#else
					lib_handle = dlopen(systempath, RTLD_NOW | RTLD_GLOBAL);
#endif
					if (!lib_handle) {
#ifdef WIN_VERSION
						int errcode = GetLastError();
						
						//char err[256];
						//FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errcode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
						//object_error(&ob, "failed to load: %s", err);

						LPVOID msg_buf;
						DWORD rv = FormatMessage(
							FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
							NULL,
							errcode,
							MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
							(LPSTR)&msg_buf,
							0,
							NULL);
						if (rv > 0) {
							object_error(&ob, "failed to load (%d): %s\n", errcode, msg_buf);
							LocalFree(msg_buf);
						}
						else {
							object_error(&ob, "failed to load (%d): unknown error code\n", errcode);
						}
#else
						object_error(&ob, "failed to load: %s", dlerror());
#endif
					} else {
#ifdef WIN_VERSION
						testfun = (testfun_t)GetProcAddress(lib_handle, "test");
						initfun = (initfun_t)GetProcAddress(lib_handle, "init");
						quitfun = (quitfun_t)GetProcAddress(lib_handle, "quit");
						gimmefun = (gimmefun_t)GetProcAddress(lib_handle, "anything");
#else
						testfun = (testfun_t)dlsym(lib_handle, "test");
						initfun = (initfun_t)dlsym(lib_handle, "init");
						quitfun = (quitfun_t)dlsym(lib_handle, "quit");
						gimmefun = (gimmefun_t)dlsym(lib_handle, "anything");
#endif
						
						if (initfun) {
							instance_handle = initfun(this);
							object_post(&ob, "init result %p", instance_handle);
						}
						
						// loaded OK:
						t_atom a[1];
						atom_setlong(a+0, 1);
						outlet_anything(outlet_msg, gensym("loaded"), 1, a);
					}
				} else {
					object_error(&ob, "couldn't create system path for file %s", file->s_name);
				}
			}
		}
	}
	
	t_max_err notify(t_symbol *s, t_symbol *msg, void *sender, void *data) {
		long argc = 16;
		t_atom temp[16];
		t_atom *argv = temp;
		if (sender == this) {
			if (msg == _sym_attr_modified) {
				t_symbol * attrname = (t_symbol *)object_method((t_object *)data,gensym("getname"));
				object_attr_getvalueof(this, attrname, &argc, &argv);
				if (argc && argv) {
					if (gimmefun) gimmefun(instance_handle, attrname, argc, argv);
				}
			}
		}
		return MAX_ERR_NONE;
	}
};

void dyn_reload_deferred(dyn *x) {
	x->reload();
}

void dyn_reload(dyn *x) {
	defer_low(x, (method)dyn_reload_deferred, 0, 0, 0);
}

void dyn_unload(dyn *x) {
	x->unload();
}

void dyn_filechanged(dyn *x, char *filename, short path) {
	defer_low(x, (method)dyn_reload_deferred, 0, 0, 0);
}

void dyn_assist(dyn *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "I am inlet %ld", a);
	}
	else {	// outlet
		sprintf(s, "I am outlet %ld", a);
	}
}

void dyn_anything(dyn *x, t_symbol *s, long argc, t_atom *argv) {
	if (x->gimmefun) x->gimmefun(x->instance_handle, s, argc, argv);
}

t_max_err dyn_notify(dyn *x, t_symbol *s, t_symbol *msg, void *sender, void *data) {
	return x->notify(s, msg, sender, data);
}

void dyn_bang(dyn * x) {
	dyn_anything(x, _sym_bang, 0, 0);
}

/*void dyn_test(dyn *x, t_atom_long i) {
	if (x->testfun && x->instance_handle) {
		t_atom a[1];
		atom_setlong(a, x->testfun(x->instance_handle, i));
		outlet_anything(x->outlet_msg, gensym("test"), 1, a);
	}
}*/

t_max_err dyn_setattr_file(dyn *x, t_symbol *s, long argc, t_atom *argv) {
	t_symbol *v = _sym_nothing;
	if(argc && argv) v = atom_getsym(argv);
	if (v) {
		x->file = v;
		defer_low(x, (method)dyn_reload_deferred, 0, 0, 0);
	}
	return MAX_ERR_NONE;
}

t_max_err dyn_setattr_autowatch(dyn *x, t_symbol *s, long argc, t_atom *argv) {
	t_atom_long a = atom_getlong(argv);
	if (a != x->autowatch) {
		x->autowatch = a;
		if (x->filewatcher) {
			if (x->autowatch) {
				filewatcher_start(x->filewatcher);
			} else {
				filewatcher_stop(x->filewatcher);
			}
		}
	}
	return MAX_ERR_NONE;
}

void * dyn_new(t_symbol *s, long argc, t_atom *argv) {
	dyn *x = NULL;
	if ((x = (dyn *)object_alloc(max_class))) {
		
		x = new (x) dyn();
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// consider 1st arg if no @file attr
		if (x->file == _sym_nothing && argc && atom_gettype(argv) == A_SYM) {
			x->file = atom_getsym(argv);
			//x->reload();
			
			defer_low(x, (method)dyn_reload_deferred, 0, 0, 0);
		}
	}
	return (x);
}

void dyn_free(dyn *x) {
	x->~dyn();
}

void ext_main(void *r)
{
	t_class *c;

	object_post(0, "ext_main %d %d %d", sizeof(t_atom_long), sizeof(int), sizeof(long));
	
	common_symbols_init();
	
	c = class_new("dyn", (method)dyn_new, (method)dyn_free, (long)sizeof(dyn), 0L, A_GIMME, 0);
	
	class_addmethod(c, (method)dyn_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)dyn_filechanged,"filechanged",A_CANT, 0);
	class_addmethod(c, (method)dyn_notify,	"notify",	A_CANT, 0);
	
	class_addmethod(c, (method)dyn_reload, "reload", 0);
	class_addmethod(c, (method)dyn_unload, "unload", 0);

	//class_addmethod(c, (method)dyn_test, "test", A_LONG, 0);
	class_addmethod(c, (method)dyn_bang, "bang", 0);
	class_addmethod(c, (method)dyn_anything, "anything", A_GIMME, 0);
	
	CLASS_ATTR_SYM(c, "file", 0, dyn, file);
	CLASS_ATTR_ACCESSORS(c, "file", 0, dyn_setattr_file);
	
	
	//CLASS_ATTR_LONG(c, "autowatch", 0, dyn, autowatch); TODO: this line was causing an error. Replacing it with the next line solved the problem but this might not be a correct full-time solution
	CLASS_ATTR_ATOM_LONG(c, "autowatch", 0, dyn, autowatch);
	CLASS_ATTR_ACCESSORS(c, "autowatch", 0, dyn_setattr_autowatch);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

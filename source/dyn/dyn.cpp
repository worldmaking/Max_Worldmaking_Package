#include "al_max.h"

#include "ext.h"
#include "ext_path.h"
#include "ext_obex.h"

#include <string>
#include <dlfcn.h> // dlopen

#define MULTILINE(...) #__VA_ARGS__

static t_class * max_class = 0;

class dyn {
public:
	
	typedef int (*testfun_t)(int);
	typedef int (*initfun_t)(t_object *);
	typedef void (*gimmefun_t)(t_object *, t_symbol *, long, t_atom *);
	
	t_object ob;
	void * outlet_result;
	void * outlet_msg;
	
	// attrs;
	t_symbol * file = _sym_nothing;
	t_atom_long autowatch = 1;
	
	void * filewatcher = 0;
#ifdef WIN_VERSION
	HMODULE lib_handle = 0;
#else
	void * lib_handle = 0;
#endif
	testfun_t testfun = 0;
	initfun_t initfun = 0;
	gimmefun_t gimmefun = 0;
	
	dyn() {
		outlet_msg = outlet_new(&ob, 0);
		outlet_result = outlet_new(&ob, 0);
	}
	
	~dyn() {
		unload();
	}
	
	void unload() {
		if (lib_handle) {
			// call the dylib's close handler
			
#ifdef WIN_VERSION
			if (FreeLibrary(lib_handle) == 0) {
				char err[256];
				FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
							  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
				object_error(&ob, "%s", err);
			}
#else
			if (dlclose(lib_handle) != 0) {
				object_error(&ob, "%s", dlerror());
			}
#endif
			lib_handle = 0;
		}
		testfun = 0;
		initfun = 0;
		gimmefun = 0;
		
		t_atom a[1];
		atom_setlong(a+0, 0);
		outlet_anything(outlet_msg, gensym("loaded"), 1, a);
	}
	
	void reload() {
		if (file) {
			if (filewatcher) {
				freeobject((t_object *)filewatcher);
				filewatcher = 0;
			}
			
			char libname[MAX_FILENAME_CHARS];
#ifdef WIN_VERSION
			snprintf_zero(libname, MAX_FILENAME_CHARS, "%s.dll", file->s_name);
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
				char systempath[MAX_FILENAME_CHARS];
				if (path_toabsolutesystempath(vol, libname, folderpath) == 0
					&& path_nameconform(folderpath, systempath, PATH_STYLE_SLASH, PATH_TYPE_BOOT) == 0) {
					
					//object_post(&ob, "path %s", systempath);
					
					unload();
					
#ifdef WIN_VERSION
					lib_handle = LoadLibraryA(systempath);
#else
					lib_handle = dlopen(systempath, RTLD_NOW | RTLD_GLOBAL);
#endif
					if (!lib_handle) {
#ifdef WIN_VERSION
						char err[256];
						FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
									  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
						object_error(&ob, "%s", err);
#else
						object_error(&ob, "%s", dlerror());
#endif
					} else {
#ifdef WIN_VERSION
						testfun = (testfun_t)GetProcAddress(lib_handle, "test");
						gimmefun = (gimmefun_t)GetProcAddress(lib_handle, "anything");
#else
						testfun = (testfun_t)dlsym(lib_handle, "test");
						gimmefun = (gimmefun_t)dlsym(lib_handle, "anything");
#endif
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
};

void * dyn_new(t_symbol *s, long argc, t_atom *argv) {
	dyn *x = NULL;
	if ((x = (dyn *)object_alloc(max_class))) {
		
		x = new (x) dyn();
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// consider 1st arg if no @file attr
		if (x->file == _sym_nothing && argc && atom_gettype(argv) == A_SYM) {
			x->file = atom_getsym(argv);
			x->reload();
		}
	}
	return (x);
}

void dyn_free(dyn *x) {
	x->~dyn();
}

void dyn_reload(dyn *x) {
	x->reload();
}

void dyn_unload(dyn *x) {
	x->unload();
}

void dyn_filechanged(dyn *x, char *filename, short path) {
	x->reload();
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
	if (x->gimmefun) x->gimmefun((t_object *)x, s, argc, argv);
}

void dyn_test(dyn *x, t_atom_long i) {
	if (x->testfun) {
		object_post(&x->ob, "test(%d) -> %d", i, x->testfun(i));
	}
}

t_max_err dyn_setattr_file(dyn *x, t_symbol *s, long argc, t_atom *argv) {
	t_symbol *v = _sym_nothing;
	if(argc && argv) v = atom_getsym(argv);
	if (v) {
		x->file = v;
		x->reload();
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

void ext_main(void *r)
{
	t_class *c;
	
	common_symbols_init();
	
	c = class_new("dyn", (method)dyn_new, (method)dyn_free, (long)sizeof(dyn), 0L, A_GIMME, 0);
	
	class_addmethod(c, (method)dyn_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)dyn_filechanged,"filechanged",A_CANT, 0);
	
	class_addmethod(c, (method)dyn_reload, "reload", 0);
	class_addmethod(c, (method)dyn_unload, "unload", 0);
	class_addmethod(c, (method)dyn_test, "test", A_LONG, 0);
	class_addmethod(c, (method)dyn_anything, "anything", A_GIMME, 0);
	
	CLASS_ATTR_SYM(c, "file", 0, dyn, file);
	CLASS_ATTR_ACCESSORS(c, "file", 0, dyn_setattr_file);
	
	
	CLASS_ATTR_LONG(c, "autowatch", 0, dyn, autowatch);
	CLASS_ATTR_ACCESSORS(c, "autowatch", 0, dyn_setattr_autowatch);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

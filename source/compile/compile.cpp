#include "al_max.h"

#include "ext.h"
#include "ext_obex.h"

#include <string>
#include <dlfcn.h> // dlopen

#define MULTILINE(...) #__VA_ARGS__

static t_class * max_class = 0;

#define MAX_SYM_ARRAY_ITEMS 32

t_symbol * system_header_path = 0;

const char * standard_header = "#include <default_header.h> \n";

class Compile {
public:
	
	typedef int (*testfun_t)(int);
	
	
	typedef int (*initfun_t)(t_object *);
	typedef void (*gimmefun_t)(t_object *, t_symbol *, long, t_atom *);
	
	t_object ob;
	
	void * outlet_result;
	
	// flag is set once all attributes have been set
	// purpose is to delay @file compile until all other attrs are initialized
	int post_init = 0;
	t_string * code_string;
	t_object * clang;
	
	testfun_t testfun;
	initfun_t initfun;
	gimmefun_t gimmefun;
	
	// attrs;
	t_symbol * file = _sym_nothing;
	t_atom_long cpp, fastmath, vectorize, use_system_headers;
	t_symbol * system_includes[MAX_SYM_ARRAY_ITEMS];
	t_symbol * includes[MAX_SYM_ARRAY_ITEMS];
	t_symbol * libraries[MAX_SYM_ARRAY_ITEMS];
	t_symbol * defines[MAX_SYM_ARRAY_ITEMS];
	
	Compile() {
		outlet_result = outlet_new(&ob, 0);
		
		cpp = 1;
		fastmath = 1;
		vectorize = 1;
		use_system_headers = 1;
		
		clang = NULL;
		
		code_string = string_new(standard_header);
		string_append(code_string, "extern \"C\" int test(int i) { object_post(0, \"input: %i\", i); return -i; } \n");
		
		for (int i=0; i<MAX_SYM_ARRAY_ITEMS; i++) {
			includes[i] = _sym_nothing;
			system_includes[i] = _sym_nothing;
			defines[i] = _sym_nothing;
			libraries[i] = _sym_nothing;
		}
	}
	
	~Compile() {
		
		if (code_string) {
			object_free(code_string);
			code_string = NULL;
		}
		if (clang) {
			object_release(clang);
			clang = NULL;
		}
	}
	
	void init() {
		post_init = 1;
		
		if (file && file != _sym_nothing) {
			file_reload();
		}
	}
	
	void file_reload() {
		if (post_init && file) {
			short vol;
			t_fourcc type;
			short res = locatefile_extended(file->s_name, &vol, &type, NULL, 0);
			if (res != 0) {
				object_error(&ob, "couldn't find file %s", file->s_name);
				return;
			}
			
			t_filehandle fh = 0;
			if(path_opensysfile(file->s_name, vol, &fh, PATH_READ_PERM) == 0) {
				t_handle h = 0;
				sysfile_readtohandle(fh, &h);
				sysmem_nullterminatehandle(h);
				t_string * code_string = string_new(*h);
				sysmem_freehandle(h);
				sysfile_close(fh);
				
				object_post(&ob, "compile %s", file->s_name);
				
				compile_string(code_string);
				object_release((t_object *)code_string);
			}
		}
	}
	
	void reset_compiler() {
		if (clang) object_release(clang);
		
		clang = (t_object *)object_new(CLASS_NOBOX, gensym("clang"), gensym("ctest"));
		if (clang == NULL) {
			object_error(&ob, "failed to create compiler");
			return;
		}
		
		// set C or C++
		object_attr_setlong(clang, gensym("cpp"), cpp);
		object_attr_setlong(clang, gensym("vectorize"), vectorize);
		object_attr_setlong(clang, gensym("fastmath"), fastmath);

		// add path to the Clang standard headers:
		if (use_system_headers) {
			object_method(clang, gensym("system_include"), system_header_path);
			add_max_symbols();
		}
		
		// basic defines:
#ifdef WIN_VERSION
		object_method(clang, gensym("define"), gensym("WIN_VERSION"));
		object_method(clang, gensym("define"), gensym("_MSC_VER"));
#endif
#ifdef MAC_VERSION
		object_method(clang, gensym("define"), gensym("MAC_VERSION"));
#endif
	}
	
	void system_include(t_symbol * path) {
		if (!clang) return;
		object_method(clang, gensym("system_include"), path);
	}
	
	void include(t_symbol * path) {
		if (!clang) return;
		object_method(clang, gensym("include"), path);
	}
	
	void define(t_symbol * sym) {
		if (!clang) return;
		object_method(clang, gensym("define"), sym);
	}
	
	int load_library(const char * libname) {
		void * dylib_handle = dlopen(libname, RTLD_NOW | RTLD_GLOBAL);
		if (!dylib_handle) {
			object_error(&ob, "%s\n", dlerror());
			return 0;
		}
		return 1;
	}

	
	void add_max_symbols() {
		if (!clang) return;
		object_method(clang, gensym("addsymbol"), gensym("object_post"), &object_post);
	}
	
	void compile(t_symbol * code) {
		if (!clang) return;
		t_string * code_string = string_new(standard_header);
		string_append(code_string, code->s_name);
		compile_string(code_string);
		object_release((t_object *)code_string);
	}
	
	void compile_string(t_string * code_string) {
		try {
			// TODO: reset compiler properly
			
			clang = (t_object *)object_new(CLASS_NOBOX, gensym("clang"), gensym("ctest"));
			
			// set C or C++
			object_attr_setlong(clang, gensym("cpp"), cpp);
			
			//object_method(clang, gensym("include_standard_headers"));

			//post("compiling: %s", code_string->s_text);
			
			// add path to the Clang standard headers:
			object_method(clang, gensym("system_include"), system_header_path);
			//object_method(clang, gensym("system_include"), gensym("/usr/local/include"));
			
			for (int i=0; i<MAX_SYM_ARRAY_ITEMS; i++) {
				t_symbol * s;
				
				s = system_includes[i];
				if (s != _sym_nothing) {
					object_method(clang, gensym("system_include"), s);
				}
				
				s = includes[i];
				if (s != _sym_nothing) {
					object_method(clang, gensym("include"), s);
				}
				
				s = defines[i];
				if (s != _sym_nothing) {
					object_method(clang, gensym("define"), s);
				}
				
				s = libraries[i];
				if (s != _sym_nothing) {
					load_library(s->s_name);
				}
			}
			
			// define macros:
			object_method(clang, gensym("define"), gensym("__STDC_LIMIT_MACROS"));
			object_method(clang, gensym("define"), gensym("__STDC_CONSTANT_MACROS"));
	#ifdef WIN_VERSION
			object_method(clang, gensym("define"), gensym("WIN_VERSION"));
			object_method(clang, gensym("define"), gensym("_MSC_VER"));
	#endif
	#ifdef MAC_VERSION
			object_method(clang, gensym("define"), gensym("MAC_VERSION"));
	#endif
			
			// compile options:
			object_attr_setlong(clang, gensym("vectorize"), 1);
			object_attr_setlong(clang, gensym("fastmath"), 1);
			
			// push specific symbols:
			// is this necessary? seems to not be necessary on OSX, maybe it is needed on Windows?
			//object_method(clang, gensym("addsymbol"), gensym("object_post"), &object_post);
			
			// load some libs
			// this will find stuff in /usr/local/lib, for example
			//load_library("libglfw.dylib");
			//load_library("libpcl_common.dylib");
			
			//object_method(clang, gensym("addsymbol"), gensym("compile_dlopen"), &compile_dlopen);
			
			
			t_atom rv, av;
			atom_setobj(&av, code_string);
			//int err = 0;
			t_max_err err = object_method_obj(clang, gensym("compile"), (t_object *)code_string, &rv);
			if (err != MAX_ERR_NONE || !object_attr_getlong(clang, gensym("didcompile"))) {
				object_release(clang);
				clang = NULL;
			}
			
			// or, read bitcode:
			// object_method_sym(clang, gensym("readbitcode"), gensym("path to bitcode"));
			
			
				
			object_method(clang, gensym("optimize"), gensym("O3"));
			
			// at the point jit is called, this clang object becomes opaque
			// but, must keep it around for as long as any code generated is potentially in use
			// only when all functions become unreachable is it safe to object_release(clang)
			object_method(clang, gensym("jit"));
			
			// posts to Max console a list of the functions in the module
			//object_method(clang, gensym("listfunctions"));
			
			// post IR header
			//object_method(clang, gensym("dump"));
			
			// write bitcode:
			//object_method(clang, gensym("writebitcode"), gensym("path to bitcode"));
			
			// Get a function pointer:
			void * fp = getfunction(gensym("test"));
			if (fp) {
				testfun = (testfun_t)fp;
				int result = testfun((int)37);
				outlet_int(outlet_result, result);
			}
			initfun = (initfun_t)getfunction(gensym("init"));
			gimmefun = (gimmefun_t)getfunction(gensym("anything"));
			
			if (initfun) initfun((t_object *)this);
			
			// there is also a "getglobal" that works in the same way as getfunction
			
			//t_atom ret_atom;
			//object_method_sym(clang, gensym("getdatalayout"), gensym("test"), &ret_atom);
			//post("data layout %s", atom_getsym(&ret_atom)->s_name);
			//object_method_sym(clang, gensym("gettargettriple"), gensym("test"), &ret_atom);
			//post("target triple %s", atom_getsym(&ret_atom)->s_name);
			//object_method_sym(clang, gensym("getmoduleid"), gensym("test"), &ret_atom);
			//post("module id %s", atom_getsym(&ret_atom)->s_name);
			
			// could reset_compiler now, we're done with it?
		} catch (const std::exception& ex) {
			object_error(&ob, "exception %s", ex.what());
		} catch (const std::string& ex) {
			object_error(&ob, "exception %s", ex.data());
		} catch (...) {
			object_error(&ob, "exception (unknown)");
		}
	}
	
	void * getfunction(t_symbol * name) {
		t_atom fun_atom;
		object_method_sym(clang, gensym("getfunction"), name, &fun_atom);
		if (fun_atom.a_w.w_obj) {
			return atom_getobj(&fun_atom);
		} else {
			object_error(&ob, "couldn't get function %s", name->s_name);
			return NULL;
		}
	}
	
	void test(t_atom_long i) {
		if (testfun) {
			outlet_int(outlet_result, testfun((int)i));
		}
	}
	
};

void compile_test(Compile *x, t_atom_long i) {
	x->test(i);
}

void * compile_new(t_symbol *s, long argc, t_atom *argv) {
	Compile *x = NULL;
	if ((x = (Compile *)object_alloc(max_class))) {
		
		x = new (x) Compile();
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:
		x->init();
	}
	return (x);
}

void compile_free(Compile *x) {
	x->~Compile();
}

void compile_assist(Compile *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "I am inlet %ld", a);
	}
	else {	// outlet
		sprintf(s, "I am outlet %ld", a);
	}
}

void compile_system_include(Compile * x, t_symbol * s) { x->system_include(s); }
void compile_include(Compile * x, t_symbol * s) { x->include(s); }
void compile_define(Compile * x, t_symbol * s) { x->define(s); }
void compile_compile(Compile * x, t_symbol * s) { x->compile(s); }
void compile_clear(Compile * x) { x->reset_compiler(); }

void compile_anything(Compile *x, t_symbol *s, long argc, t_atom *argv) {
	if (x->gimmefun) x->gimmefun((t_object *)x, s, argc, argv);
}

t_max_err compile_setattr_file(Compile *x, t_symbol *s, long argc, t_atom *argv) {
	t_symbol *v = _sym_nothing;
	if(argc && argv) v = atom_getsym(argv);
	if (v) {
		x->file = v;
		x->file_reload();
	}
	return MAX_ERR_NONE;
}

t_max_err compile_setattr_includes(Compile *x, void *attr, long argc, t_atom *argv) {
	long i;
	t_atom *a = argv;
	for (i = 0; i < MAX_SYM_ARRAY_ITEMS; i++) {
		if (i < argc && atom_getsym(a) && atom_getsym(a) != _sym_nothing)
			x->includes[i] = atom_getsym(a++);
		else
			x->includes[i] = _sym_nothing;	// surf/svg will be freed in loadpictures
	}
	return MAX_ERR_NONE;
}

t_max_err compile_setattr_system_includes(Compile *x, void *attr, long argc, t_atom *argv) {
	long i;
	t_atom *a = argv;
	for (i = 0; i < MAX_SYM_ARRAY_ITEMS; i++) {
		if (i < argc && atom_getsym(a) && atom_getsym(a) != _sym_nothing)
			x->system_includes[i] = atom_getsym(a++);
		else
			x->system_includes[i] = _sym_nothing;	// surf/svg will be freed in loadpictures
	}
	return MAX_ERR_NONE;
}

t_max_err compile_setattr_libraries(Compile *x, void *attr, long argc, t_atom *argv) {
	long i;
	t_atom *a = argv;
	for (i = 0; i < MAX_SYM_ARRAY_ITEMS; i++) {
		if (i < argc && atom_getsym(a) && atom_getsym(a) != _sym_nothing)
			x->libraries[i] = atom_getsym(a++);
		else
			x->libraries[i] = _sym_nothing;	// surf/svg will be freed in loadpictures
	}
	return MAX_ERR_NONE;
}

t_max_err compile_setattr_defines(Compile *x, void *attr, long argc, t_atom *argv) {
	long i;
	t_atom *a = argv;
	for (i = 0; i < MAX_SYM_ARRAY_ITEMS; i++) {
		if (i < argc && atom_getsym(a) && atom_getsym(a) != _sym_nothing)
			x->defines[i] = atom_getsym(a++);
		else
			x->defines[i] = _sym_nothing;	// surf/svg will be freed in loadpictures
	}
	return MAX_ERR_NONE;
}

void ext_main(void *r)
{
	t_class *c;
	
	common_symbols_init();
	
//	void * dylib_handle = dlopen("libglfw.dylib", RTLD_NOW | RTLD_GLOBAL);
//	post("dylib_handle %p\n", dylib_handle);
//	if (!dylib_handle) {
//		error("%s\n", dlerror());
//	}

	if (system_header_path == 0) {
		// want to know the containing path
		char filename[MAX_FILENAME_CHARS];
		char folderpath[MAX_FILENAME_CHARS];
		char systempath[MAX_FILENAME_CHARS];
		short outvol;
		t_fourcc outtype;
#ifdef WIN_VERSION
		strncpy_zero(filename, "compile.mxe", MAX_FILENAME_CHARS);
#else
		strncpy_zero(filename, "compile.mxo", MAX_FILENAME_CHARS);
#endif

		//t_fourcc filetypelist[3];
		//filetypelist[0] = FOUR_CHAR_CODE('iLaX');
		//filetypelist[1] = FOUR_CHAR_CODE('iLaF');
		//filetypelist[2] = FOUR_CHAR_CODE('mx64');
		//short result = locatefile_extended(filename, &outvol, &outtype, filetypelist, 3);

		short result = locatefile_extended(filename, &outvol, &outtype, NULL, 0);
		if (result == 0
			&& path_toabsolutesystempath(outvol, "../include", folderpath) == 0
			&& path_nameconform(folderpath, systempath, PATH_STYLE_SLASH, PATH_TYPE_BOOT) == 0) {

			system_header_path = gensym(systempath);

			post("%s", system_header_path->s_name);
		}
		else {
			object_error(0, "failed to locate system headers");
			return;
		}
	}
	
	c = class_new("compile", (method)compile_new, (method)compile_free, (long)sizeof(Compile),
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)compile_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)compile_test, "test", A_LONG, 0);
	
	
	class_addmethod(c, (method)compile_system_include, "system_include", A_SYM, 0);
	class_addmethod(c, (method)compile_include, "include", A_SYM, 0);
	class_addmethod(c, (method)compile_define, "define", A_SYM, 0);
	class_addmethod(c, (method)compile_compile, "compile", A_SYM, 0);
	class_addmethod(c, (method)compile_clear, "clear", 0);
	
	class_addmethod(c, (method)compile_anything, "anything", A_GIMME, 0);
	
	CLASS_ATTR_LONG(c, "cpp", 0, Compile, cpp);
	CLASS_ATTR_LONG(c, "vectorize", 0, Compile, vectorize);
	CLASS_ATTR_LONG(c, "fastmath", 0, Compile, fastmath);
	CLASS_ATTR_LONG(c, "use_system_headers", 0, Compile, use_system_headers);
	
	CLASS_ATTR_SYM(c, "file", 0, Compile, file);
	CLASS_ATTR_ACCESSORS(c, "file", 0, compile_setattr_file);
	
	CLASS_ATTR_SYM_ARRAY(c, "includes", 0, Compile, includes, MAX_SYM_ARRAY_ITEMS);
	CLASS_ATTR_ACCESSORS(c, "includes", (method)NULL, (method)compile_setattr_includes);
	CLASS_ATTR_SYM_ARRAY(c, "system_includes", 0, Compile, system_includes, MAX_SYM_ARRAY_ITEMS);
	CLASS_ATTR_ACCESSORS(c, "system_includes", (method)NULL, (method)compile_setattr_system_includes);
	CLASS_ATTR_SYM_ARRAY(c, "libraries", 0, Compile, libraries, MAX_SYM_ARRAY_ITEMS);
	CLASS_ATTR_ACCESSORS(c, "libraries", (method)NULL, (method)compile_setattr_libraries);
	CLASS_ATTR_SYM_ARRAY(c, "defines", 0, Compile, defines, MAX_SYM_ARRAY_ITEMS);
	CLASS_ATTR_ACCESSORS(c, "defines", (method)NULL, (method)compile_setattr_defines);
	
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

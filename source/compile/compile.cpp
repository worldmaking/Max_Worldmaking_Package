#include "al_max.h"

#define MULTILINE(...) #__VA_ARGS__

static t_class * max_class = 0;

t_symbol * system_header_path = 0;

const char * standard_header = "#include <default_header.h> \n";


class Compile {
public:
	
	typedef int (*testfun_t)(int);
	
	t_object ob;
	
	void * outlet_result;
	
	t_string * code_string;
	testfun_t testfun;
	t_object * clang;
	
	// attrs;
	t_atom_long cpp, fastmath, vectorize, use_system_headers;
	
	Compile() {
		outlet_result = outlet_new(&ob, 0);
		
		cpp = 1;
		fastmath = 1;
		vectorize = 1;
		use_system_headers = 1;
		
		clang = NULL;
		reset_compiler();
		
		code_string = string_new(standard_header);
		string_append(code_string, "extern \"C\" int test(int i) { object_post(0, \"input: %i\", i); return -i; } \n");
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
	
	void add_max_symbols() {
		if (!clang) return;
		object_method(clang, gensym("addsymbol"), gensym("object_post"), &object_post);
	}
	
	void compile(t_symbol * code) {
		if (!clang) return;
		
		code_string = string_new(standard_header);
		string_append(code_string, code->s_name);
		
		post("compiling");
		post("%s", code_string->s_text);
		
		
		t_atom rv, av;
		atom_setobj(&av, code_string);
		object_method_typed(clang, gensym("compile"), 1, &av, &rv);
		int err = atom_getlong(&rv);
		
		post("result %i", err);
		
		/*
		 
		if (err) {
			object_error(&ob, "compile failed");
			object_release(clang);
			clang = NULL;
		} else {
			// TODO make opt level an attr
			object_method(clang, gensym("optimize"), gensym("O3"));
			
			// at the point jit is called, this clang object becomes opaque
			// but, must keep it around for as long as any code generated is potentially in use
			// only when all functions become unreachable is it safe to object_release(clang)
			object_method(clang, gensym("jit"));
			
			// posts to Max console a list of the functions in the module
			object_method(clang, gensym("listfunctions"));
			
			// post IR header
			//object_method(clang, gensym("dump"));
			
			// write bitcode:
			//object_method(clang, gensym("writebitcode"), gensym("path to bitcode"));
			
			
			// done for now:
			reset_compiler();
		}
		 */
	}
	
	void test(t_atom_long i) {
		
		clang = (t_object *)object_new(CLASS_NOBOX, gensym("clang"), gensym("ctest"));


		if (code_string != NULL && clang != NULL) {
			
			post("compile %s", code_string->s_text);
			
			// set C or C++
			object_attr_setlong(clang, gensym("cpp"), cpp);
			

			// add include paths:
			//		object_method(clang, gensym("include"), gensym("path/to/include"));
			
			// add path to the Clang standard headers:
			object_method(clang, gensym("system_include"), system_header_path);
			
			// define macros:
			//object_method(clang, gensym("define"), gensym("__STDC_LIMIT_MACROS"));
			//object_method(clang, gensym("define"), gensym("__STDC_CONSTANT_MACROS"));
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
			object_method(clang, gensym("addsymbol"), gensym("object_post"), &object_post);
			
			t_atom rv, av;
			atom_setobj(&av, code_string);
			object_method_typed(clang, gensym("compile"), 1, &av, &rv);
			int err = atom_getlong(&rv);

			// or, read bitcode:
			// object_method_sym(clang, gensym("readbitcode"), gensym("path to bitcode"));
			
			if (err == 0) {
				
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
				t_atom fun_atom;
				object_method_sym(clang, gensym("getfunction"), gensym("test"), &fun_atom);
				if (fun_atom.a_w.w_obj) {
					testfun = (testfun_t)atom_getobj(&fun_atom);
				
					int result = testfun((int)i);
					outlet_int(outlet_result, result);
				} else {
					object_error(&ob, "couldn't get function");
				}

				// there is also a "getglobal" that works in the same way as getfunction

				//t_atom ret_atom;
				//object_method_sym(clang, gensym("getdatalayout"), gensym("test"), &ret_atom);
				//post("data layout %s", atom_getsym(&ret_atom)->s_name);
				//object_method_sym(clang, gensym("gettargettriple"), gensym("test"), &ret_atom);
				//post("target triple %s", atom_getsym(&ret_atom)->s_name);
				//object_method_sym(clang, gensym("getmoduleid"), gensym("test"), &ret_atom);
				//post("module id %s", atom_getsym(&ret_atom)->s_name);
				
			} else {
				object_release(clang);
				clang = NULL;
			}
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

void ext_main(void *r)
{
	t_class *c;
	
	common_symbols_init();

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
	
	CLASS_ATTR_LONG(c, "cpp", 0, Compile, cpp);
	CLASS_ATTR_LONG(c, "vectorize", 0, Compile, vectorize);
	CLASS_ATTR_LONG(c, "fastmath", 0, Compile, fastmath);
	CLASS_ATTR_LONG(c, "use_system_headers", 0, Compile, use_system_headers);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

#ifndef al_max_h
#define al_max_h

// see https://cycling74.com/forums/topic/error-126-loading-external/#.WIvJXhsrKHs
#define MAXAPI_USE_MSCRT

// a bunch of likely Max includes:
extern "C" {
#include "ext.h"
#include "ext_obex.h"
#include "ext_dictionary.h"
#include "ext_dictobj.h"
#include "ext_systhread.h"
	
#include "z_dsp.h"
	
#include "jit.common.h"
#include "jit.vecmath.h"
#include "jit.gl.h"
#include "max.jit.mop.h"
}

#include <new> // for in-place constructor

// get or create a named inlet:
void * dynamic_inlet(t_object * host, t_symbol * name) {
	t_object * inlet;
	object_obex_lookup(host, name, &inlet);
	if (!inlet) {
		t_object * b;
		object_obex_lookup(host,gensym("#B"),(t_object **)&b);
		object_method(b, gensym("dynlet_begin"));
		inlet = (t_object *)inlet_append(host, 0, 0, 0);
		object_method(b, gensym("dynlet_end"));
		object_obex_storeflags(host, name, inlet, OBJ_FLAG_REF);
	}
	return inlet;
}

void dynamic_inlet_remove(t_object * host, t_symbol * name) {
	t_object * inlet;
	object_obex_lookup(host, name, &inlet);
	if (inlet) {
		t_object * b;
		object_obex_lookup(host,gensym("#B"),(t_object **)&b);
		object_method(b, gensym("dynlet_begin"));
		inlet_delete(inlet);
		object_method(b, gensym("dynlet_end"));
		object_obex_storeflags(host, name, 0, OBJ_FLAG_REF);
	}
}

// get or create a named outlet:
void * dynamic_outlet(t_object * host, t_symbol * name) {
	t_object * outlet;
	object_obex_lookup(host, name, &outlet);
	if (!outlet) {
		t_object * b;
		object_obex_lookup(host,gensym("#B"),(t_object **)&b);
		object_method(b, gensym("dynlet_begin"));
		outlet = (t_object *)outlet_append(host, 0, 0);
		object_method(b, gensym("dynlet_end"));
		object_obex_storeflags(host, name, outlet, OBJ_FLAG_REF);
	}
	return outlet;
}

void dynamic_outlet_remove(t_object * host, t_symbol * name) {
	t_object * outlet;
	object_obex_lookup(host, name, &outlet);
	if (outlet) {
		t_object * b;
		object_obex_lookup(host,gensym("#B"),(t_object **)&b);
		object_method(b, gensym("dynlet_begin"));
		outlet_delete(outlet);
		object_method(b, gensym("dynlet_end"));
		object_obex_storeflags(host, name, 0, OBJ_FLAG_REF);
	}
}

#include "al_math.h"

// jitter uses xyzw format
// glm:: uses wxyz format
// xyzw -> wxyz
inline glm::quat quat_from_jitter(glm::quat const & v) {
	return glm::quat(v.z, v.w, v.x, v.y);
}

inline glm::quat quat_from_jitter(t_jit_quat const & v) {
	return glm::quat(v.w, v.x, v.y, v.z);
}

// wxyz -> xyzw
inline glm::quat quat_to_jitter(glm::quat const & v) {
	return glm::quat(v.x, v.y, v.z, v.w);
}


template <typename C>
t_symbol * jitmat_type_from_typename();
template<> t_symbol * jitmat_type_from_typename<char>() { return _jit_sym_char; };
template<> t_symbol * jitmat_type_from_typename<long>() { return _jit_sym_long; };
template<> t_symbol * jitmat_type_from_typename<float>() { return _jit_sym_float32; };
template<> t_symbol * jitmat_type_from_typename<glm::vec2>() { return _jit_sym_float32; };
template<> t_symbol * jitmat_type_from_typename<glm::vec3>() { return _jit_sym_float32; };
template<> t_symbol * jitmat_type_from_typename<glm::vec4>() { return _jit_sym_float32; };
template<> t_symbol * jitmat_type_from_typename<glm::quat>() { return _jit_sym_float32; };
template<> t_symbol * jitmat_type_from_typename<double>() { return _jit_sym_float64; };

template <typename C>
int jitmat_planecount_from_typename();
template<> int jitmat_planecount_from_typename<char>() { return 1; };
template<> int jitmat_planecount_from_typename<long>() { return 1; };
template<> int jitmat_planecount_from_typename<float>() { return 1; };
template<> int jitmat_planecount_from_typename<glm::vec2>() { return 2; };
template<> int jitmat_planecount_from_typename<glm::vec3>() { return 3; };
template<> int jitmat_planecount_from_typename<glm::vec4>() { return 4; };
template<> int jitmat_planecount_from_typename<glm::quat>() { return 4; };
template<> int jitmat_planecount_from_typename<double>() { return 1; };

size_t jitmat_type_size(t_symbol * type) {
	if (type == _jit_sym_char) return 1;
	if (type == _jit_sym_long) return sizeof(t_atom_long);
	if (type == _jit_sym_float32) return 4;
	if (type == _jit_sym_float64) return 8;
}

// a purely static base class for Max and MSP objects:
template <typename T>
class MaxCppBase {
public:
	
	static t_class * m_class;
	
};

template <typename T>
class MaxObject : public MaxCppBase<T> {
public:
	
	t_object m_ob;
	
	// C++ operator overload to treat MaxCpp6 objects as t_objects
	operator t_object & () { return m_ob; }
	
	static t_class * makeMaxClass(const char * classname) {
		t_class * c = class_new(classname, (method)MaxObject<T>::maxcpp_create, (method)MaxObject<T>::maxcpp_destroy, sizeof(T), 0, A_GIMME, 0);
		MaxCppBase<T>::m_class = c;
		class_register(CLASS_BOX, MaxCppBase<T>::m_class);
		return c;
	}
	
	static void * maxcpp_create(t_symbol * sym, long ac, t_atom * av) {
		void * x = object_alloc(MaxCppBase<T>::m_class);
		new(x) T(sym, ac, av);
		attr_args_process(x, ac, av);
		return (T *)x;
	}
	
	static void maxcpp_destroy(t_object * x) {
		((T *)x)->~T();
	}
};



#endif /* al_max_h */

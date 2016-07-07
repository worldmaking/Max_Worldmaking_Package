#ifndef al_max_h
#define al_max_h

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

#include "al_math.h"
#include "al_hashspace.h"

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
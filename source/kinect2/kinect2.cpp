// a bunch of likely Max includes:
extern "C" {
#include "ext.h"
#include "ext_obex.h"
#include "ext_dictionary.h"
#include "ext_dictobj.h"
#include "ext_systhread.h"

#include "z_dsp.h"

#include "jit.common.h"
#include "jit.gl.h"
}

#ifdef __GNUC__
#include <stdint.h>
#else
#include "stdint.h"
#endif

#include <Ole2.h>
typedef OLECHAR* WinStr;

#include "kinect.h"

#include <new> // for in-place constructor

// how many glm headers do we really need?
#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/matrix_access.hpp"
#include "glm/gtc/matrix_inverse.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/noise.hpp"
#include "glm/gtc/random.hpp"
#include "glm/gtc/type_ptr.hpp"

// unstable extensions
#include "glm/gtx/norm.hpp"
#include "glm/gtx/std_based_type.hpp"

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::quat;
//typedef glm::detail::tvec4<glm::detail::uint8> ucvec4;


// jitter uses xyzw format
// glm:: uses wxyz format
// xyzw -> wxyz
template<typename T, glm::precision P>
inline glm::detail::tquat<T, P> quat_from_jitter(glm::detail::tquat<T, P> const & v) {
	return glm::detail::tquat<T, P>(v.z, v.w, v.x, v.y);
}

// wxyz -> xyzw
template<typename T, glm::precision P>
inline glm::detail::tquat<T, P> quat_to_jitter(glm::detail::tquat<T, P> const & v) {
	return glm::detail::tquat<T, P>(v.x, v.y, v.z, v.w);
}


//	q must be a normalized quaternion
template<typename T, glm::precision P>
glm::detail::tvec3<T, P> & quat_rotate(glm::detail::tquat<T, P> const & q, glm::detail::tvec3<T, P> & v) {
	// qv = vec4(v, 0) // 'pure quaternion' derived from vector
	// return ((q * qv) * q^-1).xyz
	// reduced:
	vec4 p;
	p.x = q.w*v.x + q.y*v.z - q.z*v.y;	// x
	p.y = q.w*v.y + q.z*v.x - q.x*v.z;	// y
	p.z = q.w*v.z + q.x*v.y - q.y*v.x;	// z
	p.w = -q.x*v.x - q.y*v.y - q.z*v.z;	// w

	v.x = p.x*q.w - p.w*q.x + p.z*q.y - p.y*q.z;	// x
	v.y = p.y*q.w - p.w*q.y + p.x*q.z - p.z*q.x;	// y
	v.z = p.z*q.w - p.w*q.z + p.y*q.x - p.x*q.y;	// z

	return v;
}

// equiv. quat_rotate(quat_conj(q), v):
// q must be a normalized quaternion
template<typename T, glm::precision P>
void quat_unrotate(glm::detail::tquat<T, P> const & q, glm::detail::tvec3<T, P> & v) {
	// return quat_mul(quat_mul(quat_conj(q), vec4(v, 0)), q).xyz;
	// reduced:
	vec4 p;
	p.x = q.w*v.x + q.y*v.z - q.z*v.y;	// x
	p.y = q.w*v.y + q.z*v.x - q.x*v.z;	// y
	p.z = q.w*v.z + q.x*v.y - q.y*v.x;	// z
	p.w = q.x*v.x + q.y*v.y + q.z*v.z;	// -w

	v.x = p.w*q.x + p.x*q.w + p.y*q.z - p.z*q.y;  // x
	v.x = p.w*q.y + p.y*q.w + p.z*q.x - p.x*q.z;  // y
	v.x = p.w*q.z + p.z*q.w + p.x*q.y - p.y*q.x;   // z
}


template <typename celltype>
struct jitmat {

	t_object * mat;
	t_symbol * sym;
	t_atom name[1];
	int w, h;
	vec2 dim;

	celltype * back;

	jitmat() {
		mat = 0;
		back = 0;
		sym = 0;
	}

	~jitmat() {
		//if (mat) object_release(mat);
	}

	void init(int planecount, t_symbol * type, int width, int height) {
		w = width;
		h = height;
		t_jit_matrix_info info;
		jit_matrix_info_default(&info);
		info.planecount = planecount;
		info.type = type;
		info.dimcount = 2;
		info.dim[0] = w;
		info.dim[1] = h;
		//info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
		mat = (t_object *)jit_object_new(_jit_sym_jit_matrix, &info);
		if (!mat) post("failed to allocate matrix");
		jit_object_method(mat, _jit_sym_clear);
		sym = jit_symbol_unique();
		jit_object_method(mat, _jit_sym_getdata, &back);
		mat = (t_object *)jit_object_method(mat, _jit_sym_register, sym);
		atom_setsym(name, sym);

		dim = vec2(w, h);
	}

	celltype read_clamp(int x, int y) {
		x = x < 0 ? 0 : x >= w ? w - 1 : x;
		y = y < 0 ? 0 : y >= h ? h - 1 : y;
		return back[x + y*w];
	}

	celltype mix(celltype a, celltype b, float f) {
		return a + f*(b - a);
	}

	celltype sample(vec2 texcoord) {
		vec2 t = texcoord*(dim - 1.f);
		vec2 t0 = vec2f(floor(t.x), floor(t.y));
		vec2 t1 = t0 + 1.f;
		vec2 ta = t - t0;
		celltype v00 = read_clamp(t0.x, t0.y);
		celltype v01 = read_clamp(t1.x, t0.y);
		celltype v10 = read_clamp(t0.x, t1.y);
		celltype v11 = read_clamp(t1.x, t1.y);
		return mix(mix(v00, v01, ta.x), mix(v10, v11, ta.x), ta.y);
	}
};


#pragma pack(push, 1)
struct BGRA {
	unsigned char b, g, r, a;
};
struct ARGB {
	unsigned char a, r, g, b;
};
struct RGB {
	unsigned char r, g, b;
};
struct DepthPlayer {
	uint16_t d, p;
};
#pragma pack(pop)

static t_class * max_class = 0;
static const int        cDepthWidth = 512;
static const int        cDepthHeight = 424;
static const int        cColorWidth = 1920;
static const int        cColorHeight = 1080;

class kinect2 {
public:
	t_object ob; // max objkinectt, must be first!
	// outlets:
	void *		outlet_cloud;
	void *		outlet_rgb;
	void *		outlet_depth;
	void *		outlet_player;
	void *		outlet_skeleton;
	void *		outlet_msg;

	jitmat<uint32_t> depth_mat;
	jitmat<ARGB> rgb_mat;

	kinect2() {
		
		outlet_msg = outlet_new(&ob, 0);
		outlet_skeleton = outlet_new(&ob, 0);
		outlet_player = outlet_new(&ob, "jit_matrix");
		outlet_rgb = outlet_new(&ob, "jit_matrix");
		outlet_depth = outlet_new(&ob, "jit_matrix");
		outlet_cloud = outlet_new(&ob, "jit_matrix");

		post("create matrices");
		depth_mat.init(1, _jit_sym_long, cDepthWidth, cDepthHeight);
		post("test");
		rgb_mat.init(4, _jit_sym_char, cColorWidth, cColorHeight);

		post("created matrices");
	}


	~kinect2() {
		close();
	}

	void open(t_symbol *s, long argc, t_atom * argv) {

	}

	static void *capture_threadfunc(void *arg) {
		
		return NULL;
	}

	void poll() {
		
		object_post(&ob, "polled");
	}

	void close() {
		
	}

	void shutdown() {
		
	}

	////

	void bang() {
		
	}
};

void kinect_open(kinect2 * x, t_symbol *s, long argc, t_atom * argv) {
	x->open(s, argc, argv);
}
void kinect_bang(kinect2 * x) { x->bang(); }
void kinect_close(kinect2 * x) { x->close(); }

void * kinect_new(t_symbol *s, long argc, t_atom *argv) {
	kinect2 *x = NULL;
	if ((x = (kinect2 *)object_alloc(max_class))) {

		x = new (x)kinect2();

		// apply attrs:
		attr_args_process(x, (short)argc, argv);

		// invoke any initialization after the attrs are set from here:

	}
	return (x);
}

void kinect_free(kinect2 *x) {
	x->~kinect2();
}

void kinect_assist(kinect2 *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "I am inlet %ld", a);
	}
	else {	// outlet
		sprintf(s, "I am outlet %ld", a);
	}
}

void ext_main(void *r)
{
	t_class *c;

	// just a hack to get jitter initialized:
	post("GL: %s", jit_gl_get_version());


	c = class_new("kinect2", (method)kinect_new, (method)kinect_free, (long)sizeof(kinect2), 0L, A_GIMME, 0);

	class_addmethod(c, (method)kinect_assist, "assist", A_CANT, 0);


	class_addmethod(c, (method)kinect_open, "open", A_GIMME, 0);

	class_addmethod(c, (method)kinect_bang, "bang", 0);
	class_addmethod(c, (method)kinect_close, "close", 0);

	class_register(CLASS_BOX, c);
	max_class = c;
}
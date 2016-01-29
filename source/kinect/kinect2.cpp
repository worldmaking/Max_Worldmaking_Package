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

#include "NuiApi.h"

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::quat;

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

// TODO: is this necessary, or can I use GLM?
// there's glm::gtx::quaternion::rotate, but it takes a vec4

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
		info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
		mat = (t_object *)jit_object_new(_jit_sym_jit_matrix, &info);
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

struct vec3c { uint8_t x, y, z; };

#include <new> // for in-place constructor

static t_class * max_class = 0;
static int device_count;

// TODO not use defines for Kv2
#define DEPTH_WIDTH 640
#define DEPTH_HEIGHT 480
#define DEPTH_CELLS (DEPTH_WIDTH * DEPTH_HEIGHT)

class kinect {
public:
	t_object ob; // max objkinectt, must be first!
	// outlets:
	void *		outlet_cloud;
	void *		outlet_rgb;
	void *		outlet_depth;
	void *		outlet_player;
	void *		outlet_skeleton;
	void *		outlet_msg;

	// Kinect stuff
	INuiSensor* device;
	//INuiCoordinateMapper * mapper;
	HANDLE colorStreamHandle;
	HANDLE depthStreamHandle;
	NUI_SKELETON_FRAME skeleton_back;
	NUI_COLOR_IMAGE_POINT * imagepoints;
	Vector4 * skeletonpoints;

	t_systhread capture_thread;
	t_systhread_mutex depth_mutex;
	int capturing;

	// calibration
	int hasColorMap;
	long* colorCoordinates;
	uint16_t* mappedDepthTmp;
	uint16_t* unmappedDepthTmp;

	// local copies of the data
	// jit.matrix
	jitmat<uint32_t> depth_mat;
	jitmat<char> player_mat;
	jitmat<RGB> rgb_mat;
	jitmat<vec3> cloud_mat;
//	jitmat<vec2> rectify_mat, tmp_mat;

	/*
	// rgb matrix for raw output:
	void *		rgb_mat;
	void *		rgb_mat_wrapper;
	t_atom		rgb_name[1];
	glm::i8vec3 * rgb_back;

	// depth matrix for raw output:
	void *		depth_mat;
	void *		depth_mat_wrapper;
	t_atom		depth_name[1];
	uint32_t *	depth_back;

	// cloud matrix for output:
	void *		cloud_mat;
	void *		cloud_mat_wrapper;
	t_atom		cloud_name[1];
	glm::vec3 *	cloud_back;

	// cloud matrix for output:
	void *		texcoord_mat;
	void *		texcoord_mat_wrapper;
	t_atom		texcoord_name[1];
	glm::vec2 *	texcoord_back;
	*/




	// attrs:
	int			index;
	int			unique;	// whether we output whenever there is a bang, or only when there is new data
	int			use_rgb; // whether to output RGB, or depth only
	int			use_player;
	int			use_skeleton;
	int			align_rgb_to_cloud;	// output RGB image warped to fit cloud
	int			mirror;	// flip the X axis
	int			near_mode, seated;
	int			uselock;
	int			timeout;

	// pose of cloud:
	glm::vec3	cloud_position;
	glm::quat	cloud_quat, cloud_quat_glm;

	t_symbol * serial;

	volatile char new_rgb_data;
	volatile char new_depth_data;
	volatile char new_cloud_data;

	kinect() {
		device = 0;
		index = 0;
		capturing = 0;
		hasColorMap = 0;

		unique = 1;
		uselock = 0;
		new_rgb_data = 0;
		new_depth_data = 0;
		new_cloud_data = 0;
		

		cloud_quat.w = 1;

		mirror = 0;

		use_rgb = 1;
		use_player = 0;
		use_skeleton = 0;
		align_rgb_to_cloud = 1;

		near_mode = 0;
		seated = 0;

		outlet_msg = outlet_new(&ob, 0);
		outlet_skeleton = outlet_new(&ob, 0);
		outlet_player = outlet_new(&ob, "jit_matrix");
		outlet_rgb = outlet_new(&ob, "jit_matrix");
		outlet_depth = outlet_new(&ob, "jit_matrix");
		outlet_cloud = outlet_new(&ob, "jit_matrix");

		systhread_mutex_new(&depth_mutex, 0);

		imagepoints = (NUI_COLOR_IMAGE_POINT *)sysmem_newptr(DEPTH_CELLS * sizeof(NUI_COLOR_IMAGE_POINT));
		skeletonpoints = (Vector4 *)sysmem_newptr(DEPTH_CELLS * sizeof(Vector4));

		/*
		// create matrices:
		{
			t_jit_matrix_info info;
			rgb_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
			rgb_mat = jit_object_method(rgb_mat_wrapper, _jit_sym_getmatrix);
			// create the internal data:
			jit_matrix_info_default(&info);
			info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
			info.planecount = 3;
			info.type = gensym("char");
			info.dimcount = 2;
			info.dim[0] = DEPTH_WIDTH;
			info.dim[1] = DEPTH_HEIGHT;
			jit_object_method(rgb_mat, _jit_sym_setinfo_ex, &info);
			jit_object_method(rgb_mat, _jit_sym_clear);
			jit_object_method(rgb_mat, _jit_sym_getdata, &rgb_back);
			// cache name:
			atom_setsym(rgb_name, jit_attr_getsym(rgb_mat_wrapper, _jit_sym_name));
		}

		{
			t_jit_matrix_info info;
			depth_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
			depth_mat = jit_object_method(depth_mat_wrapper, _jit_sym_getmatrix);
			// create the internal data:
			jit_matrix_info_default(&info);
			info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
			info.planecount = 1;
			info.type = gensym("long");
			info.dimcount = 2;
			info.dim[0] = DEPTH_WIDTH;
			info.dim[1] = DEPTH_HEIGHT;
			jit_object_method(depth_mat, _jit_sym_setinfo_ex, &info);
			jit_object_method(depth_mat, _jit_sym_clear);
			jit_object_method(depth_mat, _jit_sym_getdata, &depth_back);
			// cache name:
			atom_setsym(depth_name, jit_attr_getsym(depth_mat_wrapper, _jit_sym_name));
		}

		{

			t_jit_matrix_info info;
			cloud_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
			cloud_mat = jit_object_method(cloud_mat_wrapper, _jit_sym_getmatrix);
			// create the internal data:
			jit_matrix_info_default(&info);
			info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
			info.planecount = 3;
			info.type = gensym("float32");
			info.dimcount = 2;
			info.dim[0] = DEPTH_WIDTH;
			info.dim[1] = DEPTH_HEIGHT;
			jit_object_method(cloud_mat, _jit_sym_setinfo_ex, &info);
			jit_object_method(cloud_mat, _jit_sym_clear);
			jit_object_method(cloud_mat, _jit_sym_getdata, &cloud_back);
			// cache name:
			atom_setsym(cloud_name, jit_attr_getsym(cloud_mat_wrapper, _jit_sym_name));
		}
		{
			t_jit_matrix_info info;
			texcoord_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
			texcoord_mat = jit_object_method(texcoord_mat_wrapper, _jit_sym_getmatrix);
			// create the internal data:
			jit_matrix_info_default(&info);
			info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
			info.planecount = 2;
			info.type = gensym("float32");
			info.dimcount = 2;
			info.dim[0] = DEPTH_WIDTH;
			info.dim[1] = DEPTH_HEIGHT;
			jit_object_method(texcoord_mat, _jit_sym_setinfo_ex, &info);
			jit_object_method(texcoord_mat, _jit_sym_clear);
			jit_object_method(texcoord_mat, _jit_sym_getdata, &texcoord_back);
			// cache name:
			atom_setsym(texcoord_name, jit_attr_getsym(texcoord_mat_wrapper, _jit_sym_name));
		}
		{
			t_jit_matrix_info info;
			cloud_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
			cloud_mat = jit_object_method(cloud_mat_wrapper, _jit_sym_getmatrix);
			// create the internal data:
			jit_matrix_info_default(&info);
			info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
			info.planecount = 3;
			info.type = gensym("float32");
			info.dimcount = 2;
			info.dim[0] = DEPTH_WIDTH;
			info.dim[1] = DEPTH_HEIGHT;
			jit_object_method(cloud_mat, _jit_sym_setinfo_ex, &info);
			jit_object_method(cloud_mat, _jit_sym_clear);
			jit_object_method(cloud_mat, _jit_sym_getdata, &cloud_back);
			// cache name:
			atom_setsym(cloud_name, jit_attr_getsym(cloud_mat_wrapper, _jit_sym_name));
		}

		{
			t_jit_matrix_info info;
			texcoord_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
			texcoord_mat = jit_object_method(texcoord_mat_wrapper, _jit_sym_getmatrix);
			// create the internal data:
			jit_matrix_info_default(&info);
			info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
			info.planecount = 2;
			info.type = gensym("float32");
			info.dimcount = 2;
			info.dim[0] = DEPTH_WIDTH;
			info.dim[1] = DEPTH_HEIGHT;
			jit_object_method(texcoord_mat, _jit_sym_setinfo_ex, &info);
			jit_object_method(texcoord_mat, _jit_sym_clear);
			jit_object_method(texcoord_mat, _jit_sym_getdata, &texcoord_back);
			// cache name:
			atom_setsym(texcoord_name, jit_attr_getsym(texcoord_mat_wrapper, _jit_sym_name));
		}
		*/
		
		depth_mat.init(1, _jit_sym_long, DEPTH_WIDTH, DEPTH_HEIGHT);
		player_mat.init(1, _jit_sym_char, DEPTH_WIDTH, DEPTH_HEIGHT);
		rgb_mat.init(3, _jit_sym_char, DEPTH_WIDTH, DEPTH_HEIGHT);
		cloud_mat.init(3, _jit_sym_float32, DEPTH_WIDTH, DEPTH_HEIGHT);
		//rectify_mat.init(2, _jit_sym_float32, DEPTH_WIDTH, DEPTH_HEIGHT);
		//tmp_mat.init(2, _jit_sym_float32, DEPTH_WIDTH, DEPTH_HEIGHT);


		colorCoordinates = new long[DEPTH_WIDTH*DEPTH_HEIGHT * 2];
		mappedDepthTmp = new uint16_t[DEPTH_WIDTH*DEPTH_HEIGHT];
		unmappedDepthTmp = new uint16_t[DEPTH_WIDTH*DEPTH_HEIGHT];
	}


	~kinect() {
		close();
		systhread_mutex_free(depth_mutex);
	}

	void open(t_symbol *s, long argc, t_atom * argv) {
		t_atom a[1];
		HRESULT result = NuiGetSensorCount(&device_count);
		object_post(&ob, "found %d devices", device_count);
		if (result != S_OK) {
			object_error(&ob, "failed to get sensor count");
			return;
		}

		if (device) {
			object_warn(&ob, "device already opened");
			return;
		}
		INuiSensor* dev;
		result = 0;
		if (argc > 0) {
			if (atom_gettype(argv) == A_SYM) {
				OLECHAR instanceName[100];
				char * s = atom_getsym(argv)->s_name;
				mbstowcs(instanceName, s, strlen(s) + 1);
				result = NuiCreateSensorById(instanceName, &dev);
			}
			else {
				int index = atom_getlong(argv);
				result = NuiCreateSensorByIndex(index, &dev);
			}
		}
		else {
			result = NuiCreateSensorByIndex(0, &dev);
		}
		if (result != S_OK) {
			if (E_NUI_DEVICE_IN_USE == result) {
				error("Kinect for Windows already in use.");
			}
			else if (E_NUI_NOTGENUINE == result) {
				error("Kinect for Windows is not genuine.");
			}
			else if (E_NUI_INSUFFICIENTBANDWIDTH == result) {
				error("Insufficient bandwidth.");
			}
			else if (E_NUI_NOTSUPPORTED == result) {
				error("Kinect for Windows device not supported.");
			}
			else if (E_NUI_NOTCONNECTED == result) {
				error("Kinect for Windows is not connected.");
			}
			else if (E_NUI_NOTREADY == result) {
				error("Kinect for Windows is not ready.");
			}
			else if (E_NUI_NOTPOWERED == result) {
				error("Kinect for Windows is not powered.");
			}
			else if (E_NUI_DATABASE_NOT_FOUND == result) {
				error("Kinect for Windows database not found.");
			}
			else if (E_NUI_DATABASE_VERSION_MISMATCH == result) {
				error("Kinect for Windows database version mismatch.");
			}
			else {
				error("Kinect for Windows could not initialize.");
			}
			return;
		}
		result = dev->NuiStatus();
		switch (result) {
		case S_OK:
			break;
		case S_NUI_INITIALIZING:
			object_post(&ob, "the device is connected, but still initializing"); return;
		case E_NUI_NOTCONNECTED:
			object_error(&ob, "the device is not connected"); return;
		case E_NUI_NOTGENUINE:
			object_post(&ob, "the device is not a valid kinect"); break;
		case E_NUI_NOTSUPPORTED:
			object_post(&ob, "the device is not a supported model"); break;
		case E_NUI_INSUFFICIENTBANDWIDTH:
			object_error(&ob, "the device is connected to a hub without the necessary bandwidth requirements."); return;
		case E_NUI_NOTPOWERED:
			object_post(&ob, "the device is connected, but unpowered."); return;
		default:
			object_post(&ob, "the device has some unspecified error"); return;
		}

		WinStr wstr = dev->NuiDeviceConnectionId();
		std::mbstate_t state = std::mbstate_t();
		int len = 1 + std::wcsrtombs((char *)nullptr, (const wchar_t **)&wstr, 0, &state);
		char outname[128];
		std::wcsrtombs(outname, (const wchar_t **)&wstr, len, &state);
		serial = gensym(outname);

		device = dev;
		post("init device %s", outname);
		atom_setsym(a, serial);
		outlet_anything(outlet_msg, gensym("serial"), 1, a);

		long priority = 10; // maybe increase?
		if (systhread_create((method)&capture_threadfunc, this, 0, priority, 0, &capture_thread)) {
			object_error(&ob, "Failed to create capture thread.");
			capturing = 0;
			close();
			return;
		}
	}

	static void *capture_threadfunc(void *arg) {
		kinect *x = (kinect *)arg;
		x->run();
		systhread_exit(NULL);
		return NULL;
	}

	void run() {
		HRESULT result = 0;
		DWORD dwImageFrameFlags;
		DWORD initFlags = 0;

		initFlags = 0;
		if (use_rgb) initFlags |= NUI_INITIALIZE_FLAG_USES_COLOR;
		if (use_player) {
			initFlags |= NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX;
		}
		else {
			initFlags |= NUI_INITIALIZE_FLAG_USES_DEPTH;
		}
		if (use_skeleton) {
			initFlags |= NUI_INITIALIZE_FLAG_USES_SKELETON;
		}
		result = device->NuiInitialize(initFlags);

		if (result != S_OK) {
			object_error(&ob, "failed to initialize sensor");
			goto done;
		}
		if (use_skeleton) {
			if (seated) {
				NuiSkeletonTrackingEnable(NULL, NUI_SKELETON_TRACKING_FLAG_ENABLE_SEATED_SUPPORT);
			}
			else {
				NuiSkeletonTrackingEnable(NULL, 0);
			}
		}
		object_post(&ob, "device initialized");

		if (use_rgb) {
			dwImageFrameFlags = 0;
			if (near_mode) dwImageFrameFlags |= NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE;
			result = device->NuiImageStreamOpen(
				NUI_IMAGE_TYPE_COLOR, //NUI_IMAGE_TYPE eImageType,
				NUI_IMAGE_RESOLUTION_640x480, // NUI_IMAGE_RESOLUTION eResolution,
				dwImageFrameFlags,
				2, //DWORD dwFrameLimit,
				0,
				&colorStreamHandle);
			if (result != S_OK) {
				object_error(&ob, "failed to open color stream");
				goto done;
			}
			object_post(&ob, "opened color stream");
		}

		object_post(&ob, "opened color stream");

		dwImageFrameFlags = 0;
		dwImageFrameFlags |= NUI_IMAGE_STREAM_FLAG_DISTINCT_OVERFLOW_DEPTH_VALUES;
		if (near_mode) dwImageFrameFlags |= NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE;
		NUI_IMAGE_TYPE eImageType = NUI_IMAGE_TYPE_DEPTH;
		if (use_player) {
			eImageType = NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX;
		}
		result = device->NuiImageStreamOpen(
			eImageType,
			NUI_IMAGE_RESOLUTION_640x480, // NUI_IMAGE_RESOLUTION eResolution,
			dwImageFrameFlags,
			2, //DWORD dwFrameLimit,
			0,
			&depthStreamHandle);
		if (result != S_OK) {
			object_error(&ob, "failed to open depth stream");
			goto done;
		}
		object_post(&ob, "opened depth stream");



		//id = (CString)(device->NuiUniqueId());

		//object_post(&ob, "id %s", id);
		//object_post(&ob, "aid %s cid %s", (const char*)(_bstr_t(device->NuiAudioArrayId(), false)), (const char*)(_bstr_t(device->NuiDeviceConnectionId(), false)));

		// get coordinate mapper:
		//device->NuiGetCoordinateMapper(&mapper);

		capturing = 1;
		post("starting processing");
		while (capturing) {
			pollDepth();
			if (use_rgb) pollColor();
			//if (use_skeleton) pollSkeleton();
		}
		post("finished processing");

	done:
		shutdown();
	}

	void pollDepth() {

		if (!device) return;

		HRESULT result;
		NUI_IMAGE_FRAME imageFrame;
		DWORD dwMillisecondsToWait = timeout;

		result = device->NuiImageStreamGetNextFrame(depthStreamHandle, dwMillisecondsToWait, &imageFrame);
		if (result == E_NUI_FRAME_NO_DATA) {
			// timeout with no data. bail or continue?
			systhread_sleep(30);
			return;
		}
		else if (FAILED(result)) {
			switch (result) {
			case E_INVALIDARG:
				object_error(&ob, "arg stream error"); break;
			case E_POINTER:
				object_error(&ob, "pointer stream error"); break;
			case S_FALSE:
				object_error(&ob, "timeout"); break;
			default:
				object_error(&ob, "stream error %x"); break;
			}
			systhread_sleep(30);
			return;
		}

		INuiFrameTexture * imageTexture = NULL;
		BOOL bNearMode = near_mode;

		result = device->NuiImageFrameGetDepthImagePixelFrameTexture(depthStreamHandle, &imageFrame, &bNearMode, &imageTexture);
		//imageTexture = imageFrame.pFrameTexture;

		// got data; now turn it into jitter 
		if (!imageTexture) {
			post("no data");
			goto ReleaseFrame;
		}

		NUI_LOCKED_RECT LockedRect;

		// Lock the frame data so the Kinect knows not to modify it while we're reading it
		imageTexture->LockRect(0, &LockedRect, NULL, 0);

		cloud_quat_glm = quat_from_jitter(cloud_quat);

		// Make sure we've received valid data 
		if (LockedRect.Pitch != 0) {
			
			NUI_DEPTH_IMAGE_PIXEL * src = reinterpret_cast<NUI_DEPTH_IMAGE_PIXEL*>(LockedRect.pBits);
			uint32_t * dst = depth_mat.back;
			char * dstp = player_mat.back;
			static const int cells = DEPTH_HEIGHT * DEPTH_WIDTH;
			
			// First generate packed depth values from extended depth values, which include near pixels.
			for (int i = 0; i<cells; i++) {
				unmappedDepthTmp[i] = src[i].depth << NUI_IMAGE_PLAYER_INDEX_SHIFT;
			}
			
			if (!hasColorMap) {
				// use it to generate the color map:
				device->NuiImageGetColorPixelCoordinateFrameFromDepthPixelFrameAtResolution(
					NUI_IMAGE_RESOLUTION_640x480, //colorResolution,
					NUI_IMAGE_RESOLUTION_640x480, //depthResolution,
					cells,
					unmappedDepthTmp, // depth_d16
					cells * 2,
					colorCoordinates
					);
				//post("generated color map");
				hasColorMap = 1;
			}
			
			if (uselock) systhread_mutex_lock(depth_mutex);

			if (align_rgb_to_cloud) {
				// write cells into depth and cloud matrices:
				for (int i = 0, y = 0; y<DEPTH_HEIGHT; y++) {
					for (int x = 0; x<DEPTH_WIDTH; x++, i++) {
						uint32_t d = src[i].depth;
						dst[i] = d;
						//dstp[i] = (char)src[i].playerIndex;
						if (d > 0) {
							vec3 v = depthToRealWorld(vec3(x, y, d << 3));
							//cloudTransform(v);
							cloud_mat.back[i] = v;
						}
						else {
							// invalid depth: fill zero:
							vec3 v(0, 0, 0);
							cloud_mat.back[i] = cloudTransform(v);
						}
					}
				}
			}
			else {
				// TODO
				vec2 dim(DEPTH_WIDTH, DEPTH_HEIGHT);
				vec2 inv_dim_1 = 1.f / (dim - 1.f);

			}

			if (uselock) systhread_mutex_unlock(depth_mutex);
			new_depth_data = 1;

			/*
			
			if (mirror) {
				uint32_t * dst = depth_mat.back;
				NUI_DEPTH_IMAGE_PIXEL * src = (NUI_DEPTH_IMAGE_PIXEL *)LockedRect.pBits;
				int cells = DEPTH_HEIGHT * DEPTH_WIDTH;
				do {
					*dst++ = src->depth;
					//					*dstp++ = (char)src->playerIndex;
					src++;
				} while (--cells);
			}
			else {
				for (int i = 0; i < DEPTH_CELLS; i += DEPTH_WIDTH) {
					uint32_t * dst = depth_mat.back + i;
					NUI_DEPTH_IMAGE_PIXEL * src = ((NUI_DEPTH_IMAGE_PIXEL *)LockedRect.pBits) + i + (DEPTH_WIDTH - 1);
					int cells = DEPTH_WIDTH;
					do {
						*dst++ = src->depth;
						//						*dstp++ = (char)src->playerIndex;
						src--;
					} while (--cells);
				}
			}
			new_depth_data = 1;

			
			// let the Kinect SDK convert our depth frame to points in "skeleton space"
			if (mapper->MapDepthFrameToSkeletonFrame(NUI_IMAGE_RESOLUTION_640x480,
				DEPTH_CELLS, (NUI_DEPTH_IMAGE_PIXEL *)LockedRect.pBits,
				DEPTH_CELLS, skeletonpoints)) {
				object_warn(&ob, "failed to map depth to cloud");
			}
			

				// convert to proper vec3:
				int cells = DEPTH_CELLS;
				for (int i = 0; i<cells; i++) {
					glm::vec3& dst = cloud_mat.back[i];
					const Vector4& src = skeletonpoints[i];
					// scale appropriately:
					float div = 1.f / src.w;
					// also rotate 180 around y
					dst.x = -src.x * div;
					dst.y = src.y * div;
					dst.z = -src.z * div;
				}

				new_cloud_data = 1;
			*/
		}

		// We're done with the texture so unlock it
		imageTexture->UnlockRect(0);

		//		cloud_process();
		//		local_cloud_process();


	ReleaseFrame:
		// Release the frame
		device->NuiImageStreamReleaseFrame(depthStreamHandle, &imageFrame);

	}

	void pollColor() {
		if (!device) return;

		HRESULT result;
		NUI_IMAGE_FRAME imageFrame;
		DWORD dwMillisecondsToWait = timeout;

		result = device->NuiImageStreamGetNextFrame(colorStreamHandle, dwMillisecondsToWait, &imageFrame);
		if (result == E_NUI_FRAME_NO_DATA) {
			// timeout with no data. bail or continue?
			return;
		}
		else if (FAILED(result)) {
			switch (result) {
			case E_INVALIDARG:
				object_error(&ob, "arg stream error"); break;
			case E_POINTER:
				object_error(&ob, "pointer stream error"); break;
			default:
				object_error(&ob, "stream error"); break;
			}
			//close();
			return;
		}

		int newframe = 0;

		// got data; now turn it into jitter 
		//post("frame %d", imageFrame.dwFrameNumber);
		//outlet_int(outlet_msg, imageFrame.dwFrameNumber);
		INuiFrameTexture * imageTexture = imageFrame.pFrameTexture;
		NUI_LOCKED_RECT LockedRect;

		// Lock the frame data so the Kinect knows not to modify it while we're reading it
		imageTexture->LockRect(0, &LockedRect, NULL, 0);

		// Make sure we've received valid data
		if (LockedRect.Pitch != 0) {
			const BGRA * src = (const BGRA *)LockedRect.pBits;
			RGB * dst = (RGB *)rgb_mat.back;
			//if (mirror) {
			int cells = DEPTH_HEIGHT * DEPTH_WIDTH;

			if (align_rgb_to_cloud) {
				// align color to depth:
				//std::fill(dst, dst + cells, RGB(0, 0, 0));
				for (int i = 0; i < cells; ++i) {
					int c = colorCoordinates[i * 2];
					int r = colorCoordinates[i * 2 + 1];
					if (c >= 0 && c < DEPTH_WIDTH
						&& r >= 0 && r < DEPTH_HEIGHT) {
						// valid location: depth value:
						int idx = r*DEPTH_WIDTH + c;
						dst[i].r = src[idx].r;
						dst[i].g = src[idx].g;
						dst[i].b = src[idx].b;
					}
				}
			}
			else {
				for (int i = 0; i < cells; ++i) {
					dst[i].r = src[i].r;
					dst[i].g = src[i].g;
					dst[i].b = src[i].b;
				}
			}
			newframe = 1;

			
		}

		// We're done with the texture so unlock it
		imageTexture->UnlockRect(0);

		//	ReleaseFrame:
		// Release the frame
		device->NuiImageStreamReleaseFrame(colorStreamHandle, &imageFrame);

		//		if (newframe) cloud_rgb_process();

		new_rgb_data = 1;
	}

	void close() {
		if (capturing) {
			capturing = 0;
			unsigned int ret;
			long result = systhread_join(capture_thread, &ret);
			post("thread closed");
		}
		else {
			shutdown();
		}
		hasColorMap = 0;
	}

	void shutdown() {
		if (device) {
			device->NuiShutdown();
			device->Release();
			device = 0;
		}
	}

	vec3 depthToRealWorld(const vec3& p) {
		Vector4 v = NuiTransformDepthImageToSkeleton(
			LONG(p.x),
			LONG(p.y),
			USHORT(p.z),
			NUI_IMAGE_RESOLUTION_640x480
			);

		return vec3(v.x, v.y, v.z);
	}

	vec3& cloudTransform(vec3& p) {

		p -= cloud_position;
		quat_rotate(cloud_quat_glm, p);

		return p;
	}

	////

	void bang() {
		if (use_rgb && (new_rgb_data || unique == 0)) {
			outlet_anything(outlet_rgb, _jit_sym_jit_matrix, 1, rgb_mat.name);
			new_rgb_data = 0;
		}
		if (new_depth_data || unique == 0) {
			//if (use_skeleton) outputSkeleton();
			if (use_player) outlet_anything(outlet_player, _jit_sym_jit_matrix, 1, player_mat.name);
			if (uselock) systhread_mutex_lock(depth_mutex);
			outlet_anything(outlet_depth, _jit_sym_jit_matrix, 1, depth_mat.name);
			outlet_anything(outlet_cloud, _jit_sym_jit_matrix, 1, cloud_mat.name);
			if (uselock) systhread_mutex_unlock(depth_mutex);
			new_depth_data = 0;
		}
	}
};

void kinect_open(kinect * x, t_symbol *s, long argc, t_atom * argv) {
	x->open(s, argc, argv);
}
void kinect_bang(kinect * x) { x->bang(); }

void * kinect_new(t_symbol *s, long argc, t_atom *argv) {
	kinect *x = NULL;
	if ((x = (kinect *)object_alloc(max_class))) {

		x = new (x)kinect();

		// apply attrs:
		attr_args_process(x, (short)argc, argv);

		// invoke any initialization after the attrs are set from here:

	}
	return (x);
}

void kinect_free(kinect *x) {
	x->~kinect();
}

void kinect_assist(kinect *x, void *b, long m, long a, char *s)
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

	c = class_new("kinect", (method)kinect_new, (method)kinect_free, (long)sizeof(kinect), aaaaaaaaaaaaaa0L, A_GIMME, 0);

	class_addmethod(c, (method)kinect_assist, "assist", A_CANT, 0);


	class_addmethod(c, (method)kinect_open, "open", A_GIMME, 0);

	class_addmethod(c, (method)kinect_bang, "bang", 0);

	class_register(CLASS_BOX, c);
	max_class = c;
}
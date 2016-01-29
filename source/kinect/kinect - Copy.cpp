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

using glm::quat;
using glm::vec2;
using glm::vec3;

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
		vec2 t0 = vec2(floor(t.x), floor(t.y));
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

//struct vec3c { uint8_t x, y, z; };

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
	INuiCoordinateMapper * mapper;
	HANDLE colorStreamHandle;
	HANDLE depthStreamHandle;
	NUI_COLOR_IMAGE_POINT * imagepoints;
	Vector4 * skeletonpoints;

	t_systhread capture_thread;
	int capturing;
	int hasColorMap;

	// local copies of the data
	// jit.matrix
	jitmat<uint32_t> depth_mat;
	jitmat<char> player_mat;
	jitmat<RGB> rgb_mat;
	jitmat<vec3> cloud_mat;
	//jitmat<vec2> rectify_mat, tmp_mat;

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
	vec3 *		cloud_back;

	// cloud matrix for output:
	void *		texcoord_mat;
	void *		texcoord_mat_wrapper;
	t_atom		texcoord_name[1];
	vec2 *		texcoord_back;
	*/

	// calibration
	vec2 rgb_focal, rgb_center;

	// pose of cloud:
	vec3	cloud_position;
	glm::quat	cloud_quat;

	// attrs:
	int			index; // device index
	int			unique;	// whether we output whenever there is a bang, or only when there is new data
	int			use_rgb; // whether to output RGB, or depth only
	int			use_player; // whether to output player IDs
	int			use_skeleton; // whether to output skeleton data
	int			seated;
	int			align_rgb_to_cloud;	// output RGB image warped to fit cloud
	int			mirror;	// flip the X axis
	int			near_mode;
	int			timeout;

	volatile char new_rgb_data;
	volatile char new_depth_data;
	volatile char new_cloud_data;

	uint16_t unmappedDepthTmp[DEPTH_CELLS];
	long colorCoordinates[DEPTH_CELLS * 2];

	kinect() {
		device = 0;
		capturing = 0;

		unique = 1;
		new_rgb_data = 0;
		new_depth_data = 0;
		new_cloud_data = 0;

		mirror = 1;


		use_rgb = 1;
		use_player = 0;
		use_skeleton = 0;
		seated = 0;
		align_rgb_to_cloud = 0;
		near_mode = 0;

		timeout = 30;

		outlet_msg = outlet_new(&ob, 0);
		outlet_skeleton = outlet_new(&ob, 0);
		outlet_player = outlet_new(&ob, "jit_matrix");
		outlet_rgb = outlet_new(&ob, "jit_matrix");
		outlet_depth = outlet_new(&ob, "jit_matrix");
		outlet_cloud = outlet_new(&ob, "jit_matrix");

		imagepoints = (NUI_COLOR_IMAGE_POINT *)sysmem_newptr(DEPTH_CELLS * sizeof(NUI_COLOR_IMAGE_POINT));
		skeletonpoints = (Vector4 *)sysmem_newptr(DEPTH_CELLS * sizeof(Vector4));

		// create matrices:

		depth_mat.init(1, _jit_sym_long, DEPTH_WIDTH, DEPTH_HEIGHT);
		player_mat.init(1, _jit_sym_char, DEPTH_WIDTH, DEPTH_HEIGHT);
		rgb_mat.init(3, _jit_sym_char, DEPTH_WIDTH, DEPTH_HEIGHT);
		cloud_mat.init(3, _jit_sym_float32, DEPTH_WIDTH, DEPTH_HEIGHT);
		//rectify_mat.init(2, _jit_sym_float32, KINECT_DEPTH_WIDTH, KINECT_DEPTH_HEIGHT);
		//tmp_mat.init(2, _jit_sym_float32, KINECT_DEPTH_WIDTH, KINECT_DEPTH_HEIGHT);

		/*
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
	}


	~kinect() {
		close();
	}

	void open(t_symbol *s, long argc, t_atom * argv) {
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

		index = 0;
		if (argc > 0) index = atom_getlong(argv);
		// TODO choose device by serial

		INuiSensor* dev;
		result = NuiCreateSensorByIndex(index, &dev);
		if (result != S_OK) {
			object_error(&ob, "failed to create sensor");
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

		device = dev;
		post("init device %p", device);

		long priority = 0; // maybe increase?
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
		hasColorMap = 0;

		initFlags |= NUI_INITIALIZE_FLAG_USES_COLOR;
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
				object_error(&ob, "failed to open stream");
				goto done;
			}
		}

		object_post(&ob, "opened color stream");

		dwImageFrameFlags = 0;
		dwImageFrameFlags |= NUI_IMAGE_STREAM_FLAG_DISTINCT_OVERFLOW_DEPTH_VALUES;
		if (near_mode) dwImageFrameFlags |= NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE;
		NUI_IMAGE_TYPE eImageType = NUI_IMAGE_TYPE_DEPTH;
		if (use_player) eImageType = NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX;
		result = device->NuiImageStreamOpen(
			eImageType,
			NUI_IMAGE_RESOLUTION_640x480, // NUI_IMAGE_RESOLUTION eResolution,
			dwImageFrameFlags,
			2, //DWORD dwFrameLimit,
			0,
			&depthStreamHandle);
		if (result != S_OK) {
			object_error(&ob, "failed to open stream");
			goto done;
		}
		object_post(&ob, "opened depth stream");

		// estimateCalibration();

		//id = (CString)(device->NuiUniqueId());

		//object_post(&ob, "id %s", id);
		//object_post(&ob, "aid %s cid %s", (const char*)(_bstr_t(device->NuiAudioArrayId(), false)), (const char*)(_bstr_t(device->NuiDeviceConnectionId(), false)));

		// get coordinate mapper:
		device->NuiGetCoordinateMapper(&mapper);

		capturing = 1;
		post("starting processing");
		while (capturing) {
			pollDepth();
			pollColor();
			//if (use_skeleton) pollSkeleton();
			//systhread_sleep(0);
		}
		post("finished processing");

	done:
		shutdown();
	}

	vec3 realWorldToDepth(const vec3& p) {
		const Vector4 v = { p.x, p.y, p.z, 1.f };
		LONG x = 0;
		LONG y = 0;
		USHORT d = 0;
		NuiTransformSkeletonToDepthImage(v, &x, &y, &d, NUI_IMAGE_RESOLUTION_640x480);
		d >>= 3;
		return vec3(x, y, d * 0.001f);
	}

	void estimateCalibration() {
		// deduce focal depth from depth-to-world transform
		vec3 p = realWorldToDepth(vec3(0.f, 0.f, 1.f));
		// image center
		float cx = p.x;
		float cy = p.y;
		p = realWorldToDepth(vec3(1.f, 1.f, 1.f));
		float fx = (p.x - cx);
		float fy = -(p.y - cy);
		const float correction = NUI_CAMERA_COLOR_NOMINAL_FOCAL_LENGTH_IN_PIXELS
			/ (NUI_CAMERA_DEPTH_NOMINAL_FOCAL_LENGTH_IN_PIXELS * 2.f);
		// pixels are square
		rgb_focal.x = fx * correction;
		rgb_focal.y = rgb_focal.x;
		rgb_center.x = cx;
		rgb_center.y = cy;
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
				object_warn(&ob, "timeout"); break;
			default:
				object_error(&ob, "stream error"); break;
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
		
		// Make sure we've received valid data 
		if (LockedRect.Pitch != 0) {

			
			NUI_DEPTH_IMAGE_PIXEL * src = reinterpret_cast<NUI_DEPTH_IMAGE_PIXEL *>(LockedRect.pBits);
			uint32_t * dst = depth_mat.back;
			// char * dstp = player_mat.back;
			static const int cells = DEPTH_HEIGHT * DEPTH_WIDTH;

			// first generate packed depth values fom extended depth values (which include near pixels)
			for (int i = 0; i < cells; i++) {
				unmappedDepthTmp[i] = src[i].depth << NUI_IMAGE_PLAYER_INDEX_SHIFT;
			}

			if (!hasColorMap) {
				// use it to generate the color map:
				device->NuiImageGetColorPixelCoordinateFrameFromDepthPixelFrameAtResolution(
					NUI_IMAGE_RESOLUTION_640x480,
					NUI_IMAGE_RESOLUTION_640x480,
					cells,
					unmappedDepthTmp,
					cells * 2,
					colorCoordinates);
				hasColorMap = 1;
			}

			//if (uselock) systhread_mutex_lock(depth_mutex);

			if (mirror) {
				// convert to Jitter-friendly RGB layout
				int cells = DEPTH_HEIGHT * DEPTH_WIDTH;
				do {
					*dst++ = src->depth;
					//*dstp++ = (char)src->playerIndex;
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
						//*dstp++ = (char)src->playerIndex;
						src--;
					} while (--cells);
				}
			}
			new_depth_data = 1;
		
			/*
			// generate texcoords;
			if (mapper->MapDepthFrameToColorFrame(
				NUI_IMAGE_RESOLUTION_640x480,
				DEPTH_CELLS, (NUI_DEPTH_IMAGE_PIXEL *)LockedRect.pBits,
				NUI_IMAGE_TYPE_COLOR, NUI_IMAGE_RESOLUTION_640x480,
				DEPTH_CELLS, imagepoints)) {
				object_warn(&ob, "failed to generate texcoords");
			}
			else {
				// convert imagepoints to texcoords:
				int cells = DEPTH_CELLS;
				if (mirror) {
					for (int i = 0; i<cells; i++) {
						glm::vec2& dst = texcoord_back[i];
						const NUI_COLOR_IMAGE_POINT& src = imagepoints[i];
						dst.x = src.x * (1.f / DEPTH_WIDTH);
						dst.y = 1. - src.y * (1.f / DEPTH_HEIGHT);
					}
				}
				else {
					for (int i = 0; i<cells; i++) {
						glm::vec2& dst = texcoord_back[i];
						const NUI_COLOR_IMAGE_POINT& src = imagepoints[i];
						dst.x = 1. - src.x * (1.f / DEPTH_WIDTH);
						dst.y = 1. - src.y * (1.f / DEPTH_HEIGHT);
					}
				}
			}*/

			// let the Kinect SDK convert our depth frame to points in "skeleton space"
			if (mapper->MapDepthFrameToSkeletonFrame(NUI_IMAGE_RESOLUTION_640x480,
				DEPTH_CELLS, (NUI_DEPTH_IMAGE_PIXEL *)LockedRect.pBits,
				DEPTH_CELLS, skeletonpoints)) {
				object_warn(&ob, "failed to map depth to cloud");
			}
			else {

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
			}
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

		int newframe = 0;
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
				do {
					dst->r = src->r;
					dst->g = src->g;
					dst->b = src->b;
					//dst->a = 0xFF;
					src++;
					dst++;
				} while (--cells);
			/*}
			else {
				for (int i = 0; i<KINECT_DEPTH_WIDTH * KINECT_DEPTH_HEIGHT; i += KINECT_DEPTH_WIDTH) {
					for (int x = 0; x<KINECT_DEPTH_WIDTH; ++x) {
						const BGRA& s = src[i + (KINECT_DEPTH_WIDTH - 1) - x];
						RGB& d = dst[i + x];
						d.r = s.r;
						d.g = s.g;
						d.b = s.b;
					}
				}
			}*/


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
	}

	void shutdown() {
		if (device) {
			device->NuiShutdown();
			device->Release();
			device = 0;
		}
	}

	////

	void bang() {
	
		outlet_anything(outlet_depth, _jit_sym_jit_matrix, 1, depth_mat.name);
		outlet_anything(outlet_rgb, _jit_sym_jit_matrix, 1, rgb_mat.name);
		//outlet_anything(outlet_cloud, gensym("texcoord_matrix"), 1, texcoord_name);
		outlet_anything(outlet_cloud, _jit_sym_jit_matrix, 1, cloud_mat.name);

	}
};

void kinect_open(kinect * x, t_symbol *s, long argc, t_atom * argv) {
	x->open(s, argc, argv);
}
void kinect_bang(kinect * x) { x->bang(); }

void * kinect_new(t_symbol *s, long argc, t_atom *argv) {
	kinect *x = NULL;
	x = (kinect *)object_alloc(max_class);
	post("kinect new %p", x);
	if (x) {
		
		x = new (x) kinect();
		
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
	
	c = class_new("kinect", (method)kinect_new, (method)kinect_free, (long)sizeof(kinect), 0L, A_GIMME, 0);
	
	/*
	class_addmethod(c, (method)kinect_assist,			"assist",		A_CANT, 0);


	class_addmethod(c, (method)kinect_open, "open", A_GIMME, 0);

	class_addmethod(c, (method)kinect_bang, "bang", 0);


	CLASS_ATTR_LONG(c, "use_rgb", 0, kinect, use_rgb);
	CLASS_ATTR_LONG(c, "use_player", 0, kinect, use_player);
	CLASS_ATTR_LONG(c, "use_skeleton", 0, kinect, use_skeleton);
	CLASS_ATTR_LONG(c, "timeout", 0, kinect, timeout);

	CLASS_ATTR_LONG(c, "mirror", 0, kinect, mirror);
	CLASS_ATTR_STYLE_LABEL(c, "mirror", 0, "onoff", "flip the color image around Y");
	*/
	
	class_register(CLASS_BOX, c);
	max_class = c;

	post("kinect external registered");
}

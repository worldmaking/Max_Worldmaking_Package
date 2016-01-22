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
	INuiCoordinateMapper * mapper;
	HANDLE colorStreamHandle;
	HANDLE depthStreamHandle;
	NUI_COLOR_IMAGE_POINT * imagepoints;
	Vector4 * skeletonpoints;

	t_systhread capture_thread;
	int capturing;

	// local copies of the data

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

	// pose of cloud:
	glm::vec3	cloud_position;
	glm::quat	cloud_quat;

	// attrs:
	int			unique;	// whether we output whenever there is a bang, or only when there is new data
	int			use_rgb; // whether to output RGB, or depth only
	int			align_rgb_to_cloud;	// output RGB image warped to fit cloud
	int			mirror;	// flip the X axis
	int			near_mode;

	volatile char new_rgb_data;
	volatile char new_depth_data;
	volatile char new_cloud_data;

	// attrs
	int index;

	kinect() {
		device = 0;
		capturing = 0;

		unique = 1;
		new_rgb_data = 0;
		new_depth_data = 0;
		new_cloud_data = 0;

		mirror = 0;

		use_rgb = 1;
		align_rgb_to_cloud = 0;

		near_mode = 0;

		outlet_msg = outlet_new(&ob, 0);
		outlet_skeleton = outlet_new(&ob, 0);
		outlet_player = outlet_new(&ob, "jit_matrix");
		outlet_rgb = outlet_new(&ob, "jit_matrix");
		outlet_depth = outlet_new(&ob, "jit_matrix");
		outlet_cloud = outlet_new(&ob, "jit_matrix");

		imagepoints = (NUI_COLOR_IMAGE_POINT *)sysmem_newptr(DEPTH_CELLS * sizeof(NUI_COLOR_IMAGE_POINT));
		skeletonpoints = (Vector4 *)sysmem_newptr(DEPTH_CELLS * sizeof(Vector4));

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
	}

	
	~kinect() {
		close();

		if (rgb_mat_wrapper) {
			object_free(rgb_mat_wrapper);
			rgb_mat_wrapper = NULL;
		}
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

		initFlags |= NUI_INITIALIZE_FLAG_USES_COLOR;
		//		if (player) {
		//			initFlags |= NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX;
		//		} else {
		initFlags |= NUI_INITIALIZE_FLAG_USES_DEPTH;
		//		}
		//		if (skeleton) {
		//			initFlags |= NUI_INITIALIZE_FLAG_USES_SKELETON;
		//		}
		result = device->NuiInitialize(initFlags);
		if (result != S_OK) {
			object_error(&ob, "failed to initialize sensor");
			goto done;
		}
	
		//		if (skeleton) {
		//			if (seated) {
		//				NuiSkeletonTrackingEnable(NULL, NUI_SKELETON_TRACKING_FLAG_ENABLE_SEATED_SUPPORT);
		//			} else {
		//				NuiSkeletonTrackingEnable(NULL, 0);
		//			}
		//		}

		object_post(&ob, "device initialized");

		dwImageFrameFlags = 0;
		//		if (near_mode) dwImageFrameFlags |= NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE;
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

		object_post(&ob, "opened color stream");

		dwImageFrameFlags = 0;
		dwImageFrameFlags |= NUI_IMAGE_STREAM_FLAG_DISTINCT_OVERFLOW_DEPTH_VALUES;
		//		if (near_mode) dwImageFrameFlags |= NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE;
		NUI_IMAGE_TYPE eImageType = NUI_IMAGE_TYPE_DEPTH;
		//		if (player) eImageType = NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX;
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
			//			if (skeleton) pollSkeleton();
			//systhread_sleep(0);
		}
		post("finished processing");

	done:
		shutdown();
	}
	
	void pollDepth() {

		if (!device) return;

		HRESULT result;
		NUI_IMAGE_FRAME imageFrame;
		DWORD dwMillisecondsToWait = 200;

		result = device->NuiImageStreamGetNextFrame(depthStreamHandle, dwMillisecondsToWait, &imageFrame);
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
			if (mirror) {
				uint32_t * dst = depth_back;
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
					uint32_t * dst = depth_back + i;
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
			}

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
					glm::vec3& dst = cloud_back[i];
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
		DWORD dwMillisecondsToWait = 200;

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
			RGB * dst = (RGB *)rgb_back;
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
	
		outlet_anything(outlet_depth, _jit_sym_jit_matrix, 1, depth_name);
		outlet_anything(outlet_rgb, _jit_sym_jit_matrix, 1, rgb_name);
		outlet_anything(outlet_cloud, gensym("texcoord_matrix"), 1, texcoord_name);
		outlet_anything(outlet_cloud, _jit_sym_jit_matrix, 1, cloud_name);

	}
};

void kinect_open(kinect * x, t_symbol *s, long argc, t_atom * argv) {
	x->open(s, argc, argv);
}
void kinect_bang(kinect * x) { x->bang(); }

void * kinect_new(t_symbol *s, long argc, t_atom *argv) {
	kinect *x = NULL;
	if ((x = (kinect *)object_alloc(max_class))) {
		
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
	
	c = class_new("kinect", (method)kinect_new, (method)kinect_free, (long)sizeof(kinect),
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)kinect_assist,			"assist",		A_CANT, 0);


	class_addmethod(c, (method)kinect_open, "open", A_GIMME, 0);

	class_addmethod(c, (method)kinect_bang, "bang", 0);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

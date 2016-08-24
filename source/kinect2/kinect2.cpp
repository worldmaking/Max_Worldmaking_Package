// a bunch of likely Max includes:
#include "al_max.h"

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::quat;
using glm::mat4;

// used in ext_main
// a dummy call just to get jitter initialized, otherwise _jit_sym_* symbols etc. won't be populated
// (normally this isn't necessary because jit_class_new does it, but I"m not using jit_class_new)
void initialize_jitlib() {
	t_jit_matrix_info info;
	jit_matrix_info_default(&info);
	info.type = gensym("char");
	object_free(jit_object_new(gensym("jit_matrix"), &info));
}

#ifdef __GNUC__
#include <stdint.h>
#else
#include "stdint.h"
#endif

#include <Ole2.h>
typedef OLECHAR* WinStr;

#include "kinect.h"

// Safe release for interfaces
template<class Interface>
inline void SafeRelease(Interface *& pInterfaceToRelease)
{
	if (pInterfaceToRelease != NULL)
	{
		pInterfaceToRelease->Release();
		pInterfaceToRelease = NULL;
	}
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
struct RGBA {
	unsigned char r, g, b, a;
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


	jitmat<char> player_mat;
	jitmat<vec3> cloud_mat;
	jitmat<vec2> rectify_mat, tmp_mat;
	jitmat<vec3> skel_mat;
	jitmat<uint32_t> depth_mat;
	jitmat<ARGB> rgb_mat;
	int hasColorMap;
	int capturing;
	int new_depth_data, new_rgb_data;
	t_systhread capture_thread;
	t_systhread_mutex depth_mutex;
	IKinectSensor * device;
	// Color reader
	IColorFrameReader*      m_pColorFrameReader;
	RGBQUAD * rgb_buffer;

	// attrs
	int unique, usecolor, align_depth_to_color, uselock;
	int player, skeleton, seated, near_mode, audio, high_quality_color;
	int skeleton_smoothing;
	int device_count;
	int timeout;
	vec2 rgb_focal, rgb_center;
	vec2 rgb_radial, rgb_tangential;
	vec3 position;
	vec4 orientation;
	quat orientation_glm;
	t_symbol * serial;

	kinect2() {
		
		outlet_msg = outlet_new(&ob, 0);
		outlet_skeleton = outlet_new(&ob, 0);
		outlet_player = outlet_new(&ob, "jit_matrix");
		outlet_rgb = outlet_new(&ob, "jit_matrix");
		outlet_depth = outlet_new(&ob, "jit_matrix");
		outlet_cloud = outlet_new(&ob, "jit_matrix");

		depth_mat.init(1, _jit_sym_long, cDepthWidth, cDepthHeight);
		rgb_mat.init(4, _jit_sym_char, cColorWidth, cColorHeight);

		device = 0;
		usecolor = 1;
		unique = 1;
	}

	~kinect2() {
		close();
	}

	void open(t_symbol *s, long argc, t_atom * argv) {
		t_atom a[1];

		if (device) {
			object_warn(&ob, "device already opened");
			return;
		}

		HRESULT result = 0;

		result = GetDefaultKinectSensor(&device);
		if (result != S_OK) {
			// TODO: get meaningful error string from error code
			error("Kinect for Windows could not initialize.");
			return;
		}

		hasColorMap = 0;
		long priority = 10; // maybe increase?
		if (systhread_create((method)&capture_threadfunc, this, 0, priority, 0, &capture_thread)) {
			object_error(&ob, "Failed to create capture thread.");
			capturing = 0;
			close();
			return;
		}

	}

	static void *capture_threadfunc(void *arg) {
		kinect2 *x = (kinect2 *)arg;
		x->run();
		systhread_exit(NULL);
		return NULL;
	}

	void run() {
		if (!device) return;

		// Initialize the Kinect and get the color reader
		IColorFrameSource* pColorFrameSource = NULL;

		HRESULT hr = device->Open();

		post("device opened");

		if (SUCCEEDED(hr))
		{
			hr = device->get_ColorFrameSource(&pColorFrameSource);
		}

		if (SUCCEEDED(hr))
		{
			hr = pColorFrameSource->OpenReader(&m_pColorFrameReader);
		}
		capturing = 1;
		post("starting processing");
		while (capturing) {

			if (usecolor) processColor();
			//processDepth();
			//if (skeleton) pollSkeleton();
		}
		post("finished processing");

		SafeRelease(pColorFrameSource);
	}

	void processColor() {
		if (!device) return;
		if (!m_pColorFrameReader) return;
		
		IColorFrame* pColorFrame = NULL;
		HRESULT hr = m_pColorFrameReader->AcquireLatestFrame(&pColorFrame);
		if (SUCCEEDED(hr)) {
			INT64 nTime = 0;
			IFrameDescription* pFrameDescription = NULL;
			int nWidth = 0;
			int nHeight = 0;
			ColorImageFormat imageFormat = ColorImageFormat_None;
			UINT nBufferSize = 0;
			RGBQUAD *src = NULL;

			hr = pColorFrame->get_RelativeTime(&nTime);
			if (SUCCEEDED(hr)) {
				hr = pColorFrame->get_FrameDescription(&pFrameDescription);
			}
			if (SUCCEEDED(hr)) {
				hr = pFrameDescription->get_Width(&nWidth);
			}
			if (SUCCEEDED(hr)) {
				hr = pFrameDescription->get_Height(&nHeight);
			}
			if (SUCCEEDED(hr)) {
				hr = pColorFrame->get_RawColorImageFormat(&imageFormat);
			}

			if (imageFormat != ColorImageFormat_Bgra)
			{
				if (!rgb_buffer) {
					rgb_buffer = new RGBQUAD[nWidth * nHeight];
				}

				//post("image format %d", imageFormat);
				//error("not brga");
				nBufferSize = nWidth * nHeight * sizeof(RGBQUAD);
				hr = pColorFrame->CopyConvertedFrameDataToArray(nBufferSize, reinterpret_cast<BYTE*>(rgb_buffer), ColorImageFormat_Rgba);
				if (FAILED(hr)) {
					error("failed to convert image");
					return;
				}

				src = rgb_buffer;
			}


			hr = pColorFrame->AccessRawUnderlyingBuffer(&nBufferSize, reinterpret_cast<BYTE**>(&src));
			ARGB * dst = (ARGB *)rgb_mat.back;
			int cells = nWidth * nHeight;

			if (1) { //align_depth_to_color) {
				for (int i = 0; i < cells; ++i) {
					dst[i].r = src[i].rgbRed;
					dst[i].g = src[i].rgbGreen;
					dst[i].b = src[i].rgbBlue;
				}
			}
			else {
				/*
				// align color to depth:
				//std::fill(dst, dst + cells, RGB(0, 0, 0));
				for (int i = 0; i < cells; ++i) {
					int c = colorCoordinates[i * 2];
					int r = colorCoordinates[i * 2 + 1];
					if (c >= 0 && c < KINECT_DEPTH_WIDTH
						&& r >= 0 && r < KINECT_DEPTH_HEIGHT) {
						// valid location: depth value:
						int idx = r*KINECT_DEPTH_WIDTH + c;
						dst[i].r = src[idx].r;
						dst[i].g = src[idx].g;
						dst[i].b = src[idx].b;
					}
				}*/
			}

			new_rgb_data = 1;

		}

		
	}

	void close() {
		// done with color frame reader
		SafeRelease(m_pColorFrameReader);
		// close the Kinect Sensor
		if (device)
		{
			device->Close();
			SafeRelease(device);
			device = 0;
		}
	}


	void bang() {
		if (usecolor && (new_rgb_data || unique == 0)) {
			outlet_anything(outlet_rgb, _jit_sym_jit_matrix, 1, rgb_mat.name);
			new_rgb_data = 0;
		}
		if (new_depth_data || unique == 0) {
			//if (skeleton) outputSkeleton();
			if (player) outlet_anything(outlet_player, _jit_sym_jit_matrix, 1, player_mat.name);
			if (uselock) systhread_mutex_lock(depth_mutex);
			//outlet_anything(outlet_depth, _jit_sym_jit_matrix, 1, depth_mat.name);
			//outlet_anything(outlet_cloud, _jit_sym_jit_matrix, 1, cloud_mat.name);
			if (uselock) systhread_mutex_unlock(depth_mutex);
			new_depth_data = 0;
		}
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
	initialize_jitlib();

	t_class *c;

	c = class_new("kinect2", (method)kinect_new, (method)kinect_free, (long)sizeof(kinect2), 0L, A_GIMME, 0);
	class_addmethod(c, (method)kinect_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)kinect_open, "open", A_GIMME, 0);
	class_addmethod(c, (method)kinect_bang, "bang", 0);
	class_addmethod(c, (method)kinect_close, "close", 0);

	class_register(CLASS_BOX, c);
	max_class = c;

}

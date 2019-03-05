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
		//if (back) delete[] back;
	}

	void init(int planecount, t_symbol * type, int width, int height) {
		w = width;
		h = height;
		dim = vec2(w, h);

		t_jit_matrix_info info;
		jit_matrix_info_default(&info);
		mat = (t_object *)jit_object_new(_jit_sym_jit_matrix, &info);
		if (!mat) {
			post("failed to allocate matrix");
			return;
		}

		jit_object_method(mat, _jit_sym_getinfo, &info);

		info.planecount = planecount;
		info.type = type; 
		info.dimcount = 2;
		info.dim[0] = w;
		info.dim[1] = h;
		info.dimstride[0] = sizeof(celltype);
		info.dimstride[1] = info.dimstride[0] * dim[0];
		info.size = info.dimstride[1] * info.dim[1];
		info.flags = JIT_MATRIX_DATA_REFERENCE | JIT_MATRIX_DATA_PACK_TIGHT;// | JIT_MATRIX_DATA_FLAGS_USE;
		jit_object_method(mat, gensym("freedata"));
		jit_object_method(mat, _jit_sym_setinfo_ex, &info);  //?

		back = (celltype *)malloc(w * h * sizeof(celltype)); // new celltype[planecount * w * h];
		jit_object_method(mat, _jit_sym_data, back);

		sym = jit_symbol_unique();
		mat = (t_object *)jit_object_method(mat, _jit_sym_register, sym);
		atom_setsym(name, sym);
		
		/*w = width;
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

		dim = vec2(w, h);*/
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

static t_symbol * ps_vertex_matrix;
static t_symbol * ps_normal_matrix;
static t_symbol * ps_texcoord_matrix;
static t_symbol * ps_index_matrix;
static t_class * max_class = 0;
static const int        cDepthWidth = 512;
static const int        cDepthHeight = 424;
static const int        cColorWidth = 1920;
static const int        cColorHeight = 1080;
static const int NUM_FRAMES = 8;

class kinect2 {
public:

	struct FrameData {
		glm::vec3 cloud[cDepthWidth * cDepthHeight];
	};

	t_object ob; // max objkinectt, must be first!
	// outlets:
	//void *		outlet_cloud;
	//void *		outlet_uv;
	void *		outlet_rgb;
	void *		outlet_colour_cloud;
	void *		outlet_depth;
	void *		outlet_mesh;
	void *		outlet_player;
	void *		outlet_skeleton;
	void *		outlet_msg;

	t_systhread_mutex mlock;

	jitmat<ARGB> rgb_mat;
	jitmat<uint32_t> depth_mat;
	jitmat<vec3> cloud_mat;
	jitmat<vec3> normal_mat;
	jitmat<vec2> uv_mat; // texture coordinates to get RGB for each cloud coordinate
	jitmat<uint32_t> index_mat; // indices for stitched matrix

	jitmat<vec3> colour_cloud_mat;

	jitmat<vec2> rectify_mat, tmp_mat;
	jitmat<vec3> skel_mat;
	jitmat<char> player_mat;
	int hasColorMap;
	int capturing;
	int new_depth_data = 0, new_rgb_data = 0, new_cloud_data = 0, new_uv_data = 0, new_indices_data = 0, new_colour_cloud_data = 0;
	t_systhread capture_thread;
	t_systhread_mutex depth_mutex;
	IKinectSensor * device;

	FrameData frames[NUM_FRAMES];
	int currentFrame = 0;

	// multi reader
	IMultiSourceFrameReader* m_reader;   // Kinect data source
	ICoordinateMapper* m_mapper;         // Converts between depth, color, and 3d coordinates
	RGBQUAD * m_rgb_buffer;
	UINT16 * m_depth_buffer;
	vec3 * m_cloud_buffer;

	// attrs
	int stitch, autonormals;
	int unique, use_colour, use_depth, use_colour_cloud, align_depth_to_color, uselock;
	int face_negative_z = 1;
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
	t_atom_float triangle_limit;
	t_atom_float normal_temporal_smooth;

	kinect2() {

		memset(frames, 0, sizeof(frames));
		
		outlet_msg = outlet_new(&ob, 0);
		outlet_skeleton = outlet_new(&ob, 0);
		// mesh related:
		outlet_mesh = outlet_new(&ob, "jit_matrix");
		//outlet_uv = outlet_new(&ob, "jit_matrix");
		//outlet_cloud = outlet_new(&ob, "jit_matrix");
		// depth related:
		outlet_player = outlet_new(&ob, "jit_matrix");
		outlet_depth = outlet_new(&ob, "jit_matrix");
		// colour related:
		outlet_colour_cloud = outlet_new(&ob, "jit_matrix");
		outlet_rgb = outlet_new(&ob, "jit_matrix");

		systhread_mutex_new(&mlock, 0);

		depth_mat.init(1, _jit_sym_long, cDepthWidth, cDepthHeight);
		rgb_mat.init(4, _jit_sym_char, cColorWidth, cColorHeight);
		colour_cloud_mat.init(3, _jit_sym_float32, cColorWidth, cColorHeight);

		uv_mat.init(2, _jit_sym_float32, cDepthWidth, cDepthHeight);
		cloud_mat.init(3, _jit_sym_float32, cDepthWidth, cDepthHeight);
		normal_mat.init(3, _jit_sym_float32, cDepthWidth, cDepthHeight);

		index_mat.init(1, _jit_sym_long, (cDepthWidth * cDepthHeight) * 6, 1);

		use_colour = 1;
		use_colour_cloud = 0;
		use_depth = 1;
		unique = 1;
		stitch = 1;
		autonormals = 1;
		normal_temporal_smooth = 0.1;
		face_negative_z = 1;
		triangle_limit = 0.1;

		device = 0;
		new_depth_data = new_rgb_data = new_cloud_data = new_uv_data = new_indices_data = 0;
		m_reader = nullptr;
		m_mapper = nullptr;
		m_rgb_buffer = new RGBQUAD[cColorWidth * cColorHeight];
		m_depth_buffer = new UINT16[cDepthHeight * cDepthWidth];
		m_cloud_buffer = new vec3[cDepthWidth * cDepthHeight];
	}

	~kinect2() {
		close();
		systhread_mutex_free(mlock);
		if (m_rgb_buffer) delete[] m_rgb_buffer;
		if (m_depth_buffer) delete[] m_depth_buffer;
		if (m_cloud_buffer) delete[] m_cloud_buffer;
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
		device->get_CoordinateMapper(&m_mapper);

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

		HRESULT hr = device->Open();
		if (!SUCCEEDED(hr)) {
			object_error(&ob, "failed to open device");
			return;
		}

		DWORD ftypes = 0;
		if (use_colour) ftypes |= FrameSourceTypes::FrameSourceTypes_Color;
		if (use_depth) ftypes |= FrameSourceTypes::FrameSourceTypes_Depth;
		device->OpenMultiSourceFrameReader(ftypes, &m_reader);
		

		capturing = 1;
		while (capturing) {


			//post(".");

			float zmul = face_negative_z ? -1.f : 1.f;

			IMultiSourceFrame* frame = nullptr;
			IDepthFrame* depthframe = nullptr;
			IDepthFrameReference* depthframeref = nullptr;
			IColorFrame* colorframe = nullptr;
			IColorFrameReference* colorframeref = nullptr;
			HRESULT hr = m_reader->AcquireLatestFrame(&frame);
			if (FAILED(hr)) continue;

			systhread_mutex_lock(mlock);

			/*
			if (SUCCEEDED(frame->get_FrameDescription(&frameDescription))) {
			frameDescription->get_HorizontalFieldOfView(&this->horizontalFieldOfView));
			frameDescription->get_VerticalFieldOfView(&this->verticalFieldOfView));
			frameDescription->get_DiagonalFieldOfView(&this->diagonalFieldOfView));
			}
			*/

			if (use_colour && SUCCEEDED(frame->get_ColorFrameReference(&colorframeref)) && SUCCEEDED(colorframeref->AcquireFrame(&colorframe))) {
				static const int nCells = cColorWidth * cColorHeight;
				RGBQUAD *src = m_rgb_buffer;
				HRESULT hr = colorframe->CopyConvertedFrameDataToArray(nCells * sizeof(RGBQUAD), reinterpret_cast<BYTE*>(src), ColorImageFormat_Bgra);
				if (SUCCEEDED(hr)) {
					ARGB * dst = (ARGB *)rgb_mat.back;
					for (int i = 0; i < nCells; ++i) {
						dst[i].r = src[i].rgbRed;
						dst[i].g = src[i].rgbGreen;
						dst[i].b = src[i].rgbBlue;
						dst[i].a = 255;
						new_rgb_data = 1;
					}
				}
			}
			SafeRelease(colorframe);
			SafeRelease(colorframeref);

			if (use_depth && SUCCEEDED(frame->get_DepthFrameReference(&depthframeref)) && SUCCEEDED(depthframeref->AcquireFrame(&depthframe))) {

				int nextFrame = (currentFrame + 1) % NUM_FRAMES;
				FrameData& frame = frames[nextFrame];


				INT64 relativeTime = 0;
				depthframe->get_RelativeTime(&relativeTime);
				UINT capacity;
				UINT16 * src; // depth in mm
				hr = depthframe->AccessUnderlyingBuffer(&capacity, &src);
				if (SUCCEEDED(hr)) {
					uint32_t * dst = (uint32_t *)depth_mat.back;
					// make a local copy of depth:
					for (UINT i = 0; i < capacity; i++) {
						m_depth_buffer[i] = src[i];
						dst[i] = src[i];
					}
					// done with depth frame already
					SafeRelease(depthframe);
					SafeRelease(depthframeref);
					new_depth_data = 1; 

					// TODO: 
					// map to an intermediate buffer instead
					// since we want to try to limit the amount of data spat out
					// we *can* do this just the same way we do with the index buffer
					// by noting each vertex whether it is valid or not
					// and the cloud_mat and uv_mat matrices would then be variable size
					// but then the index_mat values also need to be changed...
					// ... however it turns out that nearly all points were valid (about 90%) so this doesn't seem worth it.
					hr = m_mapper->MapDepthFrameToCameraSpace(
						cDepthWidth*cDepthHeight, m_depth_buffer,        // Depth frame data and size of depth frame
						cDepthWidth*cDepthHeight, (CameraSpacePoint *)m_cloud_buffer); // Output CameraSpacePoint array and size
					if (SUCCEEDED(hr)) {
						glm::vec3 * p = frame.cloud; //.cloud_mat.back;


						// copy into cloud, with transform
						if (face_negative_z) {
							glm::vec3 * o = m_cloud_buffer;
							for (int i = 0; i<capacity; i++) {
								p->x = -o->x;
								p->y =  o->y;
								p->z = -o->z;
								p++;
								o++;
							}
						}
						else {
							glm::vec3 * o = m_cloud_buffer;
							for (int i = 0; i<capacity; i++) {
								p->x =  o->x;
								p->y =  o->y;
								p->z =  o->z;
								p++;
								o++;
							}
						}

						new_cloud_data = 1;

						// let's look at how many of these are valid points:
						int valid = 0;
				
						if (use_colour) {
							// TODO dim or dim-1? add 0.5 for center of pixel?
							vec2 uvscale = vec2(1.f / cColorWidth, 1.f / cColorHeight);
							// iterate the points to get UVs
							for (UINT i = 0, y = 0; y < cDepthHeight; y++) {
								for (UINT x = 0; x < cDepthWidth; x++, i++) {
									DepthSpacePoint dp = { (float)x, (float)y };
									UINT16 depth_mm = m_depth_buffer[i];
									vec2 uvpt;
									m_mapper->MapDepthPointToColorSpace(dp, depth_mm, (ColorSpacePoint *)(&uvpt));
									uv_mat.back[i] = uvpt * uvscale;
									if (depth_mm > 0 && depth_mm < 8000) {
										valid++;
									}
								}
							}
							new_uv_data = 1;
						}

						// I found that typically about 195,000 out of 217,000 points were valid
						//post("valid %d\n", valid);

						if (stitch > 0) {
							int steps = MIN(stitch, cDepthHeight/2);
							float facesMaxLength = steps * triangle_limit;
							vec3 * vertices = cloud_mat.back;
							vec3 * normals = normal_mat.back;
							uint32_t * indices = index_mat.back;
							int count = 0;
							// maybe this couldbe faster?
							for (int j = 0; j < cDepthHeight - steps; j += steps) {
								for (int i = 0; i < cDepthWidth - steps; i += steps) {
									auto topLeft = cDepthWidth * j + i;
									auto topRight = topLeft + steps;
									auto bottomLeft = topLeft + cDepthWidth * steps;
									auto bottomRight = bottomLeft + steps;

									const vec3 & vTL = vertices[topLeft];
									const vec3 & vTR = vertices[topRight];
									const vec3 & vBL = vertices[bottomLeft];
									const vec3 & vBR = vertices[bottomRight];

									// cw, ccw
									// cw, ccw
									glm::vec3 nTL = glm::normalize(glm::cross(vBL - vTL, vTR - vTL));
									glm::vec3 nBR = glm::normalize(glm::cross(vBL - vTR, vBR - vTR));

									bool okTL = 0;
									//upper left triangle
									if (vTL.z*zmul > 0 && vTR.z*zmul > 0 && vBL.z*zmul > 0
										&& abs(vTL.z - vTR.z) < facesMaxLength
										&& abs(vTL.z - vBL.z) < facesMaxLength) {
										okTL = 1;

										*indices++ = topLeft;
										*indices++ = bottomLeft; // shared
										*indices++ = topRight; //shared
										count += 3;

										normals[topLeft] = nTL;
									}

									//bottom right triangle
									if (vBR.z*zmul > 0 && vTR.z*zmul > 0 && vBL.z*zmul > 0
										&& abs(vBR.z - vTR.z) < facesMaxLength
										&& abs(vBR.z - vBL.z) < facesMaxLength) {
										*indices++ = topRight; //shared
										*indices++ = bottomLeft;//shared
										*indices++ = bottomRight;
										count += 3;

										normals[bottomRight] = nBR;
										if (okTL) {
											glm::vec3 navg = glm::normalize(nTL + nBR);
											normals[bottomLeft] = navg;
											normals[topRight] = navg;
										}
										else {
											normals[bottomLeft] = nBR;
											normals[topRight] = nBR;
										}
									}
								}
							}

							// somehow update matrix
							if (count >= 6) {
								// pretend that the output matrix has changed size (even though the underlying pointer is the same)
								t_jit_matrix_info index_info;
								jit_object_method(index_mat.mat, _jit_sym_getinfo, &index_info);
								index_info.dim[0] = count;
								index_info.dimstride[1] = index_info.dimstride[0] * count;
								jit_object_method(index_mat.mat, _jit_sym_setinfo_ex, &index_info);

								new_indices_data = 1;
							}
							else {

								object_error(&ob, "not enough indices");
							}
						}
					}
				}

				if (use_colour_cloud) {
					// I'd like to also output a matrix giving a 3D position for each camera space point
					hr = m_mapper->MapColorFrameToCameraSpace(
						cDepthWidth*cDepthHeight, m_depth_buffer,        // Depth frame data and size of depth frame
						cColorWidth*cColorHeight, (CameraSpacePoint *)colour_cloud_mat.back); // Output CameraSpacePoint array and size)
					if (SUCCEEDED(hr)) {
						if (face_negative_z) {
							int n = cColorWidth*cColorHeight;
							glm::vec3 * p = colour_cloud_mat.back;
							while (n--) {
								p->z = -p->z;
								p->x = -p->x;
								p++;
							}
						}
						new_colour_cloud_data = 1;
					}
				}

				currentFrame = nextFrame;
			}

			SafeRelease(depthframe);
			SafeRelease(depthframeref);

			systhread_mutex_unlock(mlock);
		}

		SafeRelease(m_reader);
	}

	void close() {
		if (capturing) {
			capturing = 0;
			unsigned int retval;
			systhread_join(capture_thread, &retval);
		}
		if (device) {
			device->Close();
			SafeRelease(device);
		}
	}


	void bang() {


		//if (systhread_mutex_trylock(mlock) == 0) {
			if (use_colour && (new_rgb_data || unique == 0)) {
				outlet_anything(outlet_rgb, _jit_sym_jit_matrix, 1, rgb_mat.name);
				new_rgb_data = 0;
			}

			//if (skeleton) outputSkeleton();
			//if (player) outlet_anything(outlet_player, _jit_sym_jit_matrix, 1, player_mat.name);
			if (use_depth) {
				FrameData& frame = frames[currentFrame];
				memcpy(cloud_mat.back, &frame.cloud, sizeof(frame.cloud));

				if (stitch > 0) {

					if (new_uv_data || unique == 0) {
						outlet_anything(outlet_mesh, ps_texcoord_matrix, 1, uv_mat.name);
						new_uv_data = 0;
					}
					if (new_indices_data || unique == 0) {
						outlet_anything(outlet_mesh, ps_index_matrix, 1, index_mat.name);
						outlet_anything(outlet_mesh, ps_normal_matrix, 1, normal_mat.name);
						outlet_anything(outlet_mesh, ps_vertex_matrix, 1, cloud_mat.name);
						new_indices_data = 0;
					}
				} else {
					if (new_cloud_data || unique == 0) {
						outlet_anything(outlet_mesh, ps_vertex_matrix, 1, cloud_mat.name);
					}
				}
				if (new_depth_data || unique == 0) {
					//if (uselock) systhread_mutex_lock(depth_mutex);
					outlet_anything(outlet_depth, _jit_sym_jit_matrix, 1, depth_mat.name);
					//if (uselock) systhread_mutex_unlock(depth_mutex);
					new_depth_data = 0;
				}
				if (new_colour_cloud_data || unique == 0) {
					outlet_anything(outlet_colour_cloud, _jit_sym_jit_matrix, 1, colour_cloud_mat.name);
					new_cloud_data = 0;
				}
				
			}
			//systhread_mutex_unlock(mlock);
		//}
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
		switch (a) {
		case 0: sprintf(s, "colour (jit_matrix)"); break;
		case 1: sprintf(s, "3D cloud at colour dim (jit_matrix)"); break;
		case 2: sprintf(s, "depth (jit_matrix)"); break;
		case 3: sprintf(s, "player at depth dim (jit_matrix)"); break;
		case 4: sprintf(s, "matrices for jit.gl.mesh (jit_matrix)"); break;
		case 5: sprintf(s, "skeleton"); break;
		default: sprintf(s, "other messages"); break;
		}
	}
}

void ext_main(void *r)
{
	initialize_jitlib();

	ps_vertex_matrix = gensym("vertex_matrix");
	ps_normal_matrix = gensym("normal_matrix");
	ps_texcoord_matrix = gensym("texcoord_matrix");
	ps_index_matrix = gensym("index_matrix");

	t_class *c;

	c = class_new("kinect2", (method)kinect_new, (method)kinect_free, (long)sizeof(kinect2), 0L, A_GIMME, 0);
	class_addmethod(c, (method)kinect_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)kinect_open, "open", A_GIMME, 0);
	class_addmethod(c, (method)kinect_bang, "bang", 0);
	class_addmethod(c, (method)kinect_close, "close", 0);



	CLASS_ATTR_LONG(c, "unique", 0, kinect2, unique);
	CLASS_ATTR_STYLE(c, "unique", 0, "onoff");

	CLASS_ATTR_LONG(c, "use_depth", 0, kinect2, use_depth);
	CLASS_ATTR_STYLE(c, "use_depth", 0, "onoff");
	CLASS_ATTR_LONG(c, "use_colour", 0, kinect2, use_colour);
	CLASS_ATTR_STYLE(c, "use_colour", 0, "onoff");
	CLASS_ATTR_LONG(c, "use_colour_cloud", 0, kinect2, use_colour_cloud);
	CLASS_ATTR_STYLE(c, "use_colour_cloud", 0, "onoff");
	CLASS_ATTR_LONG(c, "face_negative_z", 0, kinect2, face_negative_z);
	CLASS_ATTR_STYLE(c, "face_negative_z", 0, "onoff");

	CLASS_ATTR_FLOAT(c, "triangle_limit", 0, kinect2, triangle_limit);

	CLASS_ATTR_LONG(c, "stitch", 0, kinect2, stitch);

	class_register(CLASS_BOX, c);
	max_class = c;

}

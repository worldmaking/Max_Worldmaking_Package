// a bunch of likely Max includes:
#include "al_max.h"

// used in ext_main
// a dummy call just to get jitter initialized, otherwise _jit_sym_* symbols etc. won't be populated
// (normally this isn't necessary because jit_class_new does it, but I"m not using jit_class_new)
void initialize_jitlib() {
	t_jit_matrix_info info;
	jit_matrix_info_default(&info);
	info.type = gensym("char");
	object_free(jit_object_new(gensym("jit_matrix"), &info));
}

template <typename celltype>
struct jitmat {
	
	t_object * mat;
	t_symbol * sym;
	t_atom name[1];
	int w, h;
	glm::vec2 dim;
	
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
		dim = glm::vec2(w, h);
		
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
	
	celltype sample(glm::vec2 texcoord) {
		glm::vec2 t = texcoord*(dim - 1.f);
		glm::vec2 t0 = glm::vec2(floor(t.x), floor(t.y));
		glm::vec2 t1 = t0 + 1.f;
		glm::vec2 ta = t - t0;
		celltype v00 = read_clamp(t0.x, t0.y);
		celltype v01 = read_clamp(t1.x, t0.y);
		celltype v10 = read_clamp(t0.x, t1.y);
		celltype v11 = read_clamp(t1.x, t1.y);
		return glm::mix(glm::mix(v00, v01, ta.x), glm::mix(v10, v11, ta.x), ta.y);
	}
};

static t_symbol * ps_vertex_matrix;
static t_symbol * ps_normal_matrix;
static t_symbol * ps_texcoord_matrix;
static t_symbol * ps_index_matrix;
static t_class * max_class = 0;


#ifdef __GNUC__
#include <stdint.h>
#else
#include "stdint.h"
#endif

#include <vector>
#include <algorithm>
#include <signal.h>
#include <thread>

#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/registration.h>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/logger.h>

#define KINECT_FRAME_BUFFERS 4
#define KINECT_MAX_DEVICES 2

/*
 We're going to want a cloud source
 this could be from a device stream, or from a disk stream
 
 one way of doing this is to mmap a hunk of space for streaming the frames into, as a kind of ringbuffer
 
 */

struct CloudPoint {
	glm::vec3 location;
	glm::vec3 color;
	glm::vec2 texCoord; // the location within the original image
};

struct ColorPoint {
	uint8_t b, g, r;
};

typedef uint16_t DepthPoint;

static const int cDepthWidth = 512;
static const int cDepthHeight = 424;
static const int cColorWidth = 1920;
static const int cColorHeight = 1080;

/*
 Colour and depth frames are stored separately, as they can be received at different rates
 And moreover, the depth data tends to come in at a lower latency than colour (by 0-3 frames)
 
 Maybe it would be nice to provide an interface to synchronize them, particularly when recording data to disk
 Even for realtime, sometimes it would be preferable to admit more latency to ensure depth/image data correspond
 (That means, picking an older depth frame to match the closest colour frame timestamp)
 */

struct ColourFrame {
	// RGB colour image at full resolution
	ColorPoint color[cColorWidth*cColorHeight];
	// XYZ cloud at colour resolution
	glm::vec3 cloud[cColorWidth*cColorHeight];
};

struct CloudFrame {
	// depth data, in mm. bad points will be marked with zeroes.
	uint16_t depth[cDepthWidth*cDepthHeight];
	// depth points mapped to 3D space
	// coordinate system: right-handed (like common OpenGL use)
	// scale: meters
	// origin: the Kinect IR sensor
	// orientation: +Z in front of kinect, +Y above it, and +X away from the RGB camera
	// i.e. RHS oriented looking toward the camera.
	// to re-orient the space from the camera POV, flip sign on X and Z axes.
	glm::vec3 xyz[cDepthWidth*cDepthHeight];
	// uv texture coordinates for each depth point to pick from the color image
	glm::vec2 uv[cDepthWidth*cDepthHeight];
	// color for this point
	glm::vec3 rgb[cDepthWidth*cDepthHeight];
	
	//uint64_t pointCount;
	int64_t timeStamp;
};

struct CloudDevice {
	int use_colour = 1;
	int use_uv = 1;
	int capturing = 0;
	
	std::string serial;
	int id = 0;
	
	glm::mat4 cloudTransform = glm::mat4(1.);
	
	std::thread kinect_thread;
	std::vector<CloudFrame> cloudFrames = std::vector<CloudFrame>(KINECT_FRAME_BUFFERS);
	std::vector<ColourFrame> colourFrames = std::vector<ColourFrame>(KINECT_FRAME_BUFFERS);
	int lastCloudFrame = 0;
	int lastColourFrame = 0;
	
	FILE * recordFD;
	
	// the most recently completed frame:
	const CloudFrame& cloudFrame() const {
		return cloudFrames[lastCloudFrame];
	}
	// the most recently completed frame:
	const ColourFrame& colourFrame() const {
		return colourFrames[lastColourFrame];
	}
	
	struct ColourPacket {
		uint8_t r, g, b, x;
	};
	
	libfreenect2::Freenect2Device * dev = 0;
	libfreenect2::PacketPipeline *pipeline = 0;
	
	int kinect_thread_fun() {
		
		object_post((t_object *)this, "hello from freenect thread for device %s", dev->getSerialNumber().c_str());
		libfreenect2::Freenect2Device::Config config;
		config.MinDepth = 0.5f;
		config.MaxDepth = 6.f;
		// Remove pixels on edges because ToF cameras produce noisy edges.
		config.EnableEdgeAwareFilter = true;
		///< Remove some "flying pixels".
		config.EnableBilateralFilter = true;
		dev->setConfiguration(config);
		
		int types = libfreenect2::Frame::Ir
		| libfreenect2::Frame::Depth;
		if (use_colour) types |= libfreenect2::Frame::Color;
		libfreenect2::SyncMultiFrameListener listener(types);
		libfreenect2::FrameMap frames;
		dev->setColorFrameListener(&listener);
		dev->setIrAndDepthFrameListener(&listener);
		
		if (use_colour) {
			if (!dev->start()) return -1;
		} else {
			if (!dev->startStreams(use_colour, true)) return -1;
		}
		
		libfreenect2::Registration* registration = new libfreenect2::Registration(dev->getIrCameraParams(), dev->getColorCameraParams());
		libfreenect2::Frame undistorted(cDepthWidth, cDepthHeight, 4);
		libfreenect2::Frame  registered(cDepthWidth, cDepthHeight, 4);

		
		object_post((t_object *)this, "freenect ready");
		
		size_t framecount = 0;
		while(capturing) {
			
			if (!listener.waitForNewFrame(frames, 10*1000)) { // 10 sconds
				//std::cout << "timeout!" << std::endl;
				break;
			}
			
			libfreenect2::Frame *rgb = frames[libfreenect2::Frame::Color];
			libfreenect2::Frame *ir = frames[libfreenect2::Frame::Ir];
			libfreenect2::Frame *depth = frames[libfreenect2::Frame::Depth];
			
			// do the registration to rectify the depth (and map to colour, if used):
			if (use_colour) {
				registration->apply(rgb, depth, &undistorted, &registered);
				
				// copy colur image
				int nextColourFrame = (lastColourFrame + 1) % colourFrames.size();
				ColourFrame& colourFrame = colourFrames[nextColourFrame];
				ColorPoint * dst = colourFrame.color;
				ColourPacket * src = (ColourPacket *)rgb->data;
				
				static const int nCells = cColorWidth * cColorHeight;
				for (int i = 0; i < nCells; ++i) {
					dst[i].r = src[i].r;
					dst[i].g = src[i].g;
					dst[i].b = src[i].b;
				}

				

				/*
				*/
				
				// we finished writing, we can now share this as the next frame to read:
				//colourFrame.timeStamp = currentColorFrameTime;
				lastColourFrame = nextColourFrame;
				
			} else {
				registration->undistortDepth(depth, &undistorted);
			}
			
			// get the next frame to write into:
			int nextCloudFrame = (lastCloudFrame + 1) % cloudFrames.size();
			CloudFrame& cloudFrame = cloudFrames[nextCloudFrame];
			
			// undistorted frame contains depth in mm, as float
			const float * mmptr = (float *)undistorted.data;
			uint16_t * dptr = cloudFrame.depth;
			glm::vec3 * xyzptr = cloudFrame.xyz;
			glm::vec3 * rgbptr = cloudFrame.rgb;
			glm::vec2 * uvptr = cloudFrame.uv;
			int i = 0;
			
			// TODO dim or dim-1?
			//glm::vec2 uvscale = glm::vec2(1.f / cDepthWidth, 1.f / cDepthHeight);
			glm::vec2 uvscale = glm::vec2(1.f / cColorWidth, 1.f / cColorHeight);
			
			// copy to captureFrame:
			for (int r=0; r<cDepthHeight; r++) {
				for (int c=0; c<cDepthWidth; c++) {
					float mm = mmptr[i];
					dptr[i] = mm;
					
					glm::vec3 pt;
					float rgb = 0.f;
					//registration->getPointXYZ(&undistorted, r, c, pt.x, pt.y, pt.z);
					registration->getPointXYZRGB (&undistorted, &registered, r, c, pt.x, pt.y, pt.z, rgb);
					pt = al_fixnan(pt);
					pt.z = -pt.z; // freenect puts Z +ve, but GL expects -ve
					xyzptr[i] = transform(cloudTransform, pt);
					
					const uint8_t *cp = reinterpret_cast<uint8_t*>(&rgb);
					rgbptr[i] = glm::vec3(cp[2]/255.f, cp[1]/255.f, cp[0]/255.f);
					
					// this could be wrong?
					glm::vec2 uv;
					registration->apply(c, r, mm, uv.x, uv.y);
					uv *= uvscale;
					//uv = glm::vec2(c, r) * uvscale;
					uvptr[i] = uv;
					
					i++;
					
					//if (r == 211 && c == 253) object_post((t_object *)this, "depth mm %f point %f %f %f uv %f %f", mmptr[i], pt.x, pt.y, pt.z, uv.x, uv.y);
				}
			}
			
			// we finished writing, we can now share this as the next frame to read:
			//cloudFrame.timeStamp = currentDepthFrameTime;
			//object_post((t_object *)this, "at %d depth", currentDepthFrameTime);
			lastCloudFrame = nextCloudFrame;
			
			listener.release(frames);
		}
		
		post("bye from freenect thread for device %s", dev->getSerialNumber().c_str());
		dev->stop();
		dev->close();
		return 0;
	}

	bool isRecording() {
		return recordFD;
	}
	
	// TODO: work in progress
	bool record(bool enable) {
		if (enable) {
			if (recordFD) return true; // already recording
			
			std::string filename = "kinect2.bin";
			if (filename.empty()) return false;
			
			recordFD = fopen(filename.c_str(), "wb");
			return true;
			
		} else {
			// I guess this might be bad if currently writing a frame?
			if (recordFD) fclose(recordFD);
			recordFD = 0;
			return true;
		}
	}
	
	bool start() {
		object_post((t_object *)this, "opening cloud device %s", serial.c_str());
		if (dev == 0) {
			object_error((t_object *)this, "couldn't acquire cloud device");
			return false;
		} else {
			capturing = 1;
			kinect_thread = std::thread(&CloudDevice::kinect_thread_fun, this);
			return true;
		}
		return false;
	}
	
	void close() {
		if (capturing) {
			capturing = 0;
			kinect_thread.join();
		}
	}
};


struct CloudDeviceManager {
	CloudDevice devices[KINECT_MAX_DEVICES];
	int numDevices = 0;
	
	libfreenect2::Freenect2 freenect2;

	CloudDeviceManager() {
		reset();
	}
	
	void reset() {
		for (int i=0; i<KINECT_MAX_DEVICES; i++) {
			devices[i].close();
		}
		
		numDevices = freenect2.enumerateDevices();
		object_post((t_object *)this, "found %d freenect devices", numDevices);
		
		// sort by serial number, to make it deterministic
		std::vector<std::string> serialList;
		for (int i=0; i<numDevices; i++) {
			serialList.push_back(freenect2.getDeviceSerialNumber(i));
		}
		std::sort(std::begin(serialList), std::end(serialList));
		for (int i=0; i<numDevices; i++) {
			devices[i].id = i;
			devices[i].serial = serialList[i].c_str();
			object_post((t_object *)this, "device %d serial %s", devices[i].id, serialList[i].c_str());
		}
	}
	
	bool open(int i=0) {
		if (i >= numDevices) {
			object_error((t_object *)this, "cannot open device %d, not found", i);
			return false;
		}
		
		libfreenect2::Freenect2Device * dev;
		CloudDevice& device = devices[i];
		
		if (!device.pipeline) {
			//pipeline = new libfreenect2::CpuPacketPipeline();
			device.pipeline = new libfreenect2::OpenGLPacketPipeline();
		}
		if (device.pipeline) {
			device.dev = freenect2.openDevice(device.serial, device.pipeline);
		} else {
			device.dev = freenect2.openDevice(device.serial);
		}
		
		device.start();
	}
	
	void open_all() {
		for (int i=0; i<KINECT_MAX_DEVICES; i++) {
			open(i);
		}
	}
	
	void close(int i=0) {
		devices[i].close();
	}
	
	void close_all() {
		for (int i=0; i<KINECT_MAX_DEVICES; i++) {
			close (i);
		}
	}
};

static CloudDeviceManager * manager;

struct ARGB {
	unsigned char a, r, g, b;
};

class kinect2 {
public:
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
	
	jitmat<ARGB> rgb_mat;
	jitmat<uint32_t> depth_mat;
	jitmat<glm::vec3> cloud_mat;
	jitmat<glm::vec3> normal_mat;
	jitmat<glm::vec2> uv_mat; // texture coordinates to get RGB for each cloud coordinate
	jitmat<uint32_t> index_mat; // indices for stitched matrix
	
	jitmat<glm::vec3> colour_cloud_mat;
	
	jitmat<glm::vec2> rectify_mat, tmp_mat;
	jitmat<glm::vec3> skel_mat;
	jitmat<char> player_mat;
	int hasColorMap;
	int capturing;
	int new_depth_data, new_rgb_data, new_cloud_data, new_uv_data, new_indices_data, new_colour_cloud_data;
	
	// attrs
	int stitch, autonormals;
	int unique, use_colour, use_depth, use_colour_cloud, align_depth_to_color, uselock;
	int face_negative_z = 1;
	int player, skeleton, seated, near_mode, audio, high_quality_color;
	int skeleton_smoothing;
	int device_count;
	int timeout;
	glm::vec2 rgb_focal, rgb_center;
	glm::vec2 rgb_radial, rgb_tangential;
	glm::vec3 position;
	glm::vec4 orientation;
	glm::quat orientation_glm;
	t_symbol * serial;
	t_atom_float triangle_limit;
	t_atom_float normal_temporal_smooth;
	
	CloudDevice * device;
	int whichdevice;
	
	kinect2() {
		
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
		
		//systhread_mutex_new(&mlock, 0);
		
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
		
//		m_reader = nullptr;
//		m_mapper = nullptr;
//		m_rgb_buffer = new RGBQUAD[cColorWidth * cColorHeight];
//		m_depth_buffer = new UINT16[cDepthHeight * cDepthWidth];
//		m_cloud_buffer = new vec3[cDepthWidth * cDepthHeight];
	}
	
	~kinect2() {
		close();
		//systhread_mutex_free(mlock);
//		if (m_rgb_buffer) delete[] m_rgb_buffer;
//		if (m_depth_buffer) delete[] m_depth_buffer;
//		if (m_cloud_buffer) delete[] m_cloud_buffer;
	}
	
	void open(t_symbol *s, long argc, t_atom * argv) {
		if (!manager) {
			error("no device manager for freenect2 yet");
			manager = new CloudDeviceManager;
		}
		t_atom a[1];
		
		if (device) {
			object_warn(&ob, "device already opened");
			return;
		}
		
		whichdevice = ((unsigned)atom_getlong(argv)) % KINECT_MAX_DEVICES;
		object_post(&ob, "open device %d", whichdevice);
		
		bool res = manager->open(whichdevice);
		if (!res) {
			object_error(&ob, "failed to open device %d", whichdevice);
		}
		
		device = &manager->devices[whichdevice];
	}
	
	void close() {

		if (device) {
			device->close();
			device = 0;
		}
	}
	
	void bang() {
		if (!device) return;

		//post("lastframe %d %d", device->lastCloudFrame, device->lastColourFrame);
		
		// TODO: unique
		
		if (use_colour) {
			// the most recently completed frame:
			const ColourFrame& colourframe = device->colourFrame();

			//post("copying frame %d", device->lastColourFrame);
			
			// copy into rgbmat
			for (int y=0, i=0; y<cColorHeight; y++) {
				for (int x=0; x<cColorWidth; x++, i++) {
					const ColorPoint& src = colourframe.color[i];
					ARGB& dst = rgb_mat.back[i];
					dst.r = src.b;
					dst.g = src.g;
					dst.b = src.r;
					dst.a = 255;
				}
			}
			
			outlet_anything(outlet_rgb, _jit_sym_jit_matrix, 1, rgb_mat.name);
		}
		
		if (use_depth) {
			// the most recently completed frame:
			const CloudFrame& depthframe = device->cloudFrame();
			
			// TODO copy into mats
			/*
			 // depth data, in mm. bad points will be marked with zeroes.
			 uint16_t depth[cDepthWidth*cDepthHeight];
			 // depth points mapped to 3D space
			 // coordinate system: right-handed (like common OpenGL use)
			 // scale: meters
			 // origin: the Kinect IR sensor
			 // orientation: +Z in front of kinect, +Y above it, and +X away from the RGB camera
			 // i.e. RHS oriented looking toward the camera.
			 // to re-orient the space from the camera POV, flip sign on X and Z axes.
			 glm::vec3 xyz[cDepthWidth*cDepthHeight];
			 // uv texture coordinates for each depth point to pick from the color image
			 glm::vec2 uv[cDepthWidth*cDepthHeight];
			 // color for this point
			 glm::vec3 rgb[cDepthWidth*cDepthHeight];
			 
			 //uint64_t pointCount;
			 int64_t timeStamp;
			*/
			
			/*
			 jitmat<uint32_t> depth_mat;
			 jitmat<glm::vec3> cloud_mat;
			 jitmat<glm::vec3> normal_mat;
			 jitmat<glm::vec2> uv_mat; // texture coordinates to get RGB for each cloud coordinate
			 jitmat<uint32_t> index_mat; // indices for stitched matrix
			 
			 jitmat<glm::vec3> colour_cloud_mat;
			 
			 depth_mat.init(1, _jit_sym_long, cDepthWidth, cDepthHeight);
			 rgb_mat.init(4, _jit_sym_char, cColorWidth, cColorHeight);
			 colour_cloud_mat.init(3, _jit_sym_float32, cColorWidth, cColorHeight);
			 
			 uv_mat.init(2, _jit_sym_float32, cDepthWidth, cDepthHeight);
			 cloud_mat.init(3, _jit_sym_float32, cDepthWidth, cDepthHeight);
			 normal_mat.init(3, _jit_sym_float32, cDepthWidth, cDepthHeight);
			 
			 index_mat.init(1, _jit_sym_long, (cDepthWidth * cDepthHeight) * 6, 1);
			 */
			
			int elems = cDepthWidth*cDepthHeight;
			for (int i=0; i<elems; i++) {
				depth_mat.back[i] = depthframe.depth[i];
				uv_mat.back[i] = depthframe.uv[i];
				colour_cloud_mat.back[i] = depthframe.rgb[i];
				cloud_mat.back[i] = depthframe.xyz[i];
			}
			
			
			outlet_anything(outlet_mesh, ps_texcoord_matrix, 1, uv_mat.name);
			outlet_anything(outlet_mesh, ps_vertex_matrix, 1, cloud_mat.name);
			outlet_anything(outlet_depth, _jit_sym_jit_matrix, 1, depth_mat.name);
			outlet_anything(outlet_colour_cloud, _jit_sym_jit_matrix, 1, colour_cloud_mat.name);
			
			if (stitch > 0) {
				float zmul = face_negative_z ? -1.f : 1.f;
				
				int steps = MIN(stitch, cDepthHeight/2);
				float facesMaxLength = steps * triangle_limit;
				glm::vec3 * vertices = cloud_mat.back;
				glm::vec3 * normals = normal_mat.back;
				uint32_t * indices = index_mat.back;
				int count = 0;
				// maybe this couldbe faster?
				for (int j = 0; j < cDepthHeight - steps; j += steps) {
					for (int i = 0; i < cDepthWidth - steps; i += steps) {
						auto topLeft = cDepthWidth * j + i;
						auto topRight = topLeft + steps;
						auto bottomLeft = topLeft + cDepthWidth * steps;
						auto bottomRight = bottomLeft + steps;
						
						const glm::vec3 & vTL = vertices[topLeft];
						const glm::vec3 & vTR = vertices[topRight];
						const glm::vec3 & vBL = vertices[bottomLeft];
						const glm::vec3 & vBR = vertices[bottomRight];
						
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
					
					
					outlet_anything(outlet_mesh, ps_index_matrix, 1, index_mat.name);
					outlet_anything(outlet_mesh, ps_normal_matrix, 1, normal_mat.name);
				}
				else {
					
					//object_error(&ob, "not enough indices");
				}
				
			}
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
	
	c = class_new("freenect2", (method)kinect_new, (method)kinect_free, (long)sizeof(kinect2), 0L, A_GIMME, 0);
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

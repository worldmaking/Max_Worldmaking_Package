/*

Might be important:
https://forums.oculus.com/viewtopic.php?f=20&t=24361&p=283304&hilit=opengl+0.6#p283304
Might be useful:
https://forums.oculus.com/viewtopic.php?f=39&t=91&p=277330&hilit=opengl+0.6#p277330

*/

#include "al_max.h"

struct jittex {
	void * tex;
	t_symbol * sym;
	t_atom_long dim[2];

	jittex(t_atom_long w = 640, t_atom_long h = 480) {
		tex = 0;
		sym = _jit_sym_nothing;
		dim[0] = w;
		dim[1] = h;
	}

	~jittex() {
		dest_closing();
	}

	void resize(t_atom_long w, t_atom_long h) {
		if (dim[0] != w && dim[1] != h) {
			dim[0] = w;
			dim[1] = h;
			if (tex) {
				if (jit_attr_setlong_array(tex, _jit_sym_dim, 2, dim)) object_error(nullptr, "failed to set mirror dim");
			}
		}


	}

	bool dest_changed(t_symbol * context) {
		if (tex) dest_closing();

		// create a jit.gl.texture to copy mirror to
		tex = jit_object_new(gensym("jit_gl_texture"), context);
		if (!tex) return false;
		// set texture attributes.
		sym = jit_attr_getsym(tex, gensym("name"));
		if (jit_attr_setlong_array(tex, _jit_sym_dim, 2, dim)) object_error(nullptr, "failed to set mirror dim");
		if (jit_attr_setlong(tex, gensym("rectangle"), 1)) object_error(nullptr, "failed to set mirror rectangle mode");
		//jit_attr_setsym(tex, gensym("defaultimage"), gensym("black"));
		//jit_attr_setlong(outtexture, gensym("flip"), 0);
		return true;
	}

	bool dest_closing() {
		if (tex) {
			jit_object_free(tex);
			tex = 0;
		}
		return true;
	}

	long glid() {
		return jit_attr_getlong(tex, gensym("glid"));
	}

	bool bind(void * ob3d) {
		t_jit_gl_drawinfo drawInfo;
		if (jit_gl_drawinfo_setup(ob3d, &drawInfo)) return false;
		jit_gl_bindtexture(&drawInfo, sym, 0);
		return true;
	}
	bool unbind(void * ob3d) {
		t_jit_gl_drawinfo drawInfo;
		if (jit_gl_drawinfo_setup(ob3d, &drawInfo)) return false;
		jit_gl_unbindtexture(&drawInfo, sym, 0);
		return true;
	}
};

template <typename celltype>
struct jitmat {

	t_object * mat;
	t_symbol * sym;
	t_atom name[1];
	int w, h;

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
	}
};

// The OpenVR SDK:
#include "openvr.h"

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::quat;
using glm::mat4;

glm::mat4 mat4_from_openvr(const vr::HmdMatrix34_t &m) {
	return glm::mat4(
		m.m[0][0], m.m[1][0], m.m[2][0], 0.0,
		m.m[0][1], m.m[1][1], m.m[2][1], 0.0,
		m.m[0][2], m.m[1][2], m.m[2][2], 0.0,
		m.m[0][3], m.m[1][3], m.m[2][3], 1.0f
	);
}

glm::mat4 mat4_from_openvr(const vr::HmdMatrix44_t &m) {
	return glm::mat4(
		m.m[0][0], m.m[1][0], m.m[2][0], m.m[3][0],
		m.m[0][1], m.m[1][1], m.m[2][1], m.m[3][1],
		m.m[0][2], m.m[1][2], m.m[2][2], m.m[3][2],
		m.m[0][3], m.m[1][3], m.m[2][3], m.m[3][3]
	);
}

static t_class * max_class = 0;

static t_symbol * ps_quat;
static t_symbol * ps_pos;
static t_symbol * ps_viewport;
static t_symbol * ps_frustum;
static t_symbol * ps_warning;
static t_symbol * ps_glid;
static t_symbol * ps_jit_gl_texture;

static t_symbol * ps_trigger;
static t_symbol * ps_trackpad;
static t_symbol * ps_buttons;
static t_symbol * ps_velocity;
static t_symbol * ps_angular_velocity;

static t_symbol * ps_tracked_position;
static t_symbol * ps_tracked_quat;

static t_symbol * trk_velocity;
static t_symbol * trk_angular_velocity;
static t_symbol * trk_tracked_position;
static t_symbol * trk_tracked_quat;

class htcvive {
public:

	t_object ob; // must be first!
	void * ob3d;
	void * outlet_msg;
	void * outlet_video;
	void * outlet_tracking;
	void * outlet_node;
	void * outlet_eye[2];
	void * outlet_controller[2];
	void * outlet_tex;

	t_symbol * dest_name;
	t_symbol * intexture;

	t_symbol * outname;
	void * outtexture;
	t_atom_long outdim[2];



	// attrs:
	float near_clip, far_clip;
	int mirror;
	int use_camera;

	// OpenGL
	GLuint fbo, fbomirror;
	GLuint inFBO, inRBO, inFBOtex;

	// OpenVR SDK:
	vr::IVRSystem *	mHMD;
	t_symbol * mDriver;
	t_symbol * mDisplay;
	uint32_t texdim_w, texdim_h;

	vr::TrackedDevicePose_t pRenderPoseArray[vr::k_unMaxTrackedDeviceCount];
	glm::mat4 mDevicePose[vr::k_unMaxTrackedDeviceCount];
	glm::mat4 mHMDPose;
	glm::mat4 m_mat4viewEye[2];
	glm::mat4 m_mat4projectionEye[2];

	vr::IVRRenderModels * mRenderModels;
	vr::IVRTrackedCamera * mCamera;
	vr::TrackedCameraHandle_t	m_hTrackedCamera;
	uint32_t	m_nCameraFrameWidth;
	uint32_t	m_nCameraFrameHeight;
	uint32_t	m_nCameraFrameBufferSize;
	uint32_t	m_nLastFrameSequence;
	uint8_t		* m_pCameraFrameBuffer;
	vr::EVRTrackedCameraFrameType frametype;
	jittex camtex;

	int mHandControllerDeviceIndex[2];

	htcvive(t_symbol * dest_name) : dest_name(dest_name) {


		// init Max object:
		jit_ob3d_new(this, dest_name);
		// outlets create in reverse order:
		outlet_msg = outlet_new(&ob, NULL);
		outlet_video = outlet_new(&ob, "jit_gl_texture");
		outlet_controller[1] = outlet_new(&ob, NULL);
		outlet_controller[0] = outlet_new(&ob, NULL);
		outlet_tracking = outlet_new(&ob, NULL);
		outlet_node = outlet_new(&ob, NULL);
		outlet_eye[1] = outlet_new(&ob, NULL);
		outlet_eye[0] = outlet_new(&ob, NULL);
		outlet_tex = outlet_new(&ob, "jit_gl_texture");

		mHMD = 0;
		mHMDPose = glm::mat4(1.f);
		mHandControllerDeviceIndex[0] = -1;
		mHandControllerDeviceIndex[1] = -1;


		mCamera = 0;
		m_hTrackedCamera = INVALID_TRACKED_CAMERA_HANDLE;
		m_nLastFrameSequence = 0;
		frametype = vr::VRTrackedCameraFrameType_Undistorted;

		fbo = 0;
		fbomirror = 0;
		outtexture = 0;

		intexture = _sym_nothing;
		outname = _sym_nothing;
		outdim[0] = 1512;
		outdim[1] = 1680;

		// attrs:
		near_clip = 0.15f;
		far_clip = 100.f;
		mirror = 0;
		use_camera = 0;
	}

	// attempt to connect to the Vive runtime, creating a session:
	bool connect() {
		if (mHMD) disconnect();

		vr::EVRInitError eError = vr::VRInitError_None;
		mHMD = vr::VR_Init(&eError, vr::VRApplication_Scene);
		if (eError != vr::VRInitError_None) {
			mHMD = 0;
			object_error(&ob, "Unable to init VR runtime: %s", vr::VR_GetVRInitErrorAsEnglishDescription(eError));
			return false;
		}

		if (!vr::VRCompositor()) {
			object_error(&ob, "Compositor initialization failed.");
			return false;
		}

		mRenderModels = (vr::IVRRenderModels *)vr::VR_GetGenericInterface(vr::IVRRenderModels_Version, &eError);
		if (!mRenderModels) {
			object_error(&ob, "Unable to init VR runtime: %s", vr::VR_GetVRInitErrorAsEnglishDescription(eError));
		}

		mCamera = vr::VRTrackedCamera(); // (vr::IVRTrackedCamera *)vr::VR_GetGenericInterface(vr::IVRTrackedCamera_Version, &eError);
		if (!mCamera) {
			object_post(&ob, "failed to acquire camera -- is it enabled in the SteamVR settings?");
		}
		else {
			vr::EVRTrackedCameraError camError;
			bool bHasCamera = false;

			camError = mCamera->HasCamera(vr::k_unTrackedDeviceIndex_Hmd, &bHasCamera);
			if (camError != vr::VRTrackedCameraError_None || !bHasCamera) {
				object_post(&ob, "No Tracked Camera Available! (%s)\n", mCamera->GetCameraErrorNameFromEnum(camError));
				mCamera = 0;
			}

			if (use_camera) video_start();
		}

		configure();

		outlet_anything(outlet_msg, gensym("connected"), 0, NULL);
		return true;
	}

	void disconnect() {
		if (mHMD) {
			video_stop();
			vr::VR_Shutdown();
			mHMD = 0;
		}

	}

	// usually called after session is created, and when important attributes are changed
	// invokes info() to send configuration results 
	void configure() {
		t_atom a[6];
		if (!mHMD) return;

		t_symbol * display_name = GetTrackedDeviceString(mHMD, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String);
		t_symbol * driver_name = GetTrackedDeviceString(mHMD, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SerialNumber_String);
		object_post(&ob, "display %s driver %s", display_name->s_name, driver_name->s_name);

		mHMD->GetRecommendedRenderTargetSize(&texdim_w, &texdim_h);
		// we will send as side-by-side:
		texdim_w *= 2;

		// setup cameras:
		for (int i = 0; i < 2; i++) {

			vr::HmdMatrix34_t matEyeRight = mHMD->GetEyeToHeadTransform((vr::Hmd_Eye)i);
			glm::mat4 matrixObj(
				matEyeRight.m[0][0], matEyeRight.m[1][0], matEyeRight.m[2][0], 0.0,
				matEyeRight.m[0][1], matEyeRight.m[1][1], matEyeRight.m[2][1], 0.0,
				matEyeRight.m[0][2], matEyeRight.m[1][2], matEyeRight.m[2][2], 0.0,
				matEyeRight.m[0][3], matEyeRight.m[1][3], matEyeRight.m[2][3], 1.0f
			);

			m_mat4viewEye[i] = matrixObj;
			float l, r, t, b;
			mHMD->GetProjectionRaw((vr::Hmd_Eye)i, &l, &r, &t, &b);
			atom_setfloat(a + 0, l * near_clip);
			atom_setfloat(a + 1, r * near_clip);
			atom_setfloat(a + 2, -b * near_clip);
			atom_setfloat(a + 3, -t * near_clip);
			atom_setfloat(a + 4, near_clip);
			atom_setfloat(a + 5, far_clip);
			outlet_anything(outlet_eye[i], ps_frustum, 6, a);
		}

		info();
	}

	void info() {
		t_atom a[2];
		// send texture dim (determined via configure()) to the scene jit.gl.node:
		atom_setlong(a + 0, texdim_w);
		atom_setlong(a + 1, texdim_h);
		outlet_anything(outlet_node, _jit_sym_dim, 2, a);
	}

	~htcvive() {
		// free GL resources created by this external
		dest_closing();
		// disconnect from session
		disconnect();
		// remove from jit.gl* hierarchy
		jit_ob3d_free(this);
		// actually delete object
		max_jit_object_free(this);
	}



	void video_restart() {
		video_stop();
		video_start();
	}

	void video_start() {
		uint32_t nCameraFrameBufferSize = 0;
		vr::EVRTrackedCameraError err;
		if (mCamera) {
			if (mCamera->GetCameraFrameSize(vr::k_unTrackedDeviceIndex_Hmd, frametype, &m_nCameraFrameWidth, &m_nCameraFrameHeight, &nCameraFrameBufferSize) != vr::VRTrackedCameraError_None)
			{
				object_error(&ob, "GetCameraFrameBounds() Failed!\n");
				mCamera = 0;
			}
		}

		uint32_t planes = nCameraFrameBufferSize / (m_nCameraFrameWidth * m_nCameraFrameHeight);
		//object_post(&ob, "video %i x %i, %i-plane", m_nCameraFrameWidth, m_nCameraFrameHeight, planes);

		if (mCamera && nCameraFrameBufferSize) {


			camtex.resize(m_nCameraFrameWidth, m_nCameraFrameHeight);

			if (nCameraFrameBufferSize != m_nCameraFrameBufferSize) {
				delete[] m_pCameraFrameBuffer;
				m_nCameraFrameBufferSize = nCameraFrameBufferSize;
				m_pCameraFrameBuffer = new uint8_t[m_nCameraFrameBufferSize];
				memset(m_pCameraFrameBuffer, 0, m_nCameraFrameBufferSize);
			}

			err = mCamera->AcquireVideoStreamingService(vr::k_unTrackedDeviceIndex_Hmd, &m_hTrackedCamera);
			if (m_hTrackedCamera == INVALID_TRACKED_CAMERA_HANDLE)
			{
				object_error(&ob, "AcquireVideoStreamingService() Failed! %s", mCamera->GetCameraErrorNameFromEnum(err));
				return;
			}

			/*

			// doesn't seem to be giving good numbers yet...

			t_atom a[4];
			vr::HmdVector2_t focalLength, center;
			err = mCamera->GetCameraIntrinisics(m_hTrackedCamera, frametype, &focalLength, &center);
			vr::HmdMatrix44_t projection;
			err = mCamera->GetCameraProjection(m_hTrackedCamera, frametype, near_clip, far_clip, &projection);
			atom_setfloat(&a[0], (double)focalLength.v[0]);
			atom_setfloat(&a[1], (double)focalLength.v[1]);
			atom_setfloat(&a[2], (double)center.v[0]);
			atom_setfloat(&a[3], (double)center.v[1]);
			outlet_anything(outlet_msg, gensym("video_focal_center"), 4, a);

			t_atom b[16];
			// convert matrix?
			glm::mat4 proj = mat4_from_openvr(projection);
			// so, what do we really want to do with this information?
			float * fp = glm::value_ptr(proj);
			for (int i = 0; i < 16; i++) atom_setfloat(&b[i], (double)fp[i]);
			outlet_anything(outlet_msg, gensym("video_projection"), 16, b);
			*/
		}
	}

	void video_step() {
		if (!mCamera || !m_hTrackedCamera) return;
		// get the frame header only
		vr::CameraVideoStreamFrameHeader_t frameHeader;
		vr::EVRTrackedCameraError nCameraError = mCamera->GetVideoStreamFrameBuffer(m_hTrackedCamera, frametype, nullptr, 0, &frameHeader, sizeof(frameHeader));
		if (nCameraError != vr::VRTrackedCameraError_None) { object_post(&ob, "no video %s", mCamera->GetCameraErrorNameFromEnum(nCameraError)); return; }

		// only continue if this is a new frame
		if (frameHeader.nFrameSequence == m_nLastFrameSequence) return;
		m_nLastFrameSequence = frameHeader.nFrameSequence;

		// copy frame
		nCameraError = mCamera->GetVideoStreamFrameBuffer(m_hTrackedCamera, frametype, m_pCameraFrameBuffer, m_nCameraFrameBufferSize, &frameHeader, sizeof(frameHeader));
		if (nCameraError != vr::VRTrackedCameraError_None) return;

		// would be nice to copy this as a texture on the GPU but apparently this isn't supported yet (the API exists, but returns error NotSupportedForThisDevice)
		// so instead here's uploading to a jit.gl.texture from our CPU copy:
		if (camtex.tex) {
			// update texture:
			glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT);
			glEnable(GL_TEXTURE_RECTANGLE_ARB);
			glActiveTextureARB(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_RECTANGLE_ARB, camtex.glid());
			glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, camtex.dim[0], camtex.dim[1], GL_RGBA, GL_UNSIGNED_BYTE, m_pCameraFrameBuffer);
			glPopAttrib();

			// and output:
			t_atom a[1];
			atom_setsym(&a[0], camtex.sym);
			outlet_anything(outlet_video, ps_jit_gl_texture, 1, a);
		}
	}

	void video_stop() {
		if (mCamera && m_hTrackedCamera) {
			mCamera->ReleaseVideoStreamingService(m_hTrackedCamera);
			m_hTrackedCamera = INVALID_TRACKED_CAMERA_HANDLE;
		}
	}

	void get_battery_status(unsigned int hand = 0) {
		if (!mHMD) return;
		int index = mHandControllerDeviceIndex[hand % 2];
		if (index >= 0) {
			t_atom a[2];
			atom_setlong(a + 0, hand);
			atom_setfloat(a + 1, mHMD->GetFloatTrackedDeviceProperty(index, vr::Prop_DeviceBatteryPercentage_Float));
			outlet_anything(outlet_msg, gensym("battery"), 2, a);
		}
	}

	void vibrate(unsigned int hand = 0, float ms = 1) {
		if (!mHMD) return;
		int index = mHandControllerDeviceIndex[hand % 2];
		if (index >= 0 && ms > 0.f && ms <= 5.f) {
			mHMD->TriggerHapticPulse(index, 0, ms * 1000);
		}
	}

	// TODO: complete this method
	// grab the model & texture data for tracked devices from the SteamVR driver:
	void loadModels() {
		if (!mHMD) return;

		t_atom a[2];
		for (auto id = vr::k_unTrackedDeviceIndex_Hmd + 1; id < vr::k_unMaxTrackedDeviceCount; id++) {
			if (!mHMD->IsTrackedDeviceConnected(id)) continue;

			// try to load the model & texture data:
			vr::RenderModel_t * pModel = NULL;
			vr::RenderModel_TextureMap_t *pTexture = NULL;
			t_symbol * sRenderModelName = GetTrackedDeviceString(mHMD, id, vr::Prop_RenderModelName_String);

			if (!vr::VRRenderModels()->LoadRenderModel_Async(sRenderModelName->s_name, &pModel) || pModel == NULL) {
				object_error(&ob, "unable to load render model %s", sRenderModelName->s_name);
				continue;
			}

			if (!vr::VRRenderModels()->LoadTexture_Async(pModel->diffuseTextureId, &pTexture) || pTexture == NULL) {
				vr::VRRenderModels()->FreeRenderModel(pModel);
				object_error(&ob, "unable to load texture for model %s", sRenderModelName->s_name);
				continue; // move on to the next tracked device
			}

			// TODO: export the model & texture data to Jitter
			// model would be a series of jit.matrix creations, and messages to send to jit.gl.mesh to bind them
			// texture: create an internal jit.gl.texture and export the name as above
			// all messages prefixed by "model" <id> 
			// such that it could be easily [route]d to a jit.gl.mesh per tracked device

			// for now, just export the name to see if it is working
			atom_setlong(a + 0, id);
			atom_setsym(a + 1, sRenderModelName);
			outlet_anything(outlet_msg, gensym("model"), 2, a);

			vr::VRRenderModels()->FreeRenderModel(pModel);
			vr::VRRenderModels()->FreeTexture(pTexture);
		}
	}

	bool textureset_create() {

		return true;
	}

	void textureset_destroy() {

	}

	void bang() {
		t_atom a[4];

		if (!mHMD) return;


		// get desired model matrix (for navigation)
		glm::vec3 m_position;
		jit_attr_getfloat_array(this, _jit_sym_position, 3, &m_position.x);
		t_jit_quat m_jitquat;
		jit_attr_getfloat_array(this, _jit_sym_quat, 4, &m_jitquat.x);

		glm::mat4 modelview_mat = glm::translate(glm::mat4(1.0f), m_position) * mat4_cast(quat_from_jitter(m_jitquat));

		vr::VREvent_t event;
		while (mHMD->PollNextEvent(&event, sizeof(event))) {
			switch (event.eventType) {
			case vr::VREvent_TrackedDeviceActivated:
			{
				atom_setlong(&a[0], event.trackedDeviceIndex);
				outlet_anything(outlet_msg, gensym("attached"), 1, a);
				//setupRenderModelForTrackedDevice(event.trackedDeviceIndex);
			}
			break;
			case vr::VREvent_TrackedDeviceDeactivated:
			{
				atom_setlong(&a[0], event.trackedDeviceIndex);
				outlet_anything(outlet_msg, gensym("detached"), 1, a);
			}
			break;
			case vr::VREvent_TrackedDeviceUpdated:
			{
			}
			break;
			default:
				atom_setlong(&a[0], event.eventType);
				outlet_anything(outlet_msg, gensym("event"), 1, a);
			}
		}

		// get the tracking data here
		vr::EVRCompositorError err = vr::VRCompositor()->WaitGetPoses(pRenderPoseArray, vr::k_unMaxTrackedDeviceCount, NULL, 0);
		if (err != vr::VRCompositorError_None) {
			object_error(&ob, "WaitGetPoses error");
			return;
		}

		// TODO: should we ignore button presses etc. if so?
		bool inputCapturedByAnotherProcess = mHMD->IsInputFocusCapturedByAnotherProcess();

		// check each device:
		for (int i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
			const vr::TrackedDevicePose_t& trackedDevicePose = pRenderPoseArray[i];

			if (trackedDevicePose.bDeviceIsConnected) {
				if (trackedDevicePose.bPoseIsValid) {
					// this is the view matrix relative to the 'chaperone' space origin
					// (the center of the floor space in the real world)
					// do we need to transform this by the scene?
					mDevicePose[i] = mat4_from_openvr(trackedDevicePose.mDeviceToAbsoluteTracking);
				}

				switch (mHMD->GetTrackedDeviceClass(i)) {
				case vr::TrackedDeviceClass_HMD: {
					if (trackedDevicePose.bPoseIsValid) {
						mHMDPose = mDevicePose[i];
						glm::mat4 world_pose = modelview_mat * mHMDPose;

						// probably want to output this for navigation etc. use
						glm::vec3 p = glm::vec3(mHMDPose[3]); // the translation component
						atom_setfloat(a + 0, p.x);
						atom_setfloat(a + 1, p.y);
						atom_setfloat(a + 2, p.z);
						outlet_anything(outlet_tracking, ps_tracked_position, 3, a);

						glm::quat q = glm::quat_cast(mHMDPose);
						//q = glm::normalize(q);
						atom_setfloat(a + 0, q.x);
						atom_setfloat(a + 1, q.y);
						atom_setfloat(a + 2, q.z);
						atom_setfloat(a + 3, q.w);
						outlet_anything(outlet_tracking, ps_tracked_quat, 4, a);

						p = glm::vec3(world_pose[3]); // the translation component
						atom_setfloat(a + 0, p.x);
						atom_setfloat(a + 1, p.y);
						atom_setfloat(a + 2, p.z);
						outlet_anything(outlet_tracking, _jit_sym_position, 3, a);

						q = glm::quat_cast(world_pose);
						atom_setfloat(a + 0, q.x);
						atom_setfloat(a + 1, q.y);
						atom_setfloat(a + 2, q.z);
						atom_setfloat(a + 3, q.w);
						outlet_anything(outlet_tracking, _jit_sym_quat, 4, a);

						atom_setfloat(a + 0, trackedDevicePose.vVelocity.v[0]);
						atom_setfloat(a + 1, trackedDevicePose.vVelocity.v[1]);
						atom_setfloat(a + 2, trackedDevicePose.vVelocity.v[2]);
						outlet_anything(outlet_tracking, ps_velocity, 3, a);

						atom_setfloat(a + 0, trackedDevicePose.vAngularVelocity.v[0]);
						atom_setfloat(a + 1, trackedDevicePose.vAngularVelocity.v[1]);
						atom_setfloat(a + 2, trackedDevicePose.vAngularVelocity.v[2]);
						outlet_anything(outlet_tracking, ps_angular_velocity, 3, a);
					}
				} break;
				case vr::TrackedDeviceClass_Controller: {
					// check role to see if these are hands
					vr::ETrackedControllerRole role = mHMD->GetControllerRoleForTrackedDeviceIndex(i);
					switch (role) {
					case vr::TrackedControllerRole_LeftHand:
					case vr::TrackedControllerRole_RightHand: {
						//if (trackedDevicePose.eTrackingResult == vr::TrackingResult_Running_OK) {

						int hand = (role == vr::TrackedControllerRole_RightHand);
						mHandControllerDeviceIndex[hand] = i;

						if (trackedDevicePose.bPoseIsValid) {

							mat4& tracked_pose = mDevicePose[i];
							glm::mat4 world_pose = modelview_mat * tracked_pose;

							// output the raw tracking data:
							glm::vec3 p = glm::vec3(tracked_pose[3]); // the translation component
							atom_setfloat(a + 0, p.x);
							atom_setfloat(a + 1, p.y);
							atom_setfloat(a + 2, p.z);
							outlet_anything(outlet_controller[hand], ps_tracked_position, 3, a);

							glm::quat q = glm::quat_cast(tracked_pose);
							//q = glm::normalize(q);
							atom_setfloat(a + 0, q.x);
							atom_setfloat(a + 1, q.y);
							atom_setfloat(a + 2, q.z);
							atom_setfloat(a + 3, q.w);
							outlet_anything(outlet_controller[hand], ps_tracked_quat, 4, a);

							p = glm::vec3(world_pose[3]); // the translation component
							atom_setfloat(a + 0, p.x);
							atom_setfloat(a + 1, p.y);
							atom_setfloat(a + 2, p.z);
							outlet_anything(outlet_controller[hand], _jit_sym_position, 3, a);

							q = glm::quat_cast(world_pose);
							atom_setfloat(a + 0, q.x);
							atom_setfloat(a + 1, q.y);
							atom_setfloat(a + 2, q.z);
							atom_setfloat(a + 3, q.w);
							outlet_anything(outlet_controller[hand], _jit_sym_quat, 4, a);

							atom_setfloat(a + 0, trackedDevicePose.vVelocity.v[0]);
							atom_setfloat(a + 1, trackedDevicePose.vVelocity.v[1]);
							atom_setfloat(a + 2, trackedDevicePose.vVelocity.v[2]);
							outlet_anything(outlet_controller[hand], ps_velocity, 3, a);

							atom_setfloat(a + 0, trackedDevicePose.vAngularVelocity.v[0]);
							atom_setfloat(a + 1, trackedDevicePose.vAngularVelocity.v[1]);
							atom_setfloat(a + 2, trackedDevicePose.vAngularVelocity.v[2]);
							outlet_anything(outlet_controller[hand], ps_angular_velocity, 3, a);
						}

						vr::VRControllerState_t cs;
						//OpenVR SDK 1.0.4 adds a 3rd arg for size
						mHMD->GetControllerState(i, &cs, sizeof(cs));

						atom_setlong(a + 0, (cs.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0);
						atom_setfloat(a + 1, cs.rAxis[1].x);
						outlet_anything(outlet_controller[hand], ps_trigger, 2, a);

						atom_setlong(a + 0, (cs.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad)) != 0);
						atom_setfloat(a + 1, cs.rAxis[0].x);
						atom_setfloat(a + 2, cs.rAxis[0].y);
						atom_setlong(a + 3, (cs.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad)) != 0);
						outlet_anything(outlet_controller[hand], ps_trackpad, 4, a);

						//TODO: The API appears to partition the Touchpad to D-Pad quadrants internally, investigate!
						//vr::k_EButton_DPad_Down etc.

						atom_setlong(a + 0, (cs.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu)) != 0);
						atom_setlong(a + 1, (cs.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_Grip)) != 0);
						outlet_anything(outlet_controller[hand], ps_buttons, 2, a);

						//}
					}
															  break;
					default:
						break;
					}
				} break;
				case vr::TrackedDeviceClass_GenericTracker:
				{
					if (trackedDevicePose.bPoseIsValid) {

						//Figure out which tracker it is using some kind of unique identifier
						vr::ETrackedPropertyError err = vr::TrackedProp_Success;
						char buf[32];
						uint32_t unPropLen = vr::VRSystem()->GetStringTrackedDeviceProperty(i, vr::Prop_SerialNumber_String, buf, sizeof(buf), &err);
						//Append the UID onto the data header going into Max
						if (err == vr::TrackedProp_Success)
						{
							char* result;
							result = (char*)calloc(strlen("tracker_velocity_") + strlen(buf) + 1, sizeof(char));
							strcpy(result, "tracker_velocity_");
							strcat(result, buf);
							trk_velocity = gensym(result);
							free(result);

							result = (char*)calloc(strlen("tracker_angular_velocity_") + strlen(buf) + 1, sizeof(char));
							strcpy(result, "tracker_angular_velocity_");
							strcat(result, buf);
							trk_angular_velocity = gensym(result);
							free(result);

							result = (char*)calloc(strlen("tracker_tracked_position_") + strlen(buf) + 1, sizeof(char));
							strcpy(result, "tracker_tracked_position_");
							strcat(result, buf);
							trk_tracked_position = gensym(result);
							free(result);

							result = (char*)calloc(strlen("tracker_tracked_quat_") + strlen(buf) + 1, sizeof(char));
							strcpy(result, "tracker_tracked_quat_");
							strcat(result, buf);
							trk_tracked_quat = gensym(result);
							free(result);
						}

						mat4& tracked_pose = mDevicePose[i];
						glm::mat4 world_pose = modelview_mat * tracked_pose;

						// output the raw tracking data:
						glm::vec3 p = glm::vec3(tracked_pose[3]); // the translation component
						atom_setfloat(a + 0, p.x);
						atom_setfloat(a + 1, p.y);
						atom_setfloat(a + 2, p.z);
						outlet_anything(outlet_tracking, trk_tracked_position, 3, a);
						glm::quat q = glm::quat_cast(tracked_pose);
						//q = glm::normalize(q);
						atom_setfloat(a + 0, q.x);
						atom_setfloat(a + 1, q.y);
						atom_setfloat(a + 2, q.z);
						atom_setfloat(a + 3, q.w);
						outlet_anything(outlet_tracking, trk_tracked_quat, 4, a);

						atom_setfloat(a + 0, trackedDevicePose.vVelocity.v[0]);
						atom_setfloat(a + 1, trackedDevicePose.vVelocity.v[1]);
						atom_setfloat(a + 2, trackedDevicePose.vVelocity.v[2]);
						outlet_anything(outlet_tracking, trk_velocity, 3, a);

						atom_setfloat(a + 0, trackedDevicePose.vAngularVelocity.v[0]);
						atom_setfloat(a + 1, trackedDevicePose.vAngularVelocity.v[1]);
						atom_setfloat(a + 2, trackedDevicePose.vAngularVelocity.v[2]);
						outlet_anything(outlet_tracking, trk_angular_velocity, 3, a);
					}
				} break;
				default:
					break;
				}
			}
		}

		// now update cameras:
		for (int i = 0; i < 2; i++) {
			glm::mat4 modelvieweye_mat = modelview_mat * mHMDPose * m_mat4viewEye[i];

			// modelview
			glm::vec3 p = glm::vec3(modelvieweye_mat[3]); // the translation component
			atom_setfloat(a + 0, p.x);
			atom_setfloat(a + 1, p.y);
			atom_setfloat(a + 2, p.z);
			outlet_anything(outlet_eye[i], _jit_sym_position, 3, a);

			glm::quat q = glm::quat_cast(modelvieweye_mat);
			atom_setfloat(a + 0, q.x);
			atom_setfloat(a + 1, q.y);
			atom_setfloat(a + 2, q.z);
			atom_setfloat(a + 3, q.w);
			outlet_anything(outlet_eye[i], _jit_sym_quat, 4, a);
		}

		// video:
		video_step();
	}

	// receive a texture
	// TODO: validate texture format?
	void jit_gl_texture(t_symbol * s) {
		intexture = s;
		submit();
	}

	// do all the important work here
	void submit() {
		if (!mHMD) return;


		void * texob = jit_object_findregistered(intexture);
		if (!texob) {
			object_error(&ob, "no texture to draw");
			return;	// no texture to copy from.
		}

		// TODO: verify that texob is a texture
		GLuint glid = jit_attr_getlong(texob, ps_glid);
		// get input texture dimensions
		t_atom_long texdim[2];
		jit_attr_getlong_array(texob, _sym_dim, 2, texdim);
		//post("submit texture id %ld dim %ld %ld\n", glid, texdim[0], texdim[1]);

		if (!fbo) {
			object_error(&ob, "no fbo yet");
			return;	// no texture to copy from.
		}
		if (!inFBOtex) {
			object_error(&ob, "no fbo tex yet");
			return;	// no texture to copy from.
		}

		{
			// copy our glid source into the inFBO destination
			// save some state
			GLint previousFBO;	// make sure we pop out to the right FBO
			GLint previousMatrixMode;

			glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &previousFBO);
			glGetIntegerv(GL_MATRIX_MODE, &previousMatrixMode);

			// save texture state, client state, etc.
			glPushAttrib(GL_ALL_ATTRIB_BITS);
			glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

			// TODO use rectangle 1?
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, inFBO);
			glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, inFBOtex, 0);
			if (fbo_check()) {
				glMatrixMode(GL_TEXTURE);
				glPushMatrix();
				glLoadIdentity();

				glViewport(0, 0, texdim_w, texdim_h);

				glMatrixMode(GL_PROJECTION);
				glPushMatrix();
				glLoadIdentity();
				glOrtho(0.0, texdim_w, 0.0, texdim_h, -1, 1);

				glMatrixMode(GL_MODELVIEW);
				glPushMatrix();
				glLoadIdentity();

				glClearColor(0, 0, 0, 1);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

				glColor4f(1.0, 0.0, 1.0, 1.0);

				glActiveTexture(GL_TEXTURE0);
				glClientActiveTexture(GL_TEXTURE0);
				glEnable(GL_TEXTURE_RECTANGLE_ARB);
				glBindTexture(GL_TEXTURE_RECTANGLE_ARB, glid);

				// do not need blending if we use black border for alpha and replace env mode, saves a buffer wipe
				// we can do this since our image draws over the complete surface of the FBO, no pixel goes untouched.

				glDisable(GL_BLEND);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

				// move to VA for rendering
				GLfloat tex_coords[] = {
					(GLfloat)texdim[0], (GLfloat)texdim[1],
					0.0, (GLfloat)texdim[1],
					0.0, 0.,
					(GLfloat)texdim[0], 0.
				};

				GLfloat verts[] = {
					(GLfloat)texdim_w, (GLfloat)texdim_h,
					0.0, (GLfloat)texdim_h,
					0.0, 0.0,
					(GLfloat)texdim_w, 0.0
				};

				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, 0, tex_coords);
				glEnableClientState(GL_VERTEX_ARRAY);
				glVertexPointer(2, GL_FLOAT, 0, verts);
				glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
				glDisableClientState(GL_VERTEX_ARRAY);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);

				glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);

				glMatrixMode(GL_MODELVIEW);
				glPopMatrix();
				glMatrixMode(GL_PROJECTION);
				glPopMatrix();

				glMatrixMode(GL_TEXTURE);
				glPopMatrix();
			}
			else {
				object_error(&ob, "falied to create submit FBO");
			}

			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

			glPopAttrib();
			glPopClientAttrib();

			glMatrixMode(previousMatrixMode);
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, previousFBO);

			//jit_ob3d_set_context(ctx);
		}


		vr::EVRCompositorError err;
		//GraphicsAPIConvention enum was renamed to TextureType in OpenVR SDK 1.0.5
		vr::Texture_t vrTexture = { (void*)inFBOtex, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };

		vr::VRTextureBounds_t leftBounds = { 0.f, 0.f, 0.5f, 1.f };
		vr::VRTextureBounds_t rightBounds = { 0.5f, 0.f, 1.f, 1.f };

		err = vr::VRCompositor()->Submit(vr::Eye_Left, &vrTexture, &leftBounds);
		switch (err) {
		case 0:
			break;
		case 1:
			object_error(&ob, "submit error: Request failed.");
			break;
		case 100:
			object_error(&ob, "submit error: Incompatible version.");
			break;
		case 101:
			object_error(&ob, "submit error: Do not have focus.");
			break;
		case 102:
			object_error(&ob, "submit error: Invalid texture.");
			break;
		case 103:
			object_error(&ob, "submit error: Is not scene application.");
			break;
		case 104:
			object_error(&ob, "submit error: Texture is on wrong device.");
			break;
		case 105:
			object_error(&ob, "submit error: Texture uses unsupported format.");
			break;
		case 106:
			object_error(&ob, "submit error: Shared textures not supported.");
			break;
		case 107:
			object_error(&ob, "submit error: Index out of range.");
			break;
		case 108:
			object_error(&ob, "submit error: Already submitted.");
			break;
		}

		err = vr::VRCompositor()->Submit(vr::Eye_Right, &vrTexture, &rightBounds);

		//glBindTexture(GL_TEXTURE_2D, 0);

		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// openvr header recommends this after submit:
		glFlush();
		glFinish();


		// submission done:

		t_atom a[1];
		atom_setsym(a, intexture);

		if (mirror) mirror_output(a);

		outlet_anything(outlet_tex, ps_jit_gl_texture, 1, a);
	}



	bool fbo_check() {
		GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
		if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
			if (status == GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT) {
				object_error(&ob, "failed to create render to texture target GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT");
			}
			else if (status == GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT) {
				object_error(&ob, "failed to create render to texture target GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS");
			}
			else if (status == GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT) {
				object_error(&ob, "failed to create render to texture target GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT");
			}
			else if (status == GL_FRAMEBUFFER_UNSUPPORTED_EXT) {
				object_error(&ob, "failed to create render to texture target GL_FRAMEBUFFER_UNSUPPORTED");
			}
			else {
				object_error(&ob, "failed to create render to texture target %d", status);
			}
			return false;
		}
		return true;
	}

	bool mirror_output(t_atom * a) {
		if (!mirror) return false;

		t_symbol * dst = outname;
		void * outtexture = jit_object_findregistered(dst);
		if (!outtexture) {
			object_error(&ob, "no texture to draw");
			return false;	// no texture to copy from.
		}
		long glid = jit_attr_getlong(outtexture, ps_glid);
		// get output texture dimensions
		//t_atom_long outdim[2];

		//jit_attr_getlong_array(texob, _sym_dim, 2, outdim);
		// add texture to OB3D list.
		jit_attr_setsym(this, gensym("texture"), dst);

		// update texture dim to match mirror:
		outdim[0] = texdim_w;
		outdim[1] = texdim_h;
		jit_attr_setlong_array(outtexture, _jit_sym_dim, 2, outdim);

		// get source texture:
		GLuint texId;
		vr::glSharedTextureHandle_t texhandle;
		{
			vr::EVRCompositorError err = vr::VRCompositor()->GetMirrorTextureGL(vr::Eye_Left, &texId, &texhandle);
			if (err != vr::VRCompositorError_None) {
				object_error(&ob, "failed to acquire mirror texture %d", (int)err); return false;
			}
			vr::VRCompositor()->LockGLSharedTextureForAccess(texhandle);

			// save some state
			GLint previousFBO;	// make sure we pop out to the right FBO
			GLint previousMatrixMode;

			glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &previousFBO);
			glGetIntegerv(GL_MATRIX_MODE, &previousMatrixMode);

			// save texture state, client state, etc.
			glPushAttrib(GL_ALL_ATTRIB_BITS);
			glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbomirror);
			glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, glid, 0);
			if (fbo_check()) {
				t_atom_long width = outdim[0];
				t_atom_long height = outdim[1];

				glMatrixMode(GL_TEXTURE);
				glPushMatrix();
				glLoadIdentity();

				glViewport(0, 0, width, height);

				glMatrixMode(GL_PROJECTION);
				glPushMatrix();
				glLoadIdentity();
				glOrtho(0.0, width, 0.0, height, -1, 1);

				glMatrixMode(GL_MODELVIEW);
				glPushMatrix();
				glLoadIdentity();

				glColor4f(0.0, 1.0, 1.0, 1.0);

				glActiveTexture(GL_TEXTURE0);
				glClientActiveTexture(GL_TEXTURE0);
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, texId);

				// do not need blending if we use black border for alpha and replace env mode, saves a buffer wipe
				// we can do this since our image draws over the complete surface of the FBO, no pixel goes untouched.

				glDisable(GL_BLEND);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

				GLfloat tex_coords[] = {
					1., 1.,
					0.0, 1.,
					0.0, 0.0,
					1., 0.0
				};

				GLfloat verts[] = {
					(GLfloat)width, (GLfloat)height,
					0.0, (GLfloat)height,
					0.0, 0.0,
					(GLfloat)width, 0.0
				};

				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, 0, tex_coords);
				glEnableClientState(GL_VERTEX_ARRAY);
				glVertexPointer(2, GL_FLOAT, 0, verts);
				glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
				glDisableClientState(GL_VERTEX_ARRAY);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);

				glBindTexture(GL_TEXTURE_2D, 0);

				glMatrixMode(GL_MODELVIEW);
				glPopMatrix();
				glMatrixMode(GL_PROJECTION);
				glPopMatrix();

				glMatrixMode(GL_TEXTURE);
				glPopMatrix();

				// success!
				atom_setsym(a, dst);
			}
			else {
				object_error(&ob, "falied to create mirror FBO");
			}

			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

			glPopAttrib();
			glPopClientAttrib();

			glMatrixMode(previousMatrixMode);
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, previousFBO);

			//jit_ob3d_set_context(ctx);

			vr::VRCompositor()->UnlockGLSharedTextureForAccess(texhandle);
			vr::VRCompositor()->ReleaseSharedGLTexture(texId, texhandle);

		}
		return true;
	}

	t_jit_err draw() {
		// this gets called when the jit.gl.render context updates clients
		// the htcvive object doesn't draw to the main scene, so there's nothing needed to be done here
		return JIT_ERR_NONE;
	}

	t_jit_err dest_changed() {
		configure();
		object_post(&ob, "dest_changed %d %d", texdim_w, texdim_h);

		t_symbol *context = jit_attr_getsym(this, gensym("drawto"));


		// create a jit.gl.texture to copy mirror to
		outtexture = jit_object_new(gensym("jit_gl_texture"), context);
		if (outtexture) {
			// set texture attributes.
			outname = jit_attr_getsym(outtexture, gensym("name"));
			if (jit_attr_setlong_array(outtexture, _jit_sym_dim, 2, outdim)) object_error(&ob, "failed to set mirror dim");
			if (jit_attr_setlong(outtexture, gensym("rectangle"), 1)) object_error(&ob, "failed to set mirror rectangle mode");
			//if (jit_attr_setsym(outtexture, _jit_sym_name, outname)) object_error(&ob, "failed to set mirror texture name");
			jit_attr_setsym(outtexture, gensym("defaultimage"), gensym("black"));
			//jit_attr_setlong(outtexture, gensym("flip"), 0);

			// our texture has to be bound in the new context before we can use it
			// http://cycling74.com/forums/topic.php?id=29197
			t_jit_gl_drawinfo drawInfo;
			if (jit_gl_drawinfo_setup(this, &drawInfo)) object_error(&ob, "failed to get draw info");
			jit_gl_bindtexture(&drawInfo, outname, 0);
			jit_gl_unbindtexture(&drawInfo, outname, 0);
		}
		else {
			object_error(&ob, "failed to create Jitter mirror texture");
		}


		if (!camtex.dest_changed(context)) object_error(&ob, "failed to create camera texture");
		// need to do this if you plan using the texture as an FBO target:
		if (!camtex.bind(this))  object_error(&ob, "failed to bind camera texture");
		if (!camtex.unbind(this))  object_error(&ob, "failed to unbind camera texture");

		// make a framebuffer:

		glGenFramebuffersEXT(1, &fbo);
		glGenFramebuffersEXT(1, &fbomirror);
		glGenFramebuffersEXT(1, &inFBO);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, inFBO);

		glGenRenderbuffersEXT(1, &inRBO);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, inRBO);
		glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT, texdim_w, texdim_h);
		glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, inRBO);

		glGenTextures(1, &inFBOtex);
		glBindTexture(GL_TEXTURE_2D, inFBOtex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texdim_w, texdim_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, inFBOtex, 0);

		// check FBO status
		GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
		if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
			object_error(&ob, "failed to create Jitter FBO");
			return JIT_ERR_GENERIC;
		}

		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

		return JIT_ERR_NONE;
	}

	// free any locally-allocated GL resources
	t_jit_err dest_closing() {
		//object_post(&ob, "dest_closing");
		//disconnect();

		camtex.dest_closing();

		if (fbo) {
			glDeleteFramebuffersEXT(1, &fbo);
			fbo = 0;
		}
		if (fbomirror) {
			glDeleteFramebuffersEXT(1, &fbomirror);
			fbomirror = 0;
		}
		if (outtexture) {
			jit_object_free(outtexture);
			outtexture = 0;
		}

		return JIT_ERR_NONE;
	}

	t_jit_err ui(t_line_3d *p_line, t_wind_mouse_info *p_mouse) {

		return JIT_ERR_NONE;
	}


	void mirror_create() {

	}

	void mirror_destroy() {

	}

	static t_symbol * GetTrackedDeviceString(vr::IVRSystem *pHmd, vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError *peError = NULL)
	{
		uint32_t unRequiredBufferLen = pHmd->GetStringTrackedDeviceProperty(unDevice, prop, NULL, 0, peError);
		if (unRequiredBufferLen == 0) return _sym_nothing;

		char *pchBuffer = new char[unRequiredBufferLen];
		unRequiredBufferLen = pHmd->GetStringTrackedDeviceProperty(unDevice, prop, pchBuffer, unRequiredBufferLen, peError);
		t_symbol * sResult = gensym(pchBuffer);
		delete[] pchBuffer;
		return sResult;
	}

};


void * htcvive_new(t_symbol *s, long argc, t_atom *argv) {
	htcvive *x = NULL;
	if ((x = (htcvive *)object_alloc(max_class))) {// get context:
		t_symbol * dest_name = atom_getsym(argv);

		x = new (x)htcvive(dest_name);

		// apply attrs:
		attr_args_process(x, (short)argc, argv);

		// invoke any initialization after the attrs are set from here:
		x->connect();
	}
	return (x);
}

void htcvive_free(htcvive *x) {
	x->~htcvive();
}

t_jit_err htcvive_draw(htcvive * x) { return x->draw(); }
t_jit_err htcvive_ui(htcvive * x, t_line_3d *p_line, t_wind_mouse_info *p_mouse) { return x->ui(p_line, p_mouse); }
t_jit_err htcvive_dest_closing(htcvive * x) { return x->dest_closing(); }
t_jit_err htcvive_dest_changed(htcvive * x) { return x->dest_changed(); }

void htcvive_assist(htcvive *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "bang to update tracking, texture to submit, other messages");
	}
	else {	// outlet
		switch (a) {
		case 0: sprintf(s, "output/mirror texture"); break;
		case 1: sprintf(s, "to left eye camera"); break;
		case 2: sprintf(s, "to right eye camera"); break;
		case 3: sprintf(s, "to scene node (set texture dim)"); break;
		case 4: sprintf(s, "tracking state"); break;
		case 5: sprintf(s, "left controller"); break;
		case 6: sprintf(s, "right controller"); break;
		case 7: sprintf(s, "hmd camera image (if enabled)"); break;
		default: sprintf(s, "other messages"); break;
		}
	}
}

void htcvive_connect(htcvive * x) {
	x->connect();
}

void htcvive_disconnect(htcvive * x) {
	x->disconnect();
}

void htcvive_configure(htcvive * x) {
	x->configure();
}

void htcvive_info(htcvive * x) {
	x->info();
}

void htcvive_bang(htcvive * x) {
	x->bang();
}

void htcvive_submit(htcvive * x) {
	x->submit();
}

void htcvive_vibrate(htcvive * x, t_symbol * s, long argc, t_atom * argv) {

	t_atom_long hand = argc > 0 ? atom_getlong(argv + 0) : 0;
	float ms = argc > 1 ? atom_getfloat(argv + 1) : 1.f;
	x->vibrate(hand, ms);
}

void htcvive_battery(htcvive * x, t_symbol * s, long argc, t_atom * argv) {

	t_atom_long hand = argc > 0 ? atom_getlong(argv + 0) : 0;
	x->get_battery_status(hand);
}

void htcvive_jit_gl_texture(htcvive * x, t_symbol * s, long argc, t_atom * argv) {
	if (argc > 0 && atom_gettype(argv) == A_SYM) {
		x->jit_gl_texture(atom_getsym(argv));
	}
}

t_max_err htcvive_near_clip_set(htcvive *x, t_object *attr, long argc, t_atom *argv) {
	x->near_clip = atom_getfloat(argv);
	x->configure();
	return 0;
}

t_max_err htcvive_far_clip_set(htcvive *x, t_object *attr, long argc, t_atom *argv) {
	x->far_clip = atom_getfloat(argv);
	x->configure();
	return 0;
}

t_max_err htcvive_use_camera_set(htcvive *x, t_object *attr, long argc, t_atom *argv) {
	x->use_camera = atom_getlong(argv);
	if (x->use_camera > 0) {
		switch (x->use_camera) {
		case 1: x->frametype = vr::VRTrackedCameraFrameType_Undistorted; break;
		case 2: x->frametype = vr::VRTrackedCameraFrameType_Distorted; break;
		default: x->frametype = vr::VRTrackedCameraFrameType_MaximumUndistorted; break;
		}
		x->video_restart();
	}
	else {
		x->video_stop();
	}
	return 0;
}

void htcvive_quit() {

}

void htcvive_log(int level, const char* message) {
	post("vive log %d %s", level, message);
}

void ext_main(void *r)
{
	t_class *c;

	common_symbols_init();
	ps_quat = gensym("quat");
	ps_pos = gensym("pos");
	ps_viewport = gensym("viewport");
	ps_frustum = gensym("frustum");
	ps_warning = gensym("warning");
	ps_glid = gensym("glid");
	ps_jit_gl_texture = gensym("jit_gl_texture");

	ps_trigger = gensym("trigger");
	ps_trackpad = gensym("trackpad");
	ps_buttons = gensym("buttons");
	ps_velocity = gensym("velocity");
	ps_angular_velocity = gensym("angular_velocity");

	ps_tracked_position = gensym("tracked_position");
	ps_tracked_quat = gensym("tracked_quat");

	// init

	quittask_install((method)htcvive_quit, NULL);

	c = class_new("htcvive", (method)htcvive_new, (method)htcvive_free, (long)sizeof(htcvive),
		0L /* leave NULL!! */, A_GIMME, 0);

	long ob3d_flags = JIT_OB3D_NO_MATRIXOUTPUT | JIT_OB3D_DOES_UI;
	/*
	JIT_OB3D_NO_ROTATION_SCALE;
	ob3d_flags |= JIT_OB3D_NO_POLY_VARS;
	ob3d_flags |= JIT_OB3D_NO_BLEND;
	ob3d_flags |= JIT_OB3D_NO_TEXTURE;
	ob3d_flags |= JIT_OB3D_NO_MATRIXOUTPUT;
	ob3d_flags |= JIT_OB3D_AUTO_ONLY;
	ob3d_flags |= JIT_OB3D_NO_DEPTH;
	ob3d_flags |= JIT_OB3D_NO_ANTIALIAS;
	ob3d_flags |= JIT_OB3D_NO_FOG;
	ob3d_flags |= JIT_OB3D_NO_LIGHTING_MATERIAL;
	ob3d_flags |= JIT_OB3D_NO_SHADER;
	ob3d_flags |= JIT_OB3D_NO_BOUNDS;
	ob3d_flags |= JIT_OB3D_NO_COLOR;
	*/

	void * ob3d = jit_ob3d_setup(c, calcoffset(htcvive, ob3d), ob3d_flags);
	// define our OB3D draw methods
	//jit_class_addmethod(c, (method)(htcvive_draw), "ob3d_draw", A_CANT, 0L);
	jit_class_addmethod(c, (method)(htcvive_dest_closing), "dest_closing", A_CANT, 0L);
	jit_class_addmethod(c, (method)(htcvive_dest_changed), "dest_changed", A_CANT, 0L);
	if (ob3d_flags & JIT_OB3D_DOES_UI) {
		jit_class_addmethod(c, (method)(htcvive_ui), "ob3d_ui", A_CANT, 0L);
	}
	// must register for ob3d use
	jit_class_addmethod(c, (method)jit_object_register, "register", A_CANT, 0L);



	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)htcvive_assist, "assist", A_CANT, 0);


	class_addmethod(c, (method)htcvive_jit_gl_texture, "jit_gl_texture", A_GIMME, 0);

	class_addmethod(c, (method)htcvive_connect, "connect", 0);
	class_addmethod(c, (method)htcvive_disconnect, "disconnect", 0);
	class_addmethod(c, (method)htcvive_configure, "configure", 0);
	class_addmethod(c, (method)htcvive_info, "info", 0);


	class_addmethod(c, (method)htcvive_vibrate, "vibrate", A_GIMME, 0);
	class_addmethod(c, (method)htcvive_battery, "battery", A_GIMME, 0);

	class_addmethod(c, (method)htcvive_bang, "bang", 0);
	class_addmethod(c, (method)htcvive_submit, "submit", 0);

	CLASS_ATTR_FLOAT(c, "near_clip", 0, htcvive, near_clip);
	CLASS_ATTR_FLOAT(c, "far_clip", 0, htcvive, far_clip);
	CLASS_ATTR_ACCESSORS(c, "near_clip", NULL, htcvive_near_clip_set);
	CLASS_ATTR_ACCESSORS(c, "far_clip", NULL, htcvive_far_clip_set);


	CLASS_ATTR_LONG(c, "use_camera", 0, htcvive, use_camera);
	CLASS_ATTR_LABEL(c, "use_camera", 0, "Use the camera on the HMD");
	CLASS_ATTR_ENUMINDEX4(c, "use_camera", 0, "no video", "distorted", "undistorted", "undistorted_maximized");
	CLASS_ATTR_ACCESSORS(c, "use_camera", NULL, htcvive_use_camera_set);

	//	CLASS_ATTR_LONG(c, "mirror", 0, htcvive, mirror);
	//	CLASS_ATTR_STYLE_LABEL(c, "mirror", 0, "onoff", "mirror HMD display in main window");

	class_register(CLASS_BOX, c);
	max_class = c;
}

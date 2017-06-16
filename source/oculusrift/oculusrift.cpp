/*

	Might be important:
		https://forums.oculus.com/viewtopic.php?f=20&t=24361&p=283304&hilit=opengl+0.6#p283304
	Might be useful:
		https://forums.oculus.com/viewtopic.php?f=39&t=91&p=277330&hilit=opengl+0.6#p277330


*/

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
}

#include <new> // for in-place constructor

#include "OVR_CAPI.h"
#include "OVR_CAPI_GL.h"

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8                   0x8C43
#endif

static t_class * max_class = 0;

static t_symbol * ps_quat;
static t_symbol * ps_pos;
static t_symbol * ps_viewport;
static t_symbol * ps_frustum;
static t_symbol * ps_warning;
static t_symbol * ps_glid;
static t_symbol * ps_jit_gl_texture;
static t_symbol * ps_tracked_position;
static t_symbol * ps_tracked_quat;
static t_symbol * ps_velocity;
static t_symbol * ps_angular_velocity;

static t_symbol * ps_trigger;
static t_symbol * ps_hand_trigger;
static t_symbol * ps_thumbstick;
static t_symbol * ps_buttons;

// TODO: this is a really annoying hack. The Oculus driver doesn't seem to like being reconnected too quickly 
// -- it says it reconnects, but the display remains blank or noisy.
// inserting a short wait seems to avoid it. This wait in terms of frames:
#define RECONNECTION_TIME 100

static bool oculus_initialized = 0;


void oculusrift_quit() {
	if (oculus_initialized) ovr_Shutdown();
	oculus_initialized = 0;
}

int oculusrift_init() {
	if (oculus_initialized) return 1;

	// init OVR SDK
	ovrInitParams initParams = { ovrInit_RequestVersion, OVR_MINOR_VERSION, NULL, 0, 0 };
	ovrResult result = ovr_Initialize(&initParams);
	if (OVR_FAILURE(result)) {
		error("LibOVR: failed to initialize library");
		ovrErrorInfo errorInfo;
		ovr_GetLastErrorInfo(&errorInfo);
		error("ovr_Initialize failed: %s", errorInfo.ErrorString);
		/*
		switch (result) {
		case ovrError_Initialize: object_error(NULL, "Generic initialization error."); break;
		case ovrError_LibLoad: object_error(NULL, "Couldn't load LibOVRRT."); break;
		case ovrError_LibVersion: object_error(NULL, "LibOVRRT version incompatibility."); break;
		case ovrError_ServiceConnection: object_error(NULL, "Couldn't connect to the OVR Service."); break;
		case ovrError_ServiceVersion: object_error(NULL, "OVR Service version incompatibility."); break;
		case ovrError_IncompatibleOS: object_error(NULL, "The operating system version is incompatible."); break;
		case ovrError_DisplayInit: object_error(NULL, "Unable to initialize the HMD display."); break;
		case ovrError_ServerStart:  object_error(NULL, "Unable to start the server. Is it already running?"); break;
		case ovrError_Reinitialization: object_error(NULL, "Attempted to re-initialize with a different version."); break;
		default: object_error(NULL, "unknown initialization error."); break;
		}*/
		oculus_initialized = 0;
	} else {

		quittask_install((method)oculusrift_quit, NULL);

		ovr_IdentifyClient("EngineName: Max/MSP/Jitter\n"
			"EngineVersion: 7\n"
			"EnginePluginName: [oculusrift]\n"
			"EngineEditor: true");
		oculus_initialized = 1;
	}
	return oculus_initialized;
}

class oculusrift {
public:
	t_object ob; // must be first!
	void * ob3d;
	void * outlet_msg;
	void * outlet_tracking;
	void * outlet_node;
	void * outlet_eye[2];
	void * outlet_controller[2];
	void * outlet_tex;

	t_symbol * dest_name;
	t_symbol * intexture;
	float near_clip, far_clip;
	float pixel_density;
	int max_fov;
	int perfMode;
	int mirror;
	int tracking_level;

	void * outtexture;
	t_atom_long outdim[2];
	t_symbol * outname;

	int reconnect_wait;

	ovrSession session;
	ovrSessionStatus status;
	ovrHmdDesc hmd;
	ovrGraphicsLuid luid;
	ovrEyeRenderDesc eyeRenderDesc[2];
	ovrVector3f      hmdToEyeViewOffset[2];
	ovrLayerEyeFov layer;
	ovrSizei pTextureDim;
	ovrTextureSwapChain textureChain;
	ovrMirrorTexture mirrorTexture;
	long long frameIndex;
	double sensorSampleTime;    // sensorSampleTime is fed into the layer later


	GLuint fbo, fbomirror;

	oculusrift(t_symbol * dest_name) : dest_name(dest_name) {

		// init Max object:
		jit_ob3d_new(this, dest_name);
		// outlets create in reverse order:
		outlet_msg = outlet_new(&ob, NULL);
		outlet_controller[1] = outlet_new(&ob, NULL);
		outlet_controller[0] = outlet_new(&ob, NULL);
		outlet_tracking = outlet_new(&ob, NULL);
		outlet_node = outlet_new(&ob, NULL);
		outlet_eye[1] = outlet_new(&ob, NULL);
		outlet_eye[0] = outlet_new(&ob, NULL);
		outlet_tex = outlet_new(&ob, "jit_gl_texture");

		// init state
		fbo = 0;
		fbomirror = 0;
		textureChain = 0;
		frameIndex = 0;
		pTextureDim.w = 0;
		pTextureDim.h = 0;
		intexture = _sym_nothing;

		// init attrs
		perfMode = 0;
		near_clip = 0.15f;
		far_clip = 100.f;
		pixel_density = 1.f;
		max_fov = 0;
		mirror = 0;
		tracking_level = (int)ovrTrackingOrigin_FloorLevel;

		reconnect_wait = 0;

		outdim[0] = 1024;
		outdim[1] = 768;

	}

	// attempt to connect to the OVR runtime, creating a session:
	bool connect() {
		if (session) {
			object_warn(&ob, "already connected");
			return true;
		}

		ovrResult result;

		result = ovr_Create(&session, &luid);
		if (OVR_FAILURE(result)) {
			ovrErrorInfo errInfo;
			ovr_GetLastErrorInfo(&errInfo);
			object_error(&ob, "failed to create session: %s", errInfo.ErrorString);

			object_error(NULL, errInfo.ErrorString);
			return false;
		}

		object_post(&ob, "LibOVR SDK %s, runtime %s", OVR_VERSION_STRING, ovr_GetVersionString());

		outlet_anything(outlet_msg, gensym("connected"), 0, NULL);

		// update our session status
		ovr_GetSessionStatus(session, &status);


		configure();
		return true;
	}

	void disconnect() {
		if (session) {
			// destroy any OVR resources tied to the session:
			textureset_destroy();
			mirror_destroy();

			ovr_Destroy(session);
			session = 0;
			
			outlet_anything(outlet_msg, gensym("disconnected"), 0, NULL);


			reconnect_wait = RECONNECTION_TIME;
		}

		
	}

	// usually called after session is created, and when important attributes are changed
	// invokes info() to send configuration results 
	void configure() {
		if (!session) {
			object_error(&ob, "no session to configure");
			return;
		}

		// maybe never: support disabling tracking options via ovr_ConfigureTracking()

		hmd = ovr_GetHmdDesc(session);
		// Use hmd members and ovr_GetFovTextureSize() to determine graphics configuration

		ovrSizei recommenedTex0Size, recommenedTex1Size;
		//MaxEyeFov - Maximum optical field of view that can be practically rendered for each eye.
		if (max_fov){
			recommenedTex0Size = ovr_GetFovTextureSize(session, ovrEye_Left, hmd.MaxEyeFov[0], pixel_density);
			recommenedTex1Size = ovr_GetFovTextureSize(session, ovrEye_Right, hmd.MaxEyeFov[1], pixel_density);
		}
		else{
			recommenedTex0Size = ovr_GetFovTextureSize(session, ovrEye_Left, hmd.DefaultEyeFov[0], pixel_density);
			recommenedTex1Size = ovr_GetFovTextureSize(session, ovrEye_Right, hmd.DefaultEyeFov[1], pixel_density);
		}

		// assumes a single shared texture for both eyes:
		pTextureDim.w = recommenedTex0Size.w + recommenedTex1Size.w;
		pTextureDim.h = max(recommenedTex0Size.h, recommenedTex1Size.h);
		
		switch (tracking_level) {
		case int(ovrTrackingOrigin_FloorLevel): 
			// FloorLevel will give tracking poses where the floor height is 0
			// Tracking system origin reported at floor height.
			// Prefer using this origin when your application requires the physical floor height to match the virtual floor height, such as standing experiences. When used, all poses in ovrTrackingState are reported as an offset transform from the profile calibrated floor pose. Calling ovr_RecenterTrackingOrigin will recenter the X & Z axes as well as yaw, but the Y-axis (i.e. height) will continue to be reported using the floor height as the origin for all poses.
			ovr_SetTrackingOriginType(session, ovrTrackingOrigin_FloorLevel);			
			break;
		default:
			// Tracking system origin reported at eye (HMD) height.
			// Prefer using this origin when your application requires matching user's current physical head pose to a virtual head pose without any regards to a the height of the floor. Cockpit-based, or 3rd-person experiences are ideal candidates. When used, all poses in ovrTrackingState are reported as an offset transform from the profile calibrated or recentered HMD pose. 
			ovr_SetTrackingOriginType(session, ovrTrackingOrigin_EyeLevel);
		};

		// in case this is a re-configure, clear out the previous ones:
		if (1) {
			textureset_destroy();
			mirror_destroy();

			textureset_create();
			mirror_create();
		}

		// Initialize our single full screen Fov layer.
		// (needs to happen after textureset_create)
		layer.Header.Type = ovrLayerType_EyeFov;
		layer.Header.Flags = 0;// ovrLayerFlag_TextureOriginAtBottomLeft;   // Because OpenGL. was 0.
		layer.Viewport[0].Pos.x = 0;
		layer.Viewport[0].Pos.y = 0;
		layer.Viewport[0].Size.w = pTextureDim.w / 2;
		layer.Viewport[0].Size.h = pTextureDim.h;
		layer.Viewport[1].Pos.x = pTextureDim.w / 2;
		layer.Viewport[1].Pos.y = 0;
		layer.Viewport[1].Size.w = pTextureDim.w / 2;
		layer.Viewport[1].Size.h = pTextureDim.h;

		// other layer properties are updated later per frame.
		
		info();
	}

	void info() {
		if (!session) {
			object_warn(&ob, "no session");
			return;
		}

		ovrHmdDesc hmd = ovr_GetHmdDesc(session);
		t_atom a[2];

		// TODO complete list of useful info from https://developer.oculus.com/documentation/pcsdk/latest/concepts/dg-sensor/
#define HMD_CASE(T) case T: { \
            atom_setsym(a, gensym( #T )); \
            outlet_anything(outlet_msg, gensym("hmdType"), 1, a); \
            break; \
			        }
		switch (hmd.Type) {
			HMD_CASE(ovrHmd_CV1)
			HMD_CASE(ovrHmd_DK1)
				HMD_CASE(ovrHmd_DKHD)
				HMD_CASE(ovrHmd_DK2)
		default: {
				atom_setsym(a, gensym("unknown"));
				outlet_anything(outlet_msg, gensym("Type"), 1, a);
			}
		}
#undef HMD_CASE

		atom_setsym(a, gensym(hmd.SerialNumber));
		outlet_anything(outlet_msg, gensym("serial"), 1, a);

		atom_setsym(a, gensym(hmd.Manufacturer));
		outlet_anything(outlet_msg, gensym("Manufacturer"), 1, a);
		atom_setsym(a, gensym(hmd.ProductName));
		outlet_anything(outlet_msg, gensym("ProductName"), 1, a);

		atom_setlong(a, (hmd.VendorId));
		outlet_anything(outlet_msg, gensym("VendorId"), 1, a);
		atom_setlong(a, (hmd.ProductId));
		outlet_anything(outlet_msg, gensym("ProductId"), 1, a);
		/*
		// TODO: enable
		atom_setfloat(a, (hmd.CameraFrustumHFovInRadians));
		outlet_anything(outlet_msg, gensym("CameraFrustumHFovInRadians"), 1, a);
		atom_setfloat(a, (hmd.CameraFrustumVFovInRadians));
		outlet_anything(outlet_msg, gensym("CameraFrustumVFovInRadians"), 1, a);
		atom_setfloat(a, (hmd.CameraFrustumNearZInMeters));
		outlet_anything(outlet_msg, gensym("CameraFrustumNearZInMeters"), 1, a);
		atom_setfloat(a, (hmd.CameraFrustumFarZInMeters));
		outlet_anything(outlet_msg, gensym("CameraFrustumFarZInMeters"), 1, a);
		*/
		atom_setlong(a, (hmd.AvailableHmdCaps));
		outlet_anything(outlet_msg, gensym("AvailableHmdCaps"), 1, a);
		atom_setlong(a, (hmd.DefaultHmdCaps));
		outlet_anything(outlet_msg, gensym("DefaultHmdCaps"), 1, a);
		atom_setlong(a, (hmd.AvailableTrackingCaps));
		outlet_anything(outlet_msg, gensym("AvailableTrackingCaps"), 1, a);
		atom_setlong(a, (hmd.DefaultTrackingCaps));
		outlet_anything(outlet_msg, gensym("DefaultTrackingCaps"), 1, a);
		atom_setfloat(a, (hmd.DisplayRefreshRate));
		outlet_anything(outlet_msg, gensym("DisplayRefreshRate"), 1, a);

		atom_setlong(a, hmd.FirmwareMajor);
		atom_setlong(a + 1, hmd.FirmwareMinor);
		outlet_anything(outlet_msg, gensym("Firmware"), 2, a);

		ovrSizei resolution = hmd.Resolution;
		atom_setlong(a + 0, resolution.w);
		atom_setlong(a + 1, resolution.h);
		outlet_anything(outlet_msg, gensym("resolution"), 2, a);

		// send texture dim (determined via configure()) to the scene jit.gl.node:
		atom_setlong(a + 0, pTextureDim.w);
		atom_setlong(a + 1, pTextureDim.h);
		outlet_anything(outlet_node, _jit_sym_dim, 2, a);
	}

	~oculusrift() {
		// free GL resources created by this external
		dest_closing();
		// disconnect from session
		disconnect();
		// remove from jit.gl* hierarchy
		jit_ob3d_free(this);
		// actually delete object
		max_jit_object_free(this);
	}

	bool textureset_create() {
		if (!session) return false; 
		if (textureChain) return true; // already exists

		// TODO problem here: Jitter API GL headers don't export GL_SRGB8_ALPHA8
		// might also need  GL_EXT_framebuffer_sRGB for the copy
		// "your application should call glEnable(GL_FRAMEBUFFER_SRGB); before rendering into these textures."
		// SDK says:
		// Even though it is not recommended, if your application is configured to treat the texture as a linear 
		// format (e.g.GL_RGBA) and performs linear - to - gamma conversion in GLSL or does not care about gamma - 
		// correction, then:
		// Request an sRGB format(e.g.GL_SRGB8_ALPHA8) swap - texture - set.
		// Do not call glEnable(GL_FRAMEBUFFER_SRGB); when rendering into the swap texture.
		
		//auto result = ovr_CreateSwatextureChainGL(session, GL_RGBA8, pTextureDim.w, pTextureDim.h, &textureChain);
		//auto result = ovr_CreateSwatextureChainGL(session, GL_SRGB8_ALPHA8, pTextureDim.w, pTextureDim.h, &textureChain);
		ovrTextureSwapChainDesc desc = {};
		desc.Type = ovrTexture_2D;
		desc.ArraySize = 1;
		desc.Width = pTextureDim.w;
		desc.Height = pTextureDim.h;
		desc.MipLevels = 1;
		desc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
		desc.SampleCount = 1;
		desc.StaticImage = ovrFalse;
		ovrResult result = ovr_CreateTextureSwapChainGL(session, &desc, &textureChain); 
		if (result != ovrSuccess) {
			ovrErrorInfo errInfo;
			ovr_GetLastErrorInfo(&errInfo);
			object_error(&ob, "failed to create texture set: %s", errInfo.ErrorString);
			return false;
		}

		int length = 0;
		ovr_GetTextureSwapChainLength(session, textureChain, &length);

		// we can update the layer too here:
		layer.ColorTexture[0] = textureChain;
		layer.ColorTexture[1] = textureChain;
		
		return true;
	}

	void textureset_destroy() {
		if (session && textureChain) {
			ovr_DestroyTextureSwapChain(session, textureChain);
			textureChain = 0;
		}
	}

	/*
	Frame rendering typically involves several steps:
	- obtaining predicted eye poses based on the headset tracking pose, (bang)
	- rendering the view for each eye and, finally, (jit.gl.node @capture 1 renders and then sends texture to this external)
	- submitting eye textures to the compositor through ovr_SubmitFrame. (submit, in response to texture received)
	*/

	void bang() {
		if (!session) {
			ovrResult res = ovr_GetSessionStatus(session, &status);
			if (status.ShouldQuit) {
				// the HMD display will return to Oculus Home
				// don't want to quit, but at least notify patcher:
				outlet_anything(outlet_msg, gensym("quit"), 0, NULL);

				disconnect();
				return;
			}
			if (status.ShouldRecenter) {
				ovr_RecenterTrackingOrigin(session);

				/*
				Expose attr to defeat this?

				Some applications may have reason to ignore the request or to implement it 
				via an internal mechanism other than via ovr_RecenterTrackingOrigin. In such 
				cases the application can call ovr_ClearShouldRecenterFlag() to cause the 
				recenter request to be cleared.
				*/
			}

			if (!status.HmdPresent) {
				// TODO: disconnect?
				return;
			}

			if (status.DisplayLost) {
				/*
				Destroy any TextureSwapChains or mirror textures.
				Call ovrDestroy.
				Poll ovrSessionStatus::HmdPresent until true.
				Call ovrCreate to recreate the session.
				Recreate any TextureSwapChains or mirror textures.
				Resume the application.
				ovrDetect() ??
				*/
			}

			// TODO: expose these as gettable attrs?
			//status.HmdMounted // true if the HMD is currently on the head
			// status.IsVisible // True if the game or experience has VR focus and is visible in the HMD.

			// TODO: does SDK provide notification of Rift being reconnected?

			/*
			if (reconnect_wait) {
				reconnect_wait--;
			}
			else {
				post("reconnecting...");
				if (!connect()) {
					reconnect_wait = RECONNECTION_TIME;
					
				}
				return;
			}*/
			return;
		}

		t_atom a[6];

		// get 'modelview'
		float pos[3];
		jit_attr_getfloat_array(this, gensym("position"), 3, pos);
		t_jit_quat quat;
		jit_attr_getfloat_array(this, gensym("quat"), 4, &quat.x);

		//////////////////////////////

		// Call ovr_GetRenderDesc each frame to get the ovrEyeRenderDesc, as the returned values (e.g. HmdToEyeOffset) may change at runtime.
		if (max_fov) {
			eyeRenderDesc[0] = ovr_GetRenderDesc(session, ovrEye_Left, hmd.MaxEyeFov[0]);
			eyeRenderDesc[1] = ovr_GetRenderDesc(session, ovrEye_Right, hmd.MaxEyeFov[1]);

		}
		else {
			eyeRenderDesc[0] = ovr_GetRenderDesc(session, ovrEye_Left, hmd.DefaultEyeFov[0]);
			eyeRenderDesc[1] = ovr_GetRenderDesc(session, ovrEye_Right, hmd.DefaultEyeFov[1]);
		}
		hmdToEyeViewOffset[0] = eyeRenderDesc[0].HmdToEyeOffset;
		hmdToEyeViewOffset[1] = eyeRenderDesc[1].HmdToEyeOffset;

		// Get eye poses, feeding in correct IPD offset
		ovr_GetEyePoses(session, frameIndex, ovrTrue, hmdToEyeViewOffset, layer.RenderPose, &sensorSampleTime);

		// update the camera view matrices accordingly:
		for (int eye = 0; eye < 2; ++eye)
		{
			// let's update the layer info too:
			layer.Fov[eye] = eyeRenderDesc[eye].Fov;
			layer.SensorSampleTime = sensorSampleTime;
			
			// modelview
			const ovrVector3f p = layer.RenderPose[eye].Position;
			atom_setfloat(a + 0, p.x + pos[0]);
			atom_setfloat(a + 1, p.y + pos[1]);
			atom_setfloat(a + 2, p.z + pos[2]);
			outlet_anything(outlet_eye[eye], _jit_sym_position, 3, a);

			const ovrQuatf q = layer.RenderPose[eye].Orientation;
			t_jit_quat q1;
			jit_quat_mult(&q1, (t_jit_quat *)&q, &quat);
			atom_setfloat(a + 0, q1.x);
			atom_setfloat(a + 1, q1.y);
			atom_setfloat(a + 2, q1.z);
			atom_setfloat(a + 3, q1.w);
			outlet_anything(outlet_eye[eye], _jit_sym_quat, 4, a);

			// TODO: proj matrix doesn't need to be calculated every frame; only when near/far/layer data changes
			// projection
			const ovrFovPort& fov = layer.Fov[eye];
			atom_setfloat(a + 0, -fov.LeftTan * near_clip);
			atom_setfloat(a + 1, fov.RightTan * near_clip);
			atom_setfloat(a + 2, -fov.DownTan * near_clip);
			atom_setfloat(a + 3, fov.UpTan * near_clip);
			atom_setfloat(a + 4, near_clip);
			atom_setfloat(a + 5, far_clip);
			outlet_anything(outlet_eye[eye], ps_frustum, 6, a);
		}
		//////////////////////////////

		//KC: Seems to be working just fine(?)
		
		// Query the HMD for the current tracking state.
		// Get both eye poses simultaneously, with IPD offset already included.
		double displayMidpointSeconds = ovr_GetPredictedDisplayTime(session, frameIndex);
		ovrTrackingState ts = ovr_GetTrackingState(session, displayMidpointSeconds, ovrTrue);
		ovrPosef         handPoses[2];
		ovrInputState    inputState;
		if (ts.StatusFlags & (ovrStatus_OrientationTracked | ovrStatus_PositionTracked)) {
			// get current head pose
			const ovrPosef& pose = ts.HeadPose.ThePose;

			// use the tracking state to update the layers (part of how timewarp works)
			ovr_CalcEyePoses(pose, hmdToEyeViewOffset, layer.RenderPose);

			float pos[3];
			jit_attr_getfloat_array(this, gensym("position"), 3, pos);
			t_jit_quat quat;
			jit_attr_getfloat_array(this, gensym("quat"), 4, &quat.x);

			// update the camera view matrices accordingly:
			for (int eye = 0; eye < 2; eye++) {

				// TODO: add navigation pose to this before outputting, or do that in the patcher afterward?

				// modelview
				const ovrVector3f p = layer.RenderPose[eye].Position;
				atom_setfloat(a + 0, p.x + pos[0]);
				atom_setfloat(a + 1, p.y + pos[1]);
				atom_setfloat(a + 2, p.z + pos[2]);
				outlet_anything(outlet_eye[eye], _jit_sym_position, 3, a);

				const ovrQuatf q = layer.RenderPose[eye].Orientation;
				t_jit_quat q1;
				jit_quat_mult(&q1, (t_jit_quat *)&q, &quat);
				atom_setfloat(a + 0, q1.x);
				atom_setfloat(a + 1, q1.y);
				atom_setfloat(a + 2, q1.z);
				atom_setfloat(a + 3, q1.w);
				outlet_anything(outlet_eye[eye], _jit_sym_quat, 4, a);

				// TODO: proj matrix doesn't need to be calculated every frame; only when near/far/layer data changes
				// projection
				const ovrFovPort& fov = layer.Fov[eye];
				atom_setfloat(a + 0, -fov.LeftTan * near_clip);
				atom_setfloat(a + 1, fov.RightTan * near_clip);
				atom_setfloat(a + 2, -fov.DownTan * near_clip);
				atom_setfloat(a + 3, fov.UpTan * near_clip);
				atom_setfloat(a + 4, near_clip);
				atom_setfloat(a + 5, far_clip);
				outlet_anything(outlet_eye[eye], ps_frustum, 6, a);
			}

			// Headset tracking data:
			{
				ovrVector3f p = pose.Position;
				ovrQuatf q = pose.Orientation;
				t_jit_quat q1;
				jit_quat_mult(&q1, (t_jit_quat *)&q, &quat);

				atom_setfloat(a + 0, p.x);
				atom_setfloat(a + 1, p.y);
				atom_setfloat(a + 2, p.z);
				outlet_anything(outlet_tracking, ps_tracked_position, 3, a);

				atom_setfloat(a + 0, q.x);
				atom_setfloat(a + 1, q.y);
				atom_setfloat(a + 2, q.z);
				atom_setfloat(a + 3, q.w);
				outlet_anything(outlet_tracking, ps_tracked_quat, 4, a);

				atom_setfloat(a + 0, p.x + pos[0]);
				atom_setfloat(a + 1, p.y + pos[1]);
				atom_setfloat(a + 2, p.z + pos[2]);
				outlet_anything(outlet_tracking, _jit_sym_position, 3, a);

				atom_setfloat(a + 0, q1.x);
				atom_setfloat(a + 1, q1.y);
				atom_setfloat(a + 2, q1.z);
				atom_setfloat(a + 3, q1.w);
				outlet_anything(outlet_tracking, _jit_sym_quat, 4, a);
			}

			// Grab hand poses useful for rendering hand or controller representation
			handPoses[ovrHand_Left] = ts.HandPoses[ovrHand_Left].ThePose;
			handPoses[ovrHand_Right] = ts.HandPoses[ovrHand_Right].ThePose;

			if (OVR_SUCCESS(ovr_GetInputState(session, ovrControllerType_Touch, &inputState)))
			{
				for (int i = 0; i < 2; i++) {
					// it may be useful to have the pose information in Max for other purposes:
					ovrVector3f p = handPoses[i].Position;
					ovrQuatf q = handPoses[i].Orientation;

					ovrVector3f vel = ts.HandPoses[i].LinearVelocity;
					ovrVector3f angvel = ts.HandPoses[i].AngularVelocity;

					t_jit_quat q1;
					jit_quat_mult(&q1, (t_jit_quat *)&q, &quat);

					atom_setfloat(a + 0, p.x);
					atom_setfloat(a + 1, p.y);
					atom_setfloat(a + 2, p.z);
					outlet_anything(outlet_controller[i], ps_tracked_position, 3, a);

					atom_setfloat(a + 0, q.x);
					atom_setfloat(a + 1, q.y);
					atom_setfloat(a + 2, q.z);
					atom_setfloat(a + 3, q.w);
					outlet_anything(outlet_controller[i], ps_tracked_quat, 4, a);

					atom_setfloat(a + 0, p.x + pos[0]);
					atom_setfloat(a + 1, p.y + pos[1]);
					atom_setfloat(a + 2, p.z + pos[2]);
					outlet_anything(outlet_controller[i], _jit_sym_position, 3, a);

					atom_setfloat(a + 0, q1.x);
					atom_setfloat(a + 1, q1.y);
					atom_setfloat(a + 2, q1.z);
					atom_setfloat(a + 3, q1.w);
					outlet_anything(outlet_controller[i], _jit_sym_quat, 4, a);

					atom_setfloat(a + 0, vel.x);
					atom_setfloat(a + 1, vel.y);
					atom_setfloat(a + 2, vel.z);
					outlet_anything(outlet_controller[i], ps_velocity, 3, a);

					atom_setfloat(a + 0, angvel.x);
					atom_setfloat(a + 1, angvel.y);
					atom_setfloat(a + 2, angvel.z);
					outlet_anything(outlet_controller[i], ps_angular_velocity, 3, a);

					atom_setlong(a + 0, inputState.IndexTrigger[i] > 0.25);
					atom_setfloat(a + 1, inputState.IndexTrigger[i]);
					outlet_anything(outlet_controller[i], ps_trigger, 2, a);

					atom_setlong(a + 0, inputState.HandTrigger[i] > 0.25);
					atom_setfloat(a + 1, inputState.HandTrigger[i]);
					outlet_anything(outlet_controller[i], ps_hand_trigger, 2, a);

					atom_setlong(a + 0, inputState.Touches & (i ? ovrButton_RThumb : ovrButton_LThumb));
					atom_setfloat(a + 1, inputState.Thumbstick[i].x);
					atom_setfloat(a + 2, inputState.Thumbstick[i].y);
					atom_setlong(a + 3, inputState.Buttons & (i ? ovrButton_RThumb : ovrButton_LThumb));
					outlet_anything(outlet_controller[i], ps_thumbstick, 4, a);

					atom_setlong(a + 0, inputState.Buttons & (i ? ovrButton_A : ovrButton_X));
					atom_setlong(a + 1, inputState.Buttons & (i ? ovrButton_B : ovrButton_Y));
					outlet_anything(outlet_controller[i], ps_buttons, 2, a);
				}
				
				if (inputState.Buttons & ovrButton_A)
				{
					// Handle A button being pressed
					post("A pressed");
				}
				if (inputState.HandTrigger[ovrHand_Left] > 0.5f)
				{
					// Handle hand grip...
				}
			}
		}
		
	}

	// receive a texture
	// TODO: validate texture format?
	void jit_gl_texture(t_symbol * s) {
		intexture = s;
		submit();
	}

	// send the current texture to the Oculus driver:
	void submit() {
		t_atom a[1];
		if (!session || !status.HmdPresent || !status.IsVisible) return;

		void * texob = jit_object_findregistered(intexture);
		if (!texob) {
			object_error(&ob, "no texture to draw");
			return;	// no texture to copy from.
		}
		// TODO: verify that texob is a texture
		long glid = jit_attr_getlong(texob, ps_glid);
		// get input texture dimensions
		t_atom_long texdim[2];
		jit_attr_getlong_array(texob, _sym_dim, 2, texdim);
		//post("submit texture id %ld dim %ld %ld\n", glid, texdim[0], texdim[1]);

		if (!fbo) {
			object_error(&ob, "no fbo yet");
			return;	// no texture to copy from.
		}

		if (!textureChain) {
			object_error(&ob, "no texture set yet");
			return;
		}

		///////////////////////////

		// get our next destination texture in the texture chain:
		int curIndex;
		ovr_GetTextureSwapChainCurrentIndex(session, textureChain, &curIndex);
		GLuint dstId;
		ovr_GetTextureSwapChainBufferGL(session, textureChain, curIndex, &dstId);

		// copy our input texture into this:
		submit_copy(glid, texdim[0], texdim[1], dstId, pTextureDim.w, pTextureDim.h);

		// and commit it
		ovr_CommitTextureSwapChain(session, textureChain);

		///////////////////////////

		/*
		// Increment to use next texture, just before writing
		textureChain->CurrentIndex = (textureChain->CurrentIndex + 1) % textureChain->TextureCount;
		// TODO? Clear and set up render-target.    

		ovrGLTexture* tex = (ovrGLTexture*)&textureChain->Textures[textureChain->CurrentIndex];
		GLuint dstId = tex->OGL.TexId;

		submit_copy(glid, texdim[0], texdim[1], dstId, pTextureDim.w, pTextureDim.h);
		*/

		// Submit frame with one layer we have.
		// ovr_SubmitFrame returns once frame present is queued up and the next texture slot in the ovrSwatextureChain is available for the next frame. 
		ovrLayerHeader* layers = &layer.Header;
		ovrResult       result = ovr_SubmitFrame(session, frameIndex, nullptr, &layers, 1);
		if (result == ovrError_DisplayLost) {
			/*
			TODO: If you receive ovrError_DisplayLost, the device was removed and the session is invalid.
			Release the shared resources (ovr_DestroySwatextureChain), destroy the session (ovr_Destory),
			recreate it (ovr_Create), and create new resources (ovr_CreateSwatextureChainXXX).
			The application's existing private graphics resources do not need to be recreated unless
			the new ovr_Create call returns a different GraphicsLuid.
			*/
			object_error(&ob, "fatal error connection lost.");

			disconnect();
		}
		else {
			frameIndex++;

			t_atom a[1];
			atom_setsym(a, intexture);

			mirror_output(a);

			outlet_anything(outlet_tex, ps_jit_gl_texture, 1, a);
		}
	}
	

	void submit_copy(GLuint srcID, int srcWidth, int srcHeight, GLuint dstID, int width, int height) {

		// save some state
		GLint previousFBO;	// make sure we pop out to the right FBO
		GLint previousMatrixMode;

		glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &previousFBO);
		glGetIntegerv(GL_MATRIX_MODE, &previousMatrixMode);

		// save texture state, client state, etc.
		glPushAttrib(GL_ALL_ATTRIB_BITS);
		glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

		// TODO use rectangle 1?
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, dstID, 0);
		if (fbo_check()) {	
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
			glEnable(GL_TEXTURE_RECTANGLE_ARB);
			glBindTexture(GL_TEXTURE_RECTANGLE_ARB, srcID);

			// do not need blending if we use black border for alpha and replace env mode, saves a buffer wipe
			// we can do this since our image draws over the complete surface of the FBO, no pixel goes untouched.

			glDisable(GL_BLEND);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

			// move to VA for rendering
			GLfloat tex_coords[] = {
				srcWidth, 0.,
				0.0, 0.,
				0.0, srcHeight,
				srcWidth, srcHeight
			};

			GLfloat verts[] = {
				width, height,
				0.0, height,
				0.0, 0.0,
				width, 0.0
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

	bool mirror_output(t_atom * a) {
		if (!mirror) return false;
		if (!mirrorTexture) {
			 object_error(&ob, "no mirror texture");
			 return false;
		 }

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


		// get source texture:
		GLuint texId;
		ovr_GetMirrorTextureBufferGL(session, mirrorTexture, &texId);

		//ovrGLTexture * tex = (ovrGLTexture*)mirrorTexture;
		ovrSizei mirrorTexDim = pTextureDim; // mirrorTexture->Header.TextureSize;
		

		// cache/restore context in case in capture mode
		// TODO: necessary ? JKC says no unless context changed above? should be set during draw for you. 
		//t_jit_gl_context ctx = jit_gl_get_context();
		//jit_ob3d_set_context(this);

		// add texture to OB3D list.
		jit_attr_setsym(this, gensym("texture"), dst);

		// update texture dim to match mirror:
		outdim[0] = mirrorTexDim.w;
		outdim[1] = mirrorTexDim.h;
		jit_attr_setlong_array(outtexture, _jit_sym_dim, 2, outdim);

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
				width, height,
				0.0, height,
				0.0, 0.0,
				width, 0.0
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

		return true;
	}


	void perf() {
		// just toggle through the various perf modes
		// see https://developer.oculus.com/documentation/pcsdk/latest/concepts/dg-hud/
		perfMode = (perfMode + 1) % ovrPerfHud_Count;
		ovr_SetInt(session, OVR_PERF_HUD_MODE, perfMode);
	}


	t_jit_err draw() {
		// this gets called when the jit.gl.render context updates clients
		// the oculusrift object doesn't draw to the main scene, so there's nothing needed to be done here
		return JIT_ERR_NONE;
	}

	t_jit_err dest_changed() {
		if (!session) connect();
		//object_post(&ob, "dest_changed");

		t_symbol *context = jit_attr_getsym(this, gensym("drawto"));

		glGenFramebuffersEXT(1, &fbo);
		glGenFramebuffersEXT(1, &fbomirror);

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
		return JIT_ERR_NONE;
	}

	// free any locally-allocated GL resources
	t_jit_err dest_closing() {
		//object_post(&ob, "dest_closing");
		disconnect();

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
		/*
		 post("line (%f,%f,%f)-(%f,%f,%f); mouse(%s)",
		 p_line->u[0], p_line->u[1], p_line->u[2],
		 p_line->v[0], p_line->v[1], p_line->v[2],
		 p_mouse->mousesymbol->s_name			// mouse, mouseidle
		 );
		 */
		return JIT_ERR_NONE;
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

	void mirror_create() {
		if (session && !mirrorTexture) {
			ovrMirrorTextureDesc desc;
			memset(&desc, 0, sizeof(desc));
			desc.Width = pTextureDim.w;
			desc.Height = pTextureDim.h;
			desc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
			
			// TODO SRGB?
			auto result = ovr_CreateMirrorTextureGL(session, &desc, &mirrorTexture);
			if (result != ovrSuccess) {
				ovrErrorInfo errInfo;
				ovr_GetLastErrorInfo(&errInfo);
				object_error(&ob, "failed to create mirror texture: %s", errInfo.ErrorString);

				// Sample texture access:
				//ovrGLTexture* tex = (ovrGLTexture*)mirrorTexture;
				//glBindTexture(GL_TEXTURE_2D, tex->OGL.TexId);
				//...
				//glBindTexture(GL_TEXTURE_2D, 0);
			}
		}
	}

	void mirror_destroy() {
		if (session) {
			if (mirrorTexture) {
				ovr_DestroyMirrorTexture(session, mirrorTexture);
				mirrorTexture = 0;
			}
		}
	}

};


void * oculusrift_new(t_symbol *s, long argc, t_atom *argv) {
	oculusrift *x = NULL;
	if ((x = (oculusrift *)object_alloc(max_class))) {// get context:
		t_symbol * dest_name = atom_getsym(argv);
		
		x = new (x)oculusrift(dest_name);
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
	}
	return (x);
}

void oculusrift_free(oculusrift *x) {
	x->~oculusrift();
}

t_jit_err oculusrift_draw(oculusrift * x) { return x->draw(); }
t_jit_err oculusrift_ui(oculusrift * x, t_line_3d *p_line, t_wind_mouse_info *p_mouse) { return x->ui(p_line, p_mouse); }
t_jit_err oculusrift_dest_closing(oculusrift * x) { return x->dest_closing(); }
t_jit_err oculusrift_dest_changed(oculusrift * x) { return x->dest_changed(); }

void oculusrift_assist(oculusrift *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "bang to update tracking, texture to submit, other messages");
	} else {	// outlet
		switch (a) {
		case 0: sprintf(s, "output/mirror texture"); break;
		case 1: sprintf(s, "to left eye camera"); break;
		case 2: sprintf(s, "to right eye camera"); break;
		case 3: sprintf(s, "to scene node (set texture dim)"); break;
		case 4: sprintf(s, "tracking state"); break;
		case 5: sprintf(s, "left controller"); break;
		case 6: sprintf(s, "right controller"); break;
		default: sprintf(s, "other messages"); break;
		//default: sprintf(s, "I am outlet %ld", a); break;
		}
	}
}

void oculusrift_connect(oculusrift * x) {
	x->connect();
}

void oculusrift_disconnect(oculusrift * x) {
	x->disconnect();
}

void oculusrift_configure(oculusrift * x) {
	x->configure();
}

void oculusrift_info(oculusrift * x) {
	x->info();
}

void oculusrift_bang(oculusrift * x) {
	x->bang();
}

void oculusrift_submit(oculusrift * x) {
	x->submit();
}

void oculusrift_perf(oculusrift * x) {
	x->perf();
}

void oculusrift_recenter(oculusrift * x) {
	if (x->session) ovr_RecenterTrackingOrigin(x->session);
}

t_max_err oculusrift_pixel_density_set(oculusrift *x, t_object *attr, long argc, t_atom *argv) {
	x->pixel_density = atom_getfloat(argv);

	x->configure();
	return 0;
}

t_max_err oculusrift_max_fov_set(oculusrift *x, t_object *attr, long argc, t_atom *argv) {
	x->max_fov = atom_getlong(argv);

	x->configure();
	return 0;
}

t_max_err oculusrift_tracking_level_set(oculusrift *x, t_object *attr, long argc, t_atom *argv) {
	x->tracking_level = atom_getlong(argv);

	x->configure();
	return 0;
}

void oculusrift_jit_gl_texture(oculusrift * x, t_symbol * s, long argc, t_atom * argv) {
	if (argc > 0 && atom_gettype(argv) == A_SYM) {
		x->jit_gl_texture(atom_getsym(argv));
	}
}

// Application Loop:
//  - Call ovr_GetPredictedDisplayTime() to get the current frame timing information.
//  - Call ovr_GetTrackingState() and ovr_CalcEyePoses() to obtain the predicted
//    rendering pose for each eye based on timing.
//  - Increment ovrTextureSet::CurrentIndex for each layer you will be rendering to 
//    in the next step.
//  - Render the scene content into ovrTextureSet::CurrentIndex for each eye and layer
//    you plan to update this frame. 
//  - Call ovr_SubmitFrame() to render the distorted layers to the back buffer
//    and present them on the HMD. If ovr_SubmitFrame returns ovrSuccess_NotVisible,
//    there is no need to render the scene for the next loop iteration. Instead,
//    just call ovr_SubmitFrame again until it returns ovrSuccess. ovrTextureSet::CurrentIndex 
//    for each layer should refer to the texure you want to display.
//


void oculusrift_log(int level, const char* message) {
	post("oculus log %d %s", level, message);
}

void ext_main(void *r)
{
	t_class *c;
	ovrResult result;

	common_symbols_init();
	ps_quat = gensym("quat");
	ps_pos = gensym("pos");
	ps_viewport = gensym("viewport");
	ps_frustum = gensym("frustum");
	ps_warning = gensym("warning");
	ps_glid = gensym("glid");
	ps_jit_gl_texture = gensym("jit_gl_texture");

	ps_velocity = gensym("velocity");
	ps_angular_velocity = gensym("angular_velocity");
	ps_tracked_position = gensym("tracked_position");
	ps_tracked_quat = gensym("tracked_quat");

	ps_trigger = gensym("trigger");
	ps_hand_trigger = gensym("hand_trigger");
	ps_buttons = gensym("buttons");
	ps_thumbstick = gensym("thumbstick");

	// init OVR SDK
	if (!oculusrift_init()) return;

	c = class_new("oculusrift", (method)oculusrift_new, (method)oculusrift_free, (long)sizeof(oculusrift),
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
	
	void * ob3d = jit_ob3d_setup(c, calcoffset(oculusrift, ob3d), ob3d_flags);
	// define our OB3D draw methods
	//jit_class_addmethod(c, (method)(oculusrift_draw), "ob3d_draw", A_CANT, 0L);
	jit_class_addmethod(c, (method)(oculusrift_dest_closing), "dest_closing", A_CANT, 0L);
	jit_class_addmethod(c, (method)(oculusrift_dest_changed), "dest_changed", A_CANT, 0L);
	if (ob3d_flags & JIT_OB3D_DOES_UI) {
		jit_class_addmethod(c, (method)(oculusrift_ui), "ob3d_ui", A_CANT, 0L);
	}
	// must register for ob3d use
	jit_class_addmethod(c, (method)jit_object_register, "register", A_CANT, 0L);
	

	
	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)oculusrift_assist,			"assist",		A_CANT, 0);


	class_addmethod(c, (method)oculusrift_jit_gl_texture, "jit_gl_texture", A_GIMME, 0);

	class_addmethod(c, (method)oculusrift_connect, "connect", 0);
	class_addmethod(c, (method)oculusrift_disconnect, "disconnect", 0);
	class_addmethod(c, (method)oculusrift_configure, "configure", 0);
	class_addmethod(c, (method)oculusrift_info, "info", 0);


	class_addmethod(c, (method)oculusrift_recenter, "recenter", 0);

	class_addmethod(c, (method)oculusrift_bang, "bang", 0);
	class_addmethod(c, (method)oculusrift_submit, "submit", 0);
	class_addmethod(c, (method)oculusrift_perf, "perf", 0);

	CLASS_ATTR_FLOAT(c, "near_clip", 0, oculusrift, near_clip);
	CLASS_ATTR_FLOAT(c, "far_clip", 0, oculusrift, far_clip);

	CLASS_ATTR_FLOAT(c, "pixel_density", 0, oculusrift, pixel_density);
	CLASS_ATTR_ACCESSORS(c, "pixel_density", NULL, oculusrift_pixel_density_set);

	// TODO: why is Rift not using max FOV (seems like the black overlay is not being made bigger - oculus bug?)
	CLASS_ATTR_LONG(c, "max_fov", 0, oculusrift, max_fov);
	CLASS_ATTR_ACCESSORS(c, "max_fov", NULL, oculusrift_max_fov_set);
	CLASS_ATTR_STYLE_LABEL(c, "max_fov", 0, "onoff", "use maximum field of view");

	CLASS_ATTR_LONG(c, "mirror", 0, oculusrift, mirror);
	CLASS_ATTR_STYLE_LABEL(c, "mirror", 0, "onoff", "mirror oculus display in main window");

	CLASS_ATTR_LONG(c, "tracking_level", 0, oculusrift, tracking_level);
	CLASS_ATTR_ACCESSORS(c, "tracking_level", NULL, oculusrift_tracking_level_set);

	
	class_register(CLASS_BOX, c);
	max_class = c;
}

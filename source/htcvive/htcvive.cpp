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

//
/*
// things we need from the GL headers
typedef void (APIENTRY * PFNGLTEXIMAGE2DMULTISAMPLEPROC)(GLenum target, GLsizei samples, GLint internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations);
extern PFNGLTEXIMAGE2DMULTISAMPLEPROC _funcptr_glTexImage2DMultisample;
#define glTexImage2DMultisample _funcptr_glTexImage2DMultisample
typedef void (APIENTRY * PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC)(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
extern PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC _funcptr_glRenderbufferStorageMultisample;
#define glRenderbufferStorageMultisample _funcptr_glRenderbufferStorageMultisample
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
*/
// The OpenVR SDK:
#include "openvr.h"

#include <new> // for in-place constructor

// how many glm headers do we really need?
#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/matrix_access.hpp"
#include "glm/gtc/matrix_inverse.hpp"
#include "glm/gtc/matrix_transform.hpp"
//#include "glm/gtc/noise.hpp"
//#include "glm/gtc/random.hpp"
//#include "glm/gtc/type_ptr.hpp"

// unstable extensions
//#include "glm/gtx/norm.hpp"
//#include "glm/gtx/std_based_type.hpp"

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::quat;
using glm::mat4;
//typedef glm::detail::tvec4<glm::detail::uint8> ucvec4;


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

//	q must be a normalized quaternion
template<typename T, glm::precision P>
glm::tvec3<T, P> quat_unrotate(glm::quat const & q, glm::tvec3<T, P> & v) {
	// return quat_mul(quat_mul(quat_conj(q), vec4(v, 0)), q).xyz;
	// reduced:
	glm::tvec4<T, P> p(
		q.w*v.x - q.y*v.z + q.z*v.y,  // x
		q.w*v.y - q.z*v.x + q.x*v.z,  // y
		q.w*v.z - q.x*v.y + q.y*v.x,  // z
		q.x*v.x + q.y*v.y + q.z*v.z   // w
		);
	return glm::tvec3<T, P>(
		p.w*q.x + p.x*q.w + p.y*q.z - p.z*q.y,  // x
		p.w*q.y + p.y*q.w + p.z*q.x - p.x*q.z,  // y
		p.w*q.z + p.z*q.w + p.x*q.y - p.y*q.x   // z
		);
}

//	q must be a normalized quaternion
template<typename T, glm::precision P>
glm::tvec3<T, P> quat_rotate(glm::quat const & q, glm::tvec3<T, P> & v) {
	glm::tvec4<T, P> p(
		q.w*v.x + q.y*v.z - q.z*v.y,	// x
		q.w*v.y + q.z*v.x - q.x*v.z,	// y
		q.w*v.z + q.x*v.y - q.y*v.x,	// z
		-q.x*v.x - q.y*v.y - q.z*v.z	// w
		);
	return glm::tvec3<T, P>(
		p.x*q.w - p.w*q.x + p.z*q.y - p.y*q.z,	// x
		p.y*q.w - p.w*q.y + p.x*q.z - p.z*q.x,	// y
		p.z*q.w - p.w*q.z + p.y*q.x - p.x*q.y	// z
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


class htcvive {
public:
	t_object ob; // must be first!
	void * ob3d;
	void * outlet_msg;
	void * outlet_tracking;
	void * outlet_node;
	void * outlet_eye[2];
	void * outlet_tex;

	t_symbol * dest_name;
	t_symbol * intexture;

	t_symbol * outname;
	void * outtexture;
	t_atom_long outdim[2];



	// attrs:
	float near_clip, far_clip;
	int mirror;

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

	htcvive(t_symbol * dest_name) : dest_name(dest_name) {

		// init Max object:
		jit_ob3d_new(this, dest_name);
		// outlets create in reverse order:
		outlet_msg = outlet_new(&ob, NULL);
		outlet_tracking = outlet_new(&ob, NULL);
		outlet_node = outlet_new(&ob, NULL);
		outlet_eye[1] = outlet_new(&ob, NULL);
		outlet_eye[0] = outlet_new(&ob, NULL);
		outlet_tex = outlet_new(&ob, "jit_gl_texture");

		mHMD = 0;
		mHMDPose = glm::mat4(1.f);

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
	}

	// attempt to connect to the OVR runtime, creating a session:
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


		configure();

		outlet_anything(outlet_msg, gensym("connected"), 0, NULL);
		return true;
	}

	void disconnect() {
		if (mHMD) {
			vr::VR_Shutdown();
			mHMD = 0;
		}
		
	}

	// usually called after session is created, and when important attributes are changed
	// invokes info() to send configuration results 
	void configure() {
		t_atom a[6];
		if (!mHMD) return;

		//mDriver = gensym(vr::GetTrackedDeviceString(mHMD, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String);
		//mDisplay = GetTrackedDeviceString(mHMD, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SerialNumber_String);

		mHMD->GetRecommendedRenderTargetSize(&texdim_w, &texdim_h);

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
		jit_attr_getfloat_array(this, gensym("position"), 3, &m_position.x);
		t_jit_quat m_jitquat;
		jit_attr_getfloat_array(this, gensym("quat"), 4, &m_jitquat.x);
		
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


		// check each device:
		for (int i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
			const vr::TrackedDevicePose_t& trackedDevicePose = pRenderPoseArray[i];
			if (trackedDevicePose.bPoseIsValid && trackedDevicePose.bDeviceIsConnected) {
				mDevicePose[i] = mat4_from_openvr(trackedDevicePose.mDeviceToAbsoluteTracking);
				
				
				switch (mHMD->GetTrackedDeviceClass(i)) {
				case vr::TrackedDeviceClass_HMD: {
					// this is the view matrix relative to the 'chaperone' space origin
					// (the center of the floor space in the real world)
					mHMDPose = mDevicePose[i];
					
					// probably want to output this for navigation etc. use
					glm::vec3 p = glm::vec3(mHMDPose[3]); // the translation component
					atom_setfloat(a + 0, p.x);
					atom_setfloat(a + 1, p.y);
					atom_setfloat(a + 2, p.z);
					outlet_anything(outlet_tracking, _jit_sym_position, 3, a);

					glm::quat q = glm::quat_cast(mHMDPose);
					//q = glm::normalize(q);
					atom_setfloat(a + 0, q.x);
					atom_setfloat(a + 1, q.y);
					atom_setfloat(a + 2, q.z);
					atom_setfloat(a + 3, q.w);
					outlet_anything(outlet_tracking, _jit_sym_quat, 4, a);
				}
				break;
				default:
				break;
				}

				/*
				// TODO: output controller states here?
				// check role to see if these are hands
				vr::ETrackedControllerRole role = mHMD->GetControllerRoleForTrackedDeviceIndex(i);
				switch (role)
				{
				case vr::TrackedControllerRole_LeftHand:
					break;
				case vr::TrackedControllerRole_RightHand:
					break;
				default:
					break;
				}
				*/
			}
		}


		
		// now update cameras:
		for (int i = 0; i < 2; i++) {
			// left:
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
					texdim[0], texdim[1],
					0.0, texdim[1],
					0.0, 0.,
					texdim[0], 0.
				};

				GLfloat verts[] = {
					texdim_w, texdim_h,
					0.0, texdim_h,
					0.0, 0.0,
					texdim_w, 0.0
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

		/**/
		// ready to submit:

		// getting problems here, my guess it is likely due to the gl funkiness. might need to create another fbo of the desired format to make this work
		// but it shouldn't crash!

		// might need to try something else here, like creating an internal fbo and splatting to that, and submitting it

		// or it might be that jitter's textures are built on pre-dx11 support... 

		// anyway, let's try submitting what we already have

		/** Updated scene texture to display. If pBounds is NULL the entire texture will be used.  If called from an OpenGL app, consider adding a glFlush after
		* Submitting both frames to signal the driver to start processing, otherwise it may wait until the command buffer fills up, causing the app to miss frames.
		*
		* OpenGL dirty state:
		*	glBindTexture
		*/


		
		vr::EVRCompositorError err;
		vr::Texture_t vrTexture = { (void*)inFBOtex, vr::API_OpenGL, vr::ColorSpace_Gamma }; 

		vr::VRTextureBounds_t leftBounds = { 0.f, 0.f, 0.5f, 1.f };
		vr::VRTextureBounds_t rightBounds = { 0.5f, 0.f, 1.f, 1.f };

		err = vr::VRCompositor()->Submit(vr::Eye_Left, &vrTexture, &leftBounds);
		if (err != 0) object_error(&ob, "submit error");

		err = vr::VRCompositor()->Submit(vr::Eye_Right, &vrTexture, &rightBounds);
		if (err != 0) object_error(&ob, "submit error");

		//glBindTexture(GL_TEXTURE_2D, 0);

		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		
		// openvr header recommends this after submit:
		glFlush();
		glFinish();
	
/*
		// submission done:

		t_atom a[1];
		atom_setsym(a, intexture);

		if (mirror) mirror_output(a);

		outlet_anything(outlet_tex, ps_jit_gl_texture, 1, a);*/
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
		return true;
	}

	t_jit_err draw() {
		// this gets called when the jit.gl.render context updates clients
		// the htcvive object doesn't draw to the main scene, so there's nothing needed to be done here
		return JIT_ERR_NONE;
	}

	t_jit_err dest_changed() {
		object_post(&ob, "dest_changed %d %d", texdim_w, texdim_h);

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

		
		// make a framebuffer:


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
	} else {	// outlet
		switch (a) {
		case 0: sprintf(s, "output/mirror texture"); break;
		case 1: sprintf(s, "to left eye camera"); break;
		case 2: sprintf(s, "to right eye camera"); break;
		case 3: sprintf(s, "to scene node (set texture dim)"); break;
		case 4: sprintf(s, "tracking state"); break;
		case 5: sprintf(s, "other messages"); break;
		//default: sprintf(s, "I am outlet %ld", a); break;
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

void htcvive_recenter(htcvive * x) {
	
}



void htcvive_jit_gl_texture(htcvive * x, t_symbol * s, long argc, t_atom * argv) {
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

void htcvive_quit() {
	
}

void htcvive_log(int level, const char* message) {
	post("oculus log %d %s", level, message);
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
	class_addmethod(c, (method)htcvive_assist,			"assist",		A_CANT, 0);


	class_addmethod(c, (method)htcvive_jit_gl_texture, "jit_gl_texture", A_GIMME, 0);

	class_addmethod(c, (method)htcvive_connect, "connect", 0);
	class_addmethod(c, (method)htcvive_disconnect, "disconnect", 0);
	class_addmethod(c, (method)htcvive_configure, "configure", 0);
	class_addmethod(c, (method)htcvive_info, "info", 0);


	class_addmethod(c, (method)htcvive_recenter, "recenter", 0);

	class_addmethod(c, (method)htcvive_bang, "bang", 0);
	class_addmethod(c, (method)htcvive_submit, "submit", 0);

	CLASS_ATTR_FLOAT(c, "near_clip", 0, htcvive, near_clip);
	CLASS_ATTR_FLOAT(c, "far_clip", 0, htcvive, far_clip);

	CLASS_ATTR_LONG(c, "mirror", 0, htcvive, mirror);
	CLASS_ATTR_STYLE_LABEL(c, "mirror", 0, "onoff", "mirror HMD display in main window");
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

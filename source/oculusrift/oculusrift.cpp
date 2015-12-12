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
#include "jit.gl.h"
}

#include <new> // for in-place constructor

#include "OVR_CAPI.h"
#include "OVR_CAPI_GL.h"


static t_class * max_class = 0;

static t_symbol * ps_quat;
static t_symbol * ps_pos;
static t_symbol * ps_viewport;
static t_symbol * ps_frustum;
static t_symbol * ps_warning;
static t_symbol * ps_glid;

class oculusrift {
public:
	t_object ob; // must be first!
	void * outlet_msg;

	ovrSession session;
	ovrGraphicsLuid luid;
	ovrEyeRenderDesc eyeRenderDesc[2];
	ovrVector3f      hmdToEyeViewOffset[2];
	ovrLayerEyeFov layer;
	ovrSwapTextureSet * pTextureSet;
	ovrSizei pTextureDim;
	ovrTexture * mirrorTexture;

	long long frameIndex;

	int perfMode;

	GLuint fbo, rbo;

	oculusrift() {

		outlet_msg = outlet_new(&ob, NULL);

		frameIndex = 0;
		perfMode = 0;
		fbo = 0;
		rbo = 0;

		ovrResult result;

		result = ovr_Create(&session, &luid);
		if (OVR_FAILURE(result))
		{
			error("failed to create session -- is the HMD plugged in?");
			return;
		}

		object_post(&ob, "LibOVR runtime version %s", ovr_GetVersionString());

		ovrHmdDesc hmd = ovr_GetHmdDesc(session);
		ovrSizei resolution = hmd.Resolution;

		post("created session with resolution %d %d\n", resolution.w, resolution.h);

		// just use default tracking options for now -- nothing to do for ovr_ConfigureTracking()

		//  - Use hmd members and ovr_GetFovTextureSize() to determine graphics configuration
		//    and ovr_GetRenderDesc() to get per-eye rendering parameters.
		// Configure Stereo settings.
		float pixelDensity = 1.f;
		ovrSizei recommenedTex0Size = ovr_GetFovTextureSize(session, ovrEye_Left, hmd.DefaultEyeFov[0], pixelDensity);
		ovrSizei recommenedTex1Size = ovr_GetFovTextureSize(session, ovrEye_Right, hmd.DefaultEyeFov[1], pixelDensity);
		// assumes a single shared texture for both eyes:
		pTextureDim.w = recommenedTex0Size.w + recommenedTex1Size.w;
		pTextureDim.h = max(recommenedTex0Size.h, recommenedTex1Size.h);

		post("recommended texture resolution %d %d\n", pTextureDim.w, pTextureDim.h);


		//  - Allocate render target texture sets with ovr_CreateSwapTextureSetD3D11() or
		//    ovr_CreateSwapTextureSetGL().
		pTextureSet = 0;
		// TODO problem here: Jitter API GL headers don't export GL_SRGB8_ALPHA8
		// might also need  GL_EXT_framebuffer_sRGB for the copy
		// "your application should call glEnable(GL_FRAMEBUFFER_SRGB); before rendering into these textures."
		// I'm not sure if just passing in the constant like this will actually work
		// Even though it is not recommended, if your application is configured to treat the texture as a linear 
		// format (e.g.GL_RGBA) and performs linear - to - gamma conversion in GLSL or does not care about gamma - 
		// correction, then:
		// Request an sRGB format(e.g.GL_SRGB8_ALPHA8) swap - texture - set.
		// Do not call glEnable(GL_FRAMEBUFFER_SRGB); when rendering into the swap texture.
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8                   0x8C43
#endif
		result = ovr_CreateSwapTextureSetGL(session, GL_SRGB8_ALPHA8, pTextureDim.w, pTextureDim.h, &pTextureSet);
		//result = ovr_CreateSwapTextureSetGL(session, GL_RGBA8, pTextureDim.w, pTextureDim.h, &pTextureSet);
		if (result != ovrSuccess) {
			ovrErrorInfo errInfo;
			ovr_GetLastErrorInfo(&errInfo);
			object_error(&ob, "failed to create texture set: %s", errInfo.ErrorString);
			
			// Sample texture access:
			//ovrGLTexture* tex = (ovrGLTexture*)&pTextureSet->Textures[i];
			//glBindTexture(GL_TEXTURE_2D, tex->OGL.TexId);
			//...
			//glBindTexture(GL_TEXTURE_2D, 0);
		}

		result = ovr_CreateMirrorTextureGL(session, GL_SRGB8_ALPHA8, pTextureDim.w, pTextureDim.h, &mirrorTexture);
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

		// Initialize VR structures, filling out description.
		eyeRenderDesc[0] = ovr_GetRenderDesc(session, ovrEye_Left, hmd.DefaultEyeFov[0]);
		eyeRenderDesc[1] = ovr_GetRenderDesc(session, ovrEye_Right, hmd.DefaultEyeFov[1]);
		hmdToEyeViewOffset[0] = eyeRenderDesc[0].HmdToEyeViewOffset;
		hmdToEyeViewOffset[1] = eyeRenderDesc[1].HmdToEyeViewOffset;

		// Initialize our single full screen Fov layer.
		layer.Header.Type = ovrLayerType_EyeFov;
		layer.Header.Flags = 0;
		layer.ColorTexture[0] = pTextureSet;
		layer.ColorTexture[1] = pTextureSet;
		layer.Fov[0] = eyeRenderDesc[0].Fov;
		layer.Fov[1] = eyeRenderDesc[1].Fov;
		layer.Viewport[0].Pos.x = 0;
		layer.Viewport[0].Pos.y = 0;
		layer.Viewport[0].Size.w = pTextureDim.w / 2;
		layer.Viewport[0].Size.h = pTextureDim.h;
		layer.Viewport[1].Pos.x = pTextureDim.w / 2;
		layer.Viewport[1].Pos.y = 0;
		layer.Viewport[1].Size.w = pTextureDim.w / 2;
		layer.Viewport[1].Size.h = pTextureDim.h;

		// ld.RenderPose and ld.SensorSampleTime are updated later per frame.
	}

	void info() {
		ovrHmdDesc hmd = ovr_GetHmdDesc(session);
		t_atom a[2];

		// TODO complete list of useful info from https://developer.oculus.com/documentation/pcsdk/latest/concepts/dg-sensor/
#define HMD_CASE(T) case T: { \
            atom_setsym(a, gensym( #T )); \
            outlet_anything(outlet_msg, gensym("hmdType"), 1, a); \
            break; \
	        }
		switch (hmd.Type) {
			HMD_CASE(ovrHmd_DK1)
			HMD_CASE(ovrHmd_DKHD)
			HMD_CASE(ovrHmd_DK2)
		default: {
				atom_setsym(a, gensym("unknown"));
				outlet_anything(outlet_msg, gensym("Type"), 1, a);
			}
		}
#undef HMD_CASE

		// note serial:
		atom_setsym(a, gensym(hmd.SerialNumber));
		outlet_anything(outlet_msg, gensym("serial"), 1, a);

		atom_setsym(a, gensym(hmd.Manufacturer));
		outlet_anything(outlet_msg, gensym("Manufacturer"), 1, a);
		atom_setsym(a, gensym(hmd.ProductName));
		outlet_anything(outlet_msg, gensym("ProductName"), 1, a);

		atom_setlong(a, hmd.FirmwareMajor);
		atom_setlong(a + 1, hmd.FirmwareMinor);
		outlet_anything(outlet_msg, gensym("Firmware"), 2, a);
	}
	
	~oculusrift() {
		if (session) ovr_Destroy(session);
		if (pTextureSet) ovr_DestroySwapTextureSet(session, pTextureSet);
		if (mirrorTexture) ovr_DestroyMirrorTexture(session, mirrorTexture);
	}

	/*
	Frame rendering typically involves several steps: 
	- obtaining predicted eye poses based on the headset tracking pose, 
	- rendering the view for each eye and, finally, 
	- submitting eye textures to the compositor through ovr_SubmitFrame. 
	*/

	void bang() {
		// Query the HMD for the current tracking state.

		// Get both eye poses simultaneously, with IPD offset already included.
		double displayMidpointSeconds = ovr_GetPredictedDisplayTime(session, frameIndex);
		ovrTrackingState ts = ovr_GetTrackingState(session, displayMidpointSeconds, ovrTrue);
		ovr_CalcEyePoses(ts.HeadPose.ThePose, hmdToEyeViewOffset, layer.RenderPose);

		/*
		TODO
		If the time passed into ovr_GetTrackingState is the current time or earlier,
		the tracking state returned will be based on the latest sensor readings with
		no prediction. In a production application, however, you should use the real-time
		computed value returned by GetPredictedDisplayTime.
		*/
		
		t_atom a[4];

		if (ts.StatusFlags & (ovrStatus_OrientationTracked | ovrStatus_PositionTracked))
		{
			ovrPoseStatef posestate = ts.HeadPose;
			ovrPosef pose = posestate.ThePose;
			ovrVector3f p = pose.Position;
			ovrQuatf q = pose.Orientation;
			//
			atom_setfloat(a + 0, p.x);
			atom_setfloat(a + 1, p.y);
			atom_setfloat(a + 2, p.z);
			//outlet_anything(outlet_msg, gensym("position"), 3, a);
		}

		/*
		// Render Scene to Eye Buffers
		// TODO: calculate these values and pass them to Jitter to capture the scenes:
		//The code then computes view and projection matrices and sets viewport scene rendering for each eye. In this example, view calculation combines the original pose (originPos and originRot values) with the new pose computed based on the tracking state and stored in the layer. There original values can be modified by input to move the player within the 3D world.
		for (int eye = 0; eye < 2; eye++) {
			// Get view and projection matrices for the Rift camera
			Vector3f pos = originPos + originRot.Transform(layer.RenderPose[eye].Position);
			Matrix4f rot = originRot * Matrix4f(layer.RenderPose[eye].Orientation);

			Vector3f finalUp      = rot.Transform(Vector3f(0, 1, 0));
			Vector3f finalForward = rot.Transform(Vector3f(0, 0, -1));
			Matrix4f view         = Matrix4f::LookAtRH(pos, pos + finalForward, finalUp);
        
			Matrix4f proj = ovrMatrix4f_Projection(layer.Fov[eye], 0.2f, 1000.0f, ovrProjection_RightHanded);
			// Render the scene for this eye.
			DIRECTX.SetViewport(layer.Viewport[eye]);
			roomScene.Render(proj * view, 1, 1, 1, 1, true);
		}
	*/
	}

	void jit_gl_texture(t_symbol * s) {

		
		if (!fbo) {
			glGenFramebuffersEXT(1, &fbo);
			glGenRenderbuffersEXT(1, &rbo);
			
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
			//Attach 2D texture to this FBO
			ovrGLTexture* tex = (ovrGLTexture*)&pTextureSet->Textures[pTextureSet->CurrentIndex];
			GLuint oglid = tex->OGL.TexId;
			glBindTexture(GL_TEXTURE_2D, oglid);	// is it necessary?
			glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, oglid, 0);
			glBindTexture(GL_TEXTURE_2D, 0);

			glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rbo);
			glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, pTextureDim.w, pTextureDim.h);
			//Attach depth buffer to FBO
			glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, rbo);
			//Does the GPU support current FBO configuration?
			if (!check_fbo()) {
				object_error(&ob, "falied to create FBO");
				glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);
				glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
				fbo = 0;
				rbo = 0;
				return;
			}
			glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

		}

		void * texob = jit_object_findregistered(s);
		if (!texob) return;	// no texture to copy from.

		long glid = jit_attr_getlong(texob, ps_glid);

		//post("got texture %s id %ld\n", s->s_name, glid);

		// Increment to use next texture, just before writing
		pTextureSet->CurrentIndex = (pTextureSet->CurrentIndex + 1) % pTextureSet->TextureCount;
		// TODO? Clear and set up render-target.    

		ovrGLTexture* tex = (ovrGLTexture*)&pTextureSet->Textures[pTextureSet->CurrentIndex];
		GLuint oglid = tex->OGL.TexId;

		// TODO: copy our texture input to this layer:
		
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
		//Attach 2D texture to this FBO
		glBindTexture(GL_TEXTURE_2D, oglid);	// is it necessary?
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, oglid, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		//Does the GPU support current FBO configuration?
		if (check_fbo()) {
			drawscene(glid);
		}
		// done with fbo
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
		// mipmaps?
		//glBindTexture(GL_TEXTURE_2D, oglid);
		//glGenerateMipmapEXT(GL_TEXTURE_2D);
		//glBindTexture(GL_TEXTURE_2D, 0);
		

		// Submit frame with one layer we have.
		// ovr_SubmitFrame returns once frame present is queued up and the next texture slot in the ovrSwapTextureSet is available for the next frame. 
		ovrLayerHeader* layers = &layer.Header;
		ovrResult       result = ovr_SubmitFrame(session, frameIndex, nullptr, &layers, 1);
		if (result == ovrError_DisplayLost) {
			/*
			TODO: If you receive ovrError_DisplayLost, the device was removed and the session is invalid. 
			Release the shared resources (ovr_DestroySwapTextureSet), destroy the session (ovr_Destory),
			recreate it (ovr_Create), and create new resources (ovr_CreateSwapTextureSetXXX). 
			The application's existing private graphics resources do not need to be recreated unless 
			the new ovr_Create call returns a different GraphicsLuid.
			*/
			object_error(&ob, "fatal error connection lost.");
		}

		// TODO: enable this for multi-threaded rendering
		// frameIndex++;
	}

	void drawscene(GLuint glid) {
		/*
		// save state
		//glPushAttrib(GL_VIEWPORT_BIT|GL_COLOR_BUFFER_BIT);
		glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT);
		*/

		glViewport(0, 0, pTextureDim.w, pTextureDim.h);
		/*
		glMatrixMode(GL_TEXTURE);
		glPushMatrix();
		glLoadIdentity();

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0.0, 1., 0.0, 1., -1.0, 1.0);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		//-------------------------
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_LIGHTING);

		*/
		//glActiveTexture(0);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, glid);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

		//				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		//				glTexParameterf( GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		//				glTexParameterf( GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		//				glTexParameterf( GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
		//				glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		//				glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		//				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		//				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		//				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		//				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


		//				 GLfloat verts[] = {
		//				 0.0,(GLfloat)height,
		//				 (GLfloat)width,(GLfloat)height,
		//				 (GLfloat)width,0.0,
		//				 0.0,0.0
		//				 };
		//				 
		//				 GLfloat tex_coords[] = {
		//				 0.0, (GLfloat)height,
		//				 (GLfloat)width*x->width_scale, (GLfloat)height,
		//				 (GLfloat)width*x->width_scale, 0.0,
		//				 0.0, 0.0
		//				 };
		//				 if(!x->ownsoutput && x->dsttexflipped) {
		//				 tex_coords[1]=0;
		//				 tex_coords[3]=0;
		//				 tex_coords[5]=height;
		//				 tex_coords[7]=height;
		//				 }
		//				 if(x->target == GL_TEXTURE_2D) {
		//				 tex_coords[1] /= (float)x->backingHeight;
		//				 tex_coords[3] /= (float)x->backingHeight;
		//				 tex_coords[5] /= (float)x->backingHeight;
		//				 tex_coords[7] /= (float)x->backingHeight;
		//				 tex_coords[2] /= (float)x->backingWidth;
		//				 tex_coords[4] /= (float)x->backingWidth;
		//				 }
		//				 
		//				 glEnable(x->target);
		//				 if(x->source_type == VIDTEX_SOURCE_YUV420)
		//				 fbo_texture_bind_yuv420(x);
		//				 else if (x->source_type == VIDTEX_SOURCE_YUV422)
		//				 fbo_texture_bind_yuv422(x);
		//				 else
		//				 glBindTexture(x->target,x->srctex);
		//				 
		//				 glClientActiveTextureARB(GL_TEXTURE0_ARB);
		//				 glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		//				 glTexCoordPointer(2, GL_FLOAT, 0, tex_coords );
		//				 glEnableClientState(GL_VERTEX_ARRAY);		
		//				 glVertexPointer(2, GL_FLOAT, 0, verts );
		//				 
		//				 glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );

		// render quad:

		glBegin(GL_QUADS);
		glColor3d(1., 1., 1.);
		glTexCoord2d(0., 0.);
		glVertex2d(-1., -1.);
		glColor3d(1., 0., 1.);
		glTexCoord2d(1., 0.);
		glVertex2d(1., -1.);
		glColor3d(1., 0., 0.);
		glTexCoord2d(1., 1.);
		glVertex2d(1., 1.);
		glColor3d(1., 1., 0.);
		glTexCoord2d(0., 1.);
		glVertex2d(-1., 1.);
		glEnd();

		
		//jit_gl_report_error("oculus fbo draw end");

		glBindTexture(GL_TEXTURE_2D, 0);
		//glDisable(GL_TEXTURE_2D);

		/*
		glPopClientAttrib();
		//glPopAttrib();

		*/
	}

	void perf() {
		// just toggle through the various perf modes
		// see https://developer.oculus.com/documentation/pcsdk/latest/concepts/dg-hud/
		perfMode = (perfMode + 1) % ovrPerfHud_Count;
		ovr_SetInt(session, OVR_PERF_HUD_MODE, perfMode);
	}

	bool check_fbo() {
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
	
	t_jit_err draw() {
		// this gets called, but not really sure what it should do.
		// probably nothing...
		//object_post(&ob, "draw");
		
		return JIT_ERR_NONE;
	}
	
	t_jit_err dest_changed() {
		
		object_post(&ob, "dest_changed");

	}
	
	t_jit_err dest_closing() {
		object_post(&ob, "dest_closing");
		
//		glDeleteRenderbuffersEXT(1, &rbo);
//		rbo = 0;
//		//Bind 0, which means render to back buffer, as a result, fb is unbound
//		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
//		glDeleteFramebuffersEXT(1, &fbo);
//		fbo = 0;
		
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
	
};

void * oculusrift_new(t_symbol *s, long argc, t_atom *argv) {
	oculusrift *x = NULL;
	if ((x = (oculusrift *)object_alloc(max_class))) {
		
		x = new (x) oculusrift();
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:

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
		sprintf(s, "I am inlet %ld", a);
	}
	else {	// outlet
		sprintf(s, "I am outlet %ld", a);
	}
}

void oculusrift_info(oculusrift * x) {
	x->info();
}

void oculusrift_bang(oculusrift * x) {
	x->bang();
}

void oculusrift_perf(oculusrift * x) {
	x->perf();
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

void oculusrift_quit() {
	ovr_Shutdown();
}

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

	result = ovr_Initialize(NULL);
	if (OVR_FAILURE(result)) {
		error( "LibOVR: failed to initialize library");
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
		}
		return;

		/*
		// was crashy:
		ovrErrorInfo errInfo;
		ovr_GetLastErrorInfo(&errInfo);
		object_error(NULL, errInfo.ErrorString);

		}
		
		//
		*/
	}
	quittask_install((method)oculusrift_quit, NULL);
	
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
	jit_class_addmethod(c, (method)(oculusrift_draw), "ob3d_draw", A_CANT, 0L);
	jit_class_addmethod(c, (method)(oculusrift_dest_closing), "dest_closing", A_CANT, 0L);
	jit_class_addmethod(c, (method)(oculusrift_dest_changed), "dest_changed", A_CANT, 0L);
	if (ob3d_flags & JIT_OB3D_DOES_UI) {
		jit_class_addmethod(maxclass, (method)(oculusrift_ui), "ob3d_ui", A_CANT, 0L);
	}
	// must register for ob3d use
	jit_class_addmethod(c, (method)jit_object_register, "register", A_CANT, 0L);
	

	
	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)oculusrift_assist,			"assist",		A_CANT, 0);


	class_addmethod(c, (method)oculusrift_jit_gl_texture, "jit_gl_texture", A_GIMME, 0);

	class_addmethod(c, (method)oculusrift_info, "info", 0);
	class_addmethod(c, (method)oculusrift_bang, "bang", 0);
	class_addmethod(c, (method)oculusrift_perf, "perf", 0);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

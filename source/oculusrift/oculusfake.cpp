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

// TODO: this is a really annoying hack. The Oculus driver doesn't seem to like being reconnected too quickly 
// -- it says it reconnects, but the display remains blank or noisy.
// inserting a short wait seems to avoid it. This wait in terms of frames:
#define RECONNECTION_TIME 100

class oculusrift {
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
	float near_clip, far_clip;
	float pixel_density;
	int max_fov;
	int perfMode;
	int mirror;

	void * outtexture;
	t_atom_long outdim[2];
	t_symbol * outname;

	int reconnect_wait;

	long long frameIndex;

	GLuint fbo, fbomirror;

	oculusrift(t_symbol * dest_name) : dest_name(dest_name) {

		// init Max object:
		jit_ob3d_new(this, dest_name);
		// outlets create in reverse order:
		outlet_msg = outlet_new(&ob, NULL);
		outlet_tracking = outlet_new(&ob, NULL);
		outlet_node = outlet_new(&ob, NULL);
		outlet_eye[1] = outlet_new(&ob, NULL);
		outlet_eye[0] = outlet_new(&ob, NULL);
		outlet_tex = outlet_new(&ob, "jit_gl_texture");

		// init state
		fbo = 0;
		fbomirror = 0;
		frameIndex = 0;
		intexture = _sym_nothing;

		// init attrs
		perfMode = 0;
		near_clip = 0.15f;
		far_clip = 100.f;
		pixel_density = 1.f;
		max_fov = 0;
		mirror = 0;

		reconnect_wait = 0;

		outdim[0] = 1024;
		outdim[1] = 768;

	}

	// attempt to connect to the OVR runtime, creating a session:
	bool connect() {
		
		configure();
		return true;
	}

	void disconnect() {
		
	}

	// usually called after session is created, and when important attributes are changed
	// invokes info() to send configuration results 
	void configure() {
		

		// in case this is a re-configure, clear out the previous ones:
		textureset_destroy();
		mirror_destroy();

		textureset_create();
		mirror_create();

		info();
	}

	void info() {
		t_atom a[1];
		atom_setsym(a, gensym("fake"));
		outlet_anything(outlet_msg, gensym("Type"), 1, a);
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
		post("textureset_create");

		// TODO problem here: Jitter API GL headers don't export GL_SRGB8_ALPHA8
		// might also need  GL_EXT_framebuffer_sRGB for the copy
		// "your application should call glEnable(GL_FRAMEBUFFER_SRGB); before rendering into these textures."
		// SDK says:
		// Even though it is not recommended, if your application is configured to treat the texture as a linear 
		// format (e.g.GL_RGBA) and performs linear - to - gamma conversion in GLSL or does not care about gamma - 
		// correction, then:
		// Request an sRGB format(e.g.GL_SRGB8_ALPHA8) swap - texture - set.
		// Do not call glEnable(GL_FRAMEBUFFER_SRGB); when rendering into the swap texture.

		return true;
	}

	void textureset_destroy() {
		
	}

	/*
	Frame rendering typically involves several steps:
	- obtaining predicted eye poses based on the headset tracking pose, (bang)
	- rendering the view for each eye and, finally, (jit.gl.node @capture 1 renders and then sends texture to this external)
	- submitting eye textures to the compositor through ovr_SubmitFrame. (submit, in response to texture received)
	*/

	void bang() {
		
		t_atom a[6];

		if (1) {
			
			float pos[3];
			jit_attr_getfloat_array(this, gensym("position"), 3, pos);
			t_jit_quat quat;
			jit_attr_getfloat_array(this, gensym("quat"), 4, &quat.x);

			// update the camera view matrices accordingly:
			for (int eye = 0; eye < 2; eye++) {

				atom_setfloat(a + 0, pos[0]);
				atom_setfloat(a + 1, pos[1]);
				atom_setfloat(a + 2, pos[2]);
				outlet_anything(outlet_eye[eye], _jit_sym_position, 3, a);

				atom_setfloat(a + 0, quat.x);
				atom_setfloat(a + 1, quat.y);
				atom_setfloat(a + 2, quat.z);
				atom_setfloat(a + 3, quat.w);
				outlet_anything(outlet_eye[eye], _jit_sym_quat, 4, a);

			}

			atom_setfloat(a + 0, 0);
			atom_setfloat(a + 1, 0);
			atom_setfloat(a + 2, 1);
			outlet_anything(outlet_tracking, _jit_sym_position, 3, a);

			atom_setfloat(a + 0, 0);
			atom_setfloat(a + 1, 0);
			atom_setfloat(a + 2, 0);
			atom_setfloat(a + 3, 1);
			outlet_anything(outlet_tracking, _jit_sym_quat, 4, a);
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
		
		void * texob = jit_object_findregistered(intexture);
		if (!texob) {
			object_error(&ob, "no texture to draw");
			return;	// no texture to copy from.
		}
		
		{
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
		return true;
	}


	void perf() {
	}


	t_jit_err draw() {
		// this gets called when the jit.gl.render context updates clients
		// the oculusrift object doesn't draw to the main scene, so there's nothing needed to be done here
		return JIT_ERR_NONE;
	}

	t_jit_err dest_changed() {
		object_post(&ob, "dest_changed");

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
		object_post(&ob, "dest_closing");
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
		
	}

	void mirror_destroy() {
		
	}

};


void * oculusrift_new(t_symbol *s, long argc, t_atom *argv) {
	oculusrift *x = NULL;
	if ((x = (oculusrift *)object_alloc(max_class))) {// get context:
		t_symbol * dest_name = atom_getsym(argv);
		
		x = new (x)oculusrift(dest_name);
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:
		x->connect();
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
		case 5: sprintf(s, "other messages"); break;
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
	
	common_symbols_init();
	ps_quat = gensym("quat");
	ps_pos = gensym("pos");
	ps_viewport = gensym("viewport");
	ps_frustum = gensym("frustum");
	ps_warning = gensym("warning");
	ps_glid = gensym("glid");
	ps_jit_gl_texture = gensym("jit_gl_texture");

	
	
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

	
	class_register(CLASS_BOX, c);
	max_class = c;
}

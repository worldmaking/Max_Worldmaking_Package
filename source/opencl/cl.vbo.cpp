/*
 
 This is just a simple object to turn a jit.matrix into a VBO (of vec4)
 It can handle float32 matrices of any dimension, and any planecount (it will fill in sensible values for other planes)
 
 TODO: turn this into a named object (like jit.gl.texture) with meaningful attribtues,
 so that the vbo can be used in other objects, and more importantly, in OpenCL kernels!
 
 Right now this object is drawing the data as points, but this isn't likely to be the main purpose
 Perhaps we can have a jit.cl.mesh which can receive vbos (or matrices, like jit.gl.mesh) to its inlets for how to draw...
 
 TODO: consider more complex VBOs, e.g. interleaved. How does that work for OpenCL?
 - seems like still needs to be vec4s though: https://forums.khronos.org/showthread.php/5959-passing-in-array-of-struct-to-kernels-(and-updating-VBO) and https://community.amd.com/thread/201707
 - maybe stick to individual vbos for now.
 */

#include "cl.max.h"


static t_class * max_class = 0;

class cl_vbo {
public:
	t_object ob; // must be first!
	void * ob3d;
	void * outlet_msg;
	
	GLuint glid;
	std::vector<glm::vec4> positions;
	
	cl_vbo(t_symbol *s, long argc, t_atom *argv)  {
		glid = 0;
		
		int n = 64;
		positions.resize(n);
		//colors.resize(n);
		for (int i=0; i<n; i++) {
			positions[i] = glm::vec4(glm::sphericalRand(1.f), 1.f);
			//colors[i] = glm::vec4(0.f);
		}
		
		t_symbol * dest_name = _jit_sym_nothing;
		long attrstart = max_jit_attr_args_offset(argc,argv);
		if (attrstart && argv) {
			jit_atom_arg_getsym(&dest_name, 0, attrstart, argv);
		}
		jit_ob3d_new(&ob, dest_name);
		
		//object_post(&ob, "initialized to context %s with ob3d %p=%p", dest_name->s_name, jit_ob3d_get(&ob), ob3d);
		
		// outlets create in reverse order:
		outlet_msg = outlet_new(&ob, NULL);
	}
	
	void init(t_symbol *s, long argc, t_atom *argv) {
		
	}
	
	~cl_vbo() {
		// free any GPU resources:
		dest_closing();
		// free our ob3d data
		jit_ob3d_free(&ob);
	}
	
	// receive a texture
	// TODO: validate texture format?
	void jit_gl_texture(t_symbol * s) {
		
		if(s && s != _jit_sym_nothing) {
			void * texture_ptr = jit_object_findregistered(s);
			if(texture_ptr)
			{
				// TODO: get rid of gensym
				const long glid(jit_attr_getlong(texture_ptr, gensym("glid")));
				const long gltarget(jit_attr_getlong(texture_ptr, gensym("gltarget")));
				GLuint width = jit_attr_getlong(texture_ptr,gensym("width"));
				GLuint height = jit_attr_getlong(texture_ptr,gensym("height"));
				bool flip = jit_attr_getlong(texture_ptr,gensym("flip"));
				
			}
		}
	}
	
	t_jit_err draw() {
		/*
		// draw our OpenGL geometry.
		glBegin(GL_QUADS);
		glVertex3f(-1,-1,0);
		glVertex3f(-1,1,0);
		glVertex3f(1,1,0);
		glVertex3f(1,-1,0);
		glEnd();
		 */
		
		
		if (glid) {
		
			// glEnable(GL_POINT_SMOOTH);
			// glPointSize(5.);
			 
			//printf("color buffer\n");
			//glBindBuffer(GL_ARRAY_BUFFER, vbos[1]);
			//glColorPointer(4, GL_FLOAT, 0, 0);

			//printf("vertex buffer\n");
			glBindBuffer(GL_ARRAY_BUFFER, glid);
			glVertexPointer(4, GL_FLOAT, 0, 0);

			glEnableClientState(GL_VERTEX_ARRAY);
			//glEnableClientState(GL_COLOR_ARRAY);

			glDrawArrays(GL_POINTS, 0, positions.size());

			//glDisableClientState(GL_COLOR_ARRAY);
			glDisableClientState(GL_VERTEX_ARRAY);
		}
		/* */
		
		return JIT_ERR_NONE;
	}
	
	t_jit_err dest_changed() {
		object_post(&ob, "dest_changed");
		
		glGenBuffers(1, &glid);
		if (!glid) object_error(&ob, "OpenGL failed to allocate VBO");
		
		if (glid) {
			uploadData(glid, positions.size() * sizeof(glm::vec4), &positions[0]);
			//uploadData(vbos[1], colors.size() * sizeof(glm::vec4), &colors[0]);
		}
		
		return JIT_ERR_NONE;
	}
	
	void uploadData(GLuint& id, GLsizeiptr dataSize, const GLvoid * data) {
		
		glBindBuffer(GL_ARRAY_BUFFER, id);                    // activate vbo id to use
		glBufferData(GL_ARRAY_BUFFER, dataSize, data, GL_DYNAMIC_DRAW); // upload data to video card
		
		// check data size in VBO is same as input array, if not return 0 and delete VBO
		int bufferSize = 0;
		glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
		if(dataSize != bufferSize) {
			//glDeleteBuffers(1, &id);
			id = 0;
			//cout << "[createVBO()] Data size is mismatch with input array\n";
			object_error(&ob, "[createVBO()] Data size is mismatch with input array\n");
		} else {
			//object_post(&ob, "uploaded VBO data to %d", id);
		}
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
	
	// free any locally-allocated GPU resources
	t_jit_err dest_closing() {
		object_post(&ob, "dest_closing");
		
		if (glid) glDeleteBuffers(1, &glid);
		glid = 0;
		
		return JIT_ERR_NONE;
	}
	
	template<typename T=float>
	long copy_matrix_row_to_array_vec4(T * row, long planecount, long dim, glm::vec4 * dst, long cell) {
		for (long i=0; i<dim; i++) {
			dst[cell++] = glm::make_vec4(row);
			row += planecount;
			cell++;
		}
		return cell;
	}
	
	template<typename T=float>
	long copy_matrix_to_array_vec4(long dimcount, long * dim,  long cell) {
		
		
		switch (dimcount) {
			case 1:
				dim[1]=1;
				// (fall-through to next case is intentional)
			case 2: {
				
			} break;
			default: {
				for	(int i=0; i<dim[dimcount-1]; i++) {
					//ip = bip + i * in_minfo->dimstride[dimcount-1];
					//jit_simple_calculate_ndim(x, dimcount-1, dim, planecount, in_minfo, ip, out_minfo, op);
					//copy_matrix_to_array_vec4();
				}
			}
		}
		
		
		
	}
	
	void jit_matrix(t_symbol * name) {
		// received new matrix data to update positions
		if (name == _jit_sym_nothing) { return; }
		
		void * mat = jit_object_findregistered(name);
		if (!mat) {
			object_error(&ob, "couldn't get matrix object!");
			return;
		}
		
		// verify matrix:
		long in_savelock = (long) jit_object_method(mat, _jit_sym_lock, 1);
		t_jit_matrix_info	in_minfo;
		char				*in_bp;
		jit_object_method(mat, _jit_sym_getinfo, &in_minfo);
		jit_object_method(mat, _jit_sym_getdata, &in_bp);
		t_jit_err			err = JIT_ERR_NONE;
		long dimcount, planecount;
		long dim[JIT_MATRIX_MAX_DIMCOUNT];
		long i;
		long cellcount = 1;
		long cell = 0;
		
		if (!in_bp) {
			err=JIT_ERR_INVALID_INPUT;
			goto out;
		}
		if (in_minfo.type != _jit_sym_float32) {
			err = JIT_ERR_MISMATCH_TYPE;
			goto out;
		}
		if (in_minfo.planecount < 3) {
			err = JIT_ERR_MISMATCH_PLANE;
			goto out;
		}
		
		dimcount   = in_minfo.dimcount;
		planecount = in_minfo.planecount;
		for (i=0; i<dimcount; i++) {
			dim[i] = in_minfo.dim[i];
			cellcount *= dim[i];
		}
		
		positions.resize(cellcount);
		//colors.resize(cellcount);
		cell = copy_data_ndim(positions, dimcount, dim, planecount, &in_minfo, in_bp, 0);
		if (cell != cellcount) object_error(&ob, "miscount copying data");
		
		uploadData(glid, positions.size() * sizeof(glm::vec4), &positions[0]);
		
	out:
		jit_object_method(mat,_jit_sym_lock,in_savelock);
		jit_error_code(&ob, err);
	}
	
	long copy_data_ndim(std::vector<glm::vec4>& dst_vector, long dimcount, long *dim, long planecount, t_jit_matrix_info *in_minfo, char *bip, long cell) {
		switch(dimcount) {
			case 1: dim[1]=1; // if only 1D, interperet as 2D, falling through to 2D case
			case 2: {
				long width  = dim[0];
				long height = dim[1];
				for (int i=0; i<height; i++) {
					float * row = (float *)(bip + i*in_minfo->dimstride[1]);
					for (int j=0; j<width; j++) {
						glm::vec4& dst = dst_vector[cell];
						switch (planecount) {
							case 1:  dst = glm::vec4((float)row[0], 0.f, 0.f, 1.f); break;
							case 2:  dst = glm::vec4((float)row[0], (float)row[1], 0.f, 1.f); break;
							case 3:  dst = glm::vec4((float)row[0], (float)row[1], (float)row[2], 1.f); break;
							default: dst = glm::vec4((float)row[0], (float)row[1], (float)row[2], (float)row[3]); break;
						}
						row += planecount;
						cell++;
					}
				}
			} break;
			default: {
				// if we are processing higher dimension than 2D,
				// for each lower dimensioned slice, set our
				// base pointer and recursively call this function
				// with decremented dimcount and new base pointers
				for	(int i=0; i<dim[dimcount-1]; i++) {
					char * ip = bip + i*in_minfo->dimstride[dimcount-1];
					cell = copy_data_ndim(dst_vector, dimcount-1,dim,planecount,in_minfo,ip,cell);
				}
			}
		}
		return cell;
	}
	
	/*
	t_jit_err ui(t_line_3d *p_line, t_wind_mouse_info *p_mouse) {
		
		 post("line (%f,%f,%f)-(%f,%f,%f); mouse(%s)",
		 p_line->u[0], p_line->u[1], p_line->u[2],
		 p_line->v[0], p_line->v[1], p_line->v[2],
		 p_mouse->mousesymbol->s_name			// mouse, mouseidle
		 );
	 
		return JIT_ERR_NONE;
	}
	*/
	static void * create(t_symbol *s, long argc, t_atom *argv) {
		cl_vbo *x = NULL;
		if ((x = (cl_vbo *)object_alloc(max_class))) {// get context:
			
			x = new (x)cl_vbo(s, argc, argv);
			
			// apply attrs:
			attr_args_process(x, (short)argc, argv);
			
			// invoke any initialization after the attrs are set from here:
			x->init(s, argc, argv);
		}
		return (x);
	}
	
	static void destroy(cl_vbo *x) {
		x->~cl_vbo();
	}
	
	static t_jit_err static_draw(cl_vbo * x) { return x->draw(); }
	//static t_jit_err static_ui(cl_vbo * x, t_line_3d *p_line, t_wind_mouse_info *p_mouse) { return x->ui(p_line, p_mouse); }
	static t_jit_err static_dest_closing(cl_vbo * x) { return x->dest_closing(); }
	static t_jit_err static_dest_changed(cl_vbo * x) { return x->dest_changed(); }
	
	static void static_bang(cl_vbo * x) { object_method(x, gensym("draw")); }
	static void static_jit_matrix(cl_vbo * x, t_symbol * s) { x->jit_matrix(s); }
};

void cl_vbo_assist(cl_vbo *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "bang to update tracking, texture to submit, other messages");
	} else {	// outlet
		switch (a) {
			default: sprintf(s, "I am outlet %ld", a); break;
		}
	}
}


void cl_vbo_jit_gl_texture(cl_vbo * x, t_symbol * s, long argc, t_atom * argv) {
	if (argc > 0 && atom_gettype(argv) == A_SYM) {
		x->jit_gl_texture(atom_getsym(argv));
	}
}

void cl_vbo_init() {
	t_class *c;
	
	common_symbols_init();
	
	c = class_new("cl.vbo", (method)cl_vbo::create, (method)cl_vbo::destroy, (long)sizeof(cl_vbo),
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	long ob3d_flags = 0;
	//ob3d_flags | JIT_OB3D_DOES_UI;
	//ob3d_flags |= JIT_OB3D_NO_ROTATION_SCALE;
	//ob3d_flags |= JIT_OB3D_NO_POLY_VARS;
	//ob3d_flags |= JIT_OB3D_NO_BLEND;
	//ob3d_flags |= JIT_OB3D_NO_TEXTURE;
	ob3d_flags |= JIT_OB3D_NO_MATRIXOUTPUT;
	//ob3d_flags |= JIT_OB3D_AUTO_ONLY;
	//ob3d_flags |= JIT_OB3D_NO_DEPTH;
	//ob3d_flags |= JIT_OB3D_NO_ANTIALIAS;
	//ob3d_flags |= JIT_OB3D_NO_FOG;
	//ob3d_flags |= JIT_OB3D_NO_LIGHTING_MATERIAL;
	//ob3d_flags |= JIT_OB3D_NO_SHADER;
	//ob3d_flags |= JIT_OB3D_NO_BOUNDS;
	//ob3d_flags |= JIT_OB3D_NO_COLOR;
	
	// set up object extension for 3d object, customized with flags
	void * ob3d = jit_ob3d_setup(c, calcoffset(cl_vbo, ob3d), ob3d_flags);
	// define our OB3D draw methods
	
	class_addmethod(c, (method)(cl_vbo::static_draw), "ob3d_draw", A_CANT, 0L);
	class_addmethod(c, (method)(cl_vbo::static_dest_closing), "dest_closing", A_CANT, 0L);
	class_addmethod(c, (method)(cl_vbo::static_dest_changed), "dest_changed", A_CANT, 0L);
	//if (ob3d_flags & JIT_OB3D_DOES_UI) class_addmethod(c, (method)(cl_vbo::static_ui), "ob3d_ui", A_CANT, 0L);
	
	// must register for ob3d use
	class_addmethod(c, (method)jit_object_register, "register", A_CANT, 0L);
	
	// for bang -> draw:
	class_addmethod(c, (method)cl_vbo::static_bang, "bang", 0);
	
	
	class_addmethod(c, (method)cl_vbo::static_jit_matrix, "jit_matrix", A_SYM, 0);
	
	
	/* you CAN'T call this from the patcher
	class_addmethod(c, (method)cl_vbo_assist,			"assist",		A_CANT, 0);
	
	
	class_addmethod(c, (method)cl_vbo_jit_gl_texture, "jit_gl_texture", A_GIMME, 0);
	
	class_addmethod(c, (method)cl_vbo_bang, "bang", 0);
	class_addmethod(c, (method)cl_vbo_info, "info", 0);
	class_addmethod(c, (method)cl_vbo_open, "open", 0);
	*/
	class_register(CLASS_BOX, c);
	max_class = c;
}

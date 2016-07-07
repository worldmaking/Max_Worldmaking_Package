#include "../al_max.h"

// globals
static t_class * jit_mop_class = NULL;
static t_class * max_mop_class = NULL;


class jit_mop {
public:
	t_object	ob;
	double		gain;	// our attribute (multiplied against each cell in the matrix)
	
	jit_mop() {
		gain = 0.5;
	}
	
	~jit_mop() {}
	
	// process a vector of the matrix for any of the given types
	template<typename T>
	void calculate_vector(long n, t_jit_op_info *in, t_jit_op_info *out)
	{
		T		*ip;
		T		*op;
		long	is,
		os;
		long	tmp;
		
		ip = ((T *)in->p);
		op = ((T *)out->p);
		is = in->stride;
		os = out->stride;
		
		if ((is==1) && (os==1)) {
			++n;
			--op;
			--ip;
			while (--n) {
				tmp = *++ip;
				*++op = tmp * gain;
			}
		}
		else {
			while (n--) {
				tmp = *ip;
				*op = tmp * gain;
				ip += is;
				op += os;
			}
		}
	}
	
	// We also use a C+ template for the loop that wraps the call to jit_simple_vector(),
	// further reducing code duplication in jit_simple_calculate_ndim().
	// The calls into these templates should be inlined by the compiler, eliminating concern about any added function call overhead.
	template<typename T>
	static void calculate_loop(jit_mop *x, long n, t_jit_op_info *in_opinfo, t_jit_op_info *out_opinfo, t_jit_matrix_info *in_minfo, t_jit_matrix_info *out_minfo, char *bip, char *bop, long *dim, long planecount, long datasize)
	{
		long	i;
		long	j;
		for (i=0; i<dim[1]; i++) {
			for (j=0; j<planecount; j++) {
				in_opinfo->p  = bip + i * in_minfo->dimstride[1]  + (j % in_minfo->planecount) * datasize;
				out_opinfo->p = bop + i * out_minfo->dimstride[1] + (j % out_minfo->planecount) * datasize;
				x->calculate_vector<T>(n, in_opinfo, out_opinfo);
			}
		}
	}
	
	static void calculate_ndim(jit_mop *x, long dimcount, long *dim, long planecount, t_jit_matrix_info *in_minfo, char *bip, t_jit_matrix_info *out_minfo, char *bop)
	{
		long			i;
		long			n;
		char			*ip;
		char			*op;
		t_jit_op_info	in_opinfo;
		t_jit_op_info	out_opinfo;
		
		if (dimcount < 1)
			return; // safety
		
		switch (dimcount) {
			case 1:
				dim[1]=1;
				// (fall-through to next case is intentional)
			case 2:
				// if planecount is the same then flatten planes - treat as single plane data for speed
				n = dim[0];
				if ((in_minfo->dim[0] > 1) && (out_minfo->dim[0] > 1) && (in_minfo->planecount == out_minfo->planecount)) {
					in_opinfo.stride = 1;
					out_opinfo.stride = 1;
					n *= planecount;
					planecount = 1;
					
				}
				else {
					in_opinfo.stride =  in_minfo->dim[0]>1  ? in_minfo->planecount  : 0;
					out_opinfo.stride = out_minfo->dim[0]>1 ? out_minfo->planecount : 0;
				}
				
				if (in_minfo->type == _jit_sym_char)
					jit_mop::calculate_loop<uchar>(x, n, &in_opinfo, &out_opinfo, in_minfo, out_minfo, bip, bop, dim, planecount, 1);
				else if (in_minfo->type == _jit_sym_long)
					jit_mop::calculate_loop<long>(x, n, &in_opinfo, &out_opinfo, in_minfo, out_minfo, bip, bop, dim, planecount, 4);
				else if (in_minfo->type == _jit_sym_float32)
					jit_mop::calculate_loop<float>(x, n, &in_opinfo, &out_opinfo, in_minfo, out_minfo, bip, bop, dim, planecount, 4);
				else if (in_minfo->type == _jit_sym_float64)
					jit_mop::calculate_loop<double>(x, n, &in_opinfo, &out_opinfo, in_minfo, out_minfo, bip, bop, dim, planecount, 8);
				break;
			default:
				for	(i=0; i<dim[dimcount-1]; i++) {
					ip = bip + i * in_minfo->dimstride[dimcount-1];
					op = bop + i * out_minfo->dimstride[dimcount-1];
					calculate_ndim(x, dimcount-1, dim, planecount, in_minfo, ip, out_minfo, op);
				}
		}
	}
	
	static jit_mop * create(void)
	{
		jit_mop	*x = NULL;
		
		x = (jit_mop *)jit_object_alloc(jit_mop_class);
		if (x) {
			x = new (x) jit_mop();
		}
		return x;
	}
	
	static void destroy(jit_mop *x) { x->~jit_mop(); }
};

t_jit_err jit_simple_matrix_calc(jit_mop *x, void *inputs, void *outputs)
{
	t_jit_err			err = JIT_ERR_NONE;
	long				in_savelock;
	long				out_savelock;
	t_jit_matrix_info	in_minfo;
	t_jit_matrix_info	out_minfo;
	char				*in_bp;
	char				*out_bp;
	long				i;
	long				dimcount;
	long				planecount;
	long				dim[JIT_MATRIX_MAX_DIMCOUNT];
	void				*in_matrix;
	void				*out_matrix;
	
	in_matrix 	= jit_object_method(inputs,_jit_sym_getindex,0);
	out_matrix 	= jit_object_method(outputs,_jit_sym_getindex,0);
	
	if (x && in_matrix && out_matrix) {
		in_savelock = (long) jit_object_method(in_matrix, _jit_sym_lock, 1);
		out_savelock = (long) jit_object_method(out_matrix, _jit_sym_lock, 1);
		
		jit_object_method(in_matrix, _jit_sym_getinfo, &in_minfo);
		jit_object_method(out_matrix, _jit_sym_getinfo, &out_minfo);
		
		jit_object_method(in_matrix, _jit_sym_getdata, &in_bp);
		jit_object_method(out_matrix, _jit_sym_getdata, &out_bp);
		
		if (!in_bp) {
			err=JIT_ERR_INVALID_INPUT;
			goto out;
		}
		if (!out_bp) {
			err=JIT_ERR_INVALID_OUTPUT;
			goto out;
		}
		if (in_minfo.type != out_minfo.type) {
			err = JIT_ERR_MISMATCH_TYPE;
			goto out;
		}
		
		//get dimensions/planecount
		dimcount   = out_minfo.dimcount;
		planecount = out_minfo.planecount;
		
		for (i=0; i<dimcount; i++) {
			//if dimsize is 1, treat as infinite domain across that dimension.
			//otherwise truncate if less than the output dimsize
			dim[i] = out_minfo.dim[i];
			if ((in_minfo.dim[i]<dim[i]) && in_minfo.dim[i]>1) {
				dim[i] = in_minfo.dim[i];
			}
		}
		
		jit_parallel_ndim_simplecalc2((method)jit_mop::calculate_ndim,
									  x, dimcount, dim, planecount, &in_minfo, in_bp, &out_minfo, out_bp,
									  0 /* flags1 */, 0 /* flags2 */);
		
	}
	else
		return JIT_ERR_INVALID_PTR;
	
out:
	jit_object_method(out_matrix,_jit_sym_lock,out_savelock);
	jit_object_method(in_matrix,_jit_sym_lock,in_savelock);
	return err;
}

// Max object instance data
// Note: most instance data is in the Jitter object which we will wrap
class max_mop {
public:
	t_object	ob;
	void		*obex;
	jit_mop *   mop;
	
	max_mop() {}
	~max_mop() {
		max_jit_mop_free(&ob);
		jit_object_free(max_jit_obex_jitob_get(&ob));
		max_jit_object_free(&ob);
	}
	
	static void * create(t_symbol *s, long argc, t_atom *argv)
	{
		max_mop	*x;
		void *o;
		
		x = (max_mop *)max_jit_object_alloc(max_mop_class, gensym("jit_al_mop"));
		if (x) {
			o = jit_object_new(gensym("jit_al_mop"));
			if (o) {
				x->mop = (jit_mop *)o;
				max_jit_mop_setup_simple(x, o, argc, argv);
				max_jit_attr_args(x, argc, argv);
			}
			else {
				jit_object_error((t_object *)x, "jit_al_mop: could not allocate object");
				object_free((t_object *)x);
				x = NULL;
			}
		}
		return (x);
	}
	
	static void destroy(max_mop *x) { x->~max_mop(); }
};

extern "C" void al_mop_main() {
	t_class *max_class, *jit_class;
	
	{
		long			attrflags = JIT_ATTR_GET_DEFER_LOW | JIT_ATTR_SET_USURP_LOW;
		t_jit_object	*attr;
		t_jit_object	*mop;
		
		jit_mop_class = (t_class *)jit_class_new("jit_al_mop", (method)jit_mop::create, (method)jit_mop::destroy, sizeof(jit_mop), 0);
		
		// add matrix operator (mop)
		mop = (t_jit_object *)jit_object_new(_jit_sym_jit_mop, 1, 1); // args are  num inputs and num outputs
		jit_class_addadornment(jit_mop_class, mop);
		
		// add method(s)
		jit_class_addmethod(jit_mop_class, (method)jit_simple_matrix_calc, "matrix_calc", A_CANT, 0);
		
		// add attribute(s)
		attr = (t_jit_object *)jit_object_new(_jit_sym_jit_attr_offset,
											  "gain",
											  _jit_sym_float64,
											  attrflags,
											  (method)NULL, (method)NULL,
											  calcoffset(jit_mop, gain));
		jit_class_addattr(jit_mop_class, attr);
		
		// finalize class
		jit_class_register(jit_mop_class);
	}
	
	max_class = class_new("al.mop", (method)max_mop::create, (method)max_mop::destroy, sizeof(max_mop), NULL, A_GIMME, 0);
	
	max_jit_class_obex_setup(max_class, calcoffset(max_mop, obex));
	max_jit_class_mop_wrap(max_class, jit_mop_class, 0);			// attrs & methods for name, type, dim, planecount, bang, outputmatrix, etc
	max_jit_class_wrap_standard(max_class, jit_mop_class, 0);		// attrs & methods for getattributes, dumpout, maxjitclassaddmethods, etc
	
	class_addmethod(max_class, (method)max_jit_mop_assist, "assist", A_CANT, 0);	// standard matrix-operator (mop) assist fn
	
	class_register(CLASS_BOX, max_class);
	max_mop_class = max_class;
	
	
}

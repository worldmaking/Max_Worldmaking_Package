#include "al_cv.hpp"

static t_class *maxclass;

class t_findchessboard {
public:

	typedef struct { float x, y; } vec2f;

	t_object	ob;			// the object itself (must be first)

							// many outlets:
	void *		outlet_msg;
	void *		outlet_corners;

	// attrs:
	int			size[2];
	int			fast_check, adaptive_thresh, normalize_image, filter_quads;

	// corners matrix:
	void *		corners_mat;
	void *		corners_mat_wrapper;
	t_atom		corners_mat_name[1];
	vec2f *		corners_data;

	std::vector<cv::Point2f> corners;

	t_findchessboard() {
		fast_check = 1;
		adaptive_thresh = 1;
		normalize_image = 1;
		filter_quads = 0;

		size[0] = 10;
		size[1] = 7;

		// create matrices:
		t_jit_matrix_info info;

		corners_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
		corners_mat = jit_object_method(corners_mat_wrapper, _jit_sym_getmatrix);
		// create the internal data:
		jit_matrix_info_default(&info);
		info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
		info.planecount = 2;
		info.type = gensym("float32");
		info.dimcount = 2;
		info.dim[0] = 10;
		info.dim[1] = 7;
		jit_object_method(corners_mat, _jit_sym_setinfo_ex, &info);
		jit_object_method(corners_mat, _jit_sym_clear);
		jit_object_method(corners_mat, _jit_sym_getdata, &corners_data);
		// cache name:
		atom_setsym(corners_mat_name, jit_attr_getsym(corners_mat_wrapper, _jit_sym_name));

	}

	~t_findchessboard() {
		if (corners_mat_wrapper) {
			object_free(corners_mat_wrapper);
			corners_mat_wrapper = NULL;
		}
	}

	void resize(t_atom_long x, t_atom_long y) {
		t_jit_matrix_info info;

		size[0] = x;
		size[1] = y;

		jit_object_method(corners_mat, _jit_sym_getinfo, &info);
		info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
		info.dim[0] = x;
		info.dim[1] = y;
		jit_object_method(corners_mat, _jit_sym_setinfo_ex, &info);
		jit_object_method(corners_mat, _jit_sym_clear);
		jit_object_method(corners_mat, _jit_sym_getdata, &corners_data);
	}

	void bang() {
		// output corners as a matrix.
		outlet_anything(outlet_corners, _jit_sym_jit_matrix, 1, corners_mat_name);
	}

	void jit_matrix(t_symbol * name) {
		t_jit_matrix_info in_info;
		char * in_bp;
		t_atom a[1];

		void * in_mat = jit_object_findregistered(name);

		// lock it:
		long in_savelock = (long)jit_object_method(in_mat, _jit_sym_lock, 1);

		// ensure data exists:
		jit_object_method(in_mat, _jit_sym_getdata, &in_bp);
		if (!in_bp) {
			jit_error_code(&ob, JIT_ERR_INVALID_INPUT);
			return;
		}

		// ensure the type is correct:
		jit_object_method(in_mat, _jit_sym_getinfo, &in_info);
		if (in_info.type != _jit_sym_char) {
			jit_error_code(&ob, JIT_ERR_MISMATCH_TYPE);
			return;
			//		} else if (in_info.planecount > 1) {
			//			jit_error_code(&ob, JIT_ERR_MISMATCH_PLANE);
			//			return;
		}
		else if (in_info.dimcount != 2) {
			jit_error_code(&ob, JIT_ERR_MISMATCH_DIM);
			return;
		}

		// create CV mat wrapper around Jitter matrix data
		// (cv declares dim as numrows, numcols)
		cv::Mat InImage(in_info.dim[1], in_info.dim[0], CV_8UC(in_info.planecount), in_bp, in_info.dimstride[1]);

		cv::Mat src;
		if (in_info.planecount != 1) {
			cv::cvtColor(InImage, src, CV_RGBA2GRAY);
		}
		else {
			src = InImage;
		}

		// set up CV arguments:
		cv::Size patternSize(size[0], size[1]);
		// do a fast detection first, then refine with cornerSubPix:
		int flags = 0;
		if (fast_check) flags |= CV_CALIB_CB_FAST_CHECK;
		if (adaptive_thresh) flags |= CV_CALIB_CB_ADAPTIVE_THRESH;
		if (normalize_image) flags |= CV_CALIB_CB_NORMALIZE_IMAGE;
		if (filter_quads) flags |= CV_CALIB_CB_FILTER_QUADS;

		// first pass:
		bool found = cv::findChessboardCorners(src, patternSize, corners, flags);

		// restore matrix lock state:
		jit_object_method(in_mat, _jit_sym_lock, in_savelock);

		atom_setlong(a, found);
		outlet_anything(outlet_msg, gensym("found"), 1, a);

		if (!found) {
			//object_warn(&ob, "chessboard not found");
			return;
		}

		if (corners.size() != size[0] * size[1]) {
			object_warn(&ob, "chessboard size mismatch");
			return;
		}
		

		// refine:
		cv::cornerSubPix(src, corners, cv::Size(11, 11), cv::Size(-1, -1), cv::TermCriteria(CV_TERMCRIT_EPS + CV_TERMCRIT_ITER, 30, 0.1));

		// copy into jit matrix:
		for (int i = 0; i<corners.size(); i++) {
			corners_data[i].x = corners[i].x;
			corners_data[i].y = corners[i].y;
		}

		// output:
		bang();
	}
};

t_max_err findchessboard_size_set(t_findchessboard *x, t_object *attr, long argc, t_atom *argv)
{
	if (argc < 2) return 0;

	t_atom_long ix = atom_getlong(argv);
	t_atom_long iy = atom_getlong(argv + 1);

	if (ix < 0 || iy < 0) return 0;

	x->resize(ix, iy);

	return 0;
}


void findchessboard_assist(t_findchessboard *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		if (a == 0) {
			sprintf(s, "jit_matrix / messages in");
		}
		else {
			sprintf(s, "I am inlet %ld", a);
		}
	}
	else {	// outlet
		if (a == 0) {
			sprintf(s, "corners (jit_matrix)");
		}
		else {
			sprintf(s, "messages");
		}
	}
}

void findchessboard_free(t_findchessboard *x) {
	x->~t_findchessboard();

	// free resources associated with our obex entry
	//jit_ob3d_free(x);
	max_jit_object_free(x);
}

void *findchessboard_new(t_symbol *s, long argc, t_atom *argv)
{
	t_findchessboard *x = NULL;
	if (x = (t_findchessboard *)object_alloc(maxclass)) {

		x->outlet_msg = outlet_new(x, "found");
		x->outlet_corners = outlet_new(x, "jit_matrix");

		// initialize in-place:
		x = new (x) t_findchessboard();

		// apply attrs:
		attr_args_process(x, argc, argv);
	}
	return (x);
}


t_max_err findchessboard_notify(t_findchessboard *x, t_symbol *s, t_symbol *msg, void *sender, void *data) {
	t_symbol *attrname;
	if (msg == _sym_attr_modified) {       // check notification type
		attrname = (t_symbol *)object_method((t_object *)data, _sym_getname);
		object_post((t_object *)x, "changed attr name is %s", attrname->s_name);
	}
	else {
		object_post((t_object *)x, "notify %s (self %d)", msg->s_name, sender == x);
	}
	return 0;
}

void findchessboard_jit_matrix(t_findchessboard * x, t_symbol *s) {
	x->jit_matrix(s);
}

void findchessboard_bang(t_findchessboard * x) {
	x->bang();
}

void al_cv_findchessboard_main() {
	maxclass = class_new("al.findchessboard", (method)findchessboard_new, (method)findchessboard_free, (long)sizeof(t_findchessboard),
		0L, A_GIMME, 0);

	class_addmethod(maxclass, (method)findchessboard_assist, "assist", A_CANT, 0);
	class_addmethod(maxclass, (method)findchessboard_notify, "notify", A_CANT, 0);

	class_addmethod(maxclass, (method)findchessboard_jit_matrix, "jit_matrix", A_SYM, 0);
	class_addmethod(maxclass, (method)findchessboard_bang, "bang", 0);

	
	CLASS_ATTR_LONG_ARRAY(maxclass, "size", 0, t_findchessboard, size, 2);
	CLASS_ATTR_ACCESSORS(maxclass, "size", NULL, findchessboard_size_set);


	CLASS_ATTR_LONG(maxclass, "fast_check", 0, t_findchessboard, fast_check);
	CLASS_ATTR_STYLE(maxclass, "fast_check", 0, "onoff");

	CLASS_ATTR_LONG(maxclass, "adaptive_thresh", 0, t_findchessboard, adaptive_thresh);
	CLASS_ATTR_STYLE(maxclass, "adaptive_thresh", 0, "onoff");

	CLASS_ATTR_LONG(maxclass, "normalize_image", 0, t_findchessboard, normalize_image);
	CLASS_ATTR_STYLE(maxclass, "normalize_image", 0, "onoff");

	CLASS_ATTR_LONG(maxclass, "filter_quads", 0, t_findchessboard, filter_quads);
	CLASS_ATTR_STYLE(maxclass, "filter_quads", 0, "onoff");


	class_register(CLASS_BOX, maxclass);
}
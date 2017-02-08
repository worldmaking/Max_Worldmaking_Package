#include "al_cv.hpp"

static t_class *maxclass;

class t_estimateaffine3D {
public:
	t_object ob;
	long proxy_inlet_num;
	void * proxy;

	// outlets:
	void * outlet_msg;
	void * outlet_calibration;

	

	// attributes
	double threshold;
	double confidence;

	/*
	int image_size[2];
	int images;
	int calibrate_aspect_ratio;
	int calibrate_principal_point;
	int calibrate_k1, calibrate_k2, calibrate_k3;
	int calibrate_tangent_distortion;
	int merge_points;
	int rodrigues;
	int invert_extrinsics;
	double reprojection_error;
	double distortion[5];
	double intrinsic[9];
	double rotation[9];
	*/

	// OpenCV:
	std::vector<std::vector<cv::Point3f> > dstPoints;
	std::vector<std::vector<cv::Point3f> > srcPoints;
	std::vector<std::vector<cv::Point3f> > inliners;
	// the 3x4 affine transform
	cv::Mat cvTransform;
	// the distortion coefficients (radial, tangential)
	cv::Mat cvDistortion;
	// the estimated rotation & translation of each object/image pair
	std::vector<cv::Mat> rvecs, tvecs;

	t_estimateaffine3D() {

		threshold = 3.;
		confidence = 0.99;


		image_size[0] = 640;
		image_size[1] = 480;
		reprojection_error = 0.;
		merge_points = 0;
		rodrigues = 0;
		invert_extrinsics = 0;

		// intrinsic options:
		calibrate_aspect_ratio = 1;
		calibrate_principal_point = 1;
		// lens distortion options:
		calibrate_k1 = calibrate_k2 = calibrate_k3 = 1;
		calibrate_tangent_distortion = 1;



		// add a general purpose outlet (rightmost)
		outlet_msg = outlet_new(this, 0);
		outlet_calibration = outlet_new(this, 0);

		// add a proxy inlet:
		proxy = proxy_new(this, 1, &proxy_inlet_num);

		// initialize:
		clear();
	}

	~t_estimateaffine3D() {

	}

	void clear() {

		cvDistortion = cv::Mat(5, 1, CV_64F, distortion);
		cvIntrinsic = cv::Mat(3, 3, CV_64F, intrinsic);

		objectPoints.clear();
		imagePoints.clear();
		rvecs.clear();
		tvecs.clear();

		images = 0;
	}

	void bang() {
		t_atom a[9];
		unsigned int num_images = imagePoints.size();

		// verify the data exists and has matching length: 
		if (objectPoints.size() < 1) {
			object_error(&ob, "no object points received");
			return;
		}
		else if (num_images < 1) {
			object_error(&ob, "no image points received");
			return;
		}
		if (objectPoints.size() == 1) {
			// resize to match:
			objectPoints.resize(num_images, objectPoints[0]);
		}
		else if (objectPoints.size() != num_images) {
			object_error(&ob, "cannot calibrate; number of image matrices does not match number of object matrices");
			return;
		}
		// verify the sizes match:
		for (unsigned int i = 0; i<objectPoints.size(); i++) {
			//post("%d size %d", i, objectPoints[i].size());
			if (objectPoints[i].size() != imagePoints[i].size()) {
				object_error(&ob, "cannot calibrate; the dimensions of the image and object matrices do not match");
				return;
			}
		}

		int flags = CV_CALIB_USE_INTRINSIC_GUESS;
		flags |= CV_CALIB_FIX_K4 | CV_CALIB_FIX_K5 | CV_CALIB_FIX_K6;
		if (!calibrate_k1) flags |= CV_CALIB_FIX_K1;
		if (!calibrate_k2) flags |= CV_CALIB_FIX_K2;
		if (!calibrate_k3) flags |= CV_CALIB_FIX_K3;
		if (!calibrate_aspect_ratio) flags |= CV_CALIB_FIX_ASPECT_RATIO;
		if (!calibrate_principal_point) flags |= CV_CALIB_FIX_PRINCIPAL_POINT;
		if (!calibrate_tangent_distortion) flags |= CV_CALIB_ZERO_TANGENT_DIST;

		cv::Size imageSize(image_size[0], image_size[1]);

		// initialize estimate intrinsics:
		cvIntrinsic = 0.;	// zero it
		cvIntrinsic.at<double>(0, 0) = image_size[1];	// fx
		cvIntrinsic.at<double>(1, 1) = image_size[1];	// fy
		cvIntrinsic.at<double>(0, 2) = image_size[0] / 2.;	// cx
		cvIntrinsic.at<double>(1, 2) = image_size[1] / 2.;	// cy
		cvIntrinsic.at<double>(2, 2) = 1;

		// initialize with zero distortion:
		cvDistortion = 0.;


		rvecs.clear();
		tvecs.clear();

		//object_post(&ob, "calibrate from %d images / objects", imagePoints.size(), objectPoints.size());

		// Some implementations appear to prefer putting all points into one list:
		std::vector<std::vector<cv::Point3f> > vvo(1); //object points
		std::vector<std::vector<cv::Point2f> > vvi(1); //image points
		if (merge_points) {
			for (int i = 0; i<objectPoints.size(); ++i) {
				for (int j = 0; j<objectPoints[i].size(); j++) {
					vvo[0].push_back(objectPoints[i][j]);
					vvi[0].push_back(imagePoints[i][j]);

					// verify:
					//post("%d,%d: object %f %f %f image %f %f", i, j, objectPoints[i][j].x, objectPoints[i][j].y, objectPoints[i][j].z, imagePoints[i][j].x, imagePoints[i][j].y);
				}
			}
		}

		try {
			if (merge_points) {
				reprojection_error = calibrateCamera(
					vvo, vvi,
					imageSize,
					cvIntrinsic, cvDistortion, rvecs, tvecs,
					flags
				);
			}
			else {
				reprojection_error = calibrateCamera(
					objectPoints, imagePoints,
					imageSize,
					cvIntrinsic, cvDistortion, rvecs, tvecs,
					flags
				);
			}
		}
		catch (cv::Exception& ex) {
			object_error(&ob, ex.what());
			return;
		}

		for (unsigned int i = 0; i<tvecs.size(); i++) {
			// let's look at rvecs & tvecs:
			//post("tvec %d %f %f %f", i, tvecs[i].at<double>(0), tvecs[i].at<double>(1), tvecs[i].at<double>(2));
			//post("rvec %d %f %f %f", i, rvecs[i].at<double>(0), rvecs[i].at<double>(1), rvecs[i].at<double>(2));
		}

		// output right to left:

		atom_setfloat(a, reprojection_error);
		outlet_anything(outlet_msg, gensym("reprojection_error"), 1, a);


		for (int i = 0; i<5; i++) atom_setfloat(a + i, distortion[i]);
		outlet_anything(outlet_calibration, gensym("distortion"), 5, a);

		//atom_setdouble_array(9, a, 9, intrinsic);
		for (int i = 0, y = 0; y<3; y++) {
			for (int x = 0; x<3; x++, i++) {
				atom_setfloat(a + i, cvIntrinsic.at<double>(y, x));
			}
		}
		outlet_anything(outlet_calibration, gensym("intrinsic"), 9, a);
	}

	void image_points(t_symbol * name, void * in_mat) {
		t_jit_matrix_info in_info;
		char * in_bp;

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
		if (in_info.type != _jit_sym_float32) {
			jit_error_code(&ob, JIT_ERR_MISMATCH_TYPE);
			return;
		}
		else if (in_info.planecount < 2) {
			jit_error_code(&ob, JIT_ERR_MISMATCH_PLANE);
			return;
		}
		else if (in_info.dimcount != 2) {
			jit_error_code(&ob, JIT_ERR_MISMATCH_DIM);
			return;
		}

		std::vector<cv::Point2f> pts;
		for (int i = 0, y = 0; y<in_info.dim[1]; y++) {
			char * row_bp = in_bp + y*in_info.dimstride[1];
			for (int x = 0; x<in_info.dim[0]; x++, i++) {
				// done like this so that we can accomodate matrices with planecount > 2
				glm::vec2 * cell = (glm::vec2 *)(row_bp + x*in_info.dimstride[0]);
				pts.push_back(cv::Point2f(cell->x, cell->y));
				cell++;
			}
		}
		imagePoints.push_back(pts);

		images = imagePoints.size();

		// restore matrix lock state:
		jit_object_method(in_mat, _jit_sym_lock, in_savelock);
	}

	void object_points(t_symbol * name, void * in_mat) {
		t_jit_matrix_info in_info;
		char * in_bp;

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
		if (in_info.type != _jit_sym_float32) {
			jit_error_code(&ob, JIT_ERR_MISMATCH_TYPE);
			return;
		}
		else if (in_info.planecount != 3 && in_info.planecount != 2) {
			jit_error_code(&ob, JIT_ERR_MISMATCH_PLANE);
			return;
		}
		else if (in_info.dimcount != 2) {
			jit_error_code(&ob, JIT_ERR_MISMATCH_DIM);
			return;
		}

		std::vector<cv::Point3f> pts;
		for (int i = 0, y = 0; y<in_info.dim[1]; y++) {
			glm::vec3 * cell = (glm::vec3 *)(in_bp + y*in_info.dimstride[1]);
			for (int x = 0; x<in_info.dim[0]; x++, i++) {
				// allow 2-plane matrices for planar rigs:
				float z = in_info.planecount == 3 ? cell->z : 0.;
				pts.push_back(cv::Point3f(cell->x, cell->y, z));
				cell++;
			}
		}
		objectPoints.push_back(pts);

		// restore matrix lock state:
		jit_object_method(in_mat, _jit_sym_lock, in_savelock);
	}

	void jit_matrix(t_symbol * name) {
		void * in_mat = jit_object_findregistered(name);
		if (!in_mat) {
			jit_error_code(&ob, JIT_ERR_INVALID_INPUT);
			return;
		}

		switch (proxy_getinlet(&ob)) {
		case 0:
			// image points
			image_points(name, in_mat);

			// bang()?

			break;
		case 1:
			// object points
			object_points(name, in_mat);

			break;
		default:
			break;
		}
	}

};


void estimateaffine3D_jit_matrix(t_estimateaffine3D *x, t_symbol *s) {
	x->jit_matrix(s);
}

void estimateaffine3D_bang(t_estimateaffine3D *x) {
	x->bang();
}

void estimateaffine3D_clear(t_estimateaffine3D *x) {
	x->clear();
}

void estimateaffine3D_assist(t_estimateaffine3D *x, void *b, long m, long a, char *s) {
	if (m == ASSIST_INLET) { // inlet
		switch (a) {
		case 0:
			sprintf(s, "source points (3-plane float32 matrix)");
			break;
		case 1:
			sprintf(s, "destination points (3-plane float32 matrix)");
			break;
		default:
			sprintf(s, "I am inlet %ld", a);
			break;
		}
	}
	else {	// outlet
		switch (a) {
		case 0:
			sprintf(s, "camera intrinsic matrix (list)");
			break;
		case 1:
			sprintf(s, "camera radial lens distortion coefficients k1, k2, k3 (list)");
			break;
		case 2:
			sprintf(s, "camera tangential lens distortion coefficients p1, p2 (list)");
			break;
		case 3:
			sprintf(s, "camera extrinsic rotation (list)");
			break;
		case 4:
			sprintf(s, "camera extrinsic translation x,y,z (list)");
			break;
		default:
			sprintf(s, "general messages");
			break;
		}
	}
}

void estimateaffine3D_free(t_estimateaffine3D *x)
{
	x->~t_estimateaffine3D();
}

void *estimateaffine3D_new(t_symbol *s, long argc, t_atom *argv) {
	t_estimateaffine3D *x = NULL;

	// object instantiation, NEW STYLE
	if (x = (t_estimateaffine3D *)object_alloc(maxclass)) {
		x = new(x) t_estimateaffine3D();

		// apply attrs:
		attr_args_process(x, argc, argv);
	}
	return (x);
}



void al_cv_estimateaffine3D_main()
{
	t_class *c = class_new("al.estimateaffine3D", (method)estimateaffine3D_new, (method)estimateaffine3D_free, (long)sizeof(t_estimateaffine3D), 0L, A_GIMME, 0);

	common_symbols_init();

	class_addmethod(c, (method)estimateaffine3D_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)estimateaffine3D_bang, "bang", 0);
	class_addmethod(c, (method)estimateaffine3D_clear, "clear", 0);
	class_addmethod(c, (method)estimateaffine3D_jit_matrix, "jit_matrix", A_SYM, 0);

	CLASS_ATTR_DOUBLE(c, "threshold", 0, t_estimateaffine3D, threshold);

	/*
	CLASS_ATTR_DOUBLE(c, "reprojection_error", 0, t_estimateaffine3D, reprojection_error);
	CLASS_ATTR_LONG(c, "images", 0, t_estimateaffine3D, images);

	CLASS_ATTR_DOUBLE_ARRAY(c, "distortion", 0, t_estimateaffine3D, distortion, 5);
	CLASS_ATTR_DOUBLE_ARRAY(c, "intrinsic", 0, t_estimateaffine3D, intrinsic, 9);
	CLASS_ATTR_LONG_ARRAY(c, "image_size", 0, t_estimateaffine3D, image_size, 2);

	CLASS_ATTR_LONG(c, "calibrate_aspect_ratio", 0, t_estimateaffine3D, calibrate_aspect_ratio);
	CLASS_ATTR_STYLE(c, "calibrate_aspect_ratio", 0, "onoff");
	CLASS_ATTR_LONG(c, "calibrate_tangent_distortion", 0, t_estimateaffine3D, calibrate_tangent_distortion);
	CLASS_ATTR_STYLE(c, "calibrate_tangent_distortion", 0, "onoff");
	CLASS_ATTR_LONG(c, "calibrate_principal_point", 0, t_estimateaffine3D, calibrate_principal_point);
	CLASS_ATTR_STYLE(c, "calibrate_principal_point", 0, "onoff");
	CLASS_ATTR_LONG(c, "calibrate_k1", 0, t_estimateaffine3D, calibrate_k1);
	CLASS_ATTR_STYLE(c, "calibrate_k1", 0, "onoff");
	CLASS_ATTR_LONG(c, "calibrate_k2", 0, t_estimateaffine3D, calibrate_k2);
	CLASS_ATTR_STYLE(c, "calibrate_k2", 0, "onoff");
	CLASS_ATTR_LONG(c, "calibrate_k3", 0, t_estimateaffine3D, calibrate_k3);
	CLASS_ATTR_STYLE(c, "calibrate_k3", 0, "onoff");

	CLASS_ATTR_LONG(c, "merge_points", 0, t_estimateaffine3D, merge_points);
	CLASS_ATTR_STYLE(c, "merge_points", 0, "onoff");

	CLASS_ATTR_LONG(c, "rodrigues", 0, t_estimateaffine3D, rodrigues);
	CLASS_ATTR_STYLE(c, "rodrigues", 0, "onoff");
	CLASS_ATTR_LONG(c, "invert_extrinsics", 0, t_estimateaffine3D, invert_extrinsics);
	CLASS_ATTR_STYLE(c, "invert_extrinsics", 0, "onoff");
	*/

	class_register(CLASS_BOX, c); /* CLASS_NOBOX */
	maxclass = c;
}
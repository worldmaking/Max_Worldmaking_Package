#include "al_cv.hpp"

static t_class *maxclass;

/*
http://docs.opencv.org/2.4/modules/calib3d/doc/camera_calibration_and_3d_reconstruction.html#solvepnp

finds object pose from set of 3D and 2D point correspondences

inputs: 
- array of 3D object points (3 plane float32)
- array of 2D image points (2 plane float32, matching dim)
- camera intrinsics 
- camera distortion coefficients
- rotation vector guess (optional)
- translation vector guess (optional)
- options: use pose guess, use iterative/p3p/epne methods
- or, use RANSAC version with more options (itarations, minimum inliers, minimum reprojection error)

outputs:
- rotation vector
- translation vector
- for ransac version only: indices of inliers

*/

class t_solvepnp {
public:
	t_object ob;
	long proxy_inlet_num;
	void * proxy;

	// attrs:
	int extrinsic_guess; // true/false
	int method; // iterative or epnp
	int ransac; // value indicates number of iterations, 0 means don't use RANSAC
	int ransac_min_inliers; // minimum no. of inliers to count as success
	float ransac_min_error; // maximum distance to be considered an inlier
	double intrinsic[9];
	double distortion[5];

	// outlets:
	void * outlet_msg;

	void * outlet_camera;
	void * outlet_object;
	//void * outlet_tangential;
	//void * outlet_radial;
	//void * outlet_intrinsic;

	// OpenCV:

	// the positions of the points on the chessboard (z == 0)
	std::vector<cv::Point3f> objectPoints;
	// the positions of the detected chessboard points in the camera view:
	std::vector<cv::Point2f> imagePoints;
	// the intrinsic matrix (focal length, center of projection)
	cv::Mat cvIntrinsic;
	// the distortion coefficients (radial, tangential)
	cv::Mat cvDistortion;
	// the estimated rotation & translation of each object/image pair
	cv::Mat rvec, tvec;

	glm::mat3 rotation;

	t_solvepnp() {

		ransac = 0;
		extrinsic_guess = 0;
		method = 0; // CV_ITERATIVE
		ransac_min_error = 8.f;
		ransac_min_inliers = 50;

		intrinsic[0] = 480.;
		intrinsic[1] = 0.;
		intrinsic[2] = 320;
		intrinsic[3] = 0.;
		intrinsic[4] = 480.;
		intrinsic[5] = 240.;
		intrinsic[6] = intrinsic[7] = 0.;
		intrinsic[8] = 1.;
		for (int i = 0; i < 5; i++) distortion[i] = 0.f;


		// add a general purpose outlet (rightmost)
		outlet_msg = outlet_new(this, 0);
		outlet_camera = outlet_new(this, 0);
		outlet_object = outlet_new(this, 0);

		// add a proxy inlet:
		proxy = proxy_new(this, 1, &proxy_inlet_num);

		clear();

	}

	~t_solvepnp() {

	}

	void clear() {
		objectPoints.clear();
		imagePoints.clear();

		// wrap our attrs with OpenCV matrices, giving us direct IO access :-)
		cvDistortion = cv::Mat(5, 1, CV_64F, distortion);
		cvIntrinsic = cv::Mat(3, 3, CV_64F, intrinsic);
	}

	void bang() {
		t_atom a[9];

		// verify the data exists and has matching length: 
		if (objectPoints.size() < 4) {
			object_error(&ob, "no object points received");
			return;
		}
		else if (imagePoints.size() < 4) {
			object_error(&ob, "no image points received");
			return;
		} else if (objectPoints.size() != imagePoints.size()) {
			object_error(&ob, "dimensions of the image and object matrices do not match");
			return;
		}

		bool result;
		if (ransac) {
			
			//result = cv::solvePnPRansac(objectPoints, imagePoints, cvIntrinsic, cvDistortion, rvec, tvec, extrinsic_guess, ransac, ransac_min_error, ransac_min_inliers, inliers, method ? CV_EPNP : CV_ITERATIVE);
		}
		else {
			result = cv::solvePnP(objectPoints, imagePoints, cvIntrinsic, cvDistortion, rvec, tvec, extrinsic_guess, method ? CV_EPNP : CV_ITERATIVE);
		}

		if (!result) {
			object_error(&ob, "failed to solvePnP");
			return;
		}

		// now convert rvec and tvec from OpenCV to OpenGL conventions

		// position: axis flip from OpenCV to glm:
		glm::vec3 pos(tvec.at<double>(0), -tvec.at<double>(1), -tvec.at<double>(2));

		// rotation: first convert the Rodrigues vector rvec into a 3x3 rotation matrix
		cv::Mat rmat(3, 3, CV_32F);
		cv::Rodrigues(rvec, rmat);
		// then transpose (because opencv uses row-major and glm is column-major) and axis flip from opencv to glm
		rotation = glm::mat3(
			rmat.at<double>(0, 0),
			-rmat.at<double>(1, 0),
			-rmat.at<double>(2, 0),
			rmat.at<double>(0, 1),
			-rmat.at<double>(1, 1),
			-rmat.at<double>(2, 1),
			rmat.at<double>(0, 2),
			-rmat.at<double>(1, 2),
			-rmat.at<double>(2, 2)
		);
		glm::quat q = glm::normalize(glm::quat_cast(rotation));
		// then hack because of the different cv/glm coordinate systems
		// (TODO: figure out how to fix that when instancing the rotation matrix above)
		q = glm::quat(-q.x, q.w, q.z, -q.y);

		// pos and q are the pose of the object in camera space:
		atom_setfloat(a + 0, q.x);
		atom_setfloat(a + 1, q.y);
		atom_setfloat(a + 2, q.z);
		atom_setfloat(a + 3, q.w);
		outlet_anything(outlet_object, _jit_sym_quat, 4, a);
		atom_setfloat(a, pos.x);
		atom_setfloat(a + 1, pos.y);
		atom_setfloat(a + 2, pos.z);
		outlet_anything(outlet_object, _jit_sym_position, 3, a);


		// invert quat:
		q = glm::inverse(q);
		atom_setfloat(a + 0, q.x);
		atom_setfloat(a + 1, q.y);
		atom_setfloat(a + 2, q.z);
		atom_setfloat(a + 3, q.w);
		outlet_anything(outlet_camera, _jit_sym_quat, 4, a);

		// invert position:
		pos = -pos;
		// and rotate by quat:
		pos = quat_rotate(q, pos);

		// now also compute the camera pose in object-space
		atom_setfloat(a, pos.x);
		atom_setfloat(a + 1, pos.y);
		atom_setfloat(a + 2, pos.z);
		outlet_anything(outlet_camera, _jit_sym_position, 3, a);
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

		imagePoints.clear();
		std::vector<cv::Point2f> pts;
		for (int i = 0, y = 0; y<in_info.dim[1]; y++) {
			char * row_bp = in_bp + y*in_info.dimstride[1];
			for (int x = 0; x<in_info.dim[0]; x++, i++) {
				// done like this so that we can accomodate matrices with planecount > 2
				glm::vec2 * cell = (glm::vec2 *)(row_bp + x*in_info.dimstride[0]);
				imagePoints.push_back(cv::Point2f(cell->x, cell->y));
				cell++;
			}
		}

		// restore matrix lock state:
		jit_object_method(in_mat, _jit_sym_lock, in_savelock);

		// go ahead and output:
		bang();
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

		objectPoints.clear();
		for (int i = 0, y = 0; y<in_info.dim[1]; y++) {
			glm::vec3 * cell = (glm::vec3 *)(in_bp + y*in_info.dimstride[1]);
			for (int x = 0; x<in_info.dim[0]; x++, i++) {
				// allow 2-plane matrices for planar rigs:
				float z = in_info.planecount == 3 ? cell->z : 0.;
				objectPoints.push_back(cv::Point3f(cell->x, cell->y, z));
				cell++;
			}
		}

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


void solvepnp_jit_matrix(t_solvepnp *x, t_symbol *s) {
	x->jit_matrix(s);
}

void solvepnp_bang(t_solvepnp *x) {
	x->bang();
}

void solvepnp_assist(t_solvepnp *x, void *b, long m, long a, char *s) {
	if (m == ASSIST_INLET) { // inlet
		switch (a) {
		case 0:
			sprintf(s, "image points (2-plane float32 matrix)");
			break;
		case 1:
			sprintf(s, "object points (2-plane or 3-plane float32 matrix)");
			break;
		default:
			sprintf(s, "I am inlet %ld", a);
			break;
		}
	}
	else {	// outlet
		switch (a) {
		case 0:
			sprintf(s, "position, quat of object in camera space");
			break;
		case 1:
			sprintf(s, "position, quat of camera in object space");
			break;
		default:
			sprintf(s, "general messages");
			break;
		}
	}
}

void solvepnp_free(t_solvepnp *x)
{
	x->~t_solvepnp();
}

void *solvepnp_new(t_symbol *s, long argc, t_atom *argv) {
	t_solvepnp *x = NULL;

	// object instantiation, NEW STYLE
	if (x = (t_solvepnp *)object_alloc(maxclass)) {
		x = new(x) t_solvepnp();

		// apply attrs:
		attr_args_process(x, argc, argv);
	}
	return (x);
}



void al_cv_solvepnp_main()
{
	t_class *c = class_new("al.solvepnp", (method)solvepnp_new, (method)solvepnp_free, (long)sizeof(t_solvepnp), 0L, A_GIMME, 0);

	common_symbols_init();

	class_addmethod(c, (method)solvepnp_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)solvepnp_bang, "bang", 0);
	class_addmethod(c, (method)solvepnp_jit_matrix, "jit_matrix", A_SYM, 0);

	CLASS_ATTR_DOUBLE_ARRAY(c, "distortion", 0, t_solvepnp, distortion, 5);
	CLASS_ATTR_DOUBLE_ARRAY(c, "intrinsic", 0, t_solvepnp, intrinsic, 9);

	class_register(CLASS_BOX, c); /* CLASS_NOBOX */
	maxclass = c;
}
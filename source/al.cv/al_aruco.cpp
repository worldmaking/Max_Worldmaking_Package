/*

see http://docs.opencv.org/3.2.0/d5/dae/tutorial_aruco_detection.html

TODO allow other dictionaries than DICT_ARUCO_ORIGINAL

*/


#include "al_cv.hpp"

static t_class *maxclass;

class t_aruco {
public:

	typedef struct { float x, y; } vec2f;

	t_object	ob;			// the object itself (must be first)

							// many outlets:
	void *		outlet_msg;
	void *		outlet_img;
	void *		outlet_corners;

	// attrs:
	double intrinsic[9];
	double distortion[5];
	float markersize;
	t_atom_long sought_id;

	int			size[2];
	int			fast_check, adaptive_thresh, normalize_image, filter_quads;

	// corners matrix:
	void *		corners_mat;
	void *		corners_mat_wrapper;
	t_atom		corners_mat_name[1];
	vec2f *		corners_data;

	// the intrinsic matrix (focal length, center of projection)
	cv::Mat cvIntrinsic;
	// the distortion coefficients (radial, tangential)
	cv::Mat cvDistortion;

	std::vector<cv::Point2f> corners;
	cv::Ptr<cv::aruco::Dictionary> dictionary;
	std::vector< int > markerIds;
	std::vector< std::vector<cv::Point2f> > markerCorners, rejectedCandidates;

	t_aruco() {

		outlet_msg = outlet_new(&ob, 0);
		outlet_img = outlet_new(&ob, "jit_matrix");
		outlet_corners = outlet_new(&ob, 0);

		// TODO: support other dictionaries
		dictionary = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_ARUCO_ORIGINAL);
		markersize = 0.05;
		sought_id = -1;

		fast_check = 1;
		adaptive_thresh = 1;
		normalize_image = 1;
		filter_quads = 0;

		size[0] = 10;
		size[1] = 7;

		intrinsic[0] = 580.;
		intrinsic[1] = 0.;
		intrinsic[2] = 320;
		intrinsic[3] = 0.;
		intrinsic[4] = 580.;
		intrinsic[5] = 240.;
		intrinsic[6] = intrinsic[7] = 0.;
		intrinsic[8] = 1.;
		for (int i = 0; i < 5; i++) distortion[i] = 0.f;

		// wrap our attrs with OpenCV matrices, giving us direct IO access :-)
		cvDistortion = cv::Mat(5, 1, CV_64F, distortion);
		cvIntrinsic = cv::Mat(3, 3, CV_64F, intrinsic);

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

	~t_aruco() {
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

	void marker() {
		cv::Mat markerImage;
		auto dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_ARUCO_ORIGINAL);
		cv::aruco::drawMarker(dict, 23, 200, markerImage, 1);
	}

	void jit_matrix(t_symbol * name) {
		t_jit_matrix_info in_info;
		char * in_bp;
		t_atom a[5];

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
		try {
			// create CV mat wrapper around Jitter matrix data
			// (cv declares dim as numrows, numcols)
			cv::Mat InImage(in_info.dim[1], in_info.dim[0], CV_8UC(in_info.planecount), in_bp, in_info.dimstride[1]);



			// convert to greyscale if necessary:
			cv::Mat src;
			if (in_info.planecount != 1) {
				cv::cvtColor(InImage, src, CV_RGBA2GRAY);
			}
			else {
				src = InImage;
			}

			markerIds.clear();
			corners.clear();
			markerCorners.clear();
			rejectedCandidates.clear();

			auto parameters = cv::aruco::DetectorParameters::create();
			cv::aruco::detectMarkers(src, dictionary, markerCorners, markerIds, parameters, rejectedCandidates);


			//cv::aruco::drawDetectedMarkers(src, markerCorners, markerIds);
		}
		catch (std::exception &ex) {
			object_error(&ob, "exception: %s", ex.what());
		}

		// restore matrix lock state:
		jit_object_method(in_mat, _jit_sym_lock, in_savelock);


		// output the image:
		atom_setsym(a, name);
		outlet_anything(outlet_img, _jit_sym_jit_matrix, 1, a);

		// output:
		bang();
	}

	void bang() {
		t_atom a[5];

		// output the count:
		atom_setlong(a, markerIds.size());
		outlet_anything(outlet_msg, _sym_count, 1, a);

		if (markerIds.size() < 1) {
			return;
		}

		try {

			// output individual corner data?

			// TODO: move this to jit_matrix method instead?
			// estimate poses:
			std::vector< cv::Vec3d > rvecs, tvecs;
			cv::aruco::estimatePoseSingleMarkers(markerCorners, markersize, cvIntrinsic, cvDistortion, rvecs, tvecs);

			std::vector<glm::vec3> points;

			// draw axis for each marker
			for (int i = 0; i < rvecs.size(); i++) {
				int id = markerIds[i];

				if (sought_id < 0 || id == sought_id) {




					// ok so let's try outputting these poses.
					cv::Vec3d& rvec = rvecs[i];
					cv::Vec3d& tvec = tvecs[i];

					// position: axis flip from OpenCV to glm:
					glm::vec3 pos(tvec[0], -tvec[1], -tvec[2]);

					// rotation: first convert the Rodrigues vector rvec into a 3x3 rotation matrix
					cv::Mat rmat(3, 3, CV_32F);
					cv::Rodrigues(rvec, rmat);
					// then transpose (because opencv uses row-major and glm is column-major) and axis flip from opencv to glm
					glm::mat3 rotation(
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

					atom_setlong(a, i);
					outlet_anything(outlet_msg, _jit_sym_cell, 1, a);


					atom_setlong(a, id);
					outlet_anything(outlet_msg, _sym_name, 1, a);

					// pos and q are the pose of the object in camera space:
					atom_setfloat(a + 0, q.x);
					atom_setfloat(a + 1, q.y);
					atom_setfloat(a + 2, q.z);
					atom_setfloat(a + 3, q.w);
					outlet_anything(outlet_msg, _jit_sym_quat, 4, a);

					atom_setfloat(a, pos.x);
					atom_setfloat(a + 1, pos.y);
					atom_setfloat(a + 2, pos.z);
					outlet_anything(outlet_msg, _jit_sym_position, 3, a);

					// TODO: add inverse for equivalent camera pose, as in solvePnP


					// add this to the points:

					// actually, since we have estimated the pose, we can add corner points:
					glm::vec3 vx = quat_ux(q) * (markersize * 0.5f);
					glm::vec3 vy = quat_uy(q) * (markersize * 0.5f);

					points.push_back(pos + vx + vy);
					points.push_back(pos - vx + vy);
					points.push_back(pos + vx - vy);
					points.push_back(pos - vx - vy);
				}
			}

			if (points.size() > 2) {


				// dump out all the points:

				for (int i = 0; i < points.size(); i++) {
					atom_setfloat(a + 0, points[i].x);
					atom_setfloat(a + 1, points[i].y);
					atom_setfloat(a + 2, points[i].z);
					outlet_anything(outlet_corners, _jit_sym_position, 3, a);
				}











				cv::Mat_<double> cldm(points.size(), 3);
				for (unsigned int i = 0; i < points.size(); i++) {
					cldm.row(i)(0) = points[i].x;
					cldm.row(i)(1) = points[i].y;
					cldm.row(i)(2) = points[i].z;
				}

				
				cv::Mat_<double> mean;
				cv::PCA pca(cldm, mean, CV_PCA_DATA_AS_ROW);

				if (pca.eigenvalues.rows > 2) {

					//post("evs %i %i", pca.eigenvalues.rows, pca.eigenvalues.cols);

					double p_to_plane_thresh = pca.eigenvalues.at<double>(2);
					int num_inliers = 0;
					cv::Vec3d nrm = pca.eigenvectors.row(2);
					nrm = nrm / norm(nrm);
					cv::Vec3d x0 = pca.mean;

					for (int i = 0; i < points.size(); i++) {

						cv::Vec3d p(points[i].x, points[i].y, points[i].z);

						cv::Vec3d w = p - x0;
						double D = fabs(nrm.dot(w));
						if (D < p_to_plane_thresh) num_inliers++;
					}

					//post("inliers: %i", num_inliers);
					
				}
			}

		}
		catch (std::exception &ex) {
			object_error(&ob, "exception: %s", ex.what());
		}
	}
};

t_max_err aruco_size_set(t_aruco *x, t_object *attr, long argc, t_atom *argv)
{
	if (argc < 2) return 0;

	t_atom_long ix = atom_getlong(argv);
	t_atom_long iy = atom_getlong(argv + 1);

	if (ix < 0 || iy < 0) return 0;

	x->resize(ix, iy);

	return 0;
}


void aruco_assist(t_aruco *x, void *b, long m, long a, char *s)
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

void aruco_free(t_aruco *x) {
	x->~t_aruco();

	// free resources associated with our obex entry
	//jit_ob3d_free(x);
	max_jit_object_free(x);
}

void *aruco_new(t_symbol *s, long argc, t_atom *argv)
{
	t_aruco *x = NULL;
	if (x = (t_aruco *)object_alloc(maxclass)) {

		// initialize in-place:
		x = new (x) t_aruco();

		// apply attrs:
		attr_args_process(x, argc, argv);
	}
	return (x);
}


t_max_err aruco_notify(t_aruco *x, t_symbol *s, t_symbol *msg, void *sender, void *data) {
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

void aruco_jit_matrix(t_aruco * x, t_symbol *s) {
	x->jit_matrix(s);
}

void aruco_bang(t_aruco * x) {
	x->bang();
}

void al_cv_aruco_main() {
	maxclass = class_new("al.aruco", (method)aruco_new, (method)aruco_free, (long)sizeof(t_aruco),
		0L, A_GIMME, 0);

	class_addmethod(maxclass, (method)aruco_assist, "assist", A_CANT, 0);
	class_addmethod(maxclass, (method)aruco_notify, "notify", A_CANT, 0);

	class_addmethod(maxclass, (method)aruco_jit_matrix, "jit_matrix", A_SYM, 0);
	class_addmethod(maxclass, (method)aruco_bang, "bang", 0);


	CLASS_ATTR_DOUBLE_ARRAY(maxclass, "distortion", 0, t_aruco, distortion, 5);
	CLASS_ATTR_DOUBLE_ARRAY(maxclass, "intrinsic", 0, t_aruco, intrinsic, 9);

	CLASS_ATTR_FLOAT(maxclass, "markersize", 0, t_aruco, markersize);
	CLASS_ATTR_LONG(maxclass, "sought_id", 0, t_aruco, sought_id);

	class_register(CLASS_BOX, maxclass);
}
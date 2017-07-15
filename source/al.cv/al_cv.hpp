#pragma once
#ifndef AL_CV_HPP
#define AL_CV_HPP



// CV wants to be included first, because of macro clashing I think
//#include "opencv2/core.hpp"

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/calib3d/calib3d.hpp"
#ifdef WIN_VERSION
#include "opencv2/aruco.hpp"
#endif

#include "al_max.h"

extern "C" {

	C74_EXPORT void al_cv_findchessboard_main();
	C74_EXPORT void al_cv_calibratecamera_main();
	C74_EXPORT void al_cv_solvepnp_main();
#ifdef WIN_VERSION
	C74_EXPORT void al_cv_aruco_main();
#endif
}

#endif

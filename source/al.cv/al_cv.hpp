#pragma once
#ifndef AL_CV_HPP
#define AL_CV_HPP

// CV wants to be included first, because of macro clashing I think
//#include "opencv2/core.hpp"

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/calib3d/calib3d.hpp"

#include "al_max.h"


void al_cv_findchessboard_main();
void al_cv_calibratecamera_main();

#endif

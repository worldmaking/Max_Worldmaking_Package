#include "al_cv.hpp"

/*
 OSX: brew install opencv3 --with-contrib
 */

void ext_main(void *r)
{
	t_class *c;

	common_symbols_init();
	
	// amazingly this works
	// cv::namedWindow("Display window", cv::WINDOW_AUTOSIZE); // Create a window for display.

	al_cv_findchessboard_main();
#ifdef WIN_VERSION
	al_cv_aruco_main();
#endif
	al_cv_calibratecamera_main();
	al_cv_solvepnp_main();
}

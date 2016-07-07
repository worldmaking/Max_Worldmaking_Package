#ifndef cl_max_h
#define cl_max_h

#include "../al_max.h"

#if defined __APPLE__ || defined(MACOSX)
	#include <OpenCL/opencl.h>
	#include <OpenGL/OpenGL.h>
	#pragma OPENCL EXTENSION CL_APPLE_gl_sharing : enable
#else
	#include <CL/opencl.h>
	//needed for context sharing functions
	#include <GL/glx.h>
#endif

#define STRINGIFY(A) #A

extern "C" void cl_vbo_init();

#endif /* cl_max_h */

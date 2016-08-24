#ifndef al_math_h
#define al_math_h

// how many glm headers do we really need?
#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/matrix_access.hpp"
#include "glm/gtc/matrix_inverse.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/noise.hpp"
#include "glm/gtc/random.hpp"
#include "glm/gtc/type_ptr.hpp"

// unstable extensions
//#include "glm/gtx/norm.hpp"
//#include "glm/gtx/std_based_type.hpp"

/* 
	You probably want to put this in your header:

	using glm::vec2;
	using glm::vec3;
	using glm::vec4;
	using glm::quat;
	using glm::mat4;
*/

//	q must be a normalized quaternion
template<typename T, glm::precision P>
glm::tvec3<T, P> quat_unrotate(glm::quat const & q, glm::tvec3<T, P> & v) {
	// return quat_mul(quat_mul(quat_conj(q), vec4(v, 0)), q).xyz;
	// reduced:
	glm::tvec4<T, P> p(
		q.w*v.x - q.y*v.z + q.z*v.y,  // x
		q.w*v.y - q.z*v.x + q.x*v.z,  // y
		q.w*v.z - q.x*v.y + q.y*v.x,  // z
		q.x*v.x + q.y*v.y + q.z*v.z   // w
		);
	return glm::tvec3<T, P>(
		p.w*q.x + p.x*q.w + p.y*q.z - p.z*q.y,  // x
		p.w*q.y + p.y*q.w + p.z*q.x - p.x*q.z,  // y
		p.w*q.z + p.z*q.w + p.x*q.y - p.y*q.x   // z
		);
}

//	q must be a normalized quaternion
template<typename T, glm::precision P>
glm::tvec3<T, P> quat_rotate(glm::quat const & q, glm::tvec3<T, P> & v) {
	glm::tvec4<T, P> p(
		q.w*v.x + q.y*v.z - q.z*v.y,	// x
		q.w*v.y + q.z*v.x - q.x*v.z,	// y
		q.w*v.z + q.x*v.y - q.y*v.x,	// z
		-q.x*v.x - q.y*v.y - q.z*v.z	// w
		);
	return glm::tvec3<T, P>(
		p.x*q.w - p.w*q.x + p.z*q.y - p.y*q.z,	// x
		p.y*q.w - p.w*q.y + p.x*q.z - p.z*q.x,	// y
		p.z*q.w - p.w*q.z + p.y*q.x - p.x*q.y	// z
		);
}

#endif

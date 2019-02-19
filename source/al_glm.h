#ifndef AL_GLM_H
#define AL_GLM_H

#define _USE_MATH_DEFINES
#include <math.h>

// how many glm headers do we really need?
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>

#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/noise.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <glm/gtx/matrix_transform_2d.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/vector_angle.hpp>

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

// has a uniform distribution, per http://planning.cs.uiuc.edu/node198.html
// see also http://pajarito.materials.cmu.edu/rollett/27750/lecture2.pdf
inline glm::quat quat_random() {
	float r1 = glm::linearRand(0.f, 1.f);
	float r2 = glm::linearRand(0.f, 1.f);
	float r3 = glm::linearRand(0.f, 1.f);
	float sq1 = sqrtf(r3);
	float sq2 = sqrtf(1.f-r3);
	return glm::quat(
		cosf(float(M_PI * 2.) * r1) * sq1,
		sinf(float(M_PI * 2.) * r2) * sq2,
		cosf(float(M_PI * 2.) * r2) * sq1,
		sinf(float(M_PI * 2.) * r1) * sq2
	);
}


template<typename T, glm::precision P>
inline glm::tvec3<T, P> quat_ux(glm::tquat<T, P> const & q) {
	return glm::tvec3<T, P>(
		T(1) - T(2) * ((q.y * q.y) + (q.z * q.z)),
		T(2) * ((q.x * q.y) + (q.w * q.z)),
		T(2) * ((q.x * q.z) - (q.w * q.y))
		);
}

template<typename T, glm::precision P>
inline glm::tvec3<T, P> quat_uy(glm::tquat<T, P> const & q) {
	return glm::tvec3<T, P>(
		T(2) * ((q.x * q.y) - (q.w * q.z)),
		T(1) - 2 * ((q.x * q.x) + (q.z * q.z)),
		T(2) * ((q.y * q.z) + (q.w * q.x))
		);
}

template<typename T, glm::precision P>
inline glm::tvec3<T, P> quat_uz(glm::tquat<T, P> const & q) {
	return glm::tvec3<T, P>(
		T(2) * ((q.x * q.z) + (q.w * q.y)),
		T(2) * ((q.y * q.z) - (q.w * q.x)),
		T(1) - T(2) * ((q.x * q.x) + (q.y * q.y))
		);
}

template<typename T, glm::precision P>
inline glm::tvec3<T, P> quat_uf(glm::tquat<T, P> const & q) {
	return -quat_uz(q);
}

template<typename T, glm::precision P>
inline glm::tvec2<T, P> safe_normalize (glm::tvec2<T, P> const &v) {
	T l = glm::length(v);
	if (l > T(0.00001)) {
		return v * (T(1)/l);
	} else {
		return glm::circularRand(T(1));
	}
}

template<typename T, glm::precision P>
inline glm::tvec3<T, P> safe_normalize (glm::tvec3<T, P> const &v) {
	T l = glm::length(v);
	if (l > T(0.00001)) {
		return v * (T(1)/l);
	} else {
		return glm::sphericalRand(T(1));
	}
}

template<typename T, glm::precision P>
inline glm::tvec4<T, P> safe_normalize (glm::tvec4<T, P> const &v) {
	T l = glm::length(v);
	if (l > T(0.00001)) {
		return v * (T(1)/l);
	} else {
		return glm::vec4(glm::sphericalRand(T(1)), 1.f);
	}
}

template<typename T, glm::precision P>
inline glm::tquat<T, P> safe_normalize (glm::tquat<T, P> const &v) {
	T l = glm::length(v);
	if (l > T(0.00001)) {
		return v * (T(1)/l);
	} else {
		return quat_random();
	}
}

inline glm::vec3 transform (glm::mat4 const & mat, glm::vec3 const & vec) {
	return glm::vec3(mat * glm::vec4(vec, 1.f));
}

inline glm::vec2 transform (glm::mat3 const & mat, glm::vec2 const & vec) {
	return glm::vec2(mat * glm::vec3(vec, 1.f));
}

// max should be >> 0.
template<typename T>
inline T limit(T v, float max) {
	// float len2 = glm::dot(v,v);
	// if (len2 > max*max) return v * max/sqrt(len);
	float len = glm::length(v);
	if (len > max) return v * max/len;
	// v /= glm::min(len, max);   // TODO faster or slower?
	return v;
}

#endif // AL_GLM_H


/*
Quick tut on glm:
@see http://glm.g-truc.net/0.9.4/api/a00141.html
@see http://glm.g-truc.net/0.9.5/api/modules.html

# Vectors

v3 = vec3(0.);
v4 = vec4(v3, 1.);

v.x; // access v.x, v.y, v.z etc.

i = v.length();					// no. elements in type
s = glm::length(v);				// length of vector
s = glm::length2(v);				// length squared

v = glm::normalize(v);		// will create NaNs if vector is zero length
v = glm::cross(v1, v2);
s = glm::dot(v1, v2);
s = glm::distance(v1, v2);
v = glm::faceforward(vn, vi, vref);	//If dot(Nref, I) < 0.0, return N, otherwise, return -N.
v = glm::reflect(vi, vn);	// reflect vi around vn
v = glm::refract(vi, vn, eta);

v = glm::cos(v);			// sin, tan, acos, atanh, etc.
v = glm::atan(v1, v2);		// aka atan2
v = glm::degrees(v);		// radians()

v = glm::abs(v);			// ceil, floor, fract, trunc
v = glm::pow(v1, v2);		// exp, exp2, log, log2, sqrt, inversesqrt
v = glm::mod(v, v_or_s);	// x - y * floor(x / y)
v = glm::modf(v, &iv);		// returns fract, stores integer part in iv
v = glm::round(v);			// direction of 0.5 implementation defined
v = glm::roundEven(v);		// 0.5 rounds to nearest even integer
v = glm::sign(v);
v = glm::clamp(v, vmin, vmax);	// min, max
v = glm::fma(va, vb, vc);	// return a*b+c
v = glm::mix(v1, v2, a);

// Returns 0.0 if x <= edge0 and 1.0 if x >= edge1 and performs smooth Hermite interpolation between 0 and 1 when edge0 < x < edge1.
v = glm::smoothstep(v0, v1, vx);
v = glm::step(e, v);		// v<e ? 0 : 1


v<bool> = glm::isnan(v);	// isinf
v<bool> = glm::equal(v1, v2);	// notEqual, lessThanEqual, etc.
bool = glm::any(v<bool);	// also glm::all()

# Matrices:

m = mat4(1.); // or mat4();	// identity matrix
m[3] = vec4(1, 1, 0, 1);	// set 4th column (translation)
v = vec3(m[3]);				// get translation component

v = m * v;					// vertex transformation

// (matrix types store their values in column-major order)
glm::value_ptr():

// e.g.
glVertex3fv(glm::value_ptr(v));		// or glVertex3fv(&v[0]);
glLoadMatrixfv(glm::value_ptr(m));	// or glLoadMatrixfv(&m[0][0]);

m = glm::make_mat4(ptr);	// also make_mat3, make_mat3x2, etc.

v = glm::column(m, idx);
v = glm::row(m, idx);
m = glm::transpose(m);
m = glm::inverse(m);
s = glm::determinant(m);
m = glm::matrixCompMult(m1, m2);	// component-wise multiplication
m = glm::outerProduct(vc, vr);		// generate mat by vc * vr

// Map the specified vertex into window coordinates.
v = glm::project(vec3 v, mat4 model, mat4 proj, vec4 viewport);
v = glm::unProject(vec3 win, mat4 model, mat4 proj, vec4 viewport);

m = glm::frustum(l, r, b, t, near, far);
m = glm::ortho(l, r, b, t);					// for 2D
m = glm::ortho(l, r, b, t, near, far);
m = glm::infinitePerspective(fovy, aspect, near, far);
m = glm::perspective(fovy, aspect, near, far);

m = glm::lookat(eye, at, up);

// define a picking region
m = glm::pickMatrix(vec2_center, vec2_delta, vec4_viewport);

m = glm::rotate(m, angle, axis);
m = glm::scale(m, v);
m = glm::translate(m, v);

m = glm::affineInverse(m);	// Fast inverse for affine matrix.
m = glm::inverseTranspose(m);

# Quaternions
// uses wxyz order:
q = quat(w, x, y, z);

q = q * rot;				// rot is in model space (local)
q = rot * q;				// rot is in world space (global)
Remember to normalize quaternions periodically!

s = glm::length(q);
s = glm::pitch(q);			// also roll(q), yaw(q)

q = glm::normalize(q);
q = glm::conjugate(q);
q = glm::inverse(q);
q = glm::dot(q1, q2);
v = glm::cross(q, v);

q = glm::lerp(q1, q2, s);
q = glm::mix(q1, q2, s);
q = glm::slerp(q1, q2, s);

// also greaterThan, greaterThanEqual, lessThan, notEqual, etc.
vec4_bool = glm::equal(q1, q2);

## conversions:

q = glm::angleAxis(angle, x, y, z);
q = glm::angleAxis(angle, axis);

a = glm::angle(q);
axis = glm::axis(q);

m = glm::mat3_cast(q);
m = glm::mat4_cast(q);
q = glm::quat_cast(m);		// from mat3 or mat4

pitch_yaw_roll = glm::eulerAngles(q);

# Random / Noise

s = glm::noise1(v);
vec2 = glm::noise2(v);		// etc. noise3, noise4

s = glm::perlin(v);			// classic perlin noise
s = glm::perlin(v, v_rep);	// periodic perlin noise
s = glm::simplex(v);		// simplex noise

## generate vec<n> or scalar:
gaussRand(mean, deviation);
linearRand(min, max);

## generate vec3:
ballRand(radius);			// coordinates are regulary distributed within the volume of a ball
circularRand(radius);		// coordinates are regulary distributed on a circle
diskRand(radius);			// coordinates are regulary distributed within the area of a disk
sphericalRand(radius);		// coordinates are regulary distributed on a sphere
*/



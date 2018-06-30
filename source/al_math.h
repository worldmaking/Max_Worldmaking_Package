#ifndef AL_MATH_H
#define AL_MATH_H

#include "al_glm.h"

#ifndef AL_MIN
#define AL_MIN(x,y) (x<y?x:y)
#endif

#ifndef AL_MAX
#define AL_MAX(x,y) (x>y?x:y)
#endif

inline double al_floor(double v) { return (int64_t)(v) - ((v)<0.0 && (v)!=(int64_t)(v)); } 
inline float al_floor(float v) { return (int64_t)(v) - ((v)<0.f && (v)!=(int64_t)(v)); }

inline double al_ceil(double v) { return (((v)>0.0)&&((v)!=(int64_t)(v))) ? 1.0+(v) : (v); }
inline float al_ceil(float v) { return (((v)>0.f)&&((v)!=(int32_t)(v))) ? 1.f+(v) : (v); }

inline double al_fract(double v) { return ((v)>=0.0) ? (v)-(int64_t)(v) : (-v)-(int64_t)(v); }
inline float al_fract(float v) { return ((v)>=0.f) ? (v)-(int32_t)(v) : (-v)-(int32_t)(v); }

inline bool al_isnan(float v) { return v!=v; }
inline float al_fixnan(float v) { return al_isnan(v)?0.f:v; }

inline bool al_isnan(glm::vec2 v) { 
	return al_isnan(v.x) || al_isnan(v.y); 
}
inline bool al_isnan(glm::vec3 v) { 
	return al_isnan(v.x) || al_isnan(v.y) || al_isnan(v.z); 
}
inline bool al_isnan(glm::vec4 v) { 
	return al_isnan(v.x) || al_isnan(v.y) || al_isnan(v.z) || al_isnan(v.w); 
}
inline bool al_isnan(glm::quat v) { 
	return al_isnan(v.x) || al_isnan(v.y) || al_isnan(v.z) || al_isnan(v.w); 
}

inline glm::vec2 al_fixnan(glm::vec2 v) { 
	return glm::vec2( al_fixnan(v.x), al_fixnan(v.y));
}
inline glm::vec3 al_fixnan(glm::vec3 v) { 
	return glm::vec3( al_fixnan(v.x), al_fixnan(v.y), al_fixnan(v.z) );
}
inline glm::vec4 al_fixnan(glm::vec4 v) { 
	return glm::vec4( al_fixnan(v.x), al_fixnan(v.y), al_fixnan(v.z), al_fixnan(v.w) );
}
inline glm::quat al_fixnan(glm::quat v) { 
	return glm::quat( al_fixnan(v.x), al_fixnan(v.y), al_fixnan(v.z), al_fixnan(v.w) );
}

// element-wise min & max
inline float al_min(glm::vec2 v) { return glm::min(v.x, v.y); }
inline float al_min(glm::vec3 v) { return glm::min(glm::min(v.x, v.y), v.z); }
inline float al_min(glm::vec4 v) { return glm::min(glm::min(v.x, v.y), glm::min(v.z, v.w)); }
inline float al_max(glm::vec2 v) { return glm::max(v.x, v.y); }
inline float al_max(glm::vec3 v) { return glm::max(glm::max(v.x, v.y), v.z); }
inline float al_max(glm::vec4 v) { return glm::max(glm::max(v.x, v.y), glm::max(v.z, v.w)); }

float radians(float degrees) {
	return degrees*0.01745329251994f;
}

float degrees(float radians) {
	return radians*57.29577951308233f;
}

float clip(float in, float min, float max) {
	float v = AL_MIN(in, max);
	return AL_MAX(v, min);
}

/// Wrap is Euclidean modulo remainder
// that is, the output is always zero or positive, and less than the upper bound
// and rounding is always toward -infinity

// For non-integral types:
// N must be nonzero
template<typename T, typename T1>
typename std::enable_if<!std::is_integral<T>::value, T>::type wrap(T a, T1 N) {
	// Note: "a - N*floor(a / N)" does not work due to occasional floating point error
	return glm::fract(a / T(N)) * T(N);
}

// For integral types only:
// N must be nonzero
template<typename T, typename T1>
typename std::enable_if<std::is_integral<T>::value, T>::type wrap(T a, T1 N) {
    T r = a % T(N);
    return r < 0 ? r+T(N) : r;
}

// wrap with a lower bound:
template<typename T, typename T1>
inline T wrap(T x, T1 lo, T1 hi) {
	return lo + wrap(x-lo, hi-lo);
}

// Sign function that doesn't ever return 0
inline float al_sign_nonzero(float x) { return (x < 0.f) ? -1.f : 1.f; }
inline double al_sign_nonzero(double x) { return (x < 0.) ? -1. : 1.; }\


class rnd {
public: 

	static void seed() {
		srand((unsigned int)time(NULL));
	}

	static uint64_t integer(uint64_t lim=2) {
		return (int)floorf(glm::linearRand(0.f, lim-0.0000001f));
	}

	static float uni(float lim=1.f) {
		return glm::linearRand(0.f, lim);
	}	

	static float bi(float lim=1.f) {
		return glm::linearRand(-lim, lim);
	}	

};

#endif //AL_MATH_H
// owl_math.h
// OWL C API v2.0

/***
Copyright (c) PhaseSpace, Inc 2017

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL PHASESPACE, INC
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
***/

#ifndef OWL_MATH_H
#define OWL_MATH_H

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

// standard orientation is:
// x - right
// y - up
// z - back

// POSE
// input: pos(x, y, z), angle, rotation about v(vx, vy, vz)
// output: pos(x, y, z), quaterion(cos(angle/2), v*sin(angle/2))
#define OWL_POSE(x, y, z, angle, vx, vy, vz) \
{ x, y, z, cos(angle/2.0), vx*sin(angle/2.0), vy*sin(angle/2.0), vz*sin(angle/2.0) }

float* owl_add_v3v3(const float *a, const float *b, float *ab);
float* owl_mult_v3s(const float *a, float s, float *as);
float* owl_mult_mv3_v3(const float *a, const float *b, float *ab);
float* owl_mult_mpl_pl(const float *a, const float *b, float *ab);
float* owl_mult_qq(const float *a, const float *b, float *ab);
float* owl_mult_qvq(const float *q, const float *v, float *qvq);
float* owl_mult_qvsq(const float *q, const float *v, float s, float *qvq);
float* owl_normalize_q(float *q);
float* owl_normalize_p(float *p);
float* owl_mult_pp(const float *p1, const float *p2, float *p3);
float* owl_mult_pps(const float *p1, const float *p2, float s, float *p3);
float* owl_invert_p(float *p);
float* owl_convert_pm(const float *p, float *m);
float* owl_convert_pmi(const float *p, float *m);
float* owl_convert_mp(const float *m, float *p);

//// implementation ////

inline float* owl_add_v3v3(const float *a, const float *b, float *ab)
{
  ab[0] = a[0] + b[0]; ab[1] = a[1] + b[1]; ab[2] = a[2] + b[2];
  return ab;
}

inline float* owl_mult_v3s(const float *a, float s, float *as)
{
  as[0] = a[0] * s; as[1] = a[1] * s; as[2] = a[2] * s;
  return as;
}

inline float* owl_mult_mv3_v3(const float *a, const float *b, float *ab)
{
  ab[0] = a[0]*b[0] + a[4]*b[1] + a[8] *b[2] + a[12];
  ab[1] = a[1]*b[0] + a[5]*b[1] + a[9] *b[2] + a[13];
  ab[2] = a[2]*b[0] + a[6]*b[1] + a[10]*b[2] + a[14];
  return ab;
}

// matrix * plane
inline float* owl_mult_mpl_pl(const float *a, const float *b, float *ab)
{
  ab[0] = a[0]*b[0] + a[4]*b[1] + a[8] *b[2] + a[3] *b[3];
  ab[1] = a[1]*b[0] + a[5]*b[1] + a[9] *b[2] + a[7] *b[3];
  ab[2] = a[2]*b[0] + a[6]*b[1] + a[10]*b[2] + a[11]*b[3];
  ab[3] = (-(a[0]*a[12] + a[1]*a[13] + a[2] *a[14])*b[0]
           -(a[4]*a[12] + a[5]*a[13] + a[6] *a[14])*b[1]
           -(a[8]*a[12] + a[9]*a[13] + a[10]*a[14])*b[2]
           +a[15]*b[3]);
  return ab;
}

inline float* owl_mult_qq(const float *a, const float *b, float *ab)
{
  // ab = a*b;
  // ab.s = a.s*b.s - dot(a.v,b.v);
  ab[0] = a[0]*b[0] - a[1]*b[1] - a[2]*b[2] - a[3]*b[3];
  // ab.v = cross(a.v,b.v) + a.s*b.v + b.s*a.v ;
  ab[1] = +a[2]*b[3] - a[3]*b[2] + a[0]*b[1] + b[0]*a[1];
  ab[2] = -a[1]*b[3] + a[3]*b[1] + a[0]*b[2] + b[0]*a[2];
  ab[3] = +a[1]*b[2] - a[2]*b[1] + a[0]*b[3] + b[0]*a[3];
  return ab;
}

inline float* owl_mult_qvq(const float *q, const float *v, float *qvq)
{
  // apply rotation(q) to a vector(v)
  // qvq = q*(v*q^{-1})
  float t[4];
  // quaternion t=v*q^{-1};
  // t.s = -dot(v,-q.v);
  t[0] = +v[0]*q[1] + v[1]*q[2] + v[2]*q[3];
  // t.v = cross(v,-q.v) + q.s*v ;
  t[1] = -v[1]*q[3] + v[2]*q[2] + v[0]*q[0];
  t[2] = +v[0]*q[3] - v[2]*q[1] + v[1]*q[0];
  t[3] = -v[0]*q[2] + v[1]*q[1] + v[2]*q[0];

  // qvq = q*t;
  // qvq = cross(q.v,t.v) + q.s*t.v + t.s*(-q.v) ;
  qvq[0] = +q[2]*t[3] - q[3]*t[2] + q[0]*t[1] + t[0]*q[1];
  qvq[1] = -q[1]*t[3] + q[3]*t[1] + q[0]*t[2] + t[0]*q[2];
  qvq[2] = +q[1]*t[2] - q[2]*t[1] + q[0]*t[3] + t[0]*q[3];
  return qvq;
}

inline float* owl_mult_qvsq(const float *q, const float *v, float s, float *qvq)
{
  // apply rotation(q) to a vector(v*s)
  // qvq = q*(v*q^{-1})
  float t[4];
  // quaternion t=v*s*q^{-1};
  // t.s = -dot(v,-q.v);
  t[0] = +v[0]*s*q[1] + v[1]*s*q[2] + v[2]*s*q[3];
  // t.v = cross(v*s,-q.v) + q.s*v ;
  t[1] = -v[1]*s*q[3] + v[2]*s*q[2] + v[0]*s*q[0];
  t[2] = +v[0]*s*q[3] - v[2]*s*q[1] + v[1]*s*q[0];
  t[3] = -v[0]*s*q[2] + v[1]*s*q[1] + v[2]*s*q[0];

  // qvq = q*t;
  // qvq = cross(q.v,t.v) + q.s*t.v + t.s*(-q.v) ;
  qvq[0] = +q[2]*t[3] - q[3]*t[2] + q[0]*t[1] + t[0]*q[1];
  qvq[1] = -q[1]*t[3] + q[3]*t[1] + q[0]*t[2] + t[0]*q[2];
  qvq[2] = +q[1]*t[2] - q[2]*t[1] + q[0]*t[3] + t[0]*q[3];
  return qvq;
}

inline float* owl_normalize_q(float *q)
{
  float norm = sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);

  if(norm < 1e-12) { q[0]=1; q[1]=q[2]=q[3]=0; return 0; }
  if(q[0]<0) norm = -norm;

  q[0] /= norm; q[1] /= norm; q[2] /= norm; q[3] /= norm;
  return q;
}

inline float* owl_normalize_p(float *p)
{
  return owl_normalize_q(p+3);
}

inline float* owl_mult_pp(const float *p1, const float *p2, float *p3)
{
  owl_mult_qq(p1+3, p2+3, p3+3);
  owl_mult_qvq(p1+3, p2, p3);
  return owl_add_v3v3(p1, p3, p3);
}

inline float* owl_mult_pps(const float *p1, const float *p2, float s, float *p3)
{
  owl_mult_qq(p1+3, p2+3, p3+3);
  owl_mult_qvsq(p1+3, p2, s, p3);
  return owl_add_v3v3(p1, p3, p3);
}

inline float* owl_invert_p(float *p)
{
  //R = R^{-1}, T = -R^{-1}*t=-R*t
  float _p[3];
  p[4] = -p[4];   p[5] = -p[5];   p[6] = -p[6];  // R = R^{-1}
  _p[0] = -p[0];  _p[1] = -p[1];  _p[2] = -p[2]; //_T = -T
  return owl_mult_qvq(&p[3], &_p[0], &p[0]);     // T = R*_T
}

inline float* owl_convert_pm(const float *p, float *m)
{
  float xx, xy, xz, yy, yz, zz, sx, sy, sz;
  float p4_2 = p[4]*2, p5_2 = p[5]*2, p6_2 = p[6]*2;
  xx = p4_2*p[4]; xy = p4_2*p[5]; xz = p4_2*p[6];
  yy = p5_2*p[5]; yz = p5_2*p[6];
  zz = p6_2*p[6];

  sx = p[3]*p4_2; sy = p[3]*p5_2; sz = p[3]*p6_2;

  m[0] =1.0-(yy+zz);  m[4] =    (xy-sz);  m[8] =    (xz+sy);
  m[1] =    (xy+sz);  m[5] =1.0-(xx+zz);  m[9] =    (yz-sx);
  m[2] =    (xz-sy);  m[6] =    (yz+sx);  m[10]=1.0-(xx+yy);
  m[3] =0          ;  m[7] =0          ;  m[11]=0          ;

  m[12] = p[0];
  m[13] = p[1];
  m[14] = p[2];
  m[15] = 1.0;
  return m;
}

inline float* owl_convert_pmi(const float *p, float *m)
{
  float xx, xy, xz, yy, yz, zz, sx, sy, sz;
  float p4_2 = p[4]*2, p5_2 = p[5]*2, p6_2 = p[6]*2;
  xx = p4_2*p[4]; xy = p4_2*p[5]; xz = p4_2*p[6];
  yy = p5_2*p[5]; yz = p5_2*p[6];
  zz = p6_2*p[6];

  sx = p[3]*p4_2; sy = p[3]*p5_2; sz = p[3]*p6_2;

  m[0] =1.0-(yy+zz);  m[1] =    (xy-sz);  m[2] =    (xz+sy);
  m[4] =    (xy+sz);  m[5] =1.0-(xx+zz);  m[6] =    (yz-sx);
  m[8] =    (xz-sy);  m[9] =    (yz+sx);  m[10]=1.0-(xx+yy);
  m[3] =0          ;  m[7] =0          ;  m[11]=0          ;

  m[12] = -(m[0]*p[0] +m[4]*p[1] + m[8]*p[2]);
  m[13] = -(m[1]*p[0] +m[5]*p[1] + m[9]*p[2]);
  m[14] = -(m[2]*p[0] +m[6]*p[1] + m[10]*p[2]);
  m[15] = 1.0;
  return m;
}

inline float* owl_convert_mp(const float *m, float *p)
{
  p[0] = m[12]; p[1] = m[13]; p[2] = m[14];
  float trace = m[0]+m[5]+m[10], s;
  if(trace>=0)
    {
      s = sqrt(trace+1);
      p[3] = 0.5*s; s = 0.5/s;
      p[4] = s*(m[6]-m[9]);
      p[5] = s*(m[8]-m[2]);
      p[6] = s*(m[1]-m[4]);
    }
  else
    {
      int i = 0;
      if(m[5] > m[i]) i=1;
      if(m[10] > m[i]) i=2;
      switch(i)
        {
        case 0:
          s=sqrt(1+m[0]-m[5]-m[10]);
          p[4] = 0.5*s;  s = 0.5/s;
          p[3] = s*(m[6]-m[9]);
          p[6] = s*(m[8]+m[2]);
          p[5] = s*(m[1]+m[4]);
          break;
        case 1:
          s=sqrt(1-m[0]+m[5]-m[10]);
          p[5] = 0.5*s;  s = 0.5/s;
          p[6] = s*(m[6]+m[9]);
          p[3] = s*(m[8]-m[2]);
          p[4] = s*(m[1]+m[4]);
          break;
        case 2:
          s=sqrt(1-m[0]-m[5]+m[10]);
          p[6] = 0.5*s;  s = 0.5/s;
          p[5] = s*(m[6]+m[9]);
          p[4] = s*(m[8]+m[2]);
          p[3] = s*(m[1]-m[4]);
          break;
        }
    }
  return p;
}

#ifdef __cplusplus
}
#endif

#endif // OWL_MATH_H

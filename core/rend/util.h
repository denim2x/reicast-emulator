#pragma once

#include <stdint.h>
#include <valarray>
#include <bitset>

using std::size_t;
typedef int __v4si;
typedef long long __m128i;

typedef std::valarray<float> __v4sf;
typedef __v4sf __m128;
const size_t __int = sizeof(int);

// FIXME: Use 'Float' type in __v4sf?
union Float {
  Float(float value) : _f(value) {}
  Float(int value) : _i(value) {}
  __inline operator float() const {
    return _f;
  }
  __inline operator int32_t() const {
    return _i;
  }
protected:
  float _f;
  int32_t _i;
};

union m128i {
  __m128i mm;
  int8_t m128i_u8[16];
  int8_t m128i_i8[16];
  int16_t m128i_i16[8];
  int32_t m128i_i32[4];
  uint32_t m128i_u32[4];

  m128i(int a, int b, int c, int d) : m128i_i32{a, b, c, d} { }
  m128i(unsigned a, unsigned b, unsigned c, unsigned d) : m128i_u32{a, b, c, d} { }
};

struct RECT {
  int left, top, right, bottom;
};

/**
 * MOVMSKPS - MOVe MaSK Packed Single
 *
 * DEST[0] ← SRC[31];
 * DEST[1] ← SRC[63];
 * DEST[2] ← SRC[95];
 * DEST[3] ← SRC[127];
 *
 * FIXME: Validate and test this implementation
 **/
__inline int _movmskps(__v4sf __A) {
  bitset<__int> dest;
  for (auto i = 0; i < __A.size(); i++) {
    dest[i] = Float(__A[i]) & (1<<31);
  }
  return (int)dest.to_ulong();
}

/* Creates a 4-bit mask from the most significant bits of the SPFP values.  */
__inline int _mm_movemask_ps(__m128 __A) {
  return _movmskps((__v4sf) __A);
}

__inline __m128i _mm_set_epi32(int __q3, int __q2, int __q1, int __q0) {
  return m128i(__q0, __q1, __q2, __q3);
}

__inline __m128i _mm_set1_epi32(int __A) {
  return _mm_set_epi32(__A, __A, __A, __A);
}

extern __inline __m128 _mm_setr_ps(float __Z, float __Y, float __X, float __W) {
  return __m128 {__Z, __Y, __X, __W};
}

static __m128 _mm_load_scaled_float(float v, float s) {
  return _mm_setr_ps(v, v + s, v + s + s, v + s + s + s);
}

static __m128 _mm_broadcast_float(float v) {
  return _mm_setr_ps(v, v, v, v);
}

static __m128i _mm_broadcast_int(int v) {
  __m128i rv = _mm_cvtsi32_si128(v);
  return _mm_shuffle_epi32(rv, 0);
}

__inline __m128 _mm_load_ps(float const *__P) {
  return *(__m128 *) __P;
}

static __m128 _mm_load_ps_r(float a, float b, float c, float d) {
  DECL_ALIGN(128)
  float v[4];
  v[0] = a;
  v[1] = b;
  v[2] = c;
  v[3] = d;

  return _mm_load_ps(v);
}

__forceinline int iround(float x) {
  return _mm_cvtt_ss2si(_mm_load_ss(&x));
}

float mmin(float a, float b, float c, float d) {
  float rv = min(a, b);
  rv = min(c, rv);
  return max(d, rv);
}

float mmax(float a, float b, float c, float d) {
  float rv = max(a, b);
  rv = max(c, rv);
  return min(d, rv);
}

#pragma once

// O31: <cstdlib> DOSYANIN EN TEPESINDE, rawaccel-base.hpp'den ONCE.
// Bu dosyayi ILK include eden bir TU derlenmiyordu:
//   printf '#include "simd_math.hpp"\nint main(){return 0;}\n' | g++ -mavx2 -Iinclude
//   -> 26 hata, hepsi /usr/include/c++/16/cstdlib'ta ("'lldiv_t' has not been
//      declared in '__gnu_cxx'") — yani hata simd_math.hpp'yi degil 200 satir
//      otedeki bir sistem basligini isaret ediyor, sebebi gostermiyor.
// Buraya eklenmesi: 0 hata. Iki kere yanlis yere kondu, ikisi de olculdu:
//   * <immintrin.h>'in YANINA   -> 26 hata (kalmadi)
//   * <cmath> dosyadan ONCE     -> 26 hata (yetmiyor)
//   * <cstdlib> dosyadan ONCE   ->  0 hata
// Yani sira onemli: bu satiri <rawaccel-base.hpp> tasindigi an, o include
// zincirinin bozdugu include durumu icinde giriyor ve <cstdlib> artik duzelmiyor.
// Bugun proje TU'lari buraya rawaccel.hpp uzerinden gectigi icin hicbiri bu
// yolu denemiyor — yani KORUMA KAZARA. tests/simd_parity.cpp:26-28 std
// basliklarini once include ediyor ve tuzagi KACANIYOR, kapatmiyor.
#include <cstdlib>

#include "rawaccel-base.hpp"
#include <cmath>
#include <cfloat>

namespace rawaccel {
namespace simd {

// O31: v2d_blend ve v2d_hypot SILINDI (uc backend tanimi da).
//   v2d_blend: 0 cagran. Uretici maskesiyle (v2d_cmp_ge) uc backend anlasiyor,
//     ama baska bir maske biciminde ucu uc ayri cevap veriyordu: AVX2 yalnizca
//     isaret bitine, SSE2 bit-butune, skaler truthiness'e bakiyor. Cagri olmadigi
//     icin "dogru sozlesme" cikarilamaz; sonraki cagiran keyfi birini secmek
//     zorunda kalirdi. Bir sonraki cagiranda URETICI maskesiyle yeniden yazilmali.
//   v2d_hypot: SIMD tarafi sqrt(x*x+y*y) kullaniyor ve 1e200'de `inf` donuyor
//     (skaler std::hypot 1.41421e+200 veriyor) — gercek tasma kusuru, ama
//     uretimde cagri yeri YOK; yalnizca tests/simd_parity.cpp deniyordu ve o
//     da yalnizca hypot(3,4) ile, yani bu ayrisma karsi KOR. Dogru formuller
//     skalerde duruyor; SIMD taraf ilk cagri ciktiginda olcekli uygulamayla
//     geri gelmeli.


// ============================================================================
// Compile-time feature detection
// ============================================================================

#if defined(__AVX2__) || defined(__AVX512F__)
#   define RAWACCEL_HAVE_AVX2 1
#else
#   define RAWACCEL_HAVE_AVX2 0
#endif

#if defined(__AVX512F__) && defined(__AVX512VL__) && defined(__AVX512DQ__)
#   define RAWACCEL_HAVE_AVX512 1
#else
#   define RAWACCEL_HAVE_AVX512 0
#endif

#if defined(__SSE2__)
#   define RAWACCEL_HAVE_SSE2 1
#else
#   define RAWACCEL_HAVE_SSE2 0
#endif

// ============================================================================
// Vector types (2-wide for X/Y processing)
// ============================================================================

#if RAWACCEL_HAVE_AVX2
#include <immintrin.h>

using v2d = __m256d;  // 4 doubles, we use lanes 0,1 for X,Y

// Load 2 doubles into low lanes
static inline v2d v2d_load(const double* ptr) {
    return _mm256_set_pd(0, 0, ptr[1], ptr[0]);
}
static inline v2d v2d_set1(double x) {
    return _mm256_set1_pd(x);
}
static inline v2d v2d_set(double x, double y) {
    return _mm256_set_pd(0, 0, y, x);
}
// BUGFIX: X lives in lane 0 and Y in lane 1 (see v2d_set/v2d_load); lanes
// 2,3 are zero padding. The previous body stored `hi` (lanes 2,3 = padding)
// for ptr[1], so Y was silently written as 0.0 on every AVX2 build.
static inline void v2d_store(double* ptr, v2d v) {
    _mm_storeu_pd(ptr, _mm256_castpd256_pd128(v));
}

// Arithmetic
static inline v2d v2d_add(v2d a, v2d b) { return _mm256_add_pd(a, b); }
static inline v2d v2d_sub(v2d a, v2d b) { return _mm256_sub_pd(a, b); }
static inline v2d v2d_mul(v2d a, v2d b) { return _mm256_mul_pd(a, b); }
static inline v2d v2d_div(v2d a, v2d b) { return _mm256_div_pd(a, b); }
static inline v2d v2d_min(v2d a, v2d b) { return _mm256_min_pd(a, b); }
static inline v2d v2d_max(v2d a, v2d b) { return _mm256_max_pd(a, b); }
static inline v2d v2d_sqrt(v2d a) { return _mm256_sqrt_pd(a); }
static inline v2d v2d_abs(v2d a) {
    const v2d sign_mask = _mm256_set1_pd(-0.0);
    return _mm256_andnot_pd(sign_mask, a);
}

// Comparison masks
static inline v2d v2d_cmp_lt(v2d a, v2d b) { return _mm256_cmp_pd(a, b, _CMP_LT_OQ); }
static inline v2d v2d_cmp_le(v2d a, v2d b) { return _mm256_cmp_pd(a, b, _CMP_LE_OQ); }
static inline v2d v2d_cmp_gt(v2d a, v2d b) { return _mm256_cmp_pd(a, b, _CMP_GT_OQ); }
static inline v2d v2d_cmp_ge(v2d a, v2d b) { return _mm256_cmp_pd(a, b, _CMP_GE_OQ); }

// Horizontal operations
// BUGFIX: reduce over lanes 0,1 only. The previous body folded in lanes 2,3
// (zero padding), so v2d_hmin always returned 0.0.
static inline double v2d_hmin(v2d a) {
    __m128d lo = _mm256_castpd256_pd128(a);
    return _mm_cvtsd_f64(_mm_min_sd(lo, _mm_unpackhi_pd(lo, lo)));
}
static inline double v2d_hmax(v2d a) {
    __m128d lo = _mm256_castpd256_pd128(a);
    return _mm_cvtsd_f64(_mm_max_sd(lo, _mm_unpackhi_pd(lo, lo)));
}

// Fast math - scalar fallback for transcendental functions
// (AVX2 has no native exp/pow, AVX512 has exp2 but not exp/pow)
static inline v2d v2d_fast_exp(v2d x) {
    alignas(32) double arr[4];
    v2d_store(arr, x);
    arr[0] = std::exp(arr[0]);
    arr[1] = std::exp(arr[1]);
    return v2d_load(arr);
}

static inline v2d v2d_fast_exp2(v2d x) {
    alignas(32) double arr[4];
    v2d_store(arr, x);
    arr[0] = std::exp2(arr[0]);
    arr[1] = std::exp2(arr[1]);
    return v2d_load(arr);
}

static inline v2d v2d_fast_pow(v2d base, v2d exp) {
    alignas(32) double b[4], e[4];
    v2d_store(b, base);
    v2d_store(e, exp);
    b[0] = std::pow(b[0], e[0]);
    b[1] = std::pow(b[1], e[1]);
    return v2d_load(b);
}

// Check if all lanes are finite
static inline bool v2d_all_finite(v2d a) {
    alignas(32) double arr[4];
    v2d_store(arr, a);
    return std::isfinite(arr[0]) && std::isfinite(arr[1]);
}

// Scalar extraction
static inline double v2d_get_x(v2d a) {
    return _mm_cvtsd_f64(_mm256_castpd256_pd128(a));
}
static inline double v2d_get_y(v2d a) {
    __m128d lo = _mm256_castpd256_pd128(a);
    return _mm_cvtsd_f64(_mm_unpackhi_pd(lo, lo));
}

// Rotation helper - returns {cos, sin} as v2d
static inline v2d v2d_direction(double degrees) {
    double rad = degrees * M_PI / 180.0;
    double c = std::cos(rad);
    double s = std::sin(rad);
    return v2d_set(c, s);
}

static inline void v2d_rotate(v2d& v, v2d dir) {
    // (x*cos - y*sin, x*sin + y*cos)
    double vx = v2d_get_x(v);
    double vy = v2d_get_y(v);
    double cx = v2d_get_x(dir);
    double sy = v2d_get_y(dir);
    v = v2d_set(vx * cx - vy * sy, vx * sy + vy * cx);
}

#elif RAWACCEL_HAVE_SSE2
#include <emmintrin.h>

using v2d = __m128d;  // 2 doubles exactly

static inline v2d v2d_load(const double* ptr) {
    return _mm_loadu_pd(ptr);
}
static inline v2d v2d_set1(double x) {
    return _mm_set1_pd(x);
}
static inline v2d v2d_set(double x, double y) {
    return _mm_set_pd(y, x); // Note: _mm_set_pd is hi, lo
}
static inline void v2d_store(double* ptr, v2d v) {
    _mm_storeu_pd(ptr, v);
}

static inline v2d v2d_add(v2d a, v2d b) { return _mm_add_pd(a, b); }
static inline v2d v2d_sub(v2d a, v2d b) { return _mm_sub_pd(a, b); }
static inline v2d v2d_mul(v2d a, v2d b) { return _mm_mul_pd(a, b); }
static inline v2d v2d_div(v2d a, v2d b) { return _mm_div_pd(a, b); }
static inline v2d v2d_min(v2d a, v2d b) { return _mm_min_pd(a, b); }
static inline v2d v2d_max(v2d a, v2d b) { return _mm_max_pd(a, b); }
static inline v2d v2d_sqrt(v2d a) { return _mm_sqrt_pd(a); }
static inline v2d v2d_abs(v2d a) {
    const v2d sign_mask = _mm_set1_pd(-0.0);
    return _mm_andnot_pd(sign_mask, a);
}

static inline v2d v2d_cmp_lt(v2d a, v2d b) { return _mm_cmplt_pd(a, b); }
static inline v2d v2d_cmp_le(v2d a, v2d b) { return _mm_cmple_pd(a, b); }
static inline v2d v2d_cmp_gt(v2d a, v2d b) { return _mm_cmpgt_pd(a, b); }
static inline v2d v2d_cmp_ge(v2d a, v2d b) { return _mm_cmpge_pd(a, b); }

static inline double v2d_hmin(v2d a) {
    double arr[2];
    v2d_store(arr, a);
    return arr[0] < arr[1] ? arr[0] : arr[1];
}
static inline double v2d_hmax(v2d a) {
    double arr[2];
    v2d_store(arr, a);
    return arr[0] > arr[1] ? arr[0] : arr[1];
}

static inline v2d v2d_fast_exp(v2d x) {
    double arr[2];
    v2d_store(arr, x);
    arr[0] = std::exp(arr[0]);
    arr[1] = std::exp(arr[1]);
    return v2d_load(arr);
}

static inline v2d v2d_fast_exp2(v2d x) {
    double arr[2];
    v2d_store(arr, x);
    arr[0] = std::exp2(arr[0]);
    arr[1] = std::exp2(arr[1]);
    return v2d_load(arr);
}

static inline v2d v2d_fast_pow(v2d base, v2d exp) {
    double b[2], e[2];
    v2d_store(b, base);
    v2d_store(e, exp);
    b[0] = std::pow(b[0], e[0]);
    b[1] = std::pow(b[1], e[1]);
    return v2d_load(b);
}

static inline bool v2d_all_finite(v2d a) {
    double arr[2];
    v2d_store(arr, a);
    return std::isfinite(arr[0]) && std::isfinite(arr[1]);
}

static inline double v2d_get_x(v2d a) {
    double arr[2];
    v2d_store(arr, a);
    return arr[0];
}
static inline double v2d_get_y(v2d a) {
    double arr[2];
    v2d_store(arr, a);
    return arr[1];
}

static inline v2d v2d_direction(double degrees) {
    double rad = degrees * M_PI / 180.0;
    double c = std::cos(rad);
    double s = std::sin(rad);
    return v2d_set(c, s);
}

static inline void v2d_rotate(v2d& v, v2d dir) {
    double vx = v2d_get_x(v);
    double vy = v2d_get_y(v);
    double cx = v2d_get_x(dir);
    double sy = v2d_get_y(dir);
    v = v2d_set(vx * cx - vy * sy, vx * sy + vy * cx);
}

#else
// ============================================================================
// Pure scalar fallback (no SIMD)
// ============================================================================

struct v2d { double x, y; };

static inline v2d v2d_load(const double* ptr) { return {ptr[0], ptr[1]}; }
static inline v2d v2d_set1(double x) { return {x, x}; }
static inline v2d v2d_set(double x, double y) { return {x, y}; }
static inline void v2d_store(double* ptr, v2d v) { ptr[0] = v.x; ptr[1] = v.y; }

static inline v2d v2d_add(v2d a, v2d b) { return {a.x + b.x, a.y + b.y}; }
static inline v2d v2d_sub(v2d a, v2d b) { return {a.x - b.x, a.y - b.y}; }
static inline v2d v2d_mul(v2d a, v2d b) { return {a.x * b.x, a.y * b.y}; }
static inline v2d v2d_div(v2d a, v2d b) { return {a.x / b.x, a.y / b.y}; }
static inline v2d v2d_min(v2d a, v2d b) { return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y}; }
static inline v2d v2d_max(v2d a, v2d b) { return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y}; }
static inline v2d v2d_sqrt(v2d a) { return {std::sqrt(a.x), std::sqrt(a.y)}; }
// O31: std::fabs, `x < 0 ? -x : x` DEGIL.  IEEE'de -0.0 == 0.0 oldugu icin
// karsilastirma -0.0'da false donuyor ve -0.0 OLDUGU GIBI donuyordu.  Iki SIMD
// backend'i isaret bitini kosulsuz temizliyor:
//   :97-100  AVX2   _mm256_andnot_pd(sign_mask, a)   ->  -0.0 => +0.0
//   :205-208 SSE2   _mm_andnot_pd(sign_mask, a)      ->  -0.0 => +0.0
// Yani ayni sozlesmenin iki farkli uygulamasi ayrim veriyordu.  Olcum
// (uc backend, isaret biti ayirt edilerek): avx2 +0, sse2 +0, skaler -0.
// Canli bir hata DEGIL: modify() sonundaki isfinite guard'i ve
// calc_speed_separate icindeki fabs yutuyor, uc backend'in modify() ciktisi
// birebir ayni.  Ama kirilgan — `x<0?-x:x` kalipti her kozmetik degisiklikte
// geri gelebilir ve hicbir sey onu yakalamaz (v2d_hypot'un silinme nedeni:
// ayrim vardi, kapi koruyordu, capraz-dif kirilmadan kimse fark etmedi).
// std::fabs her zaman isaret bitini temizler.
static inline v2d v2d_abs(v2d a) { return {std::fabs(a.x), std::fabs(a.y)}; }

static inline v2d v2d_cmp_lt(v2d a, v2d b) { return {a.x < b.x ? -1.0 : 0.0, a.y < b.y ? -1.0 : 0.0}; }
static inline v2d v2d_cmp_le(v2d a, v2d b) { return {a.x <= b.x ? -1.0 : 0.0, a.y <= b.y ? -1.0 : 0.0}; }
static inline v2d v2d_cmp_gt(v2d a, v2d b) { return {a.x > b.x ? -1.0 : 0.0, a.y > b.y ? -1.0 : 0.0}; }
static inline v2d v2d_cmp_ge(v2d a, v2d b) { return {a.x >= b.x ? -1.0 : 0.0, a.y >= b.y ? -1.0 : 0.0}; }

static inline double v2d_hmin(v2d a) { return a.x < a.y ? a.x : a.y; }
static inline double v2d_hmax(v2d a) { return a.x > a.y ? a.x : a.y; }

static inline v2d v2d_fast_exp(v2d x) { return {std::exp(x.x), std::exp(x.y)}; }
static inline v2d v2d_fast_exp2(v2d x) { return {std::exp2(x.x), std::exp2(x.y)}; }
static inline v2d v2d_fast_pow(v2d base, v2d exp) { return {std::pow(base.x, exp.x), std::pow(base.y, exp.y)}; }
static inline bool v2d_all_finite(v2d a) { return std::isfinite(a.x) && std::isfinite(a.y); }
static inline double v2d_get_x(v2d a) { return a.x; }
static inline double v2d_get_y(v2d a) { return a.y; }

static inline v2d v2d_direction(double degrees) {
    double rad = degrees * M_PI / 180.0;
    return {std::cos(rad), std::sin(rad)};
}

static inline void v2d_rotate(v2d& v, v2d dir) {
    double vx = v.x, vy = v.y, cx = dir.x, sy = dir.y;
    v = {vx * cx - vy * sy, vx * sy + vy * cx};
}

#endif // SIMD backend

// ============================================================================
// Fast approximate math functions (scalar)
// ============================================================================

static inline double fast_exp2(double x) { return std::exp2(x); }
static inline double fast_exp(double x) { return std::exp(x); }
static inline double fast_pow(double base, double exp) { return std::pow(base, exp); }

// Branchless min/max/clamp - use simd_ prefix to avoid conflicts
static inline double simd_min(double a, double b) { return a < b ? a : b; }
static inline double simd_max(double a, double b) { return a > b ? a : b; }
static inline double simd_clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Magnitude using hypot (overflow safe)
static inline double magnitude(double x, double y) { return std::hypot(x, y); }

} // namespace simd
} // namespace rawaccel
// AJ2 · --dallar ARAMA surucusu: SIFIR sayan dallar icin erisilebilir girdi
// arar. (olcum araci — uretim kodu DEGIL)
//
// KURAL: bir dalin sayaci 0 ise bu "bu girdi kumesi oraya girmedi" demektir.
// SILME ONERISI DEGILDIR. Arama bulursa dal ERISILEBILIRDIR. Bulamazsa
// hukum "erisilebilir girdi bulunamadi" olur ve SILME ONERILMEZ — cunku
// bulamamak erisilemezligi kanitlamaz.
//
// Arama iki katmanlidir:
//   K1  ISARET ve SONLU OLMAYAN kapsama: bilesenlerin isareti, ±Inf, NaN ve
//       denormal. L20/L62 gibi girdi tarafi korumalari sadece burada turer.
//   K2  LOG-UZAYI ince tarama: L131 (-600) ve L132 (+600) esikleri yalnizca
//       log_inner = p*log(m/M) degerine bagli; m/M uzerinde logaritmik izgara
//       ile esiklerin YANINA kadar yaklasilir, sonra orada sinirda denenir.

#include "math-vec2.hpp"

#include <cstdio>
#include <cmath>
#include <cfloat>
#include <limits>

using namespace rawaccel;

static const double INF = std::numeric_limits<double>::infinity();
static const double NAN_ = std::numeric_limits<double>::quiet_NaN();

static void call(double x, double y, double p) {
    const double r = lp_distance({x, y}, p);
    (void)r;
}

int main() {
    long long n = 0;

    // --- K1: isaret + sonlu olmayan tam kaplama ---
    const double v[] = {
        0.0, -0.0, 1.0, -1.0, 3.0, -3.0, 4.0, -4.0,
        1e-300, -1e-300, 1e300, -1e300,
        DBL_MIN, -DBL_MIN,             // 2.2e-308  denormal sinirinin altinda
        1e-320, -1e-320,               // denormal
        5e-324, -5e-324,               // en kucuk pozitif denormal
        DBL_MAX, -DBL_MAX, 1e154, -1e154, 1e155, -1e155,
        INF, -INF, NAN_
    };
    const double p1[] = {-1e-320, -1e-9, -1.0, -0.5, -0.0,
                         0.5, 1.0, 2.0, 1e9, 1e-320, 1e-9, 1e-300};
    const int nv = (int)(sizeof(v) / sizeof(v[0]));
    const int np = (int)(sizeof(p1) / sizeof(p1[0]));
    for (int i = 0; i < nv; ++i)
        for (int j = 0; j < nv; ++j)
            for (int k = 0; k < np; ++k) { call(v[i], v[j], p1[k]); ++n; }
    std::printf("arama K1: %lld cagri (isaret/Inf/NaN/denormal kapsama)\n", n);

    // --- K2: log uzayi ince tarama ---
    // log_inner = p * log(m/M). L131 ister: log_inner < -600
    //             L132 ister: log_inner > +600
    // m/M oranini logaritmik izgarada gez ve p'yi sabit tut.
    const double pp[] = {-1.0, -0.5, 0.5, 1.0, 2.0, 1e-9, 1e-300};
    const double Ms[] = {1.0, 1e-300, 1e300, 4.0};
    long long m2 = 0;
    for (double p : pp) {
        for (int e = -320; e <= 308; ++e) {
            const double r = std::pow(10.0, (double)e);   // m/M
            for (double M : Ms) {
                call(M, M * r, p);      // m = M*r
                ++m2;
            }
        }
    }
    std::printf("arama K2: %lld cagri (log_inner esigi taramasi)\n", m2);

    // --- K3: esigin TAM YANI (sonlu, sonlu olmayan) ---
    // L84 sonlu degilse log blogu girer. Bu bloga girip log_inner'in
    // -600 / +600'in otesine dusmesi icin iki esigi de asan p gerekebilir:
    // hem 1/p*log(1+(m/M)^p) > 709 (sonluluk kirilmali) hem p*log(m/M) < -600.
    long long m3 = 0;
    for (int e = -323; e <= 308; ++e) {
        const double ratio = std::pow(10.0, (double)e);
        for (int q = -320; q <= 0; ++q) {
            const double p = std::pow(10.0, (double)q);
            if (p == 0.0) continue;
            call(1.0, ratio, p); ++m3;
            call(1e300, 1e300 * ratio, p); ++m3;
            call(4.0, 4.0 * ratio, p); ++m3;
        }
    }
    std::printf("arama K3: %lld cagri (esik yaninda p x m/M caprazi)\n", m3);

    std::printf("arama toplam: %lld cagri\n", n + m2 + m3);
    return 0;
}

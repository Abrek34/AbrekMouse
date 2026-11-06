// AJ2 · --dallar TEMEL surucusu: math-vec2.hpp dallarini sayan girdi kumesi.
// (olcum araci — uretim kodu DEGIL)
//
// Amac: verilen girdi kumesi hangi dallara GIRIYOR. Sayac sifir olan dal,
// "bu kumesiyle girilmedi" demektir — erisilemez demek DEGILDIR. Kesin hukum
// ancak --arama surucusu erisilebilir girdi bulursa ya da matematiksel
// imkansizlik gosterilirse verilebilir.
//
// OLCUM ARACI KURALI: sayac elle degil, derleyicinin -fprofile-arcs sayacidir
// (prove_kod_ayni.py --dallar). Boylece sayac bu dosyadan bagimsiz olur.

#include "math-vec2.hpp"

#include <cstdio>
#include <cmath>

using namespace rawaccel;

int main() {
    // IZGARA: sonlu, makul uretim degerleri + sinir tarafi.
    const double xs[] = {0.0, 1.0, 3.0, 4.0, 1e5, 1e-300, 1e300};
    const double ps[] = {2.0, 1.0, 3.0, 0.5, -1.0, 1e-9, 1e-320};

    long long n = 0;
    for (double x : xs)
        for (double y : xs)
            for (double p : ps) {
                const double r = lp_distance({x, y}, p);
                (void)r;
                ++n;
            }
    std::printf("temel: %lld lp_distance cagrisi\n", n);
    return 0;
}

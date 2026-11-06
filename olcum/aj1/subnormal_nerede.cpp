// AJ1 PERF6-B: subnormalin NEREDE üretildiğini mikroskobik sayar.
// Soru: FTZ neden 150→15 ns? Cevap ancak hangi ifadenin subnormal
// ürettiğini bilmekle anlaşılır. 3 aday yol, her biri tek tek sayılır.
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <xmmintrin.h>

static const double DBL_MIN_ = 2.2250738585072014e-308;
static inline bool sub(double v){ return v!=0.0 && std::fabs(v) < DBL_MIN_; }

// ---- aday 1: katsayı 1.0 - exp2(log2(0.75)*t)  (trend bloğu, rawaccel.hpp:115-118)
__attribute__((noinline)) double yol1_katsayi(double t){
    const double l2 = std::log2(0.75);
    return 1.0 - std::exp2(l2 * t);
}
// ---- aday 2: trend birikimi  x *= 0.75  (rawaccel.hpp:122-123,129-130)
__attribute__((noinline)) double yol2_birikim(int n){
    double v = 1.0;
    for (int i=0;i<n;i++) v *= 0.75;
    return v;
}
// ---- aday 3: windowTotal += trend*time  (rawaccel.hpp:132-133)
__attribute__((noinline)) double yol3_topla(double trend, double t){
    double w = 0.0;
    for (int i=0;i<64;i++) w += trend * t;   // tekdüze kazanç = duz çizgi
    return w;
}

int main(int argc, char** argv){
    unsigned csr = _mm_getcsr();
    if (argc>1 && argv[1][0]=='f'){ _mm_setcsr(csr|0x8040u); printf("[FTZ+DAZ ACIK]\n"); }
    else                        { _mm_setcsr(csr&~0x8040u); printf("[NORMAL]\n"); }

    // t = 0.0008s @1000Hz — gerçek bir fare karesi
    const double t = 0.0008;
    long n1=0, n2=0, n3=0, toplam=0;
    for (int i=0;i<200000;i++){
        double a = yol1_katsayi(t);
        double b = yol2_birikim(i % 4000);
        double c = yol3_topla(b, t);
        n1 += sub(a); n2 += sub(b); n3 += sub(c);
        if (sub(a)||sub(b)||sub(c)) toplam++;
    }
    printf("  yol1 katsayi 1-exp2(l2*t) : %ld subnormal\n", n1);
    printf("  yol2 birlikim  x*=0.75    : %ld subnormal\n", n2);
    printf("  yol3 toplam   +=trend*t   : %ld subnormal\n", n3);
    printf("  ---\n");
    printf("  en az biri subnormal olan çağrı: %ld / 200000\n", toplam);

    // birikimin ne kadar süre sonra subnormal girdiğini bul
    double v=1.0; int ilk=-1;
    for (int i=0;i<60000;i++){ v*=0.75; if (ilk<0 && sub(v)){ ilk=i; break; } }
    printf("  birlikim ilk subnormal N   : %d\n", ilk);
    return 0;
}

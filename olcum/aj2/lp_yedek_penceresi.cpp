// AJ2 · math-vec2 L∞ yedeğinin PENCERESİ — iki sayı AYRI ölçülür. (olcum araci)
//
// AJ1 §63 raporu yorumdaki iki cümleyi denetledi:
//   1) yedeğin çalıştığı aralık "p < 1" olarak yazılmış,
//   2) yedek "birkaç x M" kadar bir yaklaşıklık payı veriyor.
// Olcum iyi niyetli ama TEK bir aracla iki FARKLI sayıyı birbirine karistirdi:
//   "hata > 1e-12 olan nokta"  !=  "yedek gercekten dustu olan nokta".
// Bu arac ikisini AYRI kolonlarda raporlar.
//
// GERCEK DEGER: long double (80-bit). double tasiyorsa o nokta ayri sayilir
// (referans double'inda inf -> sapma tanimsiz).
//
// Kapsam: p icin 0 < p < 16 (config.cpp:571 yalniz p<=0 -> 2; rawaccel.hpp:197
// yalniz p>=16 veya p<=0 -> max-norm, yani UST SINIR YOK), |v| icin tavan
// 6.87e22 (math-vec2.hpp tavan zinciri).
//
// PC'ler ZORUNLUDUR:
//   PC-1 duyarlilik: BOZUK bir port (her zaman M donduren) hata sayacini
//         tavan yapmali. Degilse alet duyarsiz ve "hata yok" sonucu gecersiz.
//   PC-2 dogru port: p=2'de port |v|_2 ile bite bir ayni olmali.
//   PC-3 izgara butunlugu: her noktada 0 <= m <= M olmali. (Bir onceki
//         denemede M=0.05, m=100 gibi BIR NOKTA cikti — izgara sarti ihlal
//         edilmis ve "en kotu oran" olarak yazildi.)
//
// Protokol: "RESULT <vaka> PASS|FAIL <ayrinti>", exit 0/1.

#include "math-vec2.hpp"

#include <cmath>
#include <cfloat>
#include <climits>
#include <cstdio>
#include <algorithm>
#include <vector>

using namespace rawaccel;

// --- referans: ayni dosyanin TUVA alttigi govde, metin olarak birebir ---
// (tests/oracle/ref/math-vec2.hpp:34-37) -- port ile karsilastirmak icin.
static double referans_lp(double ax, double ay, double p) {
    return std::pow(std::pow(std::fabs(ax), p) + std::pow(std::fabs(ay), p),
                    1.0 / p);
}

// --- gercek deger, long double (log-uzayi: kararli) ---
// ||v||_p = hi * (1 + (lo/hi)^p)^(1/p)
//        => log = log(hi) + (1/p)*log1p( (lo/hi)^p )
//
// IKI HATA YASANDI VE IKISI DE PC'LERCE YAKALANDI (duzeltilmis hali):
//   1) p*log(hi) yazmak: p iki kez sayilir. (1,1,p=2) -> 2 (olmali sqrt 2).
//   2) lo==0 dalinda expl(p*log(hi)) = hi^p donmek: dogru cevap hi.
//      (0^0 disinda 0^p = 0, (hi^p + 0)^(1/p) = hi.)
static long double gercek_lp(long double a, long double b, long double p) {
    if (a == 0 && b == 0) return 0;
    const long double hi = std::max(a, b), lo = std::min(a, b);
    if (lo == 0) return hi;
    return expl(logl(hi)
                + (1.0L / p) * log1pl(expl(p * (logl(lo) - logl(hi)))));
}

struct Sayac {
    long long n = 0;             // olculebilir nokta
    long long tasma = 0;         // long double tasti (ayri kapsam)
    long long hata = 0;          // goreli hata > 1e-12
    long long yedek = 0;         // port == M ve m > 0 (yedek GERCEKTEN dustu)
    // PC icin: yalnizca "M'nin yanlis olabileceği" noktalar. m/M kucukken
    // M dogru cevaptir, dolayisiyla BOZUK port (hep M) oralarda HATALI
    // sayilmamalidir. Ilk PC denemesi bu ayrimi yapmadigi icin gecersiz
    // olarak FAIL verdi — kosulun kendisi hataliydi, olcum degil.
    long long elligi = 0;        // gercek Lp, M'den en az 1e-6 kat buyuk
    long long hata_ellikli = 0;  // ve port bunu 1e-12'den kotu bozdu
    long double en_kotu = 0;     // en buyuk goreli hata
    double en_kotu_p = 0, en_kotu_M = 0, en_kotu_m = 0;
};

static Sayac olc(double p, const std::vector<double>& vs,
                 const std::vector<double>& rs, double (*port)(double, double,
                                                               double)) {
    Sayac s;
    for (double M : vs) {
        for (double r : rs) {
            const double m = M * r;
            const long double g = gercek_lp((long double)M, (long double)m,
                                            (long double)p);
            if (g > (long double)LDBL_MAX) { ++s.tasma; continue; }
            if (g <= 0) continue;
            ++s.n;
            const double q = port(M, m, p);
            const bool kotu_mu = !std::isfinite(q) || q <= 0;
            if (!kotu_mu) {
                const long double h = fabsl((long double)q - g) / g;
                if (h > 1e-12L) {
                    ++s.hata;
                    if (h > s.en_kotu) {
                        s.en_kotu = h; s.en_kotu_p = p; s.en_kotu_M = M;
                        s.en_kotu_m = m;
                    }
                }
            } else {
                ++s.hata;
            }
            // yedek gercekten dustu mu?  m>0 ve port M'ye tam denk.
            if (m > 0 && q == M && g > (long double)M) ++s.yedek;
            // M'nin yanlis olabilecegi noktalar
            if (g > (long double)M * 1.000001L) {
                ++s.elligi;
                if (kotu_mu
                    || fabsl((long double)q - g) / g > 1e-12L) ++s.hata_ellikli;
            }
        }
    }
    return s;
}

static double port_gercek(double ax, double ay, double p) {
    return lp_distance(vec2d{ax, ay}, p);
}
static double port_bozuk(double ax, double ay, double p) {
    (void)p; return ax > ay ? ax : ay;          // HER ZAMAN M
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::vector<double> vs{1e-6, 1e-3, 0.5, 1.0, 2.0, 8.0, 100.0, 1e3,
                           1e4, 1e6, 1e10, 6.87e10, 1e15, 1e18, 6.87e22};
    std::vector<double> rs{0.0, 1e-9, 1e-6, 1e-3, 0.01, 0.1, 0.3, 0.5, 0.9,
                           0.999, 1.0};
    // pratik fare araligi: |v| <= 1e4
    std::vector<double> vs_pratik{1e-6, 1e-3, 0.5, 1.0, 2.0, 8.0, 100.0,
                                  1e3, 1e4};
    std::vector<double> ps{1e-9, 1e-6, 1e-4, 1e-3, 0.01, 0.05, 0.1, 0.2, 0.3,
                           0.5, 0.7, 0.9, 0.99, 1.0, 1.5, 2.0, 3.0, 4.0, 8.0,
                           12.0, 15.0, 15.99};
    int basarisiz = 0;

    // --- PC-3: izgara butunlugu ---
    {
        bool ok = true;
        for (double M : vs) for (double r : rs)
            if (r < 0 || r > 1 || (M * r) > M) ok = false;
        printf("RESULT PC-3_izgara_butunlugu %s (her noktada 0<=m<=M; "
               "M=0.05,m=100 turleri YOK)\n", ok ? "PASS" : "FAIL");
        if (!ok) basarisiz++;
    }

    // --- PC-2: (a) GERCEK DEGER kendisi dogru mu  (b) port p=2'de hypot ile uyuyor mu
    // (a) burasi kritik: "gercek" referansinin hatasi tum olcumun gürültüsünü
    // belirler ve hicbir dis dogrulama olmazsa sessizce her seyi bozuk sayar.
    {
        bool ok = true; double en_kotu = 0;
        for (double M : vs) for (double r : rs) {
            const double m = M * r;
            const long double g = gercek_lp((long double)M, (long double)m, 2.0L);
            const double h = std::hypot(M, m);
            if (h <= 0) continue;
            en_kotu = std::max(en_kotu, (double)(fabsl(g - (long double)h)
                                                / (long double)h));
        }
        ok = (en_kotu < 1e-14);
        printf("RESULT PC-2a_gercek_deger_p2 %s (long double referans, en kotu "
               "goreli hata %.3g < 1e-14)\n", ok ? "PASS" : "FAIL", en_kotu);
        if (!ok) basarisiz++;
    }
    {
        bool ok = true; double en_kotu = 0;
        for (double M : vs) for (double r : rs) {
            const double m = M * r;
            const double q = lp_distance(vec2d{M, m}, 2.0);
            const double g = std::hypot(M, m);
            if (g <= 0) continue;
            en_kotu = std::max(en_kotu, std::fabs(q - g) / g);
        }
        ok = (en_kotu < 1e-14);
        printf("RESULT PC-2b_port_p2_hypot %s (port, en kotu goreli hata %.3g "
               "< 1e-14)\n", ok ? "PASS" : "FAIL", en_kotu);
        if (!ok) basarisiz++;
    }

    // --- PC-1: duyarlilik. BOZUK port (hep M), M'nin yanlis olabilecegi
    // HER noktada hatali olmali; dogru port HICBIR noktada hatali olmali.
    // p secimi onemli: duyarlilik p'nin yedegin TETIKLENMEDIGI bir
    // degerde olculmeli. p=1e-4'te gercek port da yedege dustugu icin
    // iki port da hatali sayilir ve ayrim kaybolur — ilk iki deneme
    // burada gecersiz FAIL verdi. p=0.5: yedek yok, port dogru.
    {
        const double p = 0.5;
        const Sayac iyi = olc(p, vs, rs, port_gercek);
        const Sayac kotu = olc(p, vs, rs, port_bozuk);
        const bool ok = (kotu.elligi > 0)
                        && (kotu.hata_ellikli == kotu.elligi)
                        && (iyi.hata_ellikli == 0);
        printf("RESULT PC-1_duyarlilik %s (p=0.5, M'nin yanlis olabilecegi "
               "%lld nokta: BOZUK port %lld hatali, dogru port %lld hatali)\n",
               ok ? "PASS" : "FAIL", kotu.elligi, kotu.hata_ellikli,
               iyi.hata_ellikli);
        if (!ok) basarisiz++;
    }

    // --- ANA OLCUM: iki sayi ayri kolonlarda ---
    printf("\n  %-8s | %-28s | %-28s\n", "p", "TUM KAPSAM |v|<=6.87e22",
           "PRATIK |v|<=1e4");
    printf("  %-8s | %6s %6s %6s | %6s %6s\n", "p",
           "hata", "YEDEK", "n", "hata", "YEDEK");
    long long toplam_hata_pratik = 0, toplam_yedek_pratik = 0;
    for (double p : ps) {
        const Sayac t = olc(p, vs, rs, port_gercek);
        const Sayac pr = olc(p, vs_pratik, rs, port_gercek);
        toplam_hata_pratik += pr.hata;
        toplam_yedek_pratik += pr.yedek;
        printf("  %-8.4g | %6lld %6lld %6lld | %6lld %6lld\n",
               p, t.hata, t.yedek, t.n, pr.hata, pr.yedek);
    }
    printf("\n  PRATIK ARALIK TOPLAMI: hata>1e-12 = %lld, gercek yedek = %lld\n",
           toplam_hata_pratik, toplam_yedek_pratik);

    // yedek gercekten dustugu en kucuk p
    {
        double en_kucuk_p = 0; long long adet = 0;
        for (double p : ps) {
            const Sayac t = olc(p, vs, rs, port_gercek);
            if (t.yedek > 0) { en_kucuk_p = p; adet = t.yedek; break; }
        }
        printf("  yedegin ilk DEGDI%si p = %.4g (%lld nokta)\n",
               en_kucuk_p ? "gi" : "medigi", en_kucuk_p, adet);
    }

    // --- REFERANS karsilastirmasi: ayni noktada port ve referans ---
    printf("\n  --- port vs REFERANS (tests/oracle/ref) ---\n");
    for (double p : {0.5, 1.0, 2.0, 1e-3, 9e-4, 1e-6, 1e-7, 1e-9}) {
        double ep = 0, er = 0;
        for (double M : vs) for (double r : rs) {
            const double m = M * r;
            const long double g = gercek_lp((long double)M, (long double)m,
                                            (long double)p);
            if (g <= 0 || g > (long double)LDBL_MAX) continue;
            const double q = port_gercek(M, m, p);
            const double f = referans_lp(M, m, p);
            if (std::isfinite(q) && q > 0)
                ep = std::max(ep, (double)(fabsl((long double)q - g) / g));
            if (std::isfinite(f) && f > 0)
                er = std::max(er, (double)(fabsl((long double)f - g) / g));
        }
        printf("  p=%-8.4g port hata=%.3g   referans hata=%.3g   %s\n",
               p, ep, er, ep <= er ? "port daha dogru" : "referans daha dogru");
    }

    printf("\n=== Sonuç: %s ===\n", basarisiz ? "FAIL" : "TAMAM");
    return basarisiz ? 1 : 0;
}

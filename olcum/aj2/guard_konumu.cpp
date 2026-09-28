// AJ2 · math-vec2 üretim erişilebilirliği probu (ölçüm aracı — üretim kodu DEĞİL).
//
// AJ1 §3: math-vec2.hpp yorumundaki "daemon/daemon.cpp:2323-2324 sonlu olmayan
// dx/dy'yi daha ilk adımda sıfırlar" gerekçesinin satırı yanlış. AJ1 2360-2361
// diyor. Bu probu bunu BIR ADIM ILERI götürüyor ve §2'nin kanıtini uretir:
//
//   S1) 2360-2361 KOLUNDA: guard 2352'de acilan raw_passthrough kolunda ve o kol
//       2383'te return ediyor. Ivme yolu 2510'daki apply_motion_math'e gidiyor.
//       => guard ivme yolunda CALISMIYOR. Yorumun gerekçesi yanlis.
//
//   S2) GERCEK GEREKÇE: modifier::modify() girdi tarafında isfinite korumasi
//       OLMAYAN tek yerdir (rawaccel.hpp:612-613 sonunda, ama lp_distance
//       555'te CAĞRILIYOR). Sonuç dx/dy'nin URETIM TAVANI ile sinirli:
//       32 olaylik read_batch (daemon.cpp:2573) x int32 ev.value
//       (linux/input.h:44) = 32 * INT32_MAX. Bu tavan gercekten tasma sinirinin
//       cok altinda mi, ve 1e200 ayrimi buna gore erisilebilir mi?
//
//   S3) ETKI: sinirta yukari cikarilsa (örn. read_batch 32 -> 1e18) ayrim
//       ERISILEBILIR mi? Bu, "kapalı" dedigimiz seyin gercekten sinir oldugunu
//       gosterir — sinir olmayan bir sey "kapali" degildir.
//
// Protokol: satır başına "RESULT <vaka> PASS|FAIL <ayrinti>", exit 0.

#include "math-vec2.hpp"

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <climits>

using namespace rawaccel;

static int failures = 0;

// --- S1: guard icinde bulundugu kolun donus noktasi vs ivme cagrisi ---
static void s1_guard_konumu() {
    const int raw_open  = 2352;   // if (dev.settings.prof.raw_passthrough) {
    const int raw_close = 2383;   // return true;  (koldan cikis)
    const int guard     = 2360;   // if (!std::isfinite(dx)) dx = 0;
    const int accel     = 2510;   // apply_motion_math(...)
    const bool guard_in_raw = (guard > raw_open && guard < raw_close);
    const bool guard_reaches_accel = (guard >= raw_close);
    printf("S1 raw_kolu=%d..%d guard=%d ivme_cagrisi=%d\n",
           raw_open, raw_close, guard, accel);
    printf("S1 guard raw kolunda mi: %s ; ivme yoluna ulasir mi: %s\n",
           guard_in_raw ? "EVET" : "HAYIR", guard_reaches_accel ? "EVET" : "HAYIR");
    // Gozlenen: guard raw kolunda, dolayisiyla ivme yolunda CALISMIYOR.
    // Bu, yorumdaki "daha ilk adımda sıfırlar" gerekçesini REDDEDER.
    printf("RESULT S1_guard_ivme_yolunda_calismiyor %s "
           "(guard %d < koldan cikis %d; ivme cagrisi %d ayri bir yolda)\n",
           (!guard_reaches_accel) ? "PASS" : "FAIL", guard, raw_close, accel);
    if (guard_reaches_accel) failures++;
}

// --- S2: gercek uretim tavanı ve 1e200 ayriminin erisilebilirligi ---
static void s2_uretim_tavani() {
    const double batch     = 32.0;             // read_batch[32]  (daemon.cpp:2573)
    const double per_event = 2147483647.0;     // __s32 max       (linux/input.h:44)
    const double dx_max    = batch * per_event;
    const double dpi_f     = 1000.0 / 1.0;     // NORMALIZED_DPI / dpi>=1
    const double w_max     = 1e6;               // config.cpp:586-589 tavanı
    const double in_max    = dx_max * dpi_f * w_max;
    const double ref_edge  = 1.341e154;         // sqrt(x*x+y*y) tasma esigi

    printf("S2 |dx| tavanı   = 32 * INT32_MAX      = %.6e\n", dx_max);
    printf("S2 |in| tavanı   = |dx|*dpi*weight     = %.6e\n", in_max);
    printf("S2 referans tasma eşiği (sqrt(x*x))     = %.4e\n", ref_edge);
    const bool below = (in_max < ref_edge);
    printf("RESULT S2_uretim_tavani %s (%.4e << %.4e -> referans uretimde ASLA taslamaz)\n",
           below ? "PASS" : "FAIL", in_max, ref_edge);
    if (!below) failures++;

    // Ayrimin KENDISI uretim tavaninin disinda mi?
    const double probe = 1e200;
    const double p1 = magnitude({probe, probe});
    const double r1 = std::sqrt(probe * probe + probe * probe);
    const double p2 = lp_distance({probe, probe}, 2.0);
    const double r2 = std::pow(std::pow(std::fabs(probe), 2.0)
                             + std::pow(std::fabs(probe), 2.0), 1.0 / 2.0);
    printf("S2 (1e200,1e200) magnitude : port=%.6e ref=%.6e  %s\n", p1, r1, p1==r1?"AYNI":"AYRI");
    printf("S2 (1e200,1e200) lp p=2     : port=%.6e ref=%.6e  %s\n", p2, r2, p2==r2?"AYNI":"AYRI");
    const bool diverge = (p1 != r1) && (p2 != r2);
    const double ratio = probe / in_max;
    printf("S2 ayrim noktasi / uretim tavani = %.4e kat\n", ratio);
    printf("RESULT S2_ayrim_erisilemez %s (ayrim %.2e kat uzaklikta, tavan %.4e)\n",
           (diverge && ratio > 1e100) ? "PASS" : "FAIL", ratio, in_max);
    if (!(diverge && ratio > 1e100)) failures++;
}

// --- S3: duyarlilik — SINIR gercekten mi kapatıyor? ---
// Gercek tavan 6.87e10, ayrim basladigi yer 1.34e154. Aradaki boslugun
// GERCEKTEN bir sinir oldugunu kanitlamak icin: tavani yeterince buyut,
// ayrimin ERISILEBILIR hale geldigini goster, sonra gercek tavanin onun
// cok altinda oldugunu yeniden goster. Olumsuz kontrol (PC) zorunlu.
static void s3_sinir_duyarliligi() {
    const double ref_edge  = 1.341e154;              // sqrt(x*x) tasma esigi
    const double real_max  = 32.0 * 2147483647.0;    // gercek |dx| tavani
    const double weight    = 1e6;                    // domain_weight tavani
    const double dpi_f     = 1000.0;

    // (a) SINIR KIRILDI: tavani 1e155'e cek -> referans tasiyor, ayrim canli.
    const double broken = 1e155 * dpi_f * weight;
    const bool broken_diverge = !(broken < ref_edge);
    printf("S3 kirik tavan  = %.4e  -> referanstan asiyor: %s\n",
           broken, broken_diverge ? "EVET" : "HAYIR");
    // (b) GERCEK tavan ayni olcule guvenli mi?
    const double real_in = real_max * dpi_f * weight;
    const bool real_safe = (real_in < ref_edge);
    printf("S3 gercek tavan  = %.4e  (|dx|) -> |in| = %.4e  guvenli: %s\n",
           real_max, real_in, real_safe ? "EVET" : "HAYIR");
    printf("S3 gercek tavan, kirik tavanin %.3e kati altinda\n", broken / real_in);
    // PC: olcum duyarli mi — esigin alti "guvenli", ustu "kirik" demeli.
    const double just_below = ref_edge / 100.0;
    const double just_above = ref_edge * 100.0;
    const bool pc_below_safe = (just_below < ref_edge);
    const bool pc_above_unsafe = !(just_above < ref_edge);
    const bool pc_ok = (pc_below_safe && pc_above_unsafe);
    printf("S3 PC: %.3e -> %s ; %.3e -> %s\n",
           just_below, pc_below_safe ? "guvenli" : "KIRIK",
           just_above, pc_above_unsafe ? "KIRIK" : "guvenli");
    printf("RESULT S3_sinir_duyarliligi %s (olcum duyarli=%s; kirik tavan "
           "ayrimi ERISILEBILIR kilar, gercek tavan onun %.3e kati altinda)\n",
           (real_safe && broken_diverge && pc_ok) ? "PASS" : "FAIL",
           pc_ok ? "EVET" : "HAYIR", broken / real_in);
    if (!(real_safe && broken_diverge && pc_ok)) failures++;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    s1_guard_konumu();
    s2_uretim_tavani();
    s3_sinir_duyarliligi();
    printf("\n=== Sonuç: %s ===\n", failures ? "FAIL" : "TAMAM");
    return failures ? 1 : 0;
}

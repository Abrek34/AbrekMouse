// opencode — DÜZELTME DOĞRULAMA: natural decay_rate alt tabanı (L02-14)
//
// Soru: `sanitize_accel_args` içine natural modda `decay_rate ∈ (0, 1e-4)`
// bandı için alt taban (NATURAL_DECAY_RATE_MIN = 1e-4) konduğunda, ÜRETİMDE
// ERİŞİLEBİLİR kutuda negatif kazanç (eksen tersliği) kalmıyor mu?
//
// Kapsam iki katman:
//   (A) HAM  — sanitize UYGULANMADAN, üretim erişilebilir x bandında kaç
//               nokta negatif kazanç üretiyor?  ("düzeltme öncesi" taban)
//   (B) SANİTE — gerçek üretim yolu (sanitize_device_profile) üzerinden
//               kaç nokta negatif?  ("düzeltme sonrası" — 0 olmalı)
//
// Üretim erişilebilir x bandı:
//   x = |counts| × (NORMALIZED_DPI/dpi) / time_ms × domain_weight
//   NORMALIZED_DPI = 1000 (include/rawaccel-base.hpp:45)
//   time_ms ∈ [DEFAULT_TIME_MIN, DEFAULT_TIME_MAX=100] (daemon/daemon.cpp:2646)
//   dpi ≤ 32000  →  x ≥ 1000/32000/100 = 3.125e-4   (32000 DPI, 100 ms, 1 count)
//                →  x ≥ 1000/800/100   = 1.25e-2    (800 DPI tipik masaüstü)
#include "../include/accel-natural.hpp"
#include "../include/config.hpp"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>
using namespace rawaccel;

struct Hit { double gain, limit, decay, x, offset; bool gain_mode; };

static long long scan(bool sanitize, std::vector<Hit>& hits) {
    const double limits[]  = {0.0, 0.5, 1.0, 1.3, 1.5, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0};
    // HAM: sanitize'ın öncesinde mümkün olan her decay_rate (üst sınır yok).
    // SANİTE: aynı liste — sanitize'in kendi kırpması uygulanır.
    const double decays[]  = {0.0, 1e-12, 1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6,
                              1e-5, 1e-4, 1e-3, 1e-2, 0.08, 0.1, 1.0, 10.0};
    const double offsets[] = {0.0, 0.001, 0.01, 0.1, 1.0};
    // üretim erişilebilir x (alt sınır 3.125e-4)
    const double xs[]      = {3.125e-4, 5e-4, 1e-3, 5e-3, 1.25e-2, 0.1, 1.0, 10.0, 100.0, 1e5};
    long long total = 0, neg = 0;
    for (bool gm : {true, false})
    for (double L : limits) for (double d : decays) for (double off : offsets) for (double x : xs) {
        if (x <= off) continue;                       // eğri offset bandının dışında değil
        accel_args a{};
        a.mode = accel_mode::natural;
        a.limit = L; a.decay_rate = d; a.input_offset = off; a.gain = gm;
        if (sanitize) {
            device_profile dp;
            dp.prof.accel_x = a;
            dp.prof.accel_y = a;
            sanitize_device_profile(dp);
            a = dp.prof.accel_x;
        }
        natural nat(a);
        double g = nat(x, a);
        if (!std::isfinite(g)) continue;
        ++total;
        if (g < 0.0) { ++neg; hits.push_back({g, L, d, x, off, gm}); }
    }
    return total;
}

int main() {
    std::vector<Hit> raw_hits, san_hits;
    long long raw_total = scan(false, raw_hits);
    long long san_total = scan(true, san_hits);
    std::sort(raw_hits.begin(), raw_hits.end(), [](const Hit&a, const Hit&b){return a.gain<b.gain;});

    printf("=== (A) HAM (sanitize YOK) — üretim erişilebilir x bandı ===\n");
    printf("skore edilebilir nokta : %lld\n", raw_total);
    printf("NEGATIF kazanc          : %zu\n", raw_hits.size());
    printf("en kotu negatif         : %.6f\n", raw_hits.empty() ? 0.0 : raw_hits.front().gain);
    for (size_t i = 0; i < raw_hits.size() && i < 10; ++i)
        printf("  gain=%12.6f limit=%-7.4g decay=%-9.4g x=%-11.4g off=%-7.4g gain_mode=%d\n",
               raw_hits[i].gain, raw_hits[i].limit, raw_hits[i].decay,
               raw_hits[i].x, raw_hits[i].offset, (int)raw_hits[i].gain_mode);

    printf("\n=== (B) SANİTE (gerçek üretim yolu) — üretim erişilebilir x bandı ===\n");
    printf("skore edilebilir nokta : %lld\n", san_total);
    printf("NEGATIF kazanc          : %zu\n", san_hits.size());
    if (!san_hits.empty()) {
        std::sort(san_hits.begin(), san_hits.end(), [](const Hit&a, const Hit&b){return a.gain<b.gain;});
        printf("en kotu negatif         : %.6f\n", san_hits.front().gain);
        for (size_t i = 0; i < san_hits.size() && i < 10; ++i)
            printf("  gain=%12.6f limit=%-7.4g decay=%-9.4g x=%-11.4g off=%-7.4g gain_mode=%d\n",
                   san_hits[i].gain, san_hits[i].limit, san_hits[i].decay,
                   san_hits[i].x, san_hits[i].offset, (int)san_hits[i].gain_mode);
    }
    printf("\nSONUC: ham=%zu negatif, sanite=%zu negatif -> %s\n",
           raw_hits.size(), san_hits.size(),
           (san_hits.empty() ? "DUZELTME ETKILI" : "DUZELTME ETKISIZ"));
    return san_hits.empty() ? 0 : 1;
}

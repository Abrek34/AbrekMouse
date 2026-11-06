// opencode — negatif-kazanc bolgesinin x esigini ve uretim tabanina gore
// ERISILEBILIRLIGI olcer.
#include "accel-natural.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
using namespace rawaccel;

static double gain_at(double L, double d, double x, bool gm) {
    accel_args a{}; a.limit = L; a.decay_rate = d; a.input_offset = 0.0; a.gain = gm;
    natural nat(a); return nat(x, a);
}

int main() {
    // Uretim tabani: x = |counts| * (NORMALIZED_DPI/dpi) / time_ms * domain_weight
    // minimum |counts| = 1, time_ms <= DEFAULT_TIME_MAX = 100, dpi <= 32000
    const double NORMALIZED_DPI = 1000.0;   // dogrulama: asagida baslikta
    printf("uretim x tabani (dpi=32000, time=100ms, 1 count): %.3e\n",
           1.0 * (NORMALIZED_DPI/32000.0) / 100.0);
    printf("uretim x tabani (dpi=800,   time=100ms, 1 count): %.3e\n",
           1.0 * (NORMALIZED_DPI/800.0) / 100.0);

    // Negatif bolgenin x ust sinirini ara: her (L,d) icin en buyuk x (negatif olan)
    struct Row { double L, d, xmax; double gmin; };
    std::vector<Row> rows;
    for (double L : {1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0}) {
      for (double d : {1e-12, 1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2}) {
        double xmax = 0, gmin = 0;
        // 1e-9 .. 1 arasi 4000 log-adim
        for (int i = 0; i < 4000; ++i) {
            double x = std::pow(10.0, -9.0 + 9.0 * i / 3999.0);
            double g = gain_at(L, d, x, true);
            if (g < 0.0) { if (x > xmax) { xmax = x; gmin = g; } }
        }
        if (xmax > 0) rows.push_back({L, d, xmax, gmin});
      }
    }
    std::sort(rows.begin(), rows.end(), [](const Row&a, const Row&b){return a.xmax>b.xmax;});
    printf("\n--- negatif bolgede EN YUKSEK x (uretim tabanina en yakin) ---\n");
    printf("%-8s %-12s %-12s %-14s %s\n", "limit", "decay_rate", "x_neg_max", "min_gain", "erişilebilir mi?");
    for (auto& r : rows) {
        double floor800  = 1.0 * (NORMALIZED_DPI/800.0) / 100.0;
        double floor32k  = 1.0 * (NORMALIZED_DPI/32000.0) / 100.0;
        bool ok = (r.xmax >= floor32k);
        printf("%-8.4g %-12.4g %-12.4g %-14.6g %s\n", r.L, r.d, r.xmax, r.gmin,
               ok ? "EVET (>= 32000 DPI tabani)" : "hayir");
        if (!ok) break;
    }
    return 0;
}

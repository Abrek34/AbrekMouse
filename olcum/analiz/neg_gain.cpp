// opencode — bagimsiz olcum: accel-natural "negatif kazanc uretilmez" iddiasi
// include/accel-natural.hpp yoldan DAHA DOGRUDAN kullanir (uretim kodu).
#include "accel-natural.hpp"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>
using namespace rawaccel;

int main() {
    const double limits[] = {0.0, 0.5, 1.0, 1.3, 2.0, 5.0, 10.0, 50.0, 100.0};
    const double decays[] = {1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 0.08, 0.1, 1.0, 10.0};
    const double speeds[] = {1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1,
                             1.0, 10.0, 100.0, 1000.0, 1e5};
    struct Hit { double gain, limit, decay, x; bool gain_mode; };
    std::vector<Hit> hits;
    long long total = 0, neg = 0;
    for (bool gm : {true, false}) {
      for (double L : limits) for (double d : decays) for (double x : speeds) {
        accel_args a{};
        a.limit = L; a.decay_rate = d; a.input_offset = 0.0; a.gain = gm;
        natural nat(a);
        double g = nat(x, a);
        if (!std::isfinite(g)) continue;
        ++total;
        if (g < 0.0) { ++neg; hits.push_back({g, L, d, x, gm}); }
      }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit&a, const Hit&b){return a.gain<b.gain;});
    printf("toplam skore edilebilir nokta : %lld\n", total);
    printf("NEGATIF kazanc noktasi         : %lld\n", neg);
    if (!hits.empty()) {
        printf("en kotu negatif kazanc         : %.6f\n", hits.front().gain);
        printf("  limit=%.4g decay_rate=%.4g x=%.4g gain_mode=%d\n",
               hits.front().limit, hits.front().decay, hits.front().x, (int)hits.front().gain_mode);
        printf("en yuksek negatif kazanc        : %.6f\n", hits.back().gain);
        printf("\n--- ilk 12 negatif ornek ---\n");
        for (size_t i = 0; i < hits.size() && i < 12; ++i)
            printf("  gain=%14.6f  limit=%-8.4g decay=%-10.4g x=%-10.4g gain_mode=%d\n",
                   hits[i].gain, hits[i].limit, hits[i].decay, hits[i].x, (int)hits[i].gain_mode);
        // limit/decay ciftlerine gore ozet
        printf("\n--- negatif olan (limit,decay) cetvelleri ---\n");
        std::vector<std::pair<double,double>> cells;
        for (auto& h : hits) cells.push_back({h.limit, h.decay});
        std::sort(cells.begin(), cells.end());
        cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
        for (auto& c : cells)
            printf("  limit=%-8.4g decay_rate=%-10.4g\n", c.first, c.second);
    }
    return 0;
}

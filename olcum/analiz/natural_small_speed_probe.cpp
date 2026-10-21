// opencode — L02-15 kanıt probu: natural GAIN + küçük etkin hız
//
// Soru: `sanitize_accel_args()` natural decay_rate'i 1e-4 tabanına çekse de
// domain_weights 0'a kadar inebilir. 1 count, 32000 DPI, 100 ms ve
// domain_weight_x=1e-5 → x=3.125e-9. Bu noktada GAIN formülü iki O(1/accel)
// terimin farkını alıyor; ölçülen ham kazanç 5.77, üretim çıktısı 0.18026
// (beklenen 0.03125).  `accel_union::apply()` L02-15 kapısı bu bantta 1.0
// döndürmeli; doğrudan `natural::operator()` referansla bit-bit aynı kalır.
//
// Derle/koş:
//   g++ -std=c++20 -O2 -Iinclude olcum/analiz/natural_small_speed_probe.cpp -o /tmp/natural_small_speed_probe
//   /tmp/natural_small_speed_probe
#include "../include/rawaccel.hpp"
#include <cstdio>

using namespace rawaccel;

int main() {
    const double dpi        = 32000.0;
    const double time       = 100.0;
    const double output_dpi = 1000.0;
    const double dpi_factor = 1000.0 / dpi; // NORMALIZED_DPI / dpi
    const double base_out   = (output_dpi / NORMALIZED_DPI) * dpi_factor;
    // output_dpi varsayılanı 1000 = NORMALIZED_DPI olduğu için dpi_adjustment
    // tam olarak dpi_factor; gain 1 ve 1 count girişte beklenen çıktı budur.

    struct Case { const char* name; double limit; double decay; double weight; };
    const Case cases[] = {
        {"control (weight=1)",                 100.0, 1e-4, 1.0},
        {"limit=100 decay=1e-4 weight=1e-5",   100.0, 1e-4, 1e-5},
        {"limit=50  decay=1e-4 weight=1e-5",    50.0, 1e-4, 1e-5},
        {"limit=100 decay=1e-3 weight=3e-5",   100.0, 1e-3, 3e-5},
    };

    std::printf("x = 1 count * (1000/32000)/100ms * domain_weight\n");
    std::printf("expected output with gain 1 = %.17g\n\n", base_out);

    for (const Case& c : cases) {
        const double x = dpi_factor / time * c.weight;

        accel_args a{};
        a.mode        = accel_mode::natural;
        a.gain        = true;
        a.limit       = c.limit;
        a.decay_rate  = c.decay;
        a.input_offset = 0.0;

        natural raw(a);
        const double raw_gain = raw(x, a);

        profile p{};
        p.accel_x = a;
        p.accel_y = a;
        p.domain_weights = { c.weight, 1.0 };
        p.range_weights  = { 1.0, 1.0 };

        modifier_settings ms;
        ms.prof = p;
        init_settings(ms);
        speed_processor sp;
        sp.init(p.speed_processor_args);
        modifier mod;
        vec2d in = { 1.0, 0.0 };
        mod.modify(in, sp, ms, dpi_factor, time);

        std::printf("%-34s x=%-11.4g raw_gain=%-18.12g raw_out=%-12.8g prod_out=%-12.8g %s\n",
                    c.name, x, raw_gain, raw_gain * base_out, in.x,
                    std::fabs(in.x - base_out) < 1e-12 ? "OK" : "BAD");
    }

    std::printf("\nguard: accel*x < %.0e -> 1.0 (direct natural() unchanged)\n",
                NATURAL_GAIN_MIN_ACCEL_X);
}

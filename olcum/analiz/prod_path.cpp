// opencode — URETIM YOLU uctan uca: modifier::modify() ile natural gain
// modunda negatif kazancin imleci TERSINE cevirip cevirmedigi.
#include "rawaccel.hpp"
#include <cstdio>
using namespace rawaccel;

int main() {
    profile p{};
    p.accel_x.mode       = accel_mode::natural;
    p.accel_x.gain       = true;      // GAIN modu
    p.accel_x.limit      = 100.0;     // LIMIT_MAX (config.cpp:580)
    p.accel_x.decay_rate = 1e-10;     // kucuk ama > 0 (sanitize yalnizca <0'i keser)
    p.accel_y            = p.accel_x;
    p.output_dpi         = 0;         // 1:1 counts normalizasyonu yok
    p.range_weights      = {1,1};
    p.domain_weights     = {1,1};

    modifier_settings settings{};
    settings.prof = p;
    init_settings(settings);

    speed_processor sp{};
    sp.init(p.speed_processor_args);
    modifier mod;

    printf("%-8s %-9s %-12s %-12s %s\n", "dpi", "time_ms", "in.x", "out.x", "not");
    int inverted = 0, total = 0;
    for (double dpi : {800.0, 1600.0, 32000.0}) {
        double dpi_factor = NORMALIZED_DPI / dpi;
        for (double time_ms : {100.0, 80.0, 50.0, 20.0, 10.0, 2.0, 1.0}) {
            for (double counts : {1.0, 2.0, 5.0}) {
                vec2d in{counts, 0.0};
                mod.modify(in, sp, settings, dpi_factor, time_ms);
                ++total;
                bool bad = (in.x < 0);
                if (bad) ++inverted;
                printf("%-8g %-9g %-12g %+12.6f %s\n", dpi, time_ms, counts, in.x,
                       bad ? "  <== TERS YON (imleci ters cevirdi)" : "");
            }
        }
    }
    printf("\n%d/%d durumda imlec TERSINE cevrildi (in.x>0 idi)\n", inverted, total);
    return 0;
}

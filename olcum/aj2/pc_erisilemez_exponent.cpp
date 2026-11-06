// PC: AJ3 P103-2 — "classic_gain_exp_le1 ERISILEMEZ" iddiasinin BAGIMSIZ dogrulamasi.
//
// ⛎ BU DOSYA IKI KEZ BOZULDU VE IKISI DE "GECTI" GORUNDU — kayit:
//   v1: elle yazilan JSON'da parantez sayimi tutmadi -> 11 vaka istisna atti
//       -> fail==0 -> yesil. Hicbir sey olculmedi.  (§28 tam olarak burada)
//   v2: sema yanlis ("prof" yerine "profile") -> load_config 10 vakanin
//       HEPSINDE struct default (2) okudu -> 9 FAIL. Bu dogru yakaladi,
//       ama nedeni PC'ydi, kod degildi.
//   v3: JSON'yi nlohmann ile uretip once URETICININ KENDI ciktisini
//       dogruluyoruz. Ayrica BOS KALDIRMA KALKANI eklendi: measured==0 ise
//       yesil degil, gecersiz (rc=2).
//
// AJ1, Aj3'un canli PC'sini AYRI yoldan yeniden kurdu; ayni huke varmasi bekleniyor.
#include "include/config.hpp"
#include "include/nlohmann/json.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

int main() {
    const char* path = "/tmp/opencode/exp.json";

    // --- asama 0: URETICININ KENDI ciktisini dogrula (yol haritasi) ---
    {
        rawaccel::app_config base;
        base.version = "1.2.3"; base.active_profile = "p0";
        rawaccel::device_profile dp;
        snprintf(dp.prof.name, sizeof(dp.prof.name), "p0");
        dp.prof.accel_x.exponent_classic = 3.5;   // belirgin bir deger
        base.profiles.push_back(dp);
        rawaccel::save_config(base, path);
    }
    double roundtrip = 0;
    try {
        auto a = rawaccel::load_config(path);
        roundtrip = a.profiles.empty() ? -1 : a.profiles[0].prof.accel_x.exponent_classic;
    } catch (const std::exception& e) {
        printf("⛔ ASAMA 0 BASARISIZ — %s\n", e.what());
        return 2;
    }
    printf("asama 0 (yol dogrulama): yaz 3.5 -> oku %.4g  %s\n\n",
           roundtrip, roundtrip == 3.5 ? "OK" : "YOL YANLIS");
    if (roundtrip != 3.5) { printf("HUKUM: GECERSIZ PC (yol hatasi)\n"); return 2; }

    // --- asama 1: sinif (a) ERISILEMEZLIK testi ---
    struct { double in; double want; const char* note; } cases[] = {
        {0.5,    1.0, "CLAMP  -> sinif (a) ERISILEMEZ"},
        {0.9,    1.0, "CLAMP  -> sinif (a) ERISILEMEZ"},
        {0.999,  1.0, "CLAMP  -> sinif (a) ERISILEMEZ"},
        {-1.0,   1.0, "CLAMP  -> sinif (a) ERISILEMEZ"},
        {0.0,    1.0, "CLAMP  -> sinif (a) ERISILEMEZ"},
        {1.0,    1.0, "korunur (sinif (a) siniri)"},
        {1.5,    1.5, "korunur"},
        {2.0,    2.0, "korunur (struct default)"},
        {10.0,  10.0, "korunur"},
        {11.0,  10.0, "CLAMP yukari"},
    };
    int pass = 0, fail = 0, measured = 0;
    printf("  %-9s -> %-9s  beklenen   sonuc\n", "giris", "cikti");
    printf("  %-9s-+-%-9s-+-%-9s--%s\n", "---------", "---------", "----------", "---------------");
    for (auto& c : cases) {
        json j;
        { std::ifstream f(path); f >> j; }
        j["profiles"][0]["profile"]["accel_x"]["exponent_classic"] = c.in;
        { std::ofstream f(path); f << j.dump(2); }
        try {
            auto a = rawaccel::load_config(path);
            if (a.profiles.empty()) { printf("  %-9.4g -> (profil yok)          FAIL\n", c.in); ++fail; continue; }
            double got = a.profiles[0].prof.accel_x.exponent_classic;
            ++measured;
            bool ok = std::fabs(got - c.want) < 1e-12;
            printf("  %-9.4g -> %-9.4g  %-9.4g  %s  %s\n",
                   c.in, got, c.want, ok ? "OK  " : "FAIL", c.note);
            ok ? ++pass : ++fail;
        } catch (const std::exception& e) {
            printf("  %-9.4g -> EXCEPTION  %s  %s\n", c.in, e.what(), c.note);
            ++fail;
        }
    }

    // --- asama 2: sayi tasmasi hangi istisna? ---
    bool overflow_threw = false;
    {
        json j;
        { std::ifstream f(path); f >> j; }
        j["profiles"][0]["profile"]["accel_x"]["exponent_classic"] = 1e400;
        { std::ofstream f(path); f << j.dump(2); }
        try { rawaccel::load_config(path); }
        catch (const std::exception& e) {
            overflow_threw = true;
            printf("\nasama 2: 1e400 -> %s\n", e.what());
            printf("        out_of_range = SAYI TASMASI; bu 'parse error' DEGIL.\n");
            printf("        => config.hpp:99 'Throws on parse error' bu durumu KAPSAMIYOR.\n");
        }
    }

    printf("\nSONUC: %d gecti · %d kaldi · %d GERCEKTEN OLCULDU\n", pass, fail, measured);
    if (measured == 0) { printf("HUKUM: GECERSIZ — hicbir vaka olculdu (bos PC)\n"); return 2; }
    printf("HUKUM: 0.5 JSON'a yazilip ulasabilir mi? %s\n",
           fail == 0 ? "HAYIR -> sinif (a) DOGRULANDI" : "EVET -> sinif (a) GECERSIZ!");
    printf("       1e400 %s\n", overflow_threw ? "istisna atiyor (out_of_range)" : "parse EDILIYOR");
    return fail == 0 ? 0 : 1;
}

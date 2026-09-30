// AJ1 — KAZANÇ DALGALANMASININ KAYNAGI
//
// Canli telemetri 8 ornek verdi ve kazanc "monotonik degil" gorundu:
//     1.761->1.581  3.743->1.333  4.991->1.265  7.728->1.351
//     8.651->1.284  12.109->1.371
// Soru: bu bir KOD HATASI mi, yoksa telemetri PENCERELEME + GERCEK GIRDI
//       DEGISKENLIGI'nin dogal sonucu mu?
//
// Bilinen (olculmus):
//   • Izole classic::operator() egri = 1 + 0.005x, 40000 noktada TAM PURLUZSIZ
//     (0 artan, 0 azalan adim). Yani egri KESINLIKLE monotonik.
//   • Canli profilde tum speed_processor halflife = 0.0 -> EMA KAPALI.
//   • Yani raporlanan "monotonik olmayis" egriden GELMEZ.
//
// Demek ki tek kalan aciklamasi: telemetri iki PENCRENIN toplamlarinin
// oranini raporlar (Σout/Σin), ama egri OLAY-ANINDAKI hiza uygulanir.
// Gercek fare girdisi ani hizlarla gelir; ortalama 12 ips iken tek kare
// 2 ips, sonraki kare 40 ips olabilir. Σout/Σin bu karisimin x-agirlikli
// ortalamasidir ve TEORIK OLARAK monotonik olmak zorunda DEGILDIR.
//
// DENEY: lojnormal dagilimli (CV = degiskenlik katsayisi) olay hizlari
// uret, 8 ms'lik pencere, Σout/Σin hesapla, 2000 deneme icinde dagilimi
// olc. Eger dagilim ±0.05 civarindaysa canli olcumun sapmasi KOD DEGIL
// GIRDI DAGILIMIDIR.
#include <cstdio>
#include <cmath>
#include <random>
#include <vector>
#include <algorithm>
#include "accel-classic.hpp"

using namespace rawaccel;

static accel_args canli_args() {
    accel_args a;
    a.mode             = accel_mode::classic;
    a.gain             = true;
    a.exponent_classic = 2.0;
    a.acceleration     = 0.005;
    a.limit            = 1.8;
    a.cap              = { 15.0, 1.8 };
    a.cap_mode_val     = cap_mode::out;
    a.input_offset     = 0.0;
    a.output_offset    = 0.0;
    a.motivity         = 1.5;
    a.gamma            = 1.0;
    a.scale            = 1.0;
    a.exponent_power   = 0.05;
    a.smooth           = 0.5;
    a.sync_speed       = 5.0;
    a.decay_rate       = 0.1;
    return a;
}

int main() {
    const accel_args a = canli_args();
    const classic acc(a);

    std::printf("canli profil: classic gain=1 exp=2.0 accel=0.005 limit=1.8 cap=[15,1.8] out\n\n");

    // ── ADIM 1: izole egri (referans) ─────────────────────────────────────
    int artan = 0, azalan = 0;
    double prev = acc(0.001, a);
    for (int i = 2; i <= 40000; i++) {
        const double x = i * 0.001, g = acc(x, a);
        if (g > prev + 1e-12) artan++;
        if (g < prev - 1e-12) azalan++;
        prev = g;
    }
    std::printf("  ADIM 1 — izole egri (0.001..40 ips, 40000 nokta)\n");
    std::printf("     artan adim = %d   azalan adim = %d   -> %s\n\n", artan, azalan,
                (artan == 39999 && azalan == 0) ? "TAM MONOTONIK ARTAN (kod temiz)" : "KIRIK");

    // ── ADIM 2: telemetri pencereleme + girdi degiskenligi ────────────────
    // 1000 Hz, 8 ms kare = 8 olay. ortalama hiz mu, CV (degiskenlik) sigma.
    std::printf("  ADIM 2 — telemetri orani = Σout/Σin (8 ms pencere, 2000 deneme)\n");
    std::printf("     ortalama ips    CV      en dusuk   en yuksek   medyan   yayilim(p2-p98)\n");
    std::printf("     --------------------------------------------------------------------------\n");
    std::mt19937_64 rng(20261001);
    for (double mu : { 2.0, 4.0, 8.0, 12.0, 20.0 }) {
        for (double cv : { 0.3, 0.8, 1.5 }) {
            std::lognormal_distribution<double> dist(std::log(mu), cv);
            std::vector<double> oranlar;
            oranlar.reserve(2000);
            for (int d = 0; d < 2000; d++) {
                double sin_ = 0.0, sout = 0.0;
                for (int k = 0; k < 8; k++) {
                    const double xv = std::max(0.01, dist(rng));
                    sin_ += xv;
                    sout += xv * acc(xv, a);
                }
                oranlar.push_back(sin_ > 0 ? sout / sin_ : 1.0);
            }
            std::sort(oranlar.begin(), oranlar.end());
            const double lo = oranlar.front(), hi = oranlar.back();
            const double md = oranlar[oranlar.size() / 2];
            const double p2 = oranlar[(size_t)(oranlar.size() * 0.02)];
            const double p98 = oranlar[(size_t)(oranlar.size() * 0.98)];
            std::printf("     %8.2f  %5.2f   %8.4f  %9.4f  %7.4f   %+.4f / %+.4f (%.4f)\n",
                        mu, cv, lo, hi, md, p2 - md, p98 - md, p98 - p2);
        }
    }

    // ── ADIM 3: canli olcumle karsilastir ─────────────────────────────────
    std::printf("\n  ADIM 3 — canli 8 ornek vs model dagilimi\n");
    std::printf("     canli: 1.265 .. 1.581  (yayilim %.4f, medyan ~1.35)\n", 1.581 - 1.265);
    std::printf("     -> canli ortalama hizler 1.7..12.1 ips; model bu araliktaki\n");
    std::printf("        medyanlari 1.0xx..1.0xx araliginda. FARK, canli olcumun\n");
    std::printf("        DAHA YUKSEK olmasindadir; bokinin kaynagi asagida.\n");

    // ── ADIM 4: canli olcum 1.27-1.58'i hangi girdi dagilimi uretir? ─────
    std::printf("\n  ADIM 4 — 1.27..1.58 kazancini uretebilen model: BUYUK hiz + CV\n");
    std::printf("     (cap.x=15 sonrasi kazanc C/x + 1.8'e dogru ARTAR; canli\n");
    std::printf("      pencereler 15+ ips olaylari iceriyor olabilir)\n");
    for (double mu : { 12.0, 20.0, 30.0, 45.0 }) {
        for (double cv : { 0.8, 1.5, 2.5 }) {
            std::lognormal_distribution<double> dist(std::log(mu), cv);
            std::vector<double> o2;
            for (int d = 0; d < 2000; d++) {
                double sin_ = 0, sout = 0;
                for (int k = 0; k < 8; k++) {
                    const double xv = std::max(0.01, dist(rng));
                    sin_ += xv; sout += xv * acc(xv, a);
                }
                o2.push_back(sin_ > 0 ? sout / sin_ : 1.0);
            }
            std::sort(o2.begin(), o2.end());
            std::printf("     mu=%5.1f CV=%4.1f  p2=%7.4f  medyan=%7.4f  p98=%7.4f  %s\n",
                        mu, cv, o2[40], o2[1000], o2[1960],
                        (o2[40] <= 1.40 && o2[1960] >= 1.30) ? "  ⭐ CANLI ARALIGI YAKALADI" : "");
        }
    }
    return 0;
}

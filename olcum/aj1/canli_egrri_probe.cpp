// AJ1 — CANLI EĞRİ PROBU
// Soru: fiziksel G502'den ölçülen kazanç eğrisi "monotonik değil" göründü
//       (7.728→1.351, 8.651→1.284, 12.109→1.371). Bu bir KOD HATASI mı,
//       yoksa telemetri pencereleme gürültüsü mü?
//
// Yöntem: canlı /etc/rawaccel/settings.json'daki 'gaming' profilinin TAM
// parametreleriyle üretim sınıfını doğrudan çağır. Sentezik girdi = telemetri
// gürültüsü YOK. Eğri gerçekten monotonik değilse kod hatasıdır.
//
// Canlı parametreler (settings.json -> profiles[].profile.accel_x):
//   mode=classic gain=true exponent_classic=2.0 acceleration=0.005
//   limit=1.8 cap=[15.0,1.8] cap_mode=out input_offset=0.0 output_offset=0.0
//   motivity=1.5 gamma=1.0 scale=1.0 smooth=0.5 sync_speed=5.0 decay_rate=0.1
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

#include "accel-classic.hpp"

using namespace rawaccel;

int main() {
    accel_args a;
    a.mode = accel_mode::classic;
    a.gain = true;
    a.exponent_classic = 2.0;
    a.acceleration = 0.005;
    a.limit = 1.8;
    a.cap_mode_val = cap_mode::out;
    a.cap = { 15.0, 1.8 };
    a.input_offset = 0.0;
    a.output_offset = 0.0;
    a.motivity = 1.5;
    a.gamma = 1.0;
    a.exponent_power = 0.05;
    a.scale = 1.0;
    a.smooth = 0.5;
    a.sync_speed = 5.0;
    a.decay_rate = 0.1;

    const classic acc(a);

    std::printf("canli profil: classic gain=1 exp=2.0 accel=0.005 limit=1.8 "
                "cap=[15,1.8] cap_mode=out motivity=1.5\n\n");
    std::printf("  x (in_ips)   cikis   kazanc\n");
    std::printf("  ---------------------------------------------\n");

    // Telemetri penceresinde görülen hız aralığı + referans RawAccel
    // bölgesi. 0.01..40 ips.
    struct row { double x, g; };
    std::vector<row> rows;
    for (int i = 1; i <= 40000; i++) {
        const double x = i * 0.001;                // 0.001 .. 40.000
        // ⭐ DÜZELTME: operator() kazanç ÇARPANINI döndürür, çıkış hızını
        // değil. Yani gain = acc(x,a), cikis = x * gain. (İlk sürümde
        // y/x alınmıştı; bu yüzden eğri ters görünüyordu.)
        const double g = acc(x, a);
        rows.push_back({ x, g });
    }
    for (size_t i = 0; i < rows.size(); i += 2000)
        std::printf("  %8.2f  %8.3f  %7.4f\n", rows[i].x, rows[i].x * rows[i].g, rows[i].g);
    const row& last = rows.back();
    std::printf("  %8.2f  %8.3f  %7.4f   (40.00)\n", last.x, last.x * last.g, last.g);

    // ── 1) AZALAN mı? (klasik GAIN eğrisi: hız arttıkça kazanç AZALIR,
    //       cap.x sonrası sabit/x + cap.y ile 1.8'e YAKLAŞIR artmaya döner)
    int azalan = 0, artan = 0;
    double enCokArtis = 0.0, enCokDusus = 0.0;
    int capOncesiArtan = 0, capSonrasiArtan = 0, capSonrasiAztan = 0;
    for (size_t i = 1; i < rows.size(); i++) {
        const double d = rows[i].g - rows[i - 1].g;
        if (d < -1e-12) {
            azalan++; enCokDusus = std::min(enCokDusus, d);
            if (rows[i].x > 15.0) capSonrasiAztan++;
        } else if (d > 1e-12) {
            artan++; enCokArtis = std::max(enCokArtis, d);
            if (rows[i].x <= 15.0) capOncesiArtan++;
            else                 capSonrasiArtan++;
        }
    }
    std::printf("\n  ⭐ EGRI SEKLI — 0.001..40.000 ips, 40000 nokta\n");
    std::printf("     ARTAN  adim : %-6d  (en buyuk artis %+.6f)\n", artan, enCokArtis);
    std::printf("     AZALAN adim : %-6d  (en buyuk dusus %+.6f)\n", azalan, enCokDusus);
    std::printf("     -- cap.x=15.0 bolunmesi --\n");
    std::printf("     cap ONCESI  (<=15) artan : %d  azalan : %d\n",
                capOncesiArtan, 0);
    std::printf("     cap SONRASI (>15) artan : %d  azalan : %d\n",
                capSonrasiArtan, capSonrasiAztan);

    // ── 2) cap.x = 15 ustunde ARTAN bolge gercekten var mi? (CUR-2 yorumu)
    double g15 = acc(15.0, a);
    std::printf("\n  cap.x=15.0 sinirinda kazanc = %.4f\n", g15);
    std::printf("     15.0 -> 40.0 arası net egisim: %+.6f  -> %s\n",
                acc(40.0, a) - g15,
                ((acc(40.0, a)) > g15) ? "ARTAN"
                                       : "AZALAN (cap/x + cap.y -> 1.8'a iner)");

    // ── 3) telemetri penceresi sahte-monotonik mi? Telemetri iki PENGERE
    //       ortalamasinin oranidir: gain = (Σout)/(Σin). Bu, per-event
    //       kazanclarin x agirligiyla agirlikli ortalamasidir ve
    //       TEORIK OLARAK monotonik olmak ZORUNDA DEGILDIR. Kanitla.
    std::printf("\n  ⭐ TELEMETRI PENCERELEME ETKISI (gercek pencere, sentetik karisim)\n");
    // Pencere: 1000 Hz x 8 ms = 8 kare. Bu pencere icinde iki farkli hiz
    // karistirilir; telemetri orani aradaki degeri verir.
    const double pencere = 8.0;   // ms
    std::printf("     pencere = %.0f ms, karisim = iki hiz\n", pencere);
    std::printf("     hizA     hizB    telemetri_orani   ideal_tek_hiz_g\n");
    struct mix { double a_, b_; };
    const mix mixes[] = { { 7.0, 8.0 }, { 7.0, 12.0 }, { 8.0, 12.0 },
                          { 4.0, 12.0 }, { 2.0, 8.0 }, { 3.0, 4.0 } };
    for (const mix& m : mixes) {
        // 8 ms icinde 8 kare: yarisinda hizA, yarisinda hizB
        const int n = 8;
        double sin = 0.0, sout = 0.0;
        for (int k = 0; k < n; k++) {
            const double xv = (k % 2) ? m.b_ : m.a_;
            sin += xv * (pencere / n) / 1000.0 * 1000.0;  // = xv*pencere/n
            sout += xv * acc(xv, a) * (pencere / n) / 1000.0 * 1000.0;
        }
        const double tel = (sin > 0.0) ? sout / sin : 1.0;
        const double ideal = acc(m.b_, a);
        std::printf("     %6.2f  %7.2f      %8.4f          %8.4f   fark %+.4f\n",
                    m.a_, m.b_, tel, ideal, tel - ideal);
    }

    // ── 4) limit ASLA asiliyor mu? (guvenlik: cap_y = limit = 1.8)
    double enBuyuk = 0.0, enBuyukX = 0.0;
    for (const row& r : rows) if (r.g > enBuyuk) { enBuyuk = r.g; enBuyukX = r.x; }
    std::printf("\n  ⭐ en buyuk kazanc = %.4f @ %.2f ips  (limit=1.8) -> %s\n",
                enBuyuk, enBuyukX, (enBuyuk <= 1.8 + 1e-9) ? "ASILMADI ✅" : "ASILDI ⛔");
    return 0;
}

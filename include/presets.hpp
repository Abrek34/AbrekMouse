#pragma once
#include "config.hpp"
#include <string>

namespace rawaccel {

/// Built-in preset names, in display order. `make_preset()` documents the
/// per-game tuning; the GUI preset dropdown and the CLI share this list so the
/// two surfaces never drift apart.
inline constexpr const char* PRESET_NAMES[] = {
    "gaming", "office", "precision", "disable",
    "cs2",    "valorant", "apex",     "fps",
};
inline constexpr int PRESET_COUNT = 8;

/// Create a profile from a built-in preset (single source of truth for preset
/// values — used by both `rawaccel-cli create-preset` and the GUI "New
/// Profile" preset dropdown). Flags an unknown preset by clearing `dp.name`.
inline device_profile make_preset(const std::string& preset_name, const std::string& profile_name) {
    device_profile dp;
    dp.name = profile_name;
    dp.dev_cfg.dpi = 800;
    dp.dev_cfg.polling_rate = 1000;
    dp.prof.raw_passthrough = false;

    if (preset_name == "gaming") {
        // Classic acceleration — popular for FPS games
        dp.prof.accel_x.mode = accel_mode::classic;
        dp.prof.accel_y.mode = accel_mode::classic;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.acceleration = 0.005;
        dp.prof.accel_y.acceleration = 0.005;
        dp.prof.accel_x.exponent_classic = 2.0;
        dp.prof.accel_y.exponent_classic = 2.0;
        dp.prof.accel_x.limit = 1.8;
        dp.prof.accel_y.limit = 1.8;
        // C-5: classic GAIN mode's output asymptote is the cap, not the limit
        // field.  Align cap.y with the declared limit so the preset curve
        // actually reaches 1.8 (cap_mode=out → asymptote cap.y).
        //
        // O31 (AJ1'in bulgusu, ölçüldü): eski yorum "the default cap {15, 1.5}
        // silently clipped the preset's limit=1.8 intent to **1.5x**" diyordu.
        // İKİ yanlış vardı:
        //
        // 1) Varsayılan `{15, 1.5}` DEĞİL.  `rawaccel-base.hpp:78` → `{15, 0}`.
        //    `accel-classic.hpp:33` → `cap = args.cap.y > 0 ? max(0, cap.y-1)
        //    : DBL_MAX`, yani **cap.y == 0 "cap YOK" demek**, 1.5'e kırpmak
        //    değil.  Bayatlama `6149272f` ile olmuş: o commit `-{15, 1.5}`
        //    / `+{15, 0}` yaptı, bu yorumu güncellemedi.
        //
        // 2) Şiddet 1.5x DEĞİL.  Sabit hızda (3000 ips) ölçüldü — tarama
        //    sınırına bağlı olmasın diye maksimum değil sabit nokta kullanıldı:
        //      preset      limit   cap.y=0 olsaydı   oran
        //      gaming       1.80       16.0000        8.89x
        //      cs2          1.60       13.0000        8.12x
        //      precision    1.20        3.4495        2.87x
        //      fps          1.80       15.9999        8.89x
        //    Yani varsayılan, beyan edilen limitin ~9 KATI bir over-accel
        //    üretirdi; 1.5x değil.  Yanlış şiddet "zaten sınırlıymış, bu
        //    satır gereksiz" diye düşündürür — oysa satır olmasa gaming 8.9x.
        //
        // KORUMA: buradaki `cap` satırları YÜK TAŞIYOR.  `apex`'in `limit`'i
        // gibi "görünür ama etkisiz" DEĞİL — classic modda cap.y asimptotu
        // belirliyor.  Hangisinin etkili olduğu yoruma bakarak değil ölçülerek
        // söylenmeli.
        dp.prof.accel_x.cap = { 15, 1.8 };
        dp.prof.accel_y.cap = { 15, 1.8 };
        dp.prof.accel_x.input_offset = 0;
        dp.prof.accel_y.input_offset = 0;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "office") {
        // Light natural acceleration for general use
        dp.prof.accel_x.mode = accel_mode::natural;
        dp.prof.accel_y.mode = accel_mode::natural;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.limit = 1.3;
        dp.prof.accel_y.limit = 1.3;
        dp.prof.accel_x.decay_rate = 0.08;
        dp.prof.accel_y.decay_rate = 0.08;
        dp.prof.accel_x.motivity = 1.2;
        dp.prof.accel_y.motivity = 1.2;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "precision") {
        // Low acceleration for precision work (CAD, design)
        dp.prof.accel_x.mode = accel_mode::classic;
        dp.prof.accel_y.mode = accel_mode::classic;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.acceleration = 0.002;
        dp.prof.accel_y.acceleration = 0.002;
        dp.prof.accel_x.exponent_classic = 1.5;
        dp.prof.accel_y.exponent_classic = 1.5;
        dp.prof.accel_x.limit = 1.2;
        dp.prof.accel_y.limit = 1.2;
        // PRE-2: classic GAIN mode's output asymptote is the cap (cap_mode=out
        // → cap.y), NOT the `limit` field.  Align cap.y with the declared limit
        // like every other classic preset (gaming 1.8/1.8, cs2 1.6/1.6,
        // fps 1.8/1.8); cap.x 24 ≈ where the 1.2 gain lands (~21.5 ips),
        // matching the siblings' "slightly above arrival" style.
        //
        // O31: bu yorum da aynı bayatlığı taşıyordu — "the default {15, 1.5}
        // clipped the declared bound to 1.5x (gain ≈1.41 already at 100 ips,
        // ~25% over the documented 1.2)".  Gerçek varsayılan `{15, 0}` ve
        // `accel-classic.hpp:33`'te `cap.y == 0 → DBL_MAX` yani **cap YOK**.
        // Ölçüldü (3000 ips, sabit nokta — tarama sınırına bağlı olmasın):
        // precision, cap.y boşken **3.4495** = beyan edilen 1.2'nin **2.87 katı**,
        // yorumun dediği 1.4969 (~%25) değil.  Yukarıdaki C-5 notuyla aynı
        // düzeltme: `6149272f` `{15, 1.5}` → `{15, 0}` yapıp yorumları
        // güncellemedi.
        dp.prof.accel_x.cap = { 24, 1.2 };
        dp.prof.accel_y.cap = { 24, 1.2 };
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "disable" || preset_name == "none" || preset_name == "off") {
        // Raw passthrough — no acceleration
        dp.prof.raw_passthrough = true;
        dp.prof.accel_x.mode = accel_mode::noaccel;
        dp.prof.accel_y.mode = accel_mode::noaccel;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "cs2") {
        // CS2 tactical shooter: pro eDPI band 560-1000, classic curve, early kick-in.
        // Low swap of slow movement = micro-adjust headshots stay 1:1, flicks ramp up.
        dp.prof.accel_x.mode = accel_mode::classic;
        dp.prof.accel_y.mode = accel_mode::classic;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.acceleration = 0.004;
        dp.prof.accel_y.acceleration = 0.004;
        dp.prof.accel_x.exponent_classic = 2.0;
        dp.prof.accel_y.exponent_classic = 2.0;
        dp.prof.accel_x.input_offset = 0;
        dp.prof.accel_y.input_offset = 0;
        dp.prof.accel_x.limit = 1.6;
        dp.prof.accel_y.limit = 1.6;
        dp.prof.accel_x.cap = { 18.0, 1.6 };
        dp.prof.accel_y.cap = { 18.0, 1.6 };
        dp.prof.accel_x.cap_mode_val = cap_mode::out;
        dp.prof.accel_y.cap_mode_val = cap_mode::out;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "valorant") {
        // Valorant (TenZ-inspired base): natural curve for smooth entry/exit,
        // modest gain, high cap so panic flicks stay controlled but fast.
        dp.prof.accel_x.mode = accel_mode::natural;
        dp.prof.accel_y.mode = accel_mode::natural;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.limit = 1.3;
        dp.prof.accel_y.limit = 1.3;
        dp.prof.accel_x.decay_rate = 0.08;
        dp.prof.accel_y.decay_rate = 0.08;
        dp.prof.accel_x.motivity = 1.2;
        dp.prof.accel_y.motivity = 1.2;
        dp.prof.accel_x.input_offset = 0.02;
        dp.prof.accel_y.input_offset = 0.02;
        dp.prof.accel_x.cap = { 30.0, 2.0 };
        dp.prof.accel_y.cap = { 30.0, 2.0 };
        dp.prof.accel_x.cap_mode_val = cap_mode::out;
        dp.prof.accel_y.cap_mode_val = cap_mode::out;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "apex") {
        // Apex Legends: tracking-heavy + verticality. Power mode ramps fast for
        // 180° flicks while light smoothing keeps track. output_offset = 1.0
        // (PRE-3): the old 0.9 plateau made gain 0.900 below 0.191 ips — i.e.
        // the whole 2-10 mm/s micro-aim band stayed sub-1:1, contradicting the
        // comment's "avoid sub-1:1 muddy feel" intent.  A 1.0 plateau is a true
        // 1:1 base; accel lifts it above 1.0 only through the power curve.
        dp.prof.accel_x.mode = accel_mode::power;
        dp.prof.accel_y.mode = accel_mode::power;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.scale = 2.2;
        dp.prof.accel_y.scale = 2.2;
        dp.prof.accel_x.exponent_power = 0.8;
        dp.prof.accel_y.exponent_power = 0.8;
        dp.prof.accel_x.input_offset = 0.02;
        dp.prof.accel_y.input_offset = 0.02;
        dp.prof.accel_x.output_offset = 1.0;
        dp.prof.accel_y.output_offset = 1.0;
        // O31 (K2): `limit` burada HIC ATANMAMIS birakiliyordu, yani struct
        // varsayilani 1.5 config'e yaziliyordu — ama fiilen baglayan cap.y
        // 2.2.  Olcum (8 presetin tamami, 0.01..3000 ips taramasi):
        //   apex  limit=1.50  cap.y=2.20  gercek azami kazanc 2.1998  → %46.7 sapma
        // diger presetler bu konvansiyonu t utuyor:
        //   gaming    limit 1.8 / cap.y 1.8 · precision 1.2 / 1.2
        //   cs2       limit 1.6 / cap.y 1.6 · fps     1.8 / 1.8
        //   valorant  limit 1.3 / cap.y 2.0 (limitin USTUNE bilerek cikiyor)
        // yani apex tek ihlal eden.
        // Calisma zamani etkisi YOK: `limit` yalnizca accel-natural.hpp tarafindan
        // okunuyor (accel-power.hpp ve accel-classic.hpp icinde hic gecmiyor,
        // olculdu), power modu cap.y'a bagli. Bu yuzden bu bir davranis hatasi
        // degil, VERI BUTUNLUGU hatasi: yanlis sayi JSON'a yaziliyor ve
        // gui/profile_mgr.inl:98'de `tr("limit")` ile KULLANICIYA gorunuyor.
        // Ayni sekilde: accel-classic.hpp'de de limit okunmadigi icin
        // gaming/precision/cs2/fps'in limit degeri de bildirimsel.
        dp.prof.accel_x.limit = 2.2;
        dp.prof.accel_y.limit = 2.2;
        dp.prof.accel_x.cap = { 28.0, 2.2 };
        dp.prof.accel_y.cap = { 28.0, 2.2 };
        dp.prof.accel_x.cap_mode_val = cap_mode::out;
        dp.prof.accel_y.cap_mode_val = cap_mode::out;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "fps") {
        // Generic FPS: balanced classic curve, moderate acceleration and cap.
        // Safe starting point for most shooters / aim trainers.
        dp.prof.accel_x.mode = accel_mode::classic;
        dp.prof.accel_y.mode = accel_mode::classic;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.acceleration = 0.005;
        dp.prof.accel_y.acceleration = 0.005;
        dp.prof.accel_x.exponent_classic = 2.0;
        dp.prof.accel_y.exponent_classic = 2.0;
        dp.prof.accel_x.input_offset = 0.01;
        dp.prof.accel_y.input_offset = 0.01;
        dp.prof.accel_x.limit = 1.8;
        dp.prof.accel_y.limit = 1.8;
        dp.prof.accel_x.cap = { 20.0, 1.8 };
        dp.prof.accel_y.cap = { 20.0, 1.8 };
        dp.prof.accel_x.cap_mode_val = cap_mode::out;
        dp.prof.accel_y.cap_mode_val = cap_mode::out;
        dp.prof.output_dpi = 1000;
    } else {
        dp.name.clear(); // signal unknown preset
    }
    return dp;
}

} // namespace rawaccel
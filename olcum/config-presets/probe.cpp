// AJ4 · config-presets ölçüm probu (ölçüm aracı — üretim kodu DEĞİL).
//
// Üç soruyu yanıtlar (AJ1 görev metni):
//   1) round-trip : serialize → deserialize → serialize, İKİNCİ JSON birincisiyle
//                   bayt-bayt eşit mi?
//   2) preset     : make_preset() değerleri tur sonunda aynı kalıyor mu?
//   3) migration  : bilinmeyen alan / eksik alan / yanlış tip / bozuk JSON
//                   çökmeden geçiyor mu?
//
// Protokol (sürücü = olcum/arsiv/driver.py):
//   probe list                 -> satır başına bir vaka adı
//   probe <vaka> [arg]         -> stdout'a "RESULT <vaka> PASS|FAIL <ayrinti>"
//                                 exit 0 = vaka tamamlandı (FAIL bile olsa),
//                                 sinyalle ölür = ÇÖKÜŞ (sürücü işaretler).
//
// /tmp/opencode kuralı (PROGRAM_SOZLESMESI §0.3): dosyalar her zaman
// HEMEN ÖNCE varlık + boyut ile doğrulanır; yoksa ölçüm çalışmamış sayılır.

#include "config.hpp"
#include "presets.hpp"
#include "nlohmann/json.hpp"

#include <cstdio>
#include <cstring>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <map>
#include <unistd.h>
#include <sys/stat.h>

using json = nlohmann::json;
namespace rawaccel {

static int g_fail = 0;

static void report(const std::string& name, bool ok, const std::string& detail) {
    std::printf("RESULT %s %s %s\n", name.c_str(), ok ? "PASS" : "FAIL",
                detail.c_str());
    if (!ok) g_fail++;
}

// ── bayt karşılaştırıcı ───────────────────────────────────────────────────────
// /tmp/opencode kuralı: varlık + boyut doğrulaması burada, ölçümün içinde.
static bool byte_diff(const std::string& a, const std::string& b,
                      std::string& out) {
    if (a == "\x01UNREADABLE" || b == "\x01UNREADABLE") {
        out = "DOSYA OKUNAMADI — ölçüm çalışmadı";
        return false;
    }
    if (a == b) { out = "0 bayt fark"; return true; }
    size_t i = 0;
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    while (i < n && a[i] == b[i]) i++;
    auto ctx = [](const std::string& s, size_t pos) {
        size_t lo = pos > 24 ? pos - 24 : 0;
        size_t hi = pos + 24 < s.size() ? pos + 24 : s.size();
        return s.substr(lo, hi - lo);
    };
    out = "ilk fark bayt " + std::to_string(i) +
          " | uzunluk " + std::to_string(a.size()) + " vs " + std::to_string(b.size()) +
          " | A: ..." + ctx(a, i) + "... | B: ..." + ctx(b, i) + "...";
    return false;
}

static std::string slurp(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) { ok = false; return "\x01UNREADABLE"; }
    std::ostringstream ss;
    ss << f.rdbuf();
    ok = true;
    return ss.str();
}

// ── alan karşılaştırıcısı ─────────────────────────────────────────────────────
// "Kaybettiğin her alan bir kusurdur" — bayt farkını ALAN adıyla raporlar.
static void dbeq(std::string& out, const char* k, double a, double b) {
    if (a != b)
        out += std::string(k) + "=" + std::to_string(a) + "->" +
               std::to_string(b) + "; ";
}
static void dstr(std::string& out, const char* k, const std::string& a,
                 const std::string& b) {
    if (a != b)
        out += std::string(k) + "='" + a + "'->'" + b + "'; ";
}
static void dint(std::string& out, const char* k, long long a, long long b) {
    if (a != b)
        out += std::string(k) + "=" + std::to_string(a) + "->" +
               std::to_string(b) + "; ";
}
static void dbool(std::string& out, const char* k, bool a, bool b) {
    if (a != b)
        out += std::string(k) + "=" + (a ? "true" : "false") + "->" +
               (b ? "true" : "false") + "; ";
}

static void diff_accel(std::string& o, const char* pfx, const accel_args& a,
                       const accel_args& b) {
    auto k = [&](const char* s) { return std::string(pfx) + "." + s; };
    if (a.mode != b.mode) o += k("mode") + " degisti; ";
    dbool(o, k("gain").c_str(), a.gain, b.gain);
    dbeq(o, k("input_offset").c_str(), a.input_offset, b.input_offset);
    dbeq(o, k("output_offset").c_str(), a.output_offset, b.output_offset);
    dbeq(o, k("acceleration").c_str(), a.acceleration, b.acceleration);
    dbeq(o, k("decay_rate").c_str(), a.decay_rate, b.decay_rate);
    dbeq(o, k("gamma").c_str(), a.gamma, b.gamma);
    dbeq(o, k("motivity").c_str(), a.motivity, b.motivity);
    dbeq(o, k("exponent_classic").c_str(), a.exponent_classic, b.exponent_classic);
    dbeq(o, k("scale").c_str(), a.scale, b.scale);
    dbeq(o, k("exponent_power").c_str(), a.exponent_power, b.exponent_power);
    dbeq(o, k("limit").c_str(), a.limit, b.limit);
    dbeq(o, k("sync_speed").c_str(), a.sync_speed, b.sync_speed);
    dbeq(o, k("smooth").c_str(), a.smooth, b.smooth);
    dbeq(o, k("cap.x").c_str(), a.cap.x, b.cap.x);
    dbeq(o, k("cap.y").c_str(), a.cap.y, b.cap.y);
    if (a.cap_mode_val != b.cap_mode_val) o += k("cap_mode") + " degisti; ";
    dint(o, k("length").c_str(), a.length, b.length);
    int n = a.length < b.length ? a.length : b.length;
    for (int i = 0; i < n; i++)
        if (a.data[i] != b.data[i])
            o += k("data") + "[" + std::to_string(i) + "] " +
                 std::to_string(a.data[i]) + "->" + std::to_string(b.data[i]) + "; ";
}

static void diff_dp(std::string& o, const device_profile& a,
                    const device_profile& b) {
    dstr(o, "device_id", a.device_id, b.device_id);
    dstr(o, "name", a.name, b.name);
    dstr(o, "match_app", a.match_app, b.match_app);
    dint(o, "dpi", a.dev_cfg.dpi, b.dev_cfg.dpi);
    dint(o, "polling_rate", a.dev_cfg.polling_rate, b.dev_cfg.polling_rate);
    dbool(o, "disable", a.dev_cfg.disable, b.dev_cfg.disable);
    dbool(o, "raw_passthrough", a.prof.raw_passthrough, b.prof.raw_passthrough);
    dbeq(o, "output_dpi", a.prof.output_dpi, b.prof.output_dpi);
    dbeq(o, "yx_output_dpi_ratio", a.prof.yx_output_dpi_ratio, b.prof.yx_output_dpi_ratio);
    dbeq(o, "lr_output_dpi_ratio", a.prof.lr_output_dpi_ratio, b.prof.lr_output_dpi_ratio);
    dbeq(o, "ud_output_dpi_ratio", a.prof.ud_output_dpi_ratio, b.prof.ud_output_dpi_ratio);
    dbeq(o, "degrees_rotation", a.prof.degrees_rotation, b.prof.degrees_rotation);
    dbeq(o, "degrees_snap", a.prof.degrees_snap, b.prof.degrees_snap);
    dbeq(o, "speed_min", a.prof.speed_min, b.prof.speed_min);
    dbeq(o, "speed_max", a.prof.speed_max, b.prof.speed_max);
    dbeq(o, "domain_weights.x", a.prof.domain_weights.x, b.prof.domain_weights.x);
    dbeq(o, "domain_weights.y", a.prof.domain_weights.y, b.prof.domain_weights.y);
    dbeq(o, "range_weights.x", a.prof.range_weights.x, b.prof.range_weights.x);
    dbeq(o, "range_weights.y", a.prof.range_weights.y, b.prof.range_weights.y);
    dbool(o, "speed_processor.whole", a.prof.speed_processor_args.whole,
          b.prof.speed_processor_args.whole);
    dbeq(o, "speed_processor.lp_norm", a.prof.speed_processor_args.lp_norm,
         b.prof.speed_processor_args.lp_norm);
    dbeq(o, "speed_processor.input_hl",
         a.prof.speed_processor_args.input_speed_smooth_halflife,
         b.prof.speed_processor_args.input_speed_smooth_halflife);
    dbeq(o, "speed_processor.scale_hl",
         a.prof.speed_processor_args.scale_smooth_halflife,
         b.prof.speed_processor_args.scale_smooth_halflife);
    dbeq(o, "speed_processor.output_hl",
         b.prof.speed_processor_args.output_speed_smooth_halflife,
         a.prof.speed_processor_args.output_speed_smooth_halflife);
    diff_accel(o, "accel_x", a.prof.accel_x, b.prof.accel_x);
    diff_accel(o, "accel_y", a.prof.accel_y, b.prof.accel_y);
}

// ── 1) round-trip: profil ─────────────────────────────────────────────────────
static void case_rt_preset(const std::string& name) {
    device_profile dp = make_preset(name, "olcum");
    const std::string j1 = profile_to_json(dp);
    device_profile dp2 = profile_from_json(j1);
    const std::string j2 = profile_to_json(dp2);
    std::string d;
    bool ok = byte_diff(j1, j2, d);
    std::string f;
    diff_dp(f, dp, dp2);
    if (!f.empty()) { ok = false; d += " | ALAN: " + f; }
    report("rt-preset-" + name, ok, d);
}

// ── 2) preset değer sadakati ──────────────────────────────────────────────────
static void case_preset(const std::string& name) {
    device_profile dp = make_preset(name, "olcum");
    device_profile dp2 = profile_from_json(profile_to_json(dp));
    std::string f;
    diff_dp(f, dp, dp2);
    report("preset-" + name, f.empty(), f.empty() ? "tum alanlar ayni" : f);
}

// ── 1b) dolu bir app_config: dosya üzerinden (save→load→save) ────────────────
static app_config build_rich() {
    app_config cfg;
    cfg.version.clear();
    cfg.active_profile = "gaming";
    cfg.use_raw_input = false;
    for (int i = 0; i < PRESET_COUNT; i++) {
        device_profile dp = make_preset(PRESET_NAMES[i], PRESET_NAMES[i]);
        dp.device_id = std::string("/dev/input/by-id/usb-rich-") + PRESET_NAMES[i];
        dp.match_app = (i % 2) ? std::string("firefox") : std::string();
        dp.dev_cfg.dpi = 1200 + i;
        dp.dev_cfg.polling_rate = 500 + 125 * i;
        dp.dev_cfg.disable = (i == 3);
        // sanitize zarfı DIŞINDA bir değer bilerek konmaz: bu vaka
        // serileştiricinin sadakatini ölçer, sanitize'i değil.
        dp.prof.degrees_rotation = 45.0;
        dp.prof.degrees_snap = 30.0;
        dp.prof.speed_min = 0.5;
        dp.prof.speed_max = 50.0;
        dp.prof.lr_output_dpi_ratio = 1.25;
        dp.prof.ud_output_dpi_ratio = 0.75;
        dp.prof.yx_output_dpi_ratio = 1.5;
        dp.prof.domain_weights = { 0.9, 1.1 };
        dp.prof.range_weights = { 1.05, 0.95 };
        dp.prof.speed_processor_args.whole = (i % 2) == 0;
        dp.prof.speed_processor_args.lp_norm = 1.5;
        dp.prof.speed_processor_args.input_speed_smooth_halflife = 12.5;
        dp.prof.speed_processor_args.scale_smooth_halflife = 7.25;
        dp.prof.speed_processor_args.output_speed_smooth_halflife = 3.5;
        cfg.profiles.push_back(dp);
    }
    // bilinçli olarak HİÇ sanitize edilmedi — ilk kayıt ile ikinci kayıt
    // arasındaki fark "serialize mı, sanitize mi" sorusunu ayırır.
    return cfg;
}

static void case_rt_app_file() {
    const std::string dir = "/tmp/opencode/aj4-probe-" + std::to_string(::getpid());
    ::mkdir("/tmp/opencode", 0755);
    ::mkdir(dir.c_str(), 0755);
    const std::string f1 = dir + "/a.json";
    const std::string f2 = dir + "/b.json";

    app_config cfg = build_rich();
    save_config(cfg, f1);
    app_config cfg2 = load_config(f1);
    save_config(cfg2, f2);

    bool ok1 = false, ok2 = false;
    const std::string s1 = slurp(f1, ok1);
    const std::string s2 = slurp(f2, ok2);
    // /tmp/opencode kuralı: dosya var mı, boş mu?
    if (!ok1 || !ok2 || s1.size() == 0 || s2.size() == 0) {
        report("rt-app-file", false,
               "DOSYA YOK/BOŞ — ölçüm çalışmadı (a=" +
               std::to_string(s1.size()) + " b=" + std::to_string(s2.size()) + ")");
        return;
    }
    std::string d;
    bool ok = byte_diff(s1, s2, d);
    if (cfg2.profiles.size() != cfg.profiles.size()) {
        ok = false;
        d += " | profil sayisi " + std::to_string(cfg.profiles.size()) +
             "->" + std::to_string(cfg2.profiles.size());
    }
    for (size_t i = 0; i < cfg.profiles.size() && i < cfg2.profiles.size(); i++) {
        std::string f;
        diff_dp(f, cfg.profiles[i], cfg2.profiles[i]);
        if (!f.empty()) { ok = false; d += " | [" + std::to_string(i) + "] " + f; }
    }
    d += " | satir_a=" + std::to_string(1 + std::count(s1.begin(), s1.end(), '\n'));
    d += " satir_b=" + std::to_string(1 + std::count(s2.begin(), s2.end(), '\n'));
    report("rt-app-file", ok, d);
}

// 1b-2) aynı, ama HAFIZA üzerinden (IPC config-push yolu): app_config_to_json /
// app_config_from_json çifti.
static void case_rt_app_mem() {
    app_config cfg = build_rich();
    const std::string j1 = app_config_to_json(cfg);
    app_config cfg2 = app_config_from_json(j1);
    const std::string j2 = app_config_to_json(cfg2);
    std::string d;
    bool ok = byte_diff(j1, j2, d);
    if (cfg2.profiles.size() != cfg.profiles.size()) {
        ok = false;
        d += " | profil sayisi " + std::to_string(cfg.profiles.size()) +
             "->" + std::to_string(cfg2.profiles.size());
    }
    report("rt-app-mem", ok, d);
}

// ── 1c) gerçek bir config dosyası üzerinden ───────────────────────────────────
// İki AYRI soru:
//   (a) KARARLILIK: ilk yazım A, A'dan yükleyip ikinci yazım B → A == B mi?
//       (kullanıcının dosyası başka bir araçtan gelmiş olabilir; bu durumda
//       orig ≠ A'dır ama bu KAYIP değildir — yeniden biçimlendirme.)
//   (b) KAYIP: orig ↔ A semantik farkı — hangi ANAHTAR düşüyor/ekleniyor?
static void json_diff(const json& a, const json& b, const std::string& p,
                      std::string& o) {
    if (a.type() != b.type()) {
        o += p + ": tip " + a.type_name() + "->" + b.type_name() + "; ";
        return;
    }
    if (a.is_object()) {
        for (const auto& kv : a.items())
            if (!b.contains(kv.key())) o += p + "." + kv.key() + " DUSTU; ";
        for (const auto& kv : b.items())
            if (!a.contains(kv.key())) o += p + "." + kv.key() + " EKLENDI; ";
        for (const auto& kv : a.items())
            if (b.contains(kv.key())) json_diff(kv.value(), b[kv.key()],
                                                p + "." + kv.key(), o);
    } else if (a.is_array()) {
        if (a.size() != b.size())
            o += p + ": boyut " + std::to_string(a.size()) + "->" +
                 std::to_string(b.size()) + "; ";
        size_t n = a.size() < b.size() ? a.size() : b.size();
        for (size_t i = 0; i < n; i++)
            json_diff(a[i], b[i], p + "[" + std::to_string(i) + "]", o);
    } else if (a != b) {
        o += p + ": " + a.dump() + "->" + b.dump() + "; ";
    }
}

static void case_rt_file(const std::string& path) {
    bool ok0 = false;
    const std::string orig = slurp(path, ok0);
    if (!ok0 || orig.size() == 0) {
        report("rt-file", false, "KAYNAK DOSYA YOK/BOŞ: " + path);
        return;
    }
    const std::string dir = "/tmp/opencode/aj4-probe-" + std::to_string(::getpid());
    ::mkdir("/tmp/opencode", 0755);
    ::mkdir(dir.c_str(), 0755);
    const std::string fa = dir + "/a.json";
    const std::string fb = dir + "/b.json";
    std::string A, B;
    try {
        save_config(load_config(path), fa);   // ilk yazım
        save_config(load_config(fa), fb);     // ikinci yazım
    } catch (const std::exception& e) {
        report("rt-file", false, std::string("YUKLEME REDDEDİLDİ: ") + e.what());
        return;
    }
    bool oka = false, okb = false;
    A = slurp(fa, oka);
    B = slurp(fb, okb);
    // §0.3: dosya var mı, boş mu?  Yoksa "0 fark" SAhte sıfırdır.
    if (!oka || !okb || A.size() == 0 || B.size() == 0) {
        report("rt-file", false, "DOSYA YOK/BOŞ — ölçüm çalışmadı (A=" +
               std::to_string(A.size()) + " B=" + std::to_string(B.size()) + ")");
        return;
    }
    std::string dk;
    bool kararli = byte_diff(A, B, dk);
    std::string sem;
    try {
        json ja = json::parse(orig), jb = json::parse(A);
        json_diff(ja, jb, "$", sem);
    } catch (const std::exception&) {
        sem = "kaynak AYRIŞTIRILAMADI (JSON değil)";
    }
    std::string d = "KARARLILIK(A==B): " + dk +
                    " | ilk-yazim orig==A: " + (orig == A ? "bayt-bayt esit" : "FARKLI") +
                    " | satir_orig=" + std::to_string(std::count(orig.begin(), orig.end(), '\n')) +
                    " satir_A=" + std::to_string(std::count(A.begin(), A.end(), '\n'));
    if (!sem.empty()) d += " | SEMANTIK: " + sem;
    report("rt-file", kararli, d);
}

// ── 1d) 0 ile "yok" ayrımı + bool ayrımı ─────────────────────────────────────
static void case_rt_zero_vs_missing() {
    // explicit 0 / false / "" değerleri default'a ÇÖKMEMELİ.
    device_profile dp;
    dp.name = "zeros";
    dp.device_id = "";
    dp.match_app = "";
    dp.dev_cfg.dpi = 1;
          dp.dev_cfg.polling_rate = 125;
    dp.dev_cfg.disable = false;
    dp.prof.name[0] = '\0';
    dp.prof.raw_passthrough = false;
    dp.prof.output_dpi = 0;          // 0 = "normalize etme" sentineli
    dp.prof.speed_min = 0;
    dp.prof.speed_max = 0;
    dp.prof.degrees_rotation = 0;
    dp.prof.accel_x.gain = false;
    dp.prof.accel_x.limit = 0;
    dp.prof.accel_x.acceleration = 0;
    dp.prof.accel_x.cap = { 0, 0 };
    dp.prof.accel_x.mode = accel_mode::noaccel;
    const std::string j1 = profile_to_json(dp);
    device_profile dp2 = profile_from_json(j1);
    const std::string j2 = profile_to_json(dp2);
    std::string d;
    bool ok = byte_diff(j1, j2, d);
    std::string f;
    diff_dp(f, dp, dp2);
    if (!f.empty()) { ok = false; d += " | ALAN: " + f; }
    report("rt-zero-vs-missing", ok, d);
}

// ── 1e) LUT verisi turu ───────────────────────────────────────────────────────
static void case_rt_lut() {
    device_profile dp;
    dp.name = "lut";
    dp.prof.accel_x.mode = accel_mode::lookup;
    dp.prof.accel_y.mode = accel_mode::lookup;
    dp.prof.accel_x.length = 8;
    dp.prof.accel_y.length = 8;
    const float pts[8] = { 0.0f, 1.0f, 1.5f, 1.75f, 3.25f, 2.5f, 10.0f, 4.0f };
    for (int i = 0; i < 8; i++) { dp.prof.accel_x.data[i] = pts[i]; dp.prof.accel_y.data[i] = pts[i]; }
    const std::string j1 = profile_to_json(dp);
    device_profile dp2 = profile_from_json(j1);
    const std::string j2 = profile_to_json(dp2);
    std::string d;
    bool ok = byte_diff(j1, j2, d);
    std::string f;
    diff_dp(f, dp, dp2);
    if (!f.empty()) { ok = false; d += " | ALAN: " + f; }
    report("rt-lut", ok, d);
}

// ── 1f) sanitize zarfı DIŞINDA bir profil: clamp + İDEMPOTANS ────────────────
// İlk tur değerleri kısabilir (bu belgeli tasarım).  asıl soru: clamp edilmiş
// değer İKİNCİ turda da aynı mı?  Değilse her kayıt/yükleme döngüsü değer
// kaydırır (sürekli göç).
static void case_rt_clamp() {
    device_profile dp;
    dp.name = "clamp";
    dp.dev_cfg.dpi = 999999;
    dp.dev_cfg.polling_rate = 1;
    dp.prof.degrees_rotation = 400.0;
    dp.prof.degrees_snap = 90.0;
    dp.prof.output_dpi = 0.5;
    dp.prof.speed_min = 10.0;
    dp.prof.speed_max = 1.0;
    dp.prof.lr_output_dpi_ratio = 500.0;
    dp.prof.accel_x.exponent_classic = 0.2;
    dp.prof.accel_x.scale = -5.0;
    dp.prof.accel_x.sync_speed = 0.0;
    dp.prof.accel_x.limit = -3.0;
    dp.prof.accel_x.cap = { -1.0, -2.0 };
    dp.prof.accel_x.motivity = -1.0;

    const std::string j1 = profile_to_json(dp);
    device_profile dp2 = profile_from_json(j1);
    const std::string j2 = profile_to_json(dp2);
    device_profile dp3 = profile_from_json(j2);
    const std::string j3 = profile_to_json(dp3);

    std::string f;
    diff_dp(f, dp, dp2);                     // ilk turun neyi değiştirdiği
    std::string dk;
    bool idem = byte_diff(j2, j3, dk);       // clamp stabil mi?
    std::string d = "idempotans(2. tur vs 3. tur): " + dk;
    d += " | ilk turda degisen alanlar: " + (f.empty() ? "YOK" : f);
    report("rt-clamp", idem, d);
}

// ── 1g) sanitize ÜST sınırı: `limit` (AJ4-K6) ────────────────────────────────
// `limit`, P120-FAZ2 bloğunun TEK üst sınırsız gain alanıyordu (config.cpp'deki
// tek sınır `limit < 0 → 0` idi), oysa onu gösteren gauge
// gui/ui_builder.inl:241 ve :411'de make_spin(0, 100, ...) — yani 1e6 elle
// girilen bir değer daemon'da 1e6 olarak çalışırken GUI yanında 100 gösteriyordu.
//
// Burada ÜÇ şey ölçülür:
//   (a) 1e6 gerçekten LIMIT_MAX'e iniyor mu            → sınır gerçekten bağlı mı
//   (b) tam SINIR değeri 100.0 hiç oynamıyor mu        → R15 round-trip testi
//       (tests/test_accel.cpp:7869/7914) 100.0'ın byte olarak korunmasını
//       şart koşuyor; sınır bunu kırarsa test kırılır
//   (c) clamp İDEMPOTANT mı                           → her kayıt/yükleme
//       döngüsü değeri kaydırmasın
static void case_lim_oversize() {
    auto fmt = [](double v) {
        char b[40];
        std::snprintf(b, sizeof b, "%.17g", v);
        return std::string(b);
    };
    device_profile dp;
    dp.name = "lim";
    dp.prof.accel_x.mode = accel_mode::natural;
    dp.prof.accel_y = dp.prof.accel_x;
    dp.prof.accel_x.limit = 1e6;             // gauge'in üstünde
    dp.prof.accel_y.limit   = 1e6;

    const std::string j1 = profile_to_json(dp);
    device_profile dp2 = profile_from_json(j1);
    const std::string j2 = profile_to_json(dp2);
    device_profile dp3 = profile_from_json(j2);
    const std::string j3 = profile_to_json(dp3);

    std::string d;
    bool ok = true;

    // (a) üst sınır gerçekten uygulanıyor mu
    if (dp2.prof.accel_x.limit != LIMIT_MAX || dp2.prof.accel_y.limit != LIMIT_MAX) {
        ok = false;
        d += "(a) 1e6 -> x=" + fmt(dp2.prof.accel_x.limit)
           + " y=" + fmt(dp2.prof.accel_y.limit)
           + " (beklenen " + fmt(LIMIT_MAX) + ") | ";
    } else {
        d += "(a) 1e6 -> " + fmt(dp2.prof.accel_x.limit) + " | ";
    }

    // (b) sınır değeri 100.0 KORUNUYOR MU (R15 bayt-sadakat sözü)
    device_profile bd = dp2;
    bd.prof.accel_x.limit = LIMIT_MAX;
    bd.prof.accel_y.limit   = LIMIT_MAX;
    device_profile bd2 = profile_from_json(profile_to_json(bd));
    if (bd2.prof.accel_x.limit != LIMIT_MAX || bd2.prof.accel_y.limit != LIMIT_MAX) {
        ok = false;
        d += "(b) SINIR " + fmt(LIMIT_MAX) + " -> x=" + fmt(bd2.prof.accel_x.limit)
           + " y=" + fmt(bd2.prof.accel_y.limit) + " (korunmali) | ";
    } else {
        d += "(b) sinir " + fmt(LIMIT_MAX) + " korundu | ";
    }

    // (c) idempotans
    std::string dk;
    if (!byte_diff(j2, j3, dk)) {
        ok = false;
        d += "(c) IDEMPOTANS DEGIL: " + dk;
    } else {
        d += "(c) idempotans: " + dk;
    }

    report("lim-oversize", ok, d);
}

// ── 2b) preset değer tablosu + VAAT TUTARLILIGI ──────────────────────────────
// Görev sınırı: ivme matematiği AJ2 hattında; buradan cap'in GERÇTEĞEN nerede
// kırpıldığı ÖLÇÜLEMEZ.  Ölçülebilir olan: preset'in beyan ettiği iki sayının
// (limit ↔ cap.y) kendi içinde tutarlı olup olmadığı ve bunların JSON'a
// yazılıp geri okunduğunda aynı kalıp kalmadığı.
//
// presets.hpp'in kendi yorumları (C-5 / O31 / K2) bu eşleşmeyi KORUNMASI
// gereken bir konvansiyon olarak tanımlar: classic ve power modda etkili
// asimptot cap.y'dir, `limit` ise yalnız natural modda okunur — o yüzden
// classic/power'de limit == cap.y olmalı.  natural'da limit okunur, cap.y
// ayrıca bağlayıcıdır ve ofis/valorant FARKLI davranır (bilgi amaçlı).
static void case_values(const std::string& name) {
    // config.cpp'deki mode_to_str/cap_to_str static ve başlıkta yok — probun
    // kendi kopyası (iç bağlantı, çakışma yok).
    auto mode_s = [](accel_mode m) -> const char* {
        switch (m) {
        case accel_mode::classic:     return "classic";
        case accel_mode::power:       return "power";
        case accel_mode::natural:     return "natural";
        case accel_mode::jump:        return "jump";
        case accel_mode::synchronous: return "synchronous";
        case accel_mode::lookup:      return "lookup";
        default:                      return "noaccel";
        }
    };
    auto cap_s = [](cap_mode c) -> const char* {
        return c == cap_mode::io ? "io" : (c == cap_mode::in ? "in" : "out");
    };
    device_profile dp = make_preset(name, "olcum");
    const accel_args& a = dp.prof.accel_x;
    const accel_args& y = dp.prof.accel_y;
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "mode=%s gain=%d limit=%.6g cap=(%.6g,%.6g) cap_mode=%s "
                  "acc=%.6g expC=%.6g scale=%.6g expP=%.6g decay=%.6g mot=%.6g "
                  "in_off=%.6g out_off=%.6g raw=%d out_dpi=%.6g",
                  mode_s(a.mode), a.gain ? 1 : 0, a.limit,
                  a.cap.x, a.cap.y, cap_s(a.cap_mode_val),
                  a.acceleration, a.exponent_classic, a.scale, a.exponent_power,
                  a.decay_rate, a.motivity, a.input_offset, a.output_offset,
                  dp.prof.raw_passthrough ? 1 : 0, dp.prof.output_dpi);
    std::string det(buf);

    // X/Y simetri ihlali?  (preset her iksini de aynı kurmalı)
    bool simetrik = (a.mode == y.mode && a.gain == y.gain &&
                     a.limit == y.limit && a.cap.x == y.cap.x &&
                     a.cap.y == y.cap.y && a.acceleration == y.acceleration &&
                     a.exponent_classic == y.exponent_classic &&
                     a.scale == y.scale && a.exponent_power == y.exponent_power &&
                     a.decay_rate == y.decay_rate && a.motivity == y.motivity &&
                     a.input_offset == y.input_offset &&
                     a.output_offset == y.output_offset);
    bool ok = simetrik;
    if (!simetrik) det += " | X/Y ASIMETRI";

    // VAAT TUTARLILIGI (sadece classic/power — natural'da limit ayrıca bağlayıcı)
    if (a.mode == accel_mode::classic || a.mode == accel_mode::power) {
        if (a.limit != a.cap.y) {
            ok = false;
            det += " | VAAT SAPMASI: limit != cap.y (limit=" +
                   std::to_string(a.limit) + " cap.y=" + std::to_string(a.cap.y) + ")";
        } else {
            det += " | vaat: limit==cap.y ✓";
        }
        // cap_mode açıkça `out` olmalı mı?  (konvansiyon: asimptot cap.y)
        if (a.cap_mode_val != cap_mode::out) {
            det += " | NOT: cap_mode=out degil";
        }
    } else if (a.mode == accel_mode::natural) {
        det += " | natural: limit okunur, cap.y ayri baglayici (bilgi)";
    } else {
        det += std::string(" | mod=") + mode_s(a.mode) +
               " (limit/cap vaadi yok)";
    }
    report("values-" + name, ok, det);
}

// ── 3) migration dayanıklılığı ────────────────────────────────────────────────
// Beklenti: YA düz yüklensir YA std::exception fırlatsın. ÇÖKÜŞ = FAIL.
// (Çöküş sinyalle olduğu için bu fonksiyona hiç dönmez; sürücü işaretler.)
static void expect_throw(const std::string& name, const std::string& js,
                         bool must_throw) {
    try {
        app_config cfg = app_config_from_json(js);
        if (must_throw) {
            report(name, false, "Beklenen RED gelmedi — sessizce yüklendi "
                                "(profiles=" + std::to_string(cfg.profiles.size()) + ")");
        } else {
            report(name, true, "yüklendi (profiles=" +
                               std::to_string(cfg.profiles.size()) +
                               ", version='" + cfg.version + "')");
        }
    } catch (const std::exception& e) {
        if (must_throw)
            report(name, true, std::string("RED: ") + e.what());
        else
            report(name, false, std::string("BEKLENMEYEN RED: ") + e.what());
    }
}

static const char* BASE =
    R"({"version":"0.9.0","active_profile":"default","use_raw_input":true,"profiles":[)"
    R"({"name":"default","device_id":"","dpi":800,"polling_rate":1000,"disable":false,)"
    R"("profile":{"name":"default","raw_passthrough":false,)"
    R"("accel_x":{"mode":"classic","gain":true,"acceleration":0.005,)"
    R"("exponent_classic":2.0,"limit":1.8,"cap":[15,1.8],"cap_mode":"out"},)"
    R"("accel_y":{"mode":"classic","gain":true,"acceleration":0.005,)"
    R"("exponent_classic":2.0,"limit":1.8,"cap":[15,1.8],"cap_mode":"out"}}}]})";

static void mig_unknown() {
    // bilinmeyen alanlar: kök, profil, accel_args
    json j = json::parse(BASE);
    j["gelecekten_gelen_alan"] = { "a", 1, true };
    j["profiles"][0]["gelecekten_profil"] = 42;
    j["profiles"][0]["profile"]["accel_x"]["gelecekten_accel"] = "x";
    try {
        app_config cfg = app_config_from_json(j.dump());
        app_config_to_json(cfg); // tekrar yaz
        // bilinmeyen alanlar düşer mi? (bilinen davranış: düşer — raporla)
        json j2 = json::parse(app_config_to_json(cfg));
        bool root_gone = !j2.contains("gelecekten_gelen_alan");
        bool prof_gone = !j2["profiles"][0].contains("gelecekten_profil");
        bool acc_gone  = !j2["profiles"][0]["profile"]["accel_x"].contains("gelecekten_accel");
        std::string d = std::string("yuklendi; bilinmeyen alanlar: kok=") +
                        (root_gone ? "DUSTU" : "saklandi") +
                        " profil=" + (prof_gone ? "DUSTU" : "saklandi") +
                        " accel=" + (acc_gone ? "DUSTU" : "saklandi");
        report("mig-unknown", true, d);
    } catch (const std::exception& e) {
        report("mig-unknown", false, std::string("BEKLENMEYEN RED: ") + e.what());
    }
}

static void mig_missing_case(const std::string& which) {
    if (which == "mig-missing")
        expect_throw("mig-missing", R"({})", false);
    else if (which == "mig-missing-profile")
        expect_throw("mig-missing-profile",
                     R"({"version":"0.9.0","profiles":[{"name":"x","dpi":800}]})", false);
    else if (which == "mig-missing-all")
        expect_throw("mig-missing-all", R"({"profiles":[]})", false);
    else
        report(which, false, "bilinmeyen vaka");
}

static void mig_type_case_impl(const std::string& which);

static void mig_type_case(const std::string& which) {
    // Hazırlık da ölçümün parçası: bir değeri hazırlarken fırlatılan istisna
    // VAKANIN sonucudur, çöküş DEĞİL.  (İlk ölçümde `json::parse("1e999")`
    // burada taşma fırlatıp probe'u SIGABRT ile öldürmüştü — ölçüm aracı
    // hatası, §7 kaydı.)
    try {
        mig_type_case_impl(which);
    } catch (const std::exception& e) {
        report(which, false, std::string("HAZIRLIK HATASI (araç): ") + e.what());
    }
}

static void mig_type_case_impl(const std::string& which) {
    auto patch = [](const char* key, const char* val) {
        json j = json::parse(BASE);
        j["profiles"][0]["profile"]["accel_x"][key] = json::parse(val);
        return j.dump();
    };
    auto root_patch = [](const char* key, const char* val) {
        json j = json::parse(BASE);
        j[key] = json::parse(val);
        return j.dump();
    };
    auto dp_patch = [](const char* key, const char* val) {
        json j = json::parse(BASE);
        j["profiles"][0][key] = json::parse(val);
        return j.dump();
    };
    if      (which == "mig-type-limit")  expect_throw(which, patch("limit", "\"çok\""), true);
    else if (which == "mig-type-cap")    expect_throw(which, patch("cap", "\"x\""), false);
    else if (which == "mig-type-mode")   expect_throw(which, patch("mode", "42"), false);
    else if (which == "mig-type-gain")   expect_throw(which, patch("gain", "\"evet\""), false);
    else if (which == "mig-type-halflife") {
        json j = json::parse(BASE);
        j["profiles"][0]["profile"]["speed_processor"] = json::parse(
            R"({"whole":"evet","lp_norm":"iki","input_speed_smooth_halflife":[]})");
        expect_throw(which, j.dump(), false);
    }
    else if (which == "mig-type-raw")    expect_throw(which, root_patch("use_raw_input", "\"evet\""), false);
    else if (which == "mig-type-active") expect_throw(which, root_patch("active_profile", "42"), false);
    else if (which == "mig-type-dpi")    expect_throw(which, dp_patch("dpi", "\"800\""), false);
    else if (which == "mig-type-disable") expect_throw(which, dp_patch("disable", "\"evet\""), false);
    // bilinmeyen mode / cap_mode  → İDARİ KARAR: RED et
    else if (which == "mig-unknown-mode")    expect_throw(which, patch("mode", "\"clasic\""), true);
    else if (which == "mig-unknown-capmode") expect_throw(which, patch("cap_mode", "\"outt\""), true);
    // profiller dizisi yanlış tip  → RED (K1)
    else if (which == "mig-profiles-type") {
        json j = json::parse(BASE); j["profiles"] = "hepsi";
        expect_throw(which, j.dump(), true);
    }
    else if (which == "mig-profiles-entry") {
        json j = json::parse(BASE); j["profiles"] = json::array({ 1, 2, 3 });
        expect_throw(which, j.dump(), true);
    }
    // taşma / sınır değerler
    else if (which == "mig-lut-overflow") {
        // BUG-5 kalıbı: lut_length taşmalı, lut_data da sunmalı (yoksa okuyucu
        // anahtarı hiç görmüyor ve vaka bir şey ölçmez).
        json j = json::parse(BASE);
        j["profiles"][0]["profile"]["accel_x"]["lut_data"]   = json::array({ 1.0, 2.0 });
        j["profiles"][0]["profile"]["accel_x"]["lut_length"] = 1e26;
        expect_throw(which, j.dump(), false);
    }
    else if (which == "mig-inf") {
        // DİKKAT: `json::parse("1e999")` İLE hazırlamak ölçüm aracını
        // öldürür (taşma istisnası probe'da) — ham metin olarak Chevrolet
        // değil, doğrudan BASE metnine yerleştir.  Config.cpp'nin kendi
        // ayrıştırıcısının `1e999` ile ne yaptığı BU vakanın konusudur.
        std::string s = BASE;
        const size_t p = s.find("\"acceleration\":0.005");
        if (p == std::string::npos) { report(which, false, "HAZIRLIK: kalıp bulunamadı"); return; }
        s.replace(p, 20, "\"acceleration\":1e999");
        expect_throw(which, s, true);
    }
    else if (which == "mig-dpi-overflow")   expect_throw(which, dp_patch("dpi", "1e300"), false);
    // bozuk sürüm
    else if (which == "mig-version-corrupt")   expect_throw(which, root_patch("version", "\"abc\""), false);
    else if (which == "mig-version-negative")  expect_throw(which, root_patch("version", "\"0.6.-1\""), false);
    else report(which, false, "bilinmeyen vaka");
}

static void mig_broken(const std::string& which) {
    std::string s;
    if (which == "trunc")   s = std::string(BASE, 120);
    else if (which == "empty") s = "";
    else if (which == "garbage") s = "{\"a\": ,,,} sonlanmamis\x01\x02\x03";
    else if (which == "half") s = R"({"profiles":[{"name":"x","profile":{"accel_x":{"mode":)";
    else if (which == "nulls") s = R"(null)";                 // "unset" idiomu — K1 muafiyeti
    else if (which == "array") s = R"([1,2,3])";              // geçerli JSON, config DEĞİL
    else if (which == "number") s = R"(42)";                  // geçerli JSON, config DEĞİL
    else { report("mig-broken-" + which, false, "bilinmeyen vaka"); return; }
    // beklenen davranış: null KABUL (K1 muafiyeti: yok edilecek bir şey yok),
    // diğerleri RED (bkz. kök-tip bulgusu).
    const bool beklenen = (which != "nulls");
    expect_throw("mig-broken-" + which, s, beklenen);
}

// KÖK DİZİ + gerçek profil verisi: K1'in YOK ETME sınıfı.  Kök dizi 3 profil
// taşıyorsa ve yükleyici "0 profil" deyip geçerse, sonraki herhangi bir kayıt
// dosyayı `"profiles": []` ile değiştirir — ölçülen K1 kaybının aynısı.
static void mig_root_array_profiles() {
    json arr = json::array();
    for (int i = 0; i < 3; i++) {
        json p = json::parse(BASE);
        p["profiles"][0]["name"] = "p" + std::to_string(i);
        arr.push_back(p["profiles"][0]);
    }
    const std::string s = arr.dump();
    try {
        app_config cfg = app_config_from_json(s);
        json j2 = json::parse(app_config_to_json(cfg));
        const size_t after = j2.contains("profiles") && j2["profiles"].is_array()
                             ? j2["profiles"].size() : 0;
        const bool ok = (after == 3);
        report("mig-root-array-profiles", ok,
               "girdi=3 profil, yukleme=" + std::to_string(cfg.profiles.size()) +
               ", tekrar yazim=" + std::to_string(after) +
               (ok ? "" : "  ← VERI KAYBI: sonraki kayit dosyayi siler"));
    } catch (const std::exception& e) {
        report("mig-root-array-profiles", true,
               std::string("RED — dosya dokunulmaz: ") + e.what());
    }
}

// iç içe geçmiş derinlik: ayrıştırıcı yığın taşırabilir (STACK OVERFLOW).
// Her derinlik AYRI vaka → probe'un tek biri çökmesi diğerlerini yutmasın.
// Kök BİR NESNE içinde taşınır: `json::parse` argüman olarak tam olarak
// değerlendirildiği için derinlik ayrıştırıcıdan hâlâ geçer; kök-tip denetimi
// (AJ4-K5) sadece PARSE SONRASI yüklemeyi reddeder.  Eski biçim (dizi kökü)
// K5'ten sonra "beklenmeyen red" veriyordu — ölçüm eski kurala göreydi yazılmış.
static void mig_nest(int depth) {
    std::string s = "{\"gelecek\":";
    s.reserve(s.size() + static_cast<size_t>(depth) * 2 + 8);
    for (int i = 0; i < depth; i++) s += '[';
    s += "1";
    for (int i = 0; i < depth; i++) s += ']';
    s += "}";
    expect_throw("mig-nest-" + std::to_string(depth), s, false);
}

// ── PC: ölçüm aracının ÇÖKÜŞÜ algılayabildiğinin kanıtı (sürücü her koşuda) ──
static void selftest_segv() {
    std::fflush(stdout);
    std::raise(SIGSEGV);
    // erişmez
    report("selftest-segv", false, "raise() dönmedi");
}

// ── PC: sürücünün FAIL yolunu da gördüğünün kanıtı (sürücü her koşuda) ───────
// Probe "kendini" bilerek başarısız gösterir; sürücü bunu OK sayarsa
// ölçüm geçersizdir.
static void selftest_diff() {
    report("selftest-diff", false, "kasitli ariza (PC): karsilastirici FAIL gostermeli");
}

// ── liste ─────────────────────────────────────────────────────────────────────
static void list_cases() {
    for (int i = 0; i < PRESET_COUNT; i++) {
        std::printf("rt-preset-%s\n", PRESET_NAMES[i]);
        std::printf("preset-%s\n", PRESET_NAMES[i]);
        std::printf("values-%s\n", PRESET_NAMES[i]);
    }
    std::printf("rt-app-file\nrt-app-mem\nrt-zero-vs-missing\nrt-lut\nrt-clamp\n");
    std::printf("lim-oversize\n");
    std::printf("mig-unknown\nmig-missing\nmig-missing-profile\nmig-missing-all\n");
    std::printf("mig-type-limit\nmig-type-cap\nmig-type-mode\nmig-type-gain\n");
    std::printf("mig-type-halflife\nmig-type-raw\nmig-type-dpi\nmig-type-active\n");
    std::printf("mig-type-disable\n");
    std::printf("mig-unknown-mode\nmig-unknown-capmode\n");
    std::printf("mig-profiles-type\nmig-profiles-entry\n");
    std::printf("mig-lut-overflow\nmig-dpi-overflow\nmig-inf\n");
    std::printf("mig-version-corrupt\nmig-version-negative\n");
    std::printf("mig-broken-trunc\nmig-broken-empty\nmig-broken-garbage\n");
    std::printf("mig-broken-half\nmig-broken-nulls\nmig-broken-array\nmig-broken-number\n");
    std::printf("mig-root-array-profiles\n");
    for (int d : { 100, 1000, 5000, 20000, 100000 })
        std::printf("mig-nest-%d\n", d);
    std::printf("selftest-segv\nselftest-diff\n");
}

} // namespace rawaccel

int main(int argc, char** argv) {
    using namespace rawaccel;
    if (argc < 2) { list_cases(); return 0; }
    const std::string c = argv[1];
    if (c == "list") { list_cases(); return 0; }
    if (c.rfind("rt-preset-", 0) == 0)  { case_rt_preset(c.substr(10)); return g_fail ? 1 : 0; }
    if (c.rfind("preset-", 0) == 0)      { case_preset(c.substr(7));    return g_fail ? 1 : 0; }
    if (c.rfind("values-", 0) == 0)      { case_values(c.substr(7));    return g_fail ? 1 : 0; }
    if (c == "rt-app-file")   { case_rt_app_file();   return g_fail ? 1 : 0; }
    if (c == "rt-app-mem")    { case_rt_app_mem();    return g_fail ? 1 : 0; }
    if (c == "rt-file")       { if (argc < 3) { std::fprintf(stderr, "rt-file <yol>\n"); return 2; }
                                case_rt_file(argv[2]); return g_fail ? 1 : 0; }
    if (c == "rt-zero-vs-missing") { case_rt_zero_vs_missing(); return g_fail ? 1 : 0; }
    if (c == "rt-lut")        { case_rt_lut();        return g_fail ? 1 : 0; }
    if (c == "rt-clamp")      { case_rt_clamp();      return g_fail ? 1 : 0; }
    if (c == "lim-oversize")  { case_lim_oversize();  return g_fail ? 1 : 0; }
    if (c == "mig-unknown")   { mig_unknown();        return g_fail ? 1 : 0; }
    if (c.rfind("mig-missing", 0) == 0) { mig_missing_case(c); return g_fail ? 1 : 0; }
    if (c.rfind("mig-type-", 0) == 0 ||
        c.rfind("mig-unknown-", 0) == 0 ||
        c.rfind("mig-profiles-", 0) == 0 ||
        c == "mig-lut-overflow" || c == "mig-dpi-overflow" || c == "mig-inf" ||
        c == "mig-version-corrupt" || c == "mig-version-negative") {
        mig_type_case(c); return g_fail ? 1 : 0;
    }
    if (c.rfind("mig-broken-", 0) == 0) { mig_broken(c.substr(11)); return g_fail ? 1 : 0; }
    if (c == "mig-root-array-profiles") { mig_root_array_profiles(); return g_fail ? 1 : 0; }
    if (c.rfind("mig-nest-", 0) == 0)   { mig_nest(std::atoi(c.c_str() + 9)); return g_fail ? 1 : 0; }
    if (c == "selftest-segv") { selftest_segv(); return g_fail ? 1 : 0; }
    if (c == "selftest-diff") { selftest_diff(); return g_fail ? 1 : 0; }
    std::fprintf(stderr, "bilinmeyen vaka: %s\n", c.c_str());
    return 2;
}

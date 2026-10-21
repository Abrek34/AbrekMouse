// RawAccel Linux — config SÜRÜM / MIGRATION kenar durum testleri (AJ-H)
//
// Derleme: bash tests/run_tests.sh  (2. kapının içinde ayrı bir ikili olarak)
// Bağımlılık yok — sadece standart C++20 ve proje başlıkları.
//
// NEDEN AYRI BİKİLİ: test_accel.cpp'ye gömülseydi AGENTS.md'deki 34405
// assertion sayısı değişir ve `olcum/aj2/prove_kod_ayni.py --sayi` (sayı
// iddiası denetimi) kırmızıya dönerdi. Bu dosya kendi özetini basar.
//
// KAPSAM: lookup+gain LUT verisi "eski (pre-0.4) semantikten" "yeni (output
// speed) semantiğine" migrate_lookup_gain() ile taşınır: bir LUT noktasının
// y'si x ile ÇARPILIR (y_new = y · x).  Bu taşıma SÜRÜMLE koşulludur:
//   · sürüm alanı YOKSA        → taşınır (eski dosya)
//   · sürüm "0.4.0"dan ESKİYSE → taşınır
//   · sürüm "0.4.0" veya yeniyse → TAŞINMAZ
// Dolayısıyla LUT'un y'sine bakarak "taşındı mı" ÖLÇEBİLİRİZ.
//
// ÖLÇÜLEN SÜRÜM ŞEKİLLERİ (Aj-G'nin istediği kenar durum seti):
//   "abc"           → sayısal olmayan  → bozuk → "her şeyden eski" sayılmalı
//   "0.6.-1"        → negatif bileşen  → bozuk → "her şeyden eski" sayılmalı
//   "99999999999"   → taşma/saturasyon → bozuk → "her şeyden eski" sayılmalı
//   "0.4"           → eksik bileşen    → "0.4.0" ile AYNI olmalı → TAŞINMAMALI
//   "0.4.0.1"       → fazla bileşen    → "0.4.0"dan YENİ → TAŞINMAMALI
//   "99.0.0"        → ilerideki sürüm  → TAŞINMAMALI + kaydedilirken korunmalı
//   (yok)           → eski dosya       → TAŞINMALI
//
// Çerçeve: SECTION/EXPECT — test_accel.cpp ile aynı basit model.

#include "../include/config.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace rawaccel;

// ── Test altyapısı ────────────────────────────────────────────────────────────
static int g_tests = 0;
static int g_passed = 0;
static int g_failed = 0;
static const char* g_section = "";

#define SECTION(name) do { g_section = (name); std::printf("\n[%s]\n", g_section); } while (0)

#define EXPECT(cond) do { \
    g_tests++; \
    auto _c = (cond); \
    if (_c) { \
        g_passed++; \
        std::printf("  PASS  %s\n", #cond); \
    } else { \
        g_failed++; \
        std::fprintf(stderr, "  FAIL  %s:%d  %s  (section: %s)\n", \
                     __FILE__, __LINE__, #cond, g_section); \
    } \
} while (0)

#define EXPECT_NEAR(a, b, tol) do { \
    g_tests++; \
    double _a = (a), _b = (b), _t = (tol); \
    if (std::fabs(_a - _b) <= _t) { \
        g_passed++; \
        std::printf("  PASS  %s ≈ %s  (%.6g ≈ %.6g)\n", #a, #b, _a, _b); \
    } else { \
        g_failed++; \
        std::fprintf(stderr, "  FAIL  %s:%d  |%s - %s| = %.6g > %.6g  (section: %s)\n", \
                     __FILE__, __LINE__, #a, #b, std::fabs(_a - _b), _t, g_section); \
    } \
} while (0)

// ── Yardımcılar ───────────────────────────────────────────────────────────────

/// LUT noktası (x=100, y=200): taşıma sonrası y = 200 · 100 = 20000 olur.
/// Böylece "taşındı mı" tek bir sayıyla ölçülür.
static constexpr double kLutYBefore = 200.0;
static constexpr double kLutYAfter  = 20000.0;

static std::string tmp_path(const std::string& name) {
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    return (dir / ("rawaccel_migtest_" + name + ".json")).string();
}

/// `version` alanı verilen sürümle bir config yaz ve YÜKLE.
/// Dönüş: yükleme başarısızysa false.
static bool load_with_version(const std::string& version_field,
                              const std::string& name,
                              app_config& out,
                              double* lut_y) {
    const std::string path = tmp_path(name);
    {
        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << "{ " << version_field << R"( "active_profile": "t", "use_raw_input": true,
  "profiles": [ { "name": "t", "device_id": "",
      "profile": { "name": "t",
          "accel_x": { "mode": "lookup", "gain": true,
               "lut_data": [0.0,0.0,100.0,200.0], "lut_length": 4 },
          "accel_y": { "mode": "lookup", "gain": true,
               "lut_data": [0.0,0.0,100.0,200.0], "lut_length": 4 } } } ] })";
    }
    try {
        out = load_config(path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "  (yükleme hatası: %s)\n", e.what());
        std::remove(path.c_str());
        return false;
    }
    std::remove(path.c_str());
    if (lut_y && !out.profiles.empty())
        *lut_y = out.profiles[0].prof.accel_x.data[3];
    return true;
}

/// Sürüm alanı YOKSA (eski dosya).
static bool load_legacy(app_config& out, double* lut_y) {
    return load_with_version("", "legacy", out, lut_y);
}

// ── Testler ───────────────────────────────────────────────────────────────────

static void test_migration_gate_by_version() {
    SECTION("config/migration — sürüm alanına göre taşıma kapısı");
    app_config cfg;
    double y = 0.0;

    // 1) Sürüm alanı yok → eski dosya → TAŞINMALI.
    EXPECT(load_legacy(cfg, &y));
    EXPECT_NEAR(y, kLutYAfter, 1e-6);
    EXPECT(cfg.version == RAWACCEL_VERSION);

    // 2) "0.3.0" → 0.4.0'dan eski → TAŞINMALI.
    EXPECT(load_with_version("\"version\": \"0.3.0\",", "v030", cfg, &y));
    EXPECT_NEAR(y, kLutYAfter, 1e-6);

    // 3) "0.4.0" → tam taşıma eşiği → TAŞINMAMALI.
    EXPECT(load_with_version("\"version\": \"0.4.0\",", "v040", cfg, &y));
    EXPECT_NEAR(y, kLutYBefore, 1e-6);

    // 4) Güncel sürüm → TAŞINMAMALI.
    EXPECT(load_with_version(std::string("\"version\": \"") + RAWACCEL_VERSION + "\",",
                             "vcur", cfg, &y));
    EXPECT_NEAR(y, kLutYBefore, 1e-6);
}

static void test_migration_corrupt_versions() {
    SECTION("config/migration — bozuk sürümler");
    app_config cfg;
    double y = 0.0;

    // 5) "abc" → sayısal olmayan → bozuk → "her şeyden eski" → TAŞINMALI.
    EXPECT(load_with_version("\"version\": \"abc\",", "vabc", cfg, &y));
    EXPECT_NEAR(y, kLutYAfter, 1e-6);
    EXPECT(cfg.version == RAWACCEL_VERSION);

    // 6) "0.6.-1" → negatif bileşen → bozuk → TAŞINMALI.
    EXPECT(load_with_version("\"version\": \"0.6.-1\",", "vneg", cfg, &y));
    EXPECT_NEAR(y, kLutYAfter, 1e-6);

    // 7) "99999999999" → taşma → bozuk → TAŞINMALI.  Bozuk sürüm ayrıca
    //    DÜZELTİLMELİ: taşıma çalıştığı için sürüm güncel sürüme damgalanır
    //    (aksi hâlde bozuk sürüm dosyada kalır ve bir daha asla taşınmaz).
    EXPECT(load_with_version("\"version\": \"99999999999\",", "vovf", cfg, &y));
    EXPECT_NEAR(y, kLutYAfter, 1e-6);
    EXPECT(cfg.version == RAWACCEL_VERSION);
}

static void test_migration_component_count() {
    SECTION("config/migration — bileşen sayısı");
    app_config cfg;
    double y = 0.0;

    // 8) "0.4" → "0.4.0" ile AYNI sürüm (eksik bileşen sıfır sayılır).
    //    Bu sürüm 0.4.0 ile aynı SEMANTİĞE sahip: verisi zaten taşınmış
    //    demektir. Taşınırsa y = 200·100 = 20000 olur ve kullanıcının eğrisi
    //    100 kat şişmiş olarak KALICI olarak kaydedilir (yükleme sürümü
    //    damgalar → bir daha taşınmaz → hasar kalıcıdır).
    EXPECT(load_with_version("\"version\": \"0.4\",", "v04", cfg, &y));
    EXPECT_NEAR(y, kLutYBefore, 1e-6);

    // 9) "0.4.0.1" → "0.4.0"dan YENİ → TAŞINMAMALI.
    EXPECT(load_with_version("\"version\": \"0.4.0.1\",", "v0401", cfg, &y));
    EXPECT_NEAR(y, kLutYBefore, 1e-6);
}

static void test_int_max_boundary() {
    SECTION("config/migration — INT_MAX sınırı");
    app_config cfg;
    double y = 0.0;

    // Tam INT_MAX ('2147483647.0') GELECEK bir sürümdür, bozuk DEĞİLDİR →
    // taşıma atlanır, sürüm kaydedilirken korunur.
    EXPECT(load_with_version("\"version\": \"2147483647.0\",", "vintmax", cfg, &y));
    EXPECT_NEAR(y, kLutYBefore, 1e-6);
    EXPECT(cfg.version == "2147483647.0");

    // INT_MAX'ı bir AŞAN bileşen ('2147483648.0') BOZUK sayılır → "her şeyden
    // eski" → taşıma çalışır ve sürüm güncel sürüme onarılır.
    EXPECT(load_with_version("\"version\": \"2147483648.0\",", "vovermax", cfg, &y));
    EXPECT_NEAR(y, kLutYAfter, 1e-6);
    EXPECT(cfg.version == RAWACCEL_VERSION);
}

static void test_future_version_is_preserved() {
    SECTION("config/migration — ilerideki sürüm korunur");
    app_config cfg;
    double y = 0.0;
    const std::string path = tmp_path("vfuture");

    // 10) "99.0.0" → ilerideki sürüm → TAŞINMAMALI ve kaydedilirken
    //     DÜŞÜRÜLMEMELİ (sürüm alanı olduğu gibi korunur).
    EXPECT(load_with_version("\"version\": \"99.0.0\",", "vfut", cfg, &y));
    EXPECT_NEAR(y, kLutYBefore, 1e-6);
    EXPECT(cfg.version == "99.0.0");

    // Kaydetme: sürüm damgası düşürülmemeli.
    {
        app_config c2 = cfg;
        try {
            save_config(c2, path);
            app_config c3 = load_config(path);
            EXPECT(c3.version == "99.0.0");
            EXPECT_NEAR(c3.profiles[0].prof.accel_x.data[3], kLutYBefore, 1e-6);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "  (kaydetme/yükleme hatası: %s)\n", e.what());
            EXPECT(false);
        }
    }
    std::remove(path.c_str());
}

// ── Ana ───────────────────────────────────────────────────────────────────────
int main() {
    std::printf("=== Config sürüm/migration kenar durum testleri (AJ-H) ===\n");

    test_migration_gate_by_version();
    test_migration_corrupt_versions();
    test_migration_component_count();
    test_int_max_boundary();
    test_future_version_is_preserved();

    std::printf("\nConfig migration test özeti: %d/%d başarılı, %d başarısız\n",
                g_passed, g_tests, g_failed);
    return g_failed ? 1 : 0;
}

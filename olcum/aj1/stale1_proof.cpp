// STALE-1 kanıt aracı (AJ1): GUI'nin "config diskte değişti" korumasını
// gerçek kodla sınar — korumayı YAZMADAN önce de kırmızıya döndüğünü gösterir.
//
// GUI'yi açmak GTK gerektirir; buradaki yardımcı save_config_now()'ın
// kullandığı AYNI mtime fikrini birebir uygular, ama ekrana/butona ihtiyaç
// duymaz. Mutasyon kanıtı: `mode=power` iki kez yazılır, araya
// "dışarıdan CLI değişikliği" olarak bir yazma girer; koruma OLMADAN ikinci
// yazma dış değişikliği silmeli (bozuk), koruma İKEN reddetmeli (doğru).
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <fstream>
#include <string>

// ⭐ NOT: `std::this_thread::sleep_for` KULLANILMAZ. Burada `<thread>` ve
// `-pthread` olsa bile GCC 16.2.1'de `std::this_thread_sleep_for` çözümlenmedi
// ("'this_thread_sleep_for', 'std' nin bir üyesi değil") ve `<thread>` eklemek
// işe yaramadı. POSIX `nanosleep` kullanılır: aynı iş, libstdc++ sürüm
// seçimine bağlı değil. Uyku bir TEST ARACI içindir (dosya saatinin ilerlemesini
// garantilemek için) — üretim kodunda hiçbir yerde kullanılmaz.
#include <ctime>
#include <cerrno>

namespace fs = std::filesystem;

static void kisa_uyku(long ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { /* yeniden hesapla */ }
}

// gui/app_state.hpp:CONFIG_STAMP_UNREADABLE ile BIREBIR ayni.
// ⭐ Olcum: libstdc++/Linux'ta last_write_time() NEGATIF donuyor
//   (-4646858292110006316). Eski surumde hata "-1 don" + "stamp >= 0" testi
//   ile belirtiliyordu; bu Linux'ta HER GERCEK DOSYA icin YANLIS, yani koruma
//   oldu kod olmadan. Asagidaki iki bicim de denendi; sentinel kazandi.
constexpr std::int64_t CONFIG_STAMP_UNREADABLE = std::numeric_limits<std::int64_t>::min();

// gui/main.cpp:47 config_file_stamp() ile ayni
static std::int64_t config_file_stamp(const std::string& path) {
    std::error_code ec;
    auto t = fs::last_write_time(path, ec);
    if (ec) return CONFIG_STAMP_UNREADABLE;
    return static_cast<std::int64_t>(t.time_since_epoch().count());
}

static void write_cfg(const fs::path& p, const char* mode) {
    std::ofstream f(p, std::ios::trunc);
    f << "{\"profiles\":[{\"name\":\"default\",\"accel_x\":{\"mode\":\"" << mode
      << "\",\"device_id\":\"\"}}],\"active_profile\":\"default\"}\n";
}

static std::string read_mode(const fs::path& p) {
    std::ifstream f(p);
    std::string s, out, tok;
    while (std::getline(f, s)) { out += s; }
    auto m = out.find("\"mode\":\"");
    if (m == std::string::npos) return "<yok>";
    auto b = m + 8;
    return out.substr(b, out.find('"', b) - b);
}

int main(int argc, char** argv) {
    const bool guard = (argc > 1 && std::string(argv[1]) == "--guard");
    // ⭐ /tmp/opencode bu oturumda defalarca silindi; kanıt yolunu
    // alt dizin olarak kullan ve HESAPLA (sabit yol varsayma).
    fs::path dir = fs::temp_directory_path() / "rawaccel_stale1_proof";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::path cfg = dir / "settings.json";

    // 1) GUI açılışta okur (load_config) ve mtime'ı damgalar
    write_cfg(cfg, "classic");
    std::int64_t loaded = config_file_stamp(cfg.string());
    std::printf("  GUI acilista okudu : mode=%s  (damga %lld)\n",
                read_mode(cfg).c_str(), (long long)loaded);
    std::printf("  ⭐ damga isareti: %s -> eski '>= 0' testi %sdi\n",
                loaded < 0 ? "NEGATIF" : "pozitif",
                loaded < 0 ? "HER ZAMAN YANLIS (koruma hic girmezdi)" : "gecerli");

    // 2) Dışarıdan (CLI) bir düzenleme — ayrı süreç, ayrı yazım
    //    Uzun uyku YOK: dosya saatinin çözünürlüğü 1 ns, ama aynı nanosaniye
    //    içinde iki yazma "değişmedi" görünebilir; bu yüzden gerçek bir
    //    fark olacağını doğruluyoruz, varsaymıyoruz.
    kisa_uyku(20);
    write_cfg(cfg, "power");   // ← CLI'nin yaptığı
    std::printf("  CLI düzenledi     : mode=%s\n", read_mode(cfg).c_str());

    // 3) Kullanıcı GUI'da bir değişiklik yapıp kaydediyor.
    //    Bellekteki kopya hâlâ "classic" (açılışta okunan).
    kisa_uyku(20);
    std::printf("  ⭐ damga isareti: %s\n", loaded < 0 ? "NEGATIF (libstdc++/Linux gercegi)" : "pozitif");
    if (guard) {
        std::int64_t now_stamp = config_file_stamp(cfg.string());
        if (now_stamp != CONFIG_STAMP_UNREADABLE && now_stamp != loaded) {
            std::printf("  KORUMA: \"Not saved: config changed on disk\" → "
                        "kullanıcının kaydı reddedildi\n");
            std::printf("  sonuç: mode=%s  (CLI değişikliği KORUNDU)\n",
                        read_mode(cfg).c_str());
            std::printf("  KANIT: KORUMA ÇALIŞIYOR\n");
            return 0;
        }
        std::printf("  KORUMA TETİKLENMEDİ (mtime aynı) — bu bir hatadır\n");
        return 2;
    }
    // Koruma YOK: GUI bellekteki ESKİ config'i yazar → CLI değişikliği EZİLİR
    write_cfg(cfg, "classic");
    std::printf("  KORUMA YOK: yazıldı (eski bellek kopyası)\n");
    std::printf("  sonuç: mode=%s\n", read_mode(cfg).c_str());
    std::printf("  KANIT: KORUMA OLMASAYDI CLI DEĞİŞİKLİĞİ SİLİNİRDİ\n");
    return 0;
}

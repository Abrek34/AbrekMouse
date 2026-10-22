// RawAccel Linux — HID++ transport protokol testleri (AJ-H)
//
// Derleme: bash tests/run_tests.sh  (2. kapının içinde ayrı bir ikili olarak)
// Bağımlılık yok — sadece standart C++20 ve proje başlıkları.
//
// NEDEN AYRI BİR İKİLİ (test_accel.cpp'ye gömülmedi):
//   `tests/olcum/aj2/sayisal_iddialar.txt` (AGENTS.md'deki 34405 assertion
//   sayısı dahil) test_accel.cpp'nin SECTION ve EXPECT sayımını ÖLÇER.
//   Buraya test gömülseydi o sayılar çürür ve `--sayi` kapısı kırmızıya dönerdi
//   (belgedeki sayı "ölçülmüş" olmalı).  Bu dosya kendi sayımını kendi
//   basar ve test_accel.cpp'nin sayılarına dokunmaz.
//
// KAPSAM: HID++ protokol katmanı.  Bu katman bugüne kadar HİÇ test edilemiyordu:
//   send_feature_request() / resolve_feature_index() private, gerçek hidraw
//   düğümü gerekiyor (AGENTS.md: "no test seam was added to the class for it").
//   include/logitech_hidpp.hpp'a eklenen `hidpp_io_backend` arayüzü sayesinde
//   byte seviyesinin üstündeki protokol katmanı (özellik indeksi çözümleme,
//   istek/yanıt eşleştirme, bildirim zulası, kısa-yük bütçesi) sahte bir
//   HID++ 2.0 cihaz üzerinde GERÇEKTEN çalıştırılıyor.
//
// Çerçeve: SECTION/EXPECT — test_accel.cpp ile aynı basit model.

#include "../include/logitech_hidpp.hpp"

#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <utility>
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

// ── Sahte HID++ 2.0 cihaz ────────────────────────────────────────────────────
//
// Bu çerçeve Solaar'ın konuştuğu çerçeveyi kullanır (bu kodun `is_hidpp_error`
// ve `hidpp_long_packet::from_bytes` yaptığı gibi):
//   yanıt uzun (0x11) rapor: [0]=0x11 [1]=device_index [2]=feature_index
//                            [3]=(function<<4)|sw_id [4..]=payload (16 bayt)
//   HATA çerçevesi: [2] = 0xFF (veya 0x8F) → özel hata bildirimi.
//   bildirim: [2]=sub_id (<0x40 ve (address&0x0F)==0 → HID++ 2.0), [3]=address
class fake_hidpp_device : public hidpp_io_backend {
public:
    struct request_record {
        uint8_t report_id    = 0;
        uint8_t device_index = 0;
        uint8_t feature_index = 0;
        uint8_t function_id  = 0;   // NORMALİZE edilmiş (0x48 → 0x04)
        uint8_t software_id  = 0;
        uint8_t byte3        = 0;   // istekin ham 3. baytı (yanıtta yankılanır)
        std::vector<uint8_t> params;
    };

    /// Cihazın bildirdiği özellikler: feature_id -> dinamik indeks.
    std::map<uint16_t, uint8_t> feature_index_by_id;
    /// Dinamik indeks -> feature_id (GetFeatureId cevabı için).
    std::map<uint8_t, uint16_t> feature_id_by_index;

    /// Elle hazırlanmış yanıt kuyruğu (test, belirli bir isteğe özel cevap
    /// koyabilir).  Kuyruk boşsa cihaz otomatik cevap üretir.
    std::deque<std::vector<uint8_t>> queued;
    /// Otomatik üretilen "özel istek" cevapları: (feature_index, function_id)
    /// -> payload.  Bulunamazsa cihaz hata çerçevesi döner.
    std::map<std::pair<uint8_t, uint8_t>, std::vector<uint8_t>> canned;
    /// Bildirilecek hata: istek başarısız sayılır (nullopt döner).
    bool fail_with_error = false;
    /// Cihaz kalıcı olarak ölü mü.
    bool dead = false;

    std::vector<request_record> written;
    /// Gerçekten kabloya çıkan istek sayısı (otomatik keşif dahil).
    size_t wire_requests = 0;

    /// Belirli bir (özellik, fonksiyon) çiftine giden istek sayısı.
    size_t requests_for(uint8_t feature_index, uint8_t function_id) const {
        size_t n = 0;
        for (const auto& rec : written)
            if (rec.feature_index == feature_index && rec.function_id == function_id)
                ++n;
        return n;
    }

    bool write_packet(const uint8_t* data, size_t len) override {
        if (!data || len < 4) return false;
        request_record rec;
        rec.report_id = data[0];
        rec.device_index = data[1];
        rec.feature_index = data[2];
        rec.byte3 = data[3];
        rec.function_id = hidpp_normalize_function_id(data[3] >> 4);
        rec.software_id = static_cast<uint8_t>(data[3] & 0x0F);
        const size_t param_off = (rec.report_id == 0x10) ? 4 : 4;
        const size_t param_len = (len > param_off) ? (len - param_off) : 0;
        if (param_len) rec.params.assign(data + param_off, data + param_off + param_len);
        written.push_back(rec);
        ++wire_requests;
        produce_response(rec);
        return true;
    }

    bool read_packet(uint8_t* buf, size_t max_len, size_t& out_len,
                     std::chrono::milliseconds) override {
        out_len = 0;
        if (dead) return false;      // ölü uç nokta: hiç yanıt gelmez
        if (queued.empty()) return false;   // zaman aşımı (geçici)
        if (max_len < queued.front().size()) return false;
        out_len = queued.front().size();
        std::memcpy(buf, queued.front().data(), out_len);
        queued.pop_front();
        return true;
    }

    bool is_dead() const override { return dead; }

    /// Bir bildirim çerçevesi kuyruğa ekle (protokol katmanı bunu zulasa
    /// stash'lemeli, DISCARDS ETMEMELİ — BUG-22 regresyonu).
    void push_notification(uint8_t feature_index, uint8_t address = 0x00) {
        std::vector<uint8_t> frame(20, 0);
        frame[0] = 0x11;
        frame[1] = 0x01;          // device index
        frame[2] = feature_index; // sub-id (< 0x40 → HID++ 2.0)
        frame[3] = address;       // (address & 0x0F) == 0 → 2.0 bildirimi
        queued.push_back(frame);
    }

private:
    void produce_response(const request_record& rec) {
        // Hata çerçevesi: [2] = 0xFF → is_hidpp_error() bunu yakalar.
        if (fail_with_error) {
            queued.push_back(error_frame(rec));
            return;
        }
        // ROOT.GetFeature (0x0000 fn 0x00) — params: feature_id (2 bayt BE)
        if (rec.feature_index == 0x00 && rec.function_id == 0x00 &&
            rec.params.size() >= 2) {
            const uint16_t fid = static_cast<uint16_t>(
                (static_cast<uint16_t>(rec.params[0]) << 8) | rec.params[1]);
            const auto it = feature_index_by_id.find(fid);
            const uint8_t index = (it == feature_index_by_id.end()) ? 0x00 : it->second;
            queued.push_back(payload_frame(rec, {index}));
            return;
        }
        // FEATURE_SET.GetCount (fn 0x00) — ROOT hariç özellik sayısı
        // (Solaar: GetCount yanıtı ROOT'u SAYMAZ; taşıma count = payload + 1)
        if (rec.feature_index == feature_set_index() && rec.function_id == 0x00) {
            const uint16_t count = static_cast<uint16_t>(feature_index_by_id.size());
            queued.push_back(payload_frame(rec, {static_cast<uint8_t>(count)}));
            return;
        }
        // FEATURE_SET.GetFeatureId (fn 0x01) — params: dinamik indeks
        if (rec.feature_index == feature_set_index() && rec.function_id == 0x01 &&
            !rec.params.empty()) {
            const uint8_t index = rec.params[0];
            const auto it = feature_id_by_index.find(index);
            if (it == feature_id_by_index.end()) {
                queued.push_back(error_frame(rec));
                return;
            }
            queued.push_back(payload_frame(rec, {
                static_cast<uint8_t>(it->second >> 8),
                static_cast<uint8_t>(it->second & 0xFF),
                0x00, 0x01}));
            return;
        }
        // Cihazın ilan ettiği bir özelliğe özel hazır cevap
        const auto it = canned.find({rec.feature_index, rec.function_id});
        if (it != canned.end()) {
            queued.push_back(payload_frame(rec, it->second));
            return;
        }
        queued.push_back(error_frame(rec));
    }

    uint8_t feature_set_index() const {
        const auto it = feature_index_by_id.find(0x0001);
        return (it == feature_index_by_id.end()) ? 0x01 : it->second;
    }

    static std::vector<uint8_t> payload_frame(const request_record& rec,
                                               const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> frame(20, 0);
        frame[0] = 0x11;
        frame[1] = rec.device_index;
        frame[2] = rec.feature_index;
        // 3. bayt İSTEĞİN HAM HALİYLE yankılanır: cihaz, fonksiyon
        // seçicisini (function<<4|sw_id) talep üstünde nasıl gördüyse
        // cevapta aynen taşır.  (normalizasyon taşımada yapılır — 0x48 → 0x04)
        frame[3] = rec.byte3;
        for (size_t i = 0; i < payload.size() && i + 4 < frame.size(); ++i)
            frame[4 + i] = payload[i];
        return frame;
    }

    static std::vector<uint8_t> error_frame(const request_record& rec) {
        std::vector<uint8_t> frame(20, 0);
        frame[0] = 0x11;
        frame[1] = rec.device_index;
        frame[2] = 0xFF;   // is_hidpp_error() → hata
        frame[3] = rec.byte3;
        return frame;
    }
};

// ── Yardımcı kurulum ──────────────────────────────────────────────────────────
//
// Standart sahte cihaz: FEATURE_SET(1) + BATTERY_STATUS(2) + ADJUSTABLE_DPI(3)
// + ONBOARD_PROFILES(4)
static std::unique_ptr<fake_hidpp_device> make_device() {
    auto dev = std::make_unique<fake_hidpp_device>();
    dev->feature_index_by_id[0x0001] = 1;   // feature_set
    dev->feature_index_by_id[0x1000] = 2;   // battery_status
    dev->feature_index_by_id[0x2201] = 3;   // adjustable_dpi
    dev->feature_index_by_id[0x8100] = 4;   // onboard_profiles
    dev->feature_id_by_index[1] = 0x0001;
    dev->feature_id_by_index[2] = 0x1000;
    dev->feature_id_by_index[3] = 0x2201;
    dev->feature_id_by_index[4] = 0x8100;
    return dev;
}

// ── Testler ───────────────────────────────────────────────────────────────────

static void test_hidpp_feature_discovery() {
    SECTION("hidpp/feature-discovery");
    auto dev = make_device();
    HidppTransport transport(std::move(dev));

    EXPECT(transport.is_open());
    // Özellik keşfi: ROOT.GetFeature → FEATURE_SET.GetCount →
    // FEATURE_SET.GetFeatureId.  Üç özellik ilan edildi (ROOT hariç).
    EXPECT(transport.supports_feature(0x1000));
    EXPECT(transport.supports_feature(0x2201));
    // İlan edilmemiş özellik yerel olarak reddedilmeli.
    EXPECT(!transport.supports_feature(0x9999));
    EXPECT(!transport.supports_feature(0x0000));   // ROOT bir "özellik" değil
}

static void test_hidpp_feature_request_payload() {
    SECTION("hidpp/feature-request-payload");
    auto dev = make_device();
    dev->canned[{2, 0x00}] = {0x64, 0x02, 0x00};   // %100 pil, şarj değil
    HidppTransport transport(std::move(dev));

    auto payload = transport.feature_request(0x1000, 0x00, nullptr, 0);
    EXPECT(payload.has_value());
    if (payload) {
        EXPECT(payload->size() >= 3);
        EXPECT((*payload)[0] == 0x64);
        EXPECT((*payload)[1] == 0x02);
    }
}

static void test_hidpp_unknown_feature_rejected_locally() {
    SECTION("hidpp/unknown-feature-rejected-locally");
    auto dev = make_device();
    auto* devp = dev.get();
    HidppTransport transport(std::move(dev));

    // İlan edilmemiş özellik: taşıma ROOT.GetFeature ile sorar (keşif), cevap
    // "indeks 0" (= desteklenmiyor) döner ve istek TELDE GÖNDERİLMEZ.
    auto payload = transport.feature_request(0x9999, 0x00, nullptr, 0);
    EXPECT(!payload.has_value());
    // Yalnızca keşif sorgusu (ROOT.GetFeature) kabloya çıkmalı; özellik
    // isteğinin kendisi çıkmamalı.
    EXPECT(devp->requests_for(0x00, 0x00) == 1);
    EXPECT(devp->written.size() == 1);
}

static void test_hidpp_short_payload_budget() {
    SECTION("hidpp/short-payload-budget");
    auto dev = make_device();
    auto* devp = dev.get();
    HidppTransport transport(std::move(dev));

    // HIDPP_SHORT_PAYLOAD_MAX = 16; 17 baytlik parametre reddedilmeli ve
    // telde HİÇBİR bayt gitmemeli (sessiz kırpma değil, tam red).
    std::vector<uint8_t> too_big(HIDPP_SHORT_PAYLOAD_MAX + 1, 0xAB);
    auto payload = transport.feature_request(0x1000, 0x00, too_big.data(),
                                              too_big.size());
    EXPECT(!payload.has_value());
    EXPECT(devp->wire_requests == 0);
}

static void test_hidpp_notification_stash() {
    SECTION("hidpp/notification-stash");
    auto dev = make_device();
    dev->canned[{2, 0x00}] = {0x64, 0x02, 0x00};
    // Yanıttan ÖNCE bir bildirim kuyruğa ekle: protokol katmanı bildirimi
    // yanıt sanmamalı, zulası atmalı (BUG-22).
    dev->push_notification(0x1D);   // sub-id 0x1D (< 0x40) → HID++ 2.0 bildirimi
    HidppTransport transport(std::move(dev));

    auto payload = transport.feature_request(0x1000, 0x00, nullptr, 0);
    EXPECT(payload.has_value());
    // Bildirim kaybolmamalı: drain_notifications() onu vermeli.
    auto notifications = transport.drain_notifications();
    EXPECT(notifications.size() == 1);
    if (!notifications.empty()) {
        EXPECT(notifications[0].feature_index == 0x1D);
        EXPECT(notifications[0].type == hidpp_notification::kind::hidpp20);
    }
}

static void test_hidpp_error_frame() {
    SECTION("hidpp/error-frame");
    auto dev = make_device();
    dev->fail_with_error = true;
    HidppTransport transport(std::move(dev));

    // Hata çerçevesi nullopt dönmeli — ve takılmamalı (zaman aşımına düşmemeli).
    auto payload = transport.feature_request(0x1000, 0x00, nullptr, 0);
    EXPECT(!payload.has_value());
    // Hata sonrası taşıma hâlâ kullanılabilir olmalı.
    EXPECT(transport.is_open());
}

static void test_hidpp_dead_backend() {
    SECTION("hidpp/dead-backend");
    auto dev = make_device();
    dev->dead = true;
    HidppTransport transport(std::move(dev));

    EXPECT(transport.is_open());
    // Ölü uç nokta: istek başarısız olmalı VE taşıma kendini kapatmalı
    // (fd yolunun yaptığı gibi) — çağıranlar her seferinde zaman aşımı
    // beklemek zorunda kalmamalı.
    auto payload = transport.feature_request(0x1000, 0x00, nullptr, 0);
    EXPECT(!payload.has_value());
    EXPECT(!transport.is_open());
    // Kapatıldıktan sonra bildirim yolu da kapalı olmalı.
    auto notifications = transport.drain_notifications();
    EXPECT(notifications.empty());
}

static void test_hidpp_onboard_sector_payload_budget() {
    SECTION("hidpp/onboard-sector-payload-budget");
    // O31-H1 regresyonu: onboard sektör yazımı parametre vektörünü
    // 2 (sector) + chunk olarak kurar.  Chunk 16 olursa vektör 18 bayt olur,
    // `param_len > 16` her çağrıyı reddeder ve yol HİÇBİR ZAMAN yazmaz —
    // kullanıcıya da "başarılı" görünür.  Buradaki ölçüt: yazma isteği
    // GERÇEKTEN tele çıkmalı ve parametre vektörü bütçenin tam sınırında
    // (16) olmalı.  Chunk kayarsa (14 → 16) bu test KIRMIZIYA döner.
    auto dev = make_device();
    // Feature 0x8100 OnboardProfiles: fn 0x70 (write_sector) taşımada 4-bit seçiciye
    // normalleşir (0x70 → 0x07) — sahte cihaz da NORMALİZE id ile eşleşir.
    dev->canned[{4, hidpp_normalize_function_id(0x70)}] = {};  // write_sector → başarılı
    auto* devp = dev.get();
    HidppTransport transport(std::move(dev));

    const std::vector<uint8_t> payload(16, 0xAB);
    const bool written = transport.write_onboard_profile_sector(0x0003, payload);
    EXPECT(written);

    // 16 bayt / 14 baytlık chunk → 2 sektör yazma isteği (Feature 0x8100 Fn 7 = 0x70).
    const uint8_t fn_write_sector = hidpp_normalize_function_id(0x70);
    EXPECT(fn_write_sector == 0x07);
    const size_t writes = devp->requests_for(4, fn_write_sector);
    EXPECT(writes == 2);
    // Her istek parametre vektörünü tam bütçeyle göndermeli (2 + 14 = 16).
    for (const auto& rec : devp->written) {
        if (rec.feature_index != 4 || rec.function_id != fn_write_sector) continue;
        EXPECT(rec.params.size() == HIDPP_SHORT_PAYLOAD_MAX);
    }
    // Sektör numarası büyük-endian ve ilk 2 baytta olmalı.
    bool sector_ok = false;
    for (const auto& rec : devp->written) {
        if (rec.feature_index != 4 || rec.function_id != fn_write_sector) continue;
        if (rec.params.size() >= 2 &&
            rec.params[0] == 0x00 && rec.params[1] == 0x03) {
            sector_ok = true;
            break;
        }
    }
    EXPECT(sector_ok);
}

static void test_hidpp_null_backend_is_safe() {
    SECTION("hidpp/null-backend-is-safe");
    // Null backend: çökme yok, sadece "hiç açık değil".
    HidppTransport transport(std::unique_ptr<hidpp_io_backend>{});
    EXPECT(!transport.is_open());
    auto payload = transport.feature_request(0x1000, 0x00, nullptr, 0);
    EXPECT(!payload.has_value());
}

// ── Ana ───────────────────────────────────────────────────────────────────────
int main() {
    std::printf("=== HID++ transport protokol testleri (AJ-H) ===\n");

    test_hidpp_feature_discovery();
    test_hidpp_feature_request_payload();
    test_hidpp_unknown_feature_rejected_locally();
    test_hidpp_short_payload_budget();
    test_hidpp_notification_stash();
    test_hidpp_error_frame();
    test_hidpp_dead_backend();
    test_hidpp_onboard_sector_payload_budget();
    test_hidpp_null_backend_is_safe();

    std::printf("\nHID++ test özeti: %d/%d başarılı, %d başarısız\n",
                g_passed, g_tests, g_failed);
    return g_failed ? 1 : 0;
}

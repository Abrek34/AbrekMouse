// ── tr.inl — lightweight localization (English + Türkçe) ─────────────────────
//
// Data-driven string table: the English source text is BOTH the UI default and
// the dictionary key. tr(key) returns the Turkish rendering when the active
// language is Turkish, else the key unchanged.
//
// Two mechanisms:
//   1. Static widgets that live for the whole window lifetime are registered as
//      (widget, kind, key) in g_tr_registry. A runtime language switch re-applies
//      every registered translation in place via refresh_language().
//   2. Transient / regenerated content (dialogs, status messages, dropdown
//      items, LUT rows, mode hints) is translated with tr()/trf() at the point
//      of creation/write — no registration needed.
//
// Language resolution: an explicit preference (header-bar dropdown, persisted
// to <config_dir>/gui_lang) wins; otherwise the system locale is detected via
// setlocale()/LANG ("auto" mode).
//
// The AppState pointer is only used inside refresh_language()/on_lang_changed()
// and is always passed explicitly — no global app state (see AGENTS.md).

#include <clocale>
#include <cstdarg>
#include <atomic>
#include <unordered_map>

enum TrKind { TR_LABEL, TR_MARKUP, TR_BUTTON, TR_CHECK, TR_TOOLTIP };

struct TrEntry {
    GtkWidget*  w;
    TrKind      kind;
    std::string key;
};

static std::atomic<int> g_lang { 0 }; // L-BUG-17: 0 = English, 1 = Türkçe (atomic)
static std::vector<TrEntry> g_tr_registry;

static void tr_register(GtkWidget* w, TrKind kind, const char* key) {
    g_tr_registry.push_back({w, kind, key});
}

static const char* tr(const char* key) {
    if (!key) return key;
    if (g_lang == 1) {
        static const std::unordered_map<std::string, const char*> D = {
            // ── Dropdown items ──────────────────────────────────────────────
            {"None (1:1)",           "Yok (1:1)"},
            {"Classic",              "Klasik"},
            {"Power",                "Güç"},
            {"Natural",              "Doğal"},
            {"Jump",                 "Zıplama"},
            {"Synchronous",          "Senkron"},
            {"Lookup (LUT)",         "Arama (LUT)"},
            {"Output (out)",         "Çıkış (out)"},
            {"Input (in)",           "Giriş (in)"},
            {"I/O (io)",             "G/Ç (io)"},
            {"Euclidean",            "Öklid"},
            {"Max",                  "Maks"},
            {"Lp",                   "Lp"},
            {"Separate",             "Ayrı"},
            {"Auto (locale)",        "Otomatik (sistem)"},
            // ── Section headers (markup) ────────────────────────────────────
            {"<b>Acceleration — X Axis</b>",                    "<b>İvme — X Ekseni</b>"},
            {"<b>Y Axis</b>",                                   "<b>Y Ekseni</b>"},
            {"<b>Rotation &amp; Snap</b>",                      "<b>Döndürme &amp; Açı Yapıştırma</b>"},
            {"<b>Speed Limit</b>",                              "<b>Hız Limiti</b>"},
            {"<b>Speed Processor</b>",                          "<b>Hız İşlemcisi</b>"},
            {"<b>Device</b>",                                   "<b>Cihaz</b>"},
            {"<b>Device Assignment</b>",                        "<b>Cihaz Atama</b>"},
            {"<b>Hardware Settings (Logitech HID++)</b>",
                                                                "<b>Donanım Ayarları (Logitech HID++)</b>"},
            {"<b>Gain Curve</b>  <small>(scroll = zoom)</small>",
                                                                "<b>Kazanım Eğrisi</b>  <small>(tekerlek = yakınlaştır)</small>"},
            {"Speed (ips)",         "Hız (inç/sn)"},
            {"Gain",                "Kazanç"},
            {"X Axis",              "X Ekseni"},
            {"Y Axis",              "Y Ekseni"},
            {"%.0f ips  (scroll=zoom)",                  "%.0f inç/sn  (tekerlek=yakınlaştır)"},
            {"%.0f ips  (left click=add, right click=remove)",
                                                                "%.0f inç/sn  (sol tık=ekle, sağ tık=kaldır)"},
            // ── Buttons / checks / labels ───────────────────────────────────
            {"● Checking...",             "● Denetleniyor..."},
            {"Reload",                    "Yeniden Yükle"},
            {"Stop",                      "Durdur"},
            {"Start",                     "Başlat"},
            {"Apply & Reload",            "Uygula ve Yeniden Yükle"},
            {"Save",                      "Kaydet"},
            {"New profile",               "Yeni profil"},
            {"Duplicate profile",         "Profili çoğalt"},
            {"Could not duplicate profile: no free name.",
                                                                "Profil çoğaltılamadı: boş isim yok."},
            {"Rename profile",            "Profili yeniden adlandır"},
            {"Reset to defaults",         "Varsayılana sıfırla"},
            {"Delete profile",            "Profili sil"},
            {"Import profile",            "Profili içe aktar"},
            {"Export profile",            "Profili dışa aktar"},
            {"Import profile (JSON)",     "Profili içe aktar (JSON)"},
            {"Export failed: cannot write %s", "Dışa aktarılamadı: %s yazılamadı"},
            {"Exported profile \"%s\" → %s",  "\"%s\" profili → %s dışa aktarıldı"},
            {"Export failed: %s",               "Dışa aktarma hatası: %s"},
            {"Import failed: invalid profile JSON — %s", "İçe aktarma hatası: geçersiz profil JSON — %s"},
            {"Import failed: profile has no \"name\".", "İçe aktarma hatası: profilin \"name\" alanı yok."},
            {"Import failed: file too large.", "İçe aktarma hatası: dosya çok büyük."},
            {"Import failed: profile \"%s\" already exists.", "İçe aktarma hatası: \"%s\" profili zaten var."},
            {"Import failed: %s",               "İçe aktarma hatası: %s"},
            {"Import failed: %s. Fix the file and try again.", "İçe aktarma hatası: %s. Dosyayı düzeltip tekrar deneyin."},
            {"Imported profile: %s",            "Profil içe aktarıldı: %s"},
            {"Raw Passthrough (bypass all acceleration)", "Ham Geçiş (tüm ivmeyi atla)"},
            {"Gain mode (recommended)",   "Kazanım modu (önerilir)"},
            {"+ Add Point",               "+ Nokta Ekle"},
            {"Sort",                      "Sırala"},
            {"Same as X (linked)",        "X ile aynı (bağlı)"},
            {"Apply to Device",           "Cihaza Uygula"},
            {"Fix Now",                   "Şimdi Düzelt"},
            {"Manual",                    "Manuel"},
            {"Ready.",                    "Hazır."},
            {"Mode:",             "Mod:"},
            {"Accel:",            "İvme:"},
            {"Exp (cls):",        "Üs (kls):"},
            {"Exp (pwr):",        "Üs (güç):"},
            {"Exp:",              "Üs:"},
            {"Limit:",            "Limit:"},
            {"Input Offset:",     "Giriş Kayması:"},
            {"Decay Rate:",       "Sönüm Oranı:"},
            {"Cap X:",            "Cap X:"},
            {"Cap Y:",            "Cap Y:"},
            {"Cap Mode:",         "Cap Modu:"},
            {"Sync Speed:",       "Senk. Hız:"},
            {"Smoothing:",        "Yumuşatma:"},
            {"Motivity:",         "Motivite:"},
            {"Gamma:",            "Gamma:"},
            {"Out Offset:",       "Çıkış Kayması:"},
            {"Scale:",            "Ölçek:"},
            {"Spd:",              "Hız:"},
            {"Gain:",             "Kazanç:"},
            {"Lp Norm:",          "Lp Normu:"},
            {"Rotation (°):",     "Döndürme (°):"},
            {"Snap (°):",         "Açı Yapıştırma (°):"},
            {"LR Ratio:",         "Sağ/Sol Oranı:"},
            {"UD Ratio:",         "Aşa/Yuk Oranı:"},
            {"Min (ips):",        "Min (ips):"},
            {"Max (ips):",        "Maks (ips):"},
            {"DPI:",              "DPI:"},
            {"Polling Rate:",     "Yoklama Hızı:"},
            {"Output DPI:",       "Çıkış DPI:"},
            {"Distance:",         "Mesafe:"},
            {"Input HL:",         "Giriş YO:"},
            {"Scale HL:",         "Ölçek YO:"},
            {"Output HL:",        "Çıkış YO:"},
            {"Mouse:",            "Fare:"},
            {"App:",              "Uygulama:"},
            {"App class (e.g. firefox) — optional", "Uygulama sınıfı (örn. firefox) — isteğe bağlı"},
            {"All devices (default)", "Tüm cihazlar (varsayılan)"},
            {"(unplugged)",           "(bağlı değil)"},
            {"HID++ Device:",         "HID++ Cihaz:"},
            {"Hardware DPI:",         "Donanım DPI:"},
            {"Hardware Polling Rate:","Donanım Yoklama Hızı:"},
            {"Lift-off Distance:",    "Kaldırma Mesafesi:"},
            {"Low",                   "Düşük"},
            {"Medium",                "Orta"},
            {"High",                  "Yüksek"},
            // ── Small hint labels (markup) ──────────────────────────────────
            {"<small>Left click: add point on graph\n"
             "Right click: remove point on graph\n"
             "Points are speed (ips) → gain pairs.</small>",
             "<small>Sol tık: grafiğe nokta ekler\n"
             "Sağ tık: grafikten nokta siler\n"
             "Noktalar hız (ips) → kazanım çiftleridir.</small>"},
            {"<small>Set Max to 0 to disable speed clamping.</small>",
             "<small>Hız sınırlamayı kapatmak için Maks değerini 0 yapın.</small>"},
            {"<small>HL = EMA half-life in ms. 0 = smoothing off.\n"
             "Separate: X and Y each processed by their own axis.</small>",
             "<small>YO = ms cinsinden EMA yarı ömrü. 0 = yumuşatma kapalı.\n"
             "Ayrı: X ve Y kendi eksenleriyle ayrı işlenir.</small>"},
            {"<small>This profile applies only to the selected mouse.\n"
             "The daemon uses a stable USB composite ID as device_id.</small>",
             "<small>Bu profil yalnızca seçilen fare için geçerlidir.\n"
             "Daemon, cihaz kimliği olarak stabil bir USB bileşen kimliği kullanır.</small>"},
            {"<b>KDE: Mouse acceleration is NOT disabled!</b>  "
             "KDE will apply its own curve on top of RawAccel → double acceleration.",
             "<b>KDE: Fare ivmesi devre dışı DEĞİL!</b>  "
             "KDE, RawAccel'in üzerine kendi eğrisini uygular → çift ivme."},
            // ── Tooltips ────────────────────────────────────────────────────
            {"When enabled, the entire acceleration pipeline is bypassed.\n"
             "No rotation, snap, speed clamp, weights, or sub-pixel accumulation —\n"
             "raw kernel counts are written directly to uinput (1:1 passthrough).",
             "Açıkken tüm ivme hattı atlanır.\n"
             "Döndürme, açı yapıştırma, hız sınırı, ağırlıklar veya alt-piksel biriktirme yok —\n"
             "ham çekirdek sayacı doğrudan uinput'a yazılır (1:1 geçiş)."},
            {"Classic/Jump/Natural/Synchronous: use the integral (output-speed) form of the curve.\n"
             "Lookup: gain points are treated as output speeds (gain = y / speed).",
             "Klasik/Zıplama/Doğal/Senkron: eğrinin integral (çıkış hızı) formunu kullanır.\n"
             "Arama tablosu: kazanım noktaları çıkış hızı olarak yorumlanır (kazanım = y / hız)."},
            {"Classic: acceleration coefficient of the power curve.",
             "Klasik: güç eğrisinin ivme katsayısı."},
            {"Classic: exponent of the power curve.",
             "Klasik: güç eğrisinin üssü."},
            {"Power: exponent of the curve.",
             "Güç: eğrinin üssü."},
            {"Natural: gain limit above the 1.0 baseline.",
             "Doğal: 1.0 tabanının üzerindeki kazanım limiti."},
            {"Classic/Natural: speeds below this map to 1.0 (no acceleration).",
             "Klasik/Doğal: bunun altındaki hızlar 1.0'a karşılık gelir (ivme yok)."},
            {"Natural: how quickly gain approaches the limit.",
             "Doğal: kazanımın limite ne kadar hızlı yaklaştığı."},
            {"Classic/Power: cap input speed (ips) — combined with Cap Mode.\n"
             "Jump: step position — input speed where the jump occurs.",
             "Klasik/Güç: giriş hızını sınırlar (ips) — Cap Modu ile birlikte.\n"
             "Zıplama: basamak konumu — zıplamanın olduğu giriş hızı."},
            {"Classic/Power: cap output gain/DPI multiplier — combined with Cap Mode.\n"
             "Jump: step amount — gain after the jump.",
             "Klasik/Güç: çıkış kazanımını/DPI çarpanını sınırlar — Cap Modu ile birlikte.\n"
             "Zıplama: basamak miktarı — zıplamadan sonraki kazanım."},
            {"Classic/Power: out = clamp gain, in = clamp speed, io = clamp both at a selected point.",
             "Klasik/Güç: out = kazanımı sınırla, in = hızı sınırla, io = seçili noktada ikisini birden sınırla."},
            {"Synchronous: speed where the multiplier = 1.",
             "Senkron: çarpanın 1 olduğu hız."},
            {"Jump: sigmoid steepness. Synchronous: sharpness of the tanh blend (smaller = smoother).",
             "Zıplama: sigmoid dikliği. Senkron: tanh harmanının keskinliği (küçük = daha yumuşak)."},
            {"Power: raises the curve on the output side.",
             "Güç: eğriyi çıkış tarafında yükseltir."},
            {"Power: scale factor of the curve.",
             "Güç: eğrinin ölçek çarpanı."},
            {"Synchronous: maximum multiplier (minimum = 1/motivity).",
             "Senkron: maksimum çarpan (minimum = 1/motivite)."},
            {"Synchronous: width of the activation curve in log space.",
             "Senkron: aktivasyon eğrisinin log uzaydaki genişliği."},
            {"Left/right output DPI ratio (1.0 = off). Values >1 amplify rightward movement.",
             "Sol/sağ çıkış DPI oranı (1.0 = kapalı). >1 değerler sağa hareketi güçlendirir."},
            {"Up/down output DPI ratio (1.0 = off). Values >1 amplify downward movement.",
             "Yukarı/aşağı çıkış DPI oranı (1.0 = kapalı). >1 değerler aşağı hareketi güçlendirir."},
            {"Minimum speed clamp (ips). 0 = disabled.",
             "Minimum hız sınırı (ips). 0 = kapalı."},
            {"Maximum speed clamp (ips). Set to 0 to disable clamping.",
             "Maksimum hız sınırı (ips). Kapatmak için 0 yapın."},
            {"How speed is calculated from X/Y input:\n"
             "  Euclidean — √(x²+y²)  (default)\n"
             "  Max — max(|x|,|y|)\n"
             "  Lp — generalized norm\n"
             "  Separate — X and Y processed independently",
             "Hız, X/Y girişinden şöyle hesaplanır:\n"
             "  Öklid — √(x²+y²)  (varsayılan)\n"
             "  Maks — max(|x|,|y|)\n"
             "  Lp — genelleştirilmiş norm\n"
             "  Ayrı — X ve Y bağımsız işlenir"},
            {"Lp-norm exponent (only used when Distance = Lp). 2 = Euclidean, large values → Max.",
             "Lp-norm üssü (yalnızca Mesafe = Lp iken kullanılır). 2 = Öklid, büyük değerler → Maks."},
            {"EMA half-life for input speed smoothing (ms). 0 = off.",
             "Giriş hızı yumuşatması için EMA yarı ömrü (ms). 0 = kapalı."},
            {"EMA half-life for scale smoothing (ms). 0 = off.",
             "Ölçek yumuşatması için EMA yarı ömrü (ms). 0 = kapalı."},
            {"EMA half-life for output speed smoothing (ms). 0 = off.",
             "Çıkış hızı yumuşatması için EMA yarı ömrü (ms). 0 = kapalı."},
            {"Output DPI normalization value (default: 1000).\n"
             "Change this to match your monitor's effective DPI scaling.",
             "Çıkış DPI normalizasyon değeri (varsayılan: 1000).\n"
             "Monitörünüzün etkin DPI ölçeğine uyacak şekilde değiştirin."},
            {"Rescan connected mice",
             "Bağlı fareleri yeniden tara"},
            {"Rescan Logitech HID++ devices",
             "Logitech HID++ cihazlarını yeniden tara"},
            {"Physical sensor DPI stored on the Logitech mouse (HID++).\n"
             "Independent of the software DPI above.",
             "Logitech farenin sensörüne kayıtlı fiziksel DPI (HID++).\n"
             "Yukarıdaki yazılımsal DPI'dan bağımsızdır."},
            {"Report/polling rate of the Logitech mouse (HID++).\n"
             "Values the device rejects are reported in the status line.",
             "Logitech farenin rapor/yoklama hızı (HID++).\n"
             "Cihazın reddettiği değerler durum satırında bildirilir."},
            {"Lift-off distance of the sensor (HID++ 0x2202).\n"
             "Only devices that support it accept this value.",
             "Sensörün kaldırma mesafesi (HID++ 0x2202).\n"
             "Yalnızca destekleyen cihazlar bu değeri kabul eder."},
            {"Write DPI, polling rate and lift-off distance to the physical mouse.",
             "DPI, yoklama hızı ve kaldırma mesafesini fiziksel fareye yazar."},
            {"Sort points by speed value (ascending)",
             "Noktaları hız değerine göre sırala (artan)"},
            {"Remove this point",
             "Bu noktayı kaldır"},
            {"Sets PointerAccelerationProfile=Flat in ~/.config/kwinrc\n"
             "and reloads KWin input settings immediately (no logout needed).",
             "~/.config/kwinrc dosyasına PointerAccelerationProfile=Flat yazar\n"
             "ve KWin giriş ayarlarını hemen yeniden yükler (oturum kapatmaya gerek yok)."},
            {"Open KDE System Settings → Input Devices → Mouse\n"
             "and set Pointer Acceleration to Flat.",
             "KDE Sistem Ayarları → Giriş Aygıtları → Fare'yi açın\n"
             "ve Pointer Acceleration'ı Flat yapın."},
            // ── Mode hints ────────────────────────────────────────────────────
            {"1:1 output — no curve parameters.",
             "1:1 çıkış — eğri parametresi yok."},
            {"Uses: Accel, Exp (cls), Input Offset, Cap X/Y, Cap Mode.",
             "Kullanır: İvme, Üs (kls), Giriş Kayması, Cap X/Y, Cap Modu."},
            {"Uses: Scale, Exp (pwr), Output Offset, Cap X/Y, Cap Mode.",
             "Kullanır: Ölçek, Üs (güç), Çıkış Kayması, Cap X/Y, Cap Modu."},
            {"Uses: Limit, Decay Rate, Input Offset, Gain.",
             "Kullanır: Limit, Sönüm Oranı, Giriş Kayması, Kazanım."},
            {"Uses: Cap X (step position), Cap Y (step amount), Smooth.",
             "Kullanır: Cap X (basamak konumu), Cap Y (basamak miktarı), Yumuşatma."},
            {"Uses: Sync Speed, Motivity, Gamma, Smooth.",
             "Kullanır: Senk. Hız, Motivite, Gamma, Yumuşatma."},
            {"Edit gain points on the curve (right panel).",
             "Kazanım noktalarını eğri üzerinde düzenleyin (sağ panel)."},
            // ── Daemon status / battery (markup + format) ────────────────────
            {"<span foreground='#40c040'>● Daemon running</span>",
             "<span foreground='#40c040'>● Daemon çalışıyor</span>"},
            {"<span foreground='#c04040'>● Daemon stopped</span>",
             "<span foreground='#c04040'>● Daemon durduruldu</span>"},
            {"<b><span foreground='red'>Battery: %d%% (Low!)</span></b>",
             "<b><span foreground='red'>Pil: %d%% (Düşük!)</span></b>"},
            {"<b>Battery: %d%%</b>",
             "<b>Pil: %d%%</b>"},
            {"<b>Battery: unknown</b>",
             "<b>Pil: bilinmiyor</b>"},
            // ── Latency snapshot display ────────────────────────────────────
            {"Latency: —",
             "Gecikme: —"},
            {"Latency: no samples recorded yet.",
             "Gecikme: henüz örnek kaydedilmedi."},
            {"Latency: Avg %s · p50 %s · p95 %s · p99 %s · Max %s µs",
             "Gecikme: Ort %s · p50 %s · p95 %s · p99 %s · Maks %s µs"},
            {"Performance",
             "Performans"},
            {"Read the daemon's latency snapshot (Avg/p50/p95/p99/Max)",
             "Daemon'un gecikme anlık görüntüsünü oku (Ort/p50/p95/p99/Maks)"},
            // ── Mouse lock test window (P104) ──────────────────────────────
            {"Mouse Test",
             "Fare Testi"},
            {"Mouse lock test window — locks the pointer inside and shows live speed/gain. ESC releases.",
             "Fare kilit test penceresi — imleci içeride kilitler ve canlı hız/kazanç gösterir. Kilit ESC ile bırakılır."},
            {"Mouse Lock Test",
             "Fare Kilit Testi"},
            {"In (ips):",
             "Giriş (ips):"},
            {"Out (ips):",
             "Çıkış (ips):"},
            {"Gain (×):",
             "Kazanç (×):"},
            {"Pointer is locked inside this fullscreen test window.\nMove the mouse to see live speed/gain.\nPress ESC to release.",
             "İmleç bu tam ekran test penceresinin içine kilitli.\nCanlı hız/kazanç için fareyi hareket ettirin.\nKilit ESC ile bırakılır."},
            {"Pointer grab failed — the cursor is confined as best effort (no pointer lock).\nMove the mouse to see live speed/gain.\nPress ESC to release.",
             "İmleç yakalama başarısız — imleç en iyi çabayla sınırlanıyor (imleç kilidi yok).\nCanlı hız/kazanç için fareyi hareket ettirin.\nKilit ESC ile bırakılır."},
            {"Pointer lock unavailable — no pointer lock: this Wayland session cannot grab the pointer.\nThe cursor is NOT locked inside this window (fullscreen coverage only).\nMove the mouse, then press ESC to close.",
             "İmleç kilidi yok — bu Wayland oturumu imleci yakalayamıyor.\nİmleç bu pencerenin içine kilitlenmiş DEĞİL (yalnızca tam ekran kapsamı).\nFareyi hareket ettirin, ardından kapatmak için ESC'ye basın."},
            {"Awaiting motion…",
             "Hareket bekleniyor…"},
            {"Raw 1:1 passthrough — no telemetry is produced (accelerated mode only).",
             "Ham 1:1 geçiş — telemetri üretilmez (yalnızca hızlandırılmış mod)."},
            {"Mouse test: pointer released (ESC).",
             "Fare testi: imleç serbest bırakıldı (ESC)."},
            {"Mouse test: pointer lock released (window unfocused).",
             "Fare testi: imleç kilidi bırakıldı (pencere odak dışı)."},
            // ── Status-bar messages ──────────────────────────────────────────
            {"Device list refreshed: ",   "Cihaz listesi yenilendi: "},
            {" mouse(s) found.",          " fare bulundu."},
            {"Scanning HID++ devices…",   "HID++ cihazları taranıyor…"},
            {"No Logitech HID++ devices found.",
                                         "Logitech HID++ cihazı bulunamadı."},
            {"Select a Logitech HID++ device first.",
                                         "Önce bir Logitech HID++ cihazı seçin."},
            {"unknown",                   "bilinmiyor"},
            {"unsupported",               "desteklenmez"},
            {"—",                         "—"},
            {"Current: DPI %s · %d Hz · LOD %s",
                                         "Geçerli: DPI %s · %d Hz · LOD %s"},
            {"Could not query the device's current settings.",
                                         "Cihazın geçerli ayarları okunamadı."},
            {"DPI→%d%s",                  "DPI→%d%s"},
            {"Rate→%d Hz%s",              "Hız→%d Hz%s"},
            {"LOD→",                      "LOD→"},
            {"(rejected)",                "(reddedildi)"},
            {" (charging)",              " (şarj oluyor)"},
            {"Notification: battery %s%s",
                                         "Bildirim: pil %s%s"},
            // P169 — HID++ battery / capability rows.
            {"Battery: —",                "Pil: —"},
            {"Capabilities: —",           "Yetenekler: —"},
            {"<b>Battery:</b> %s",        "<b>Pil:</b> %s"},
            {"<b>Battery:</b> %s (%s) · %s",
                                         "<b>Pil:</b> %s (%s) · %s"},
            {"Battery:",                  "Pil:"},
            {"Unknown",                   "Bilinmiyor"},
            {"Offline",                   "Çevrimdışı"},
            {"Charging",                  "Şarj oluyor"},
            {"Discharging",               "Şarj olmuyor"},
            {"Unified Battery",           "Birleşik Pil"},
            {"Battery Status",            "Pil Durumu"},
            {"Battery Voltage",           "Pil Gerilimi"},
            {"HID++ 1.0 register",        "HID++ 1.0 kaydı"},
            {"not advertised",            "bildirilmedi"},
            {"DPI: adjustable",           "DPI: ayarlanabilir"},
            {"DPI: adjustable + X/Y (extended)",
                                         "DPI: ayarlanabilir + X/Y (genişletilmiş)"},
            {"DPI: not advertised",       "DPI: bildirilmedi"},
            {"LOD: supported (0x2202)",   "LOD: destekleniyor (0x2202)"},
            {"LOD: not advertised",       "LOD: bildirilmedi"},
            {"Rate: extended (up to 8 kHz)",
                                         "Hız: genişletilmiş (8 kHz'e kadar)"},
            {"Rate: legacy (1..8 ms)",    "Hız: klasik (1..8 ms)"},
            {"Rate: not advertised",      "Hız: bildirilmedi"},
            {"Onboard profiles: advertised (read-only)",
                                         "Yerleşik profiller: bildirildi (salt okunur)"},
            {"Onboard profiles: not advertised",
                                         "Yerleşik profiller: bildirilmedi"},
            {"Battery source: ",          "Pil kaynağı: "},
            {"HID++ protocol: not implemented by this device",
                                         "HID++ protokolü: bu cihaz tarafından uygulanmıyor"},
            {"Onboard DPI / report rate / LOD are not available on this hardware.",
                                         "Donanım DPI / rapor hızı / kaldırma mesafesi bu donanımda kullanılamaz."},
            {"Model ID: ",                "Model Kimliği: "},
            {"Model quirks: known (write-protect policy)",
                                         "Model quirks: biliniyor (yazma-koruma politikası)"},
            {"Daemon live battery: %d%%", "Daemon canlı pil: %d%%"},
            {"Cannot open %s — make sure you are in the 'input' group "
             "(sudo usermod -aG input $USER) and reinstall the udev rule.",
             "%s açılamıyor — 'input' grubunda olduğunuzdan emin olun "
             "(sudo usermod -aG input $USER) ve udev kuralını yeniden kurun."},
            {"Maximum number of points reached.",
             "Maksimum nokta sayısına ulaşıldı."},
            {"LUT point added: speed=%d gain=%s",
             "LUT noktası eklendi: hız=%d kazanç=%s"},
            {"LUT point removed.",
             "LUT noktası kaldırıldı."},
            {"Warning: LUT (%s axis) was truncated to %d points (max capacity).",
             "Uyarı: LUT (%s ekseni) %d noktaya kısaltıldı (maks kapasite)."},
            {"Warning: unsaved changes to \"%s\" were discarded.",
             "Uyarı: \"%s\" için kaydedilmemiş değişiklikler atıldı."},
            {"A profile with that name already exists.",
             "Bu ada sahip bir profil zaten var."},
            {"Maximum number of profiles (%d) reached.",
             "Maksimum profil sayısına (%d) ulaşıldı."},
            {"At least one profile is required.",
             "En az bir profil gerekli."},
            {"Profile reset to defaults — press Save to keep.",
             "Profil varsayılana sıfırlandı — kalıcı yapmak için Kaydet'e basın."},
            {"Starting daemon via systemd...",
             "Daemon systemd üzerinden başlatılıyor..."},
            {" Falling back to direct daemon start...",
             " Doğrudan daemon başlatmaya düşülüyor..."},
            {"Starting daemon...",
             "Daemon başlatılıyor..."},
            {"Stopping daemon via systemd...",
             "Daemon systemd üzerinden durduruluyor..."},
            {"Stopping daemon...",
             "Daemon durduruluyor..."},
            {"Daemon reloaded (IPC).",
             "Daemon yeniden yüklendi (IPC)."},
            {"Daemon reloaded (SIGHUP).",
             "Daemon yeniden yüklendi (SIGHUP)."},
            {"Requesting daemon reload via systemd...",
             "Daemon yeniden yükleme systemd üzerinden isteniyor..."},
            {"fork() failed.",
             "fork() başarısız oldu."},
            {"pkexec could not execute the command (126).",
             "pkexec komutu çalıştıramadı (126)."},
            {"pkexec refused or the command was not found (127).",
             "pkexec reddetti ya da komut bulunamadı (127)."},
            {"pkexec exited with code %d.",
             "pkexec %d koduyla çıktı."},
            {"pkexec was terminated by signal %d.",
             "pkexec %d sinyaliyle sonlandırıldı."},
            {"pkexec rawaccel-daemon %s",
             "pkexec rawaccel-daemon %s"},
            {"Daemon is not running.",
             "Daemon çalışmıyor."},
            {" kill() failed: ",
             " kill() başarısız oldu: "},
            {"rawaccel-daemon not found. Install with: sudo scripts/install.sh",
             "rawaccel-daemon bulunamadı. Kurulum: sudo scripts/install.sh"},
            {"KDE: libinput acceleration disabled. Changes applied immediately.",
             "KDE: libinput ivmesi devre dışı bırakıldı. Değişiklikler anında uygulandı."},
            {"KDE: Could not write to kwinrc. Edit manually: System Settings → Input Devices → Mouse → Pointer Acceleration = Flat.",
             "KDE: kwinrc yazılamadı. Elle düzeltin: Sistem Ayarları → Giriş Aygıtları → Fare → Pointer Acceleration = Flat."},
            {"Permission denied (EPERM) — cannot signal the daemon.\n"
             "Fix: ensure you are in the 'input' group:\n"
             "  sudo usermod -aG input $USER  (then log out and back in)\n"
             "Or restart the daemon from the GUI using pkexec.",
             "İzin reddedildi (EPERM) — daemon'a sinyal gönderilemiyor.\n"
             "Çözüm: 'input' grubunda olduğunuzdan emin olun:\n"
             "  sudo usermod -aG input $USER  (sonra oturumu kapatıp yeniden açın)\n"
             "Ya da daemon'u GUI içinden pkexec ile yeniden başlatın."},
            // ── Dialog labels / prompts ──────────────────────────────────────
            {"Cancel",                     "İptal"},
            {"OK",                         "Tamam"},
            {"Delete",                     "Sil"},
            {"Reset",                      "Sıfırla"},
            {"New Profile",                "Yeni Profil"},
            {"Profile name (e.g. gaming)", "Profil adı (ör. oyun)"},
            {"Preset",                     "Ön Ayar"},
            {"None (blank)",               "Yok (boş)"},
            // ── Preset preview tokens ────────────────────────────────────────
            {"mode",                       "mod"},
            {"gain",                       "kazanım"},
            {"exp",                        "üs"},
            {"limit",                      "limit"},
            {"scale",                      "ölçek"},
            {"cap",                        "cap"},
            {"no acceleration (1:1 raw)",  "ivme yok (1:1 ham)"},
            {"%s → %s",                    "%s → %s"},
            {"Rename Profile",             "Profili Yeniden Adlandır"},
            {"New name",                   "Yeni ad"},
            {"Delete Profile",             "Profili Sil"},
            {"Delete profile \"%s\"?",     "\"%s\" adlı profil silinsin mi?"},
            {" (copy)",                    " (kopya)"},
            {"Reset to Defaults",          "Varsayılana Sıfırla"},
            {"Reset \"%s\" to default values?\nThis cannot be undone.",
             "\"%s\" adlı profil varsayılan değerlere mi sıfırlansın?\nBu işlem geri alınamaz."},
            {"Save Profile As",            "Profil Olarak Kaydet"},
            {"Profile name",               "Profil adı"},
            {"Overwrite Profile",          "Profili Ez"},
            {"Overwrite",                  "Üzerine Yaz"},
            {"Profile \"%s\" already exists.\nIts settings will be permanently replaced with the current settings.\nContinue?",
             "\"%s\" profili zaten var.\nAyarları kalıcı olarak geçerli ayarlarla değiştirilecek.\nDevam edilsin mi?"},
            {"Unsaved Changes",            "Kaydedilmemiş Değişiklikler"},
            {"You have unsaved changes.\nDo you want to quit?",
             "Kaydedilmemiş değişiklikleriniz var.\nÇıkmak istiyor musunuz?"},
            {"Quit Without Saving",        "Kaydetmeden Çık"},
            {"Save and Quit",              "Kaydet ve Çık"},
// ── Save / config messages ───────────────────────────────────────
             {"Saved & reloaded: %s",       "Kaydedildi ve yeniden yüklendi: %s"},
             {"Saved: %s",                  "Kaydedildi: %s"},
             {"Applied & reloaded: %s",     "Daemon'a uygulandı ve yeniden yüklendi: %s"},
             {"Saved locally & signaled daemon (reload only): %s",
                                             "Yerel olarak kaydedildi ve daemon sinyallendi (yalnızca yeniden yükleme): %s"},
             {"Saved locally, but the daemon was not updated: %s",
                                             "Yerel olarak kaydedildi, ancak daemon güncellenemedi: %s"},
             {"Saved locally, but the daemon REJECTED the new config (old config kept): %s",
                                             "Yerel olarak kaydedildi, ancak daemon yeni yapılandırmayı REDDETTİ (eski yapılandırma korundu): %s"},
             {"Saved locally, but the daemon is not running: %s",
                                             "Yerel olarak kaydedildi, ancak daemon çalışmıyor: %s"},
             {"Save error: %s",             "Kaydetme hatası: %s"},
             {"Config load error (%s) — defaults loaded; could not back up %s.",
              "Yapılandırma yükleme hatası (%s) — varsayılanlar yüklendi; %s dosyası yedeklenemedi."},
             {"Config load error (%s) — corrupt file backed up to %s; defaults loaded.",
              "Yapılandırma yükleme hatası (%s) — bozuk dosya %s konumuna yedeklendi; varsayılanlar yüklendi."},
            {"Warning: duplicate device IDs — %s. Only the first matching profile is used by the daemon.",
             "Uyarı: çift cihaz kimliği — %s. Daemon yalnızca eşleşen ilk profili kullanır."},
            {"\"%s\" & \"%s\" share device %s",
             "\"%s\" ve \"%s\", %s cihazını paylaşıyor"},
        };
        auto it = D.find(key);
        if (it != D.end()) return it->second;
    }
    return key;
}

/// trf(key, ...) — format the *current-language* rendering of a source string.
static std::string trf(const char* key, ...) {
    const char* fmt = tr(key);
    va_list ap;
    va_start(ap, key);
    // Two-pass: first measure, then format into exact-size buffer.
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) { va_end(ap2); return fmt; }
    std::string buf(static_cast<size_t>(needed) + 1, '\0');
    vsnprintf(buf.data(), buf.size(), fmt, ap2);
    va_end(ap2);
    buf.resize(static_cast<size_t>(needed));
    return buf;
}

/// Create-and-register helpers for persistent (window-lifetime) widgets.
static GtkWidget* trlbl(const char* key) {
    GtkWidget* w = gtk_label_new(tr(key));
    tr_register(w, TR_LABEL, key);
    return w;
}
static GtkWidget* trmlbl(const char* key) {
    GtkWidget* w = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(w), tr(key));
    tr_register(w, TR_MARKUP, key);
    return w;
}
static GtkWidget* trbtn(const char* key) {
    GtkWidget* w = gtk_button_new_with_label(tr(key));
    tr_register(w, TR_BUTTON, key);
    return w;
}
static GtkWidget* trchk(const char* key) {
    GtkWidget* w = gtk_check_button_new_with_label(tr(key));
    tr_register(w, TR_CHECK, key);
    return w;
}
static void trtip(GtkWidget* w, const char* key) {
    gtk_widget_set_tooltip_text(w, tr(key));
    tr_register(w, TR_TOOLTIP, key);
}

/// Fill a GtkDropDown's model with the translated strings of `keys`.
static void tr_combo_fill(GtkWidget* combo, const char* const* keys) {
    GtkStringList* sl = gtk_string_list_new(nullptr);
    for (int i = 0; keys[i]; i++) gtk_string_list_append(sl, tr(keys[i]));
    gtk_drop_down_set_model(GTK_DROP_DOWN(combo), G_LIST_MODEL(sl));
    g_object_unref(sl);
}

// ── Language preference ───────────────────────────────────────────────────────

static int load_lang_override(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return -1;
    char buf[16] = {};
    (void)!fscanf(f, "%15s", buf);
    fclose(f);
    if (strcmp(buf, "en") == 0) return 0;
    if (strcmp(buf, "tr") == 0) return 1;
    return -1;
}

static void save_lang_pref(const std::string& path, int ov) {
    // Atomic write: write to a temp file then rename to prevent corruption
    // if the process crashes mid-write.  O_NOFOLLOW+O_EXCL prevent symlink
    // following and two-writer races (same policy as kde_atomic_write).
    std::string tmp_path = path + ".tmp";
    int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        // GUI-D2: a stale .tmp left by an earlier crash blocks O_EXCL forever.
        // It cannot be a concurrent write (we hold the main thread), so remove
        // it and retry once — otherwise the language preference silently stops
        // persisting after any crash.
        if (errno == EEXIST) {
            unlink(tmp_path.c_str());
            fd = open(tmp_path.c_str(),
                      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
        }
        if (fd < 0) return;
    }
    FILE* f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(tmp_path.c_str()); return; }
    if (ov <= -1) fputs("auto\n", f);
    else if (ov == 0) fputs("en\n", f);
    else              fputs("tr\n", f);
    if (fclose(f) != 0 || rename(tmp_path.c_str(), path.c_str()) != 0) {
        unlink(tmp_path.c_str());
    }
}

static bool sys_locale_is_turkish() {
    const char* lc = setlocale(LC_MESSAGES, nullptr);
    if (!lc || lc[0] == '\0') lc = getenv("LANG");
    return lc && (strncmp(lc, "tr_", 3) == 0 || strcmp(lc, "tr") == 0);
}

static int resolve_lang(int override) {
    if (override == 1) return 1;
    if (override == 0) return 0;
    return sys_locale_is_turkish() ? 1 : 0;
}

// ── Runtime language switch ───────────────────────────────────────────────────

static void refresh_language(AppState* S) {
    S->updating = true;

    // Rebuild translated dropdown models (selection restored right after).
    static const char* MODE_KEYS[] = {
        "None (1:1)", "Classic", "Power", "Natural", "Jump", "Synchronous",
        "Lookup (LUT)", nullptr };
    static const char* CAP_KEYS[] = {"Output (out)", "Input (in)", "I/O (io)", nullptr};
    static const char* DIST_KEYS[] = {"Euclidean", "Max", "Lp", "Separate", nullptr};
    static const char* LOD_KEYS[]  = {"Low", "Medium", "High", nullptr};

    if (S->mode_combo)      tr_combo_fill(S->mode_combo, MODE_KEYS);
    if (S->mode_combo_y)    tr_combo_fill(S->mode_combo_y, MODE_KEYS);
    if (S->cap_mode_combo)  tr_combo_fill(S->cap_mode_combo, CAP_KEYS);
    if (S->dist_mode_combo) tr_combo_fill(S->dist_mode_combo, DIST_KEYS);

    // GUI-O3: the HID++ lift-off combo is built with tr()'d entries at
    // construction; without a rebuild here, switching language leaves stale
    // strings in a device they were never set in.  Selection survives (indices
    // are stable across languages).
    if (S->hw_lod_combo) {
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(S->hw_lod_combo));
        tr_combo_fill(S->hw_lod_combo, LOD_KEYS);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(S->hw_lod_combo), sel);
    }

    // Device combo: "All devices (default)" plus device labels (names untranslated).
    if (S->device_id_combo) {
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(S->device_id_combo));
        GtkStringList* sl = gtk_string_list_new(nullptr);
        gtk_string_list_append(sl, tr("All devices (default)"));
        for (auto& m : S->mice_list) {
            std::string lbl = m.name + "  [" + m.event_node + "]";
            gtk_string_list_append(sl, lbl.c_str());
        }
        gtk_drop_down_set_model(GTK_DROP_DOWN(S->device_id_combo), G_LIST_MODEL(sl));
        g_object_unref(sl);
        // R7-LANGDM: the rebuilt model has no "(unplugged)" placeholder (that is
        // appended by device_combo_select() from devices.inl, called after this
        // refresh via rebuild_profile_combo).  If the saved selection pointed at
        // the old placeholder, sel is now out of range — clamp instead of passing
        // an invalid index to gtk_drop_down_set_selected (GLib critical on stderr
        // + transient blank binding; self-corrected on the next line anyway).
        guint n = g_list_model_get_n_items(G_LIST_MODEL(sl));
        if (sel >= n) sel = n - 1;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(S->device_id_combo), sel);
    }

    // Re-apply every registered static widget in place.
    for (auto& e : g_tr_registry) {
        const char* t = tr(e.key.c_str());
        switch (e.kind) {
        case TR_LABEL:   gtk_label_set_text(GTK_LABEL(e.w), t);          break;
        case TR_MARKUP:  gtk_label_set_markup(GTK_LABEL(e.w), t);        break;
        case TR_BUTTON:  gtk_button_set_label(GTK_BUTTON(e.w), t);       break;
        case TR_CHECK:   gtk_check_button_set_label(GTK_CHECK_BUTTON(e.w), t); break;
        case TR_TOOLTIP: gtk_widget_set_tooltip_text(e.w, t);            break;
        }
    }

    S->updating = false;

    // Restore combo selections from the profile + re-apply per-mode visibility.
    rebuild_profile_combo(S);   // -> profile_to_widgets, update_mode_sensitivity
    update_daemon_status(S);    // daemon status + battery markup are dynamic
    mouse_test_refresh_language(S); // open test window stays in sync (BUG-09)

    // Translate the idle status if it still shows a ready-state in either
    // language.  The old check only matched the English default, so switching
    // Turkish → English left a stale "Hazır." on the bar indefinitely.
    if (S->status_bar) {
        const char* cur = gtk_label_get_text(GTK_LABEL(S->status_bar));
        const char* ready = tr("Ready.");
        if (cur && (strcmp(cur, "Ready.") == 0 ||
                    (ready && strcmp(cur, ready) == 0)))
            gtk_label_set_text(GTK_LABEL(S->status_bar), ready);
    }
}

static void on_lang_changed(GtkDropDown* dd, GParamSpec*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (S->updating) return;
    int sel = (int)gtk_drop_down_get_selected(dd);
    int ov  = sel - 1; // -1 = auto (locale), 0 = English, 1 = Türkçe
    if (ov == S->lang_override) return;
    S->lang_override = ov;
    if (!S->lang_path.empty()) save_lang_pref(S->lang_path, ov);
    g_lang = resolve_lang(ov);
    refresh_language(S);
}
# Bug Hata Raporları — Linux RawAccel

*Raporlayan: aj3 (big-pickle)*
*Tarih: 10 Eylül 2026*
*Yöntem: kaynak kod satır-satır analiz — düzeltme YAPILMADI, yalnız raporlama.*

> Bu dosya, programın her köşesinin detaylı analizi sonucu bulunan tüm hata, bug ve
> eksiklikleri toplar. Bulgu bulundukça dosyaya işlenir. Son büyük bölüm (Bölüm 11+)
> en güncel bağımsız bug-avı turunu içerir.

---

## İçindekiler

1. Önceki turların kapalı bulguları (BUG-01..21, P-serisi, FINDING*/RISK-DEEP*)
2. Yeni turlar: G-BUG-1 .. G-BUG-n & doğrulanmış temiz yollar (Bölüm 11+)

---

## Bölüm 1 — Önceki turların kapalı bulguları (özet)

Önceki tur raporları (bug_raporlari.md / bug_raporlari_aj3.md / bug_raporları) kullanıcı
tarafından silindi; kesin listeler `aihaberlesme.md` mesaj günlüğünde saklıdır. Bu bölüm
tüm önceki turlarda raporlanıp düzeltilmiş / kapalı olan madde kimliklerini listeler
(ayrıntılar `aihaberlesme.md` + CHANGELOG'da):

- **BUG-01..BUG-21** — evdev/uinput, config, IPC, LUT, SYN_DROPPED, hotplug, telemetri
  hataları (çoğu 0.5.0-0.6.4 arasında düzeltildi).
- **P-serisi (P1..P165)** — perf/metrik/parametre/dokümantasyon/yorum bulguları.
- **T-serisi (T1..T30+)** — görev/mimari doğrulamaları.
- **A5-01..A5-10** — P115 kullanıcı emri bug avı (hepsi düzeltildi).
- **FINDING-D1..D5, G1..G4, A1..A5, C1..C4, T1..T3, RISK-DEEP-1..5** — detay bulguları.

Bu raporda yalnızca **yeni** (önceki turlarda raporlanmamış) bug'lar + bu turda doğrulanan
temiz yollar sayılır.

---

# Bölüm 11 — Agresif Bug Avı: Yeni Bulgular (10 Eylül 2026)

Kapsam: `daemon/daemon.cpp` (tam), `cli/main.cpp` (tam), `include/rawaccel.hpp`,
`src/config.cpp`, `include/presets.hpp`, `include/config.hpp`, `gui/graph.inl`,
`gui/mouse_test.inl`, `gui/daemon_comm.inl` (status kısmı), `include/accel-lookup.hpp`,
`include/accel-*.hpp` (jump/power/natural/classic/synchronous). Satır-satır okundu.

## G-BUG-1 — LUT düzenleyici speed spin'i 500'e clamp → **sessiz veri bozulması** (GUI)

- **Konum:** `gui/graph.inl:406,408` (spin aralığı), `gui/graph.inl:443-492` (`lut_list_changed`)
- **Senaryo:**
  1. `graph.inl:406` — speed spin aralığı `new_with_range(0.0, 500.0, 0.5)`.
  2. JSON'dan içe aktarılan veya `on_lut_add_point` (son nokta 495 iken 505 üretir,
     `graph.inl:504`) ile eklenen bir LUT noktasının speed'i > 500 olabilir —
     `src/config.cpp` LUT nokta speed'lerini hiçbir üst sınıra tabi tutmaz (yalnızca
     float kapsamına). DPI 32000'de fiziksel 14.5 ips üzeri kolayca 500+ olur; yüksek
     hızlı fling'lerde 600-1000+ ips normaldir.
  3. `rebuild_lut_list` içinde `gtk_spin_button_set_value(..., pts[i].first)`
     (`graph.inl:408`) 500 üstü değeri **ekranda sessizce 500'e** düşürür.
  4. Kullanıcı listedeki HERHANGİ bir spin'e dokunduğunda `on_lut_spin_changed` →
     `lut_list_changed` çalışır; bu fonksiyon noktaları `ax.data`'dan DEĞİL, tüm row'ların
     spin button'larından `gtk_spin_button_get_value` ile yeniden okur
     (`graph.inl:456-460`) → >500'lük nokta veri olarak **500 olarak geri yazılır** ve
     `lut_set_points` (sıralama + yazma) ile `ax.data`, ardından `save_config` ile kalıcı
     olur. Ekran kapatılıp açılsa bile bozulmuş değer değişmez.
- **Etki:** İnce ayarlanmış yüksek-hız bölgesi noktaları tek spin dokunuşuyla kalıcı
  olarak 500 ips'e çakılır; kullanıcıya hiçbir uyarı/onay verilmez (sessiz veri kaybı).
- **Öncelik:** Orta-Yüksek (veri bozulması + sessiz).
- **Öneri (uygulanmadı):** ya speed spin üst sınırı kaldırılmalı ya da `lut_list_changed`
  `ax.data`'dan geri okumalı; 500 üstü noktada açık uyarı gösterilmeli.

## G-BUG-2 — `append_fixed` yerel-ayar onarımı ayraç virgülünü noktaya çeviriyor → **status JSON bozuk** (Daemon)

- **Konum:** `daemon/daemon.cpp:1499-1511` (`append_fixed`), çağrıları `:1643-1658`.
- **Kanıt (çalıştırılan repro, /tmp/opencode/probe.cpp):**
  `append_fixed` snprintf biçimi `",\"%s\":%.*f"` ile **baştaki ayraç virgülünü de**
  tampona yazar; döngü (`for ... if (nb[i]==',') nb[i]='.'`) **tüm** virgülleri
  (locale onarımı amacıyla) noktaya çevirir — buna `"lat_samples"`/`"lat_avg_us"` vb.
  önündeki alan ayracı DAHİL. Üretilen örnek:
  `...,"lat_samples":1234."lat_avg_us":12.50."lat_p50_us":100.00...`
  `nlohmann::json::parse` → `parse_error` (probe doğruladı: col 56, "invalid number;
  expected digit after '.'"). Tüketici `cli/main.cpp` de aynen bu hata ile çakılıyor.
- **Tetikler:** Bir cihazda hareket olur olmaz telemetri dolar (`telem_ok=true`) ve/veya
  latency örneği varsa (`lat_samples>0`) `status_json` `append_fixed` çağırır →
  **sonraki TÜM status yanıtları geçersiz JSON** olur (ilk fare hareketinden sonra kalıcı).
- **Etki:**
  - `rawaccel-cli status` (`cli/main.cpp:1330`) — `nlohmann::json::parse(resp)` throw →
    canlı cihaz detayları (algılanan DPI/poll/batarya/hız) **hiç gösterilmez**, yerine
    "(daemon unreachable for live device details: ...)" hatası basılır.
  - `rawaccel-cli status --json` (`cli/main.cpp:1262`) — aynı throw → `out["devices"]`
    asla doldurulmaz, `device_error` yazılır. Skriptler boş/hatalı çıktı görür.
  - GUI (`daemon_device_field`, `gui/mouse_test.inl`, `on_perf_clicked`) kendi esnek
    (substring/strtod) parser'ını kullandığı için **şans eseri** çalışır: `strtod`
    ikinci noktada durur ve kısmi sayıyı döndürür. Yani hata yalnızca CLI/dış tüketicileri
    vurur — GUI maskeler.
  - "status JSON" belgelenmiş bir JSON uç noktasıdır (AGENTS.md P121/BUG-05/06 telemetri
    sözleşmesi); sözleşme ihlali dış tüketicileri de etkiler.
- **Öncelik:** Yüksek (belgelenmiş JSON yükü bozuk; CLI canlı cihaz özelliği ölü).
- **Öneri (uygulanmadı):** onarımı yalnızca değer bölgesine uygula (ayraç virgülünü
  koru), ör. `ostringstream` + `use_facet<numpunct<char>>` ile locale yerel ayar noktasını
  değiştir ya da snprintf'ten sonra yalnızca ilk ':' sonrasını onar.

## Bu turda doğrulanıp "bug değil / korumalı" olarak kapatılan adaylar

- **speed clamp + accel sıralaması** (`rawaccel.hpp:282-289`): clamp'ı delta'ya oran
  olarak uygulamak, referans RawAccel'in input speed'i clamp'layıp gain uygulamasıyla
  matematiksel birebir (|in'|·f = clamp(|in|·f)).
- **IPS_FACTOR_MAX=1e6 clamp** (`rawaccel.hpp:250-252`): yalnız DPI=1 + 1µs interval'de
  aktif; gerçekçi değildir.
- **`time<=0` erken dönüşü** (`rawaccel.hpp:242`): EMA smoother'ları zehirlenmeden korur.
- **Boş/tek LUT** (`accel-lookup.hpp:56,62`): length=0/1 → pairs=0 → noaccel (güvenli);
  duplicate-X denominator=0 guard (O2), t=±Inf korumalı.
- **`speed_processor::init`** lp_norm epsilon karşılaştırması; `separate` bayrağı,
  Lp/max/separate modları (oracle grid 1047 satırda doğrulandı).
- **config.cpp `sanitize_*` zinciri**: NaN/Inf→default, kapaklar (accel 1e-4..1e6,
  DPI 1..32000, poll 125..8000, LUT length kapasite, domain/range 0..1e6 P86 ceiling),
  JSON tip guard'ları, 256-char name/device_id, version-stamped migration (P43-BF1).
- **Daemon event sıralaması**: REL birikimi, non-motion'da flush, SYN_DROPPED penceresi
  (kirlenmiş olaylar atılır), raw passthrough birebir iletim — girdi sırası korunuyor.
- **status_json seqlock okuma** (telem_samples release-bump → bağlı retry, `telem_ok=false`),
  **config atomic yazma + .bak hardlink**, **IPC deadline'ları** (10s/5s/1MB body cap).
- **CLI arity/domain/NaN guard'ları** (P115/P120/P82 düzeltmeleri korunmuş), trailing
  `-c` guard, unknown-command guard, `--flag=value` formları.
- **presets.hpp** değerleri (gaming/office/cs2/valorant/apex/fps): cap/limit tutarlı,
  `make_preset`'te scale ≤ SCALE_MAX, cap ≤ CAP_X_MAX/CAP_Y_MAX.
- **motion_math/lat_stats'ın satır-satır doğrulaması**: histogram finite guard,
  clamps — temiz.
- **Daemon uinput_write hatası → disconnect bayrağı, subpixel remainder taşıma,**
  **NOW mono_RAW tek saat kaynağı, batched REL write** (P93) — doğru.

## Bölüm 11 Sonuçları

- **Yeni orta/yüksek hata:** 2 (G-BUG-1: GUI LUT sessiz veri kaybı; G-BUG-2: status JSON bozuk)
- **Kapatılan adaylar:** 12 (doğru/korumalı yollar)
- **Not:** G-BUG-2 GUI parser'ı tarafından maskeleniyor; sıkı JSON tüketicileri (CLI +
  skriptler) tamamen kırıldığı için hata gerçek ve kullanıcı görünür.

---
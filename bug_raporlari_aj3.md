# RawAccel Linux - Bug & Hata Raporu
## Hazirlayan: aj3 | Tarih: 2026-09-10

Bu dosya, RawAccel Linux projesinde tespit edilen hatalari, duzeltilmis
sorunlari ve potansiyel riskleri belgelemek icin hazirlanmistir.

---

# BOLUM 1: MOUSE HAREKET HESAPLAMA HATALARI

## BUG-01 (DUZELTILMIS): Synchronous motivity<1 egrisi ters donme
- **Dosya:** include/accel-synchronous.hpp:82-93
- **Durum:** Duzeltildi
- **Ozet:** motivity<1 degerinde tanh aktivasyonu tek simetrisini kaybediyordu.
  gamma_const negatif oldugunda z degiskeni egrinin isaretini tasiyordu, ancak
  onceki kod `log_diff` uzerinden dalga cizmisti. Bu, `smooth=0.5, motivity=0.3`
  gibi ayarlarda yerel egriyi tersine ceviriyordu (1/m yerine m donusu).
- **Duzeltme:** `|z|` ile `pow()` sonlu tutuldu, `sign(z)` ile tek simetri
  geri yuklendi. `motivity>1` icin bit-identical kaldi.
- **Oyun etkisi:** YUKSEK - Synchronous mod kullanan oyuncular yanlis ivmelenme
  algilayabilirdi.

## BUG-02 (DUZELTILMIS): Power exponent tutarliligi
- **Dosya:** include/accel-power.hpp:19-36
- **Durum:** Duzeltildi
- **Ozet:** `scale_from_gain_point` ucu 1e-3'te taban aliyordu ama
  `base_fn_impl` ham `exponent_power` kullaniyordu. Sonuc: ep [1e-4, 1e-3)
  araliginda `gain(cap_x) < cap_y` olusuyordu - kullanici istedigi kapa
  ulasamiyordu.
- **Duzeltme:** Tek paylasilan `exponent = max(ep, 1e-3)` her yerde kullanildi.
- **Oyun etkisi:** DUSUK - Bu parametre araligi GUI'dan secilemez, sadece ham
  API ile ulasilabilir.

## BUG-09 (DUZELTILMIS): Classic in-cap mode NaN uretimi
- **Dosya:** include/accel-classic.hpp:85-94
- **Durum:** Duzeltildi
- **Ozet:** `cap_mode::in` ve `cap.x <= input_offset` durumunda `base_fn`
  `pow(negatif_taban, kesirli_us)` hesapliyordu. IEEE 754 gore NaN uretiyordu.
  Bu NaN `minsd(finite, NaN)` uzerinden tum ciktilari zehirliyordu - fare
  hareket tamamen kayboluyordu.
- **Duzeltme:** `cap.x > 0 && cap.x > input_offset` kosulu eklendi.
- **Oyun etkisi:** ORTA - Degenerate konfigurasyonlarda fare tamamen duruyordu.

## BUG-15 (DUZELTILMIS): Subpixel remainder tasmasi
- **Dosya:** daemon/motion_math.hpp:55-67
- **Durum:** Duzeltildi
- **Ozet:** Patholojik buyuk giris (ornegin hatali surucu INT_MAX REL olaylari
  enjekte ettiginde) motion INT araligina kitlendiginde "kalan" degeri
  milyarlarca olabiliyordu. Saglam bir kalan her zaman (-1, 1) araligindadir.
  Buyuk kalan sonraki her kareyi de INT_MAX'a kitliyordu - donme etkisi.
- **Duzeltme:** `|kalan| >= 1.0` ise sifirlanir. Ayni zamanda NaN/Inf korumasi.
- **Oyun etkisi:** YUKSEK - Patholojik durumlarda fare donme efekti.

## BUG-18 (DUZELTILMIS): SYN_DROPPED durum kaybi
- **Dosya:** daemon/daemon.cpp:1272-1275
- **Durum:** Duzeltildi
- **Ozet:** `syn_dropped` bayragi fonksiyon yerel degiskeni olarak
  tanimliyordu. Eger SYN_DROPPED bir okuma toplu isinin sonuna duserse ve
  eslesme SYN_REPORT bir sonraki epoll dongusune kalirsa, bayragi sifirlanir
  ve guvensiz olaylar iletilirdi. Linux giris protokolune gore SYN_DROPPED
  ile SYN_REPORT arasindaki TUM olaylar guvensizdir.
- **Duzeltme:** `syn_dropped` artik `mouse_device` yapisi uyesi, cagri
  arasinda korunur.
- **Oyun etkisi:** YUKSEK - Güvensiz olaylar fare atlamasina neden olur.

---

# BOLUM 2: CLI / KONFIGURASYON HATALARI

## BUG-10 (DUZELTILMIS): CLI uzerinden NaN/Inf JSON yazilmasi
- **Dosya:** cli/main.cpp:819-826
- **Durum:** Duzeltildi
- **Ozet:** `std::stod("NaN")` degeri NaN uretiyordu ve JSON olarak
  "NaN" olarak kaydediliyordu. JSON standartina gore gecersiz.
  Daemon yukleme sirasinda sanitize ederdi ama dosya kendisi bozuk kalirdi.
- **Duzeltme:** `isfinite()` kontrolu ile NaN/Inf reddedilir.
- **Oyun etkisi:** DUSUK - Sadece dogrudan JSON duzenleme ile ulasilabilir.

## BUG-11 (DUZELTILMIS): CLI numeric parse trailing garbage
- **Dosya:** cli/main.cpp:806-817
- **Durum:** Duzeltildi
- **Ozet:** `std::stod("1.5junk")` sessizce 1.5 donduruyordu. Yazim
  hatalari farketmeden kaydedilirdi.
- **Duzeltme:** `pos` parametresi ile tum stringin tugadigi dogrulanir.
- **Oyun etkisi:** DUSUK - Yanlis degerler sanitize ile sinirlandirilir.

## BUG-12 (DUZELTILMIS): Bos profil adi
- **Dosya:** cli/main.cpp:407-413
- **Durum:** Duzeltildi
- **Ozet:** Bos profil adi sonraki tum komutlarda belirsizlik olusturuyordu.
  `delete ""`, `show ""`, `set-param ""` gibi islemler anlamaszdi.
- **Duzeltme:** Bos ad reddedilir.
- **Oyun etkisi:** DUSUK

## BUG-19 (DUZELTILMIS): Yanlis mode degeri sessizce noaccel olurdu
- **Dosya:** cli/main.cpp:753-768
- **Durum:** Duzeltildi
- **Ozet:** "classicc" gibi yazim hatasi yapilan mode degeri sessizce
  noaccel'a donuyordu - kullanici farkinda bile olmadan ivmelenme devre
  disi kalirdi.
- **Duzeltme:** Strict validation, bilinmeyen mode hatasi dondurur.
- **Oyun etkisi:** ORTA - Ivmelenme sessizce devre disi kalabilirdi.

## BUG-21 (DUZELTILMIS): Raw passthrough latency dump mesaji
- **Dosya:** daemon/daemon.cpp:1401-1410
- **Durum:** Duzeltildi
- **Ozet:** Raw passthrough modunda `flush_motion()` hic cagirilmaz, bu
  yuzden `dev.lat` hic guncellenmez. Eski mesaj "No motion events" idi ve
  kullanicilari yaniltiyordu.
- **Duzeltme:** Explicit "raw passthrough - per-event measurement yok" mesaji.
- **Oyun etkisi:** DUSUK - Sadeleiksel.

---

# BOLUM 3: GERCEK ZAMANLI PERFORMANS ANALIZI (OYUNCU ODAKLI)

## 3.1 Hot Path (Sicak Yol) Maliyet Analizi

Her fare olayi icin toplam maliyet olcumleri:

| Katman                          | Maliyet per olay    | Sonuc                    |
|---------------------------------|---------------------|--------------------------|
| Ivmelenme math + EMA smooth     | 20-425 ns           | Darbe degil              |
| Subpixel accumulation           | ~43 ns              | Onemsiz                  |
| Kernel okuma (evdev)            | p50 ~7.5 us         | Syscall hakim             |
| Kernel yazma (uinput delivery)  | p50 ~25-35 us       | Ana maliyet terimi       |

**Sonuc:** Ivmelenme matematiği 20-50 ns (tek-eksen); en kotu durum
(both axes + 4 EMA) ~425 ns. 8000 Hz'de (125 us kare butcesi)
CPU'nun ~%2'sini kullanir. **Mevzu degil.**

## 3.2 Syscall Modeli (3 syscall per olay)

1. `clock_gettime(CLOCK_MONOTONIC_RAW)` #1 - flush_motion girisi
2. Tek batch `write()` - hizlandirilmis REL_X+REL_Y (P93 optimizasyonu)
3. `clock_gettime` #2 - bitis

**Not:** epoll_wait 10ms timeout sadece housekeeping icin (hot-plug, IPC,
sinyaller). Hareket olaylari es zamanli olarak islenir, asla ertelenmez.

## 3.3 Tepki Suresi (Response Time) Analizi

### EMA Smoothing - Tek "hissedilebilir gecikme" VATKASI

input_speed_smooth_halflife degeri synchronous modda en onemli
parametredir:

| Input halflife | 95% oturma | 99% oturma | Yanlis-gain penceresi      |
|----------------|-----------|-----------|---------------------------|
| 0 (varsayilan) | 0         | 0         | yok - aninda               |
| 10 ms          | ~14 ms    | ~18-35 ms | ~19 ms (1 60Hz kareye yaklasik) |
| 100 ms         | ~148 ms   | ~243-383 ms | ~190 ms hizli/costurucu |

**Oyuncu onerisi:**
```
input_speed_smooth_halflife = 0      # veya <= 10 ms, asla 100 ms
smooth = 0.25-0.5, motivity <= 2, gamma = 1
sync_speed takip bandinin DISINDA (veya halflife=0 ile ustunde)
```

### Islem Gecikmesi (Processing Latency) Referans Degerleri

Ornek canli olcum (calisan daemon):
```
=== RawAccel Processing Latency ===
  Device: P57 GameSpeed Test Mouse
    Samples  : 7158
    Min      : 0.62 us
    Avg      : 2.29 us
    p50      : 2.25 us
    p95      : 3.25 us
    p99      : 4.25 us
    Max      : 96.14 us
===================================
```

"iyi" gorunen degerler (proje referans olcumleri):

| Veri seti                          | p50    | p95    | p99    |
|------------------------------------|--------|--------|--------|
| P31 sentetik hot-path (VM)         | 33     | 86     | 266    |
| P57/P64/P73 canli daemon (VM)      | 1.75-2.25 | 2.75-3.75 | 3.75-5.25 |
| P94/P101 precision ramp (VM)       | 1.75   | 3.25-3.75 | 4.75  |
| **Gercek donanim hedefi**          | **tek haneli** | **tek haneli** | **< ~10** |

**p99'da tek haneli mikrosaniye** oyun icin hedef degerdir.

---

# BOLUM 4: SENSOR TAKIP VE DONANIM ANALIZI

## 4.1 DPI Normalizasyonu

Giris hizlari normalizesi: `input_ips = (counts/s) * (1000 / device_dpi)`
Normalizasyon sabiti: `NORMALIZED_DPI = 1000`

**Oneemli:** Profil `dpi` degeri farenin gercek hardware ayari ile eslesmelidir.
Eslesmezse ips hesaplari yanlis olur ve ivmelenme egrisi dogru calismaz.

Oneri:
- Farenin dogal DPI adimini tercih et (sensor interpolasyon/duzeltme
  yaptigi adimlari kacir - "yikanmis" his)
- Daha fazla DPI = daha hizli degil! Ekrandaki mesafe normalize edilmistir.
  Daha yuksek DPI sadece granulariteyi artirir (daha hassas alt-piksel).

## 4.2 Polleme Hizi Tespiti

Daemon sysfs uzerinden polleme hizini otomatik tespit eder:
- Dogrudan `polling_rate` sysfs dugumu (bazı HID suruculeri acar)
- USB bInterval + USB hiz sinifi tespiti (high-speed: 125us mikroframe)

Tespit araligi: 125-8000 Hz (POLL_RATE_MIN/MAX)

**Oyun onerisi:** Faredeki gercek donanim hizini kullan. Daemonin degeri
yukari cikarmanin kazanc yoktur - sadece ips normalizasyonunu etkiler.

## 4.3 Pil Seviyesi Tespiti

En iyi cabayla pil tespiti:
- Sadece farenin kendi `power_supply` alt agacindan
- `type == "Battery"` dosyasindan dogrulama
- Laptop pili ile karistirilmaz (P121/BUG-04 korumasi)
- Logitech HID++ bildirimleri ile canli pil guncelleme (ayri thread)

## 4.4 Hot-Plug Davranisi

- inotify ile `/dev/input` izleme
- 8 x 10ms = ~80ms bekleme (USBHub enumerate ~50-100ms)
- Basarisiz cihazlar icin 10sn reddetme listesi (backoff)
- Stabil cihaz ID'leri: `usb:VVVV:PPPP:serial` formati ile reboot-stable
- Boss cihaz durumunda her ~2sn otomatik tarama (self-healing startup)

---

# BOLUM 5: POTANSIYEL RISKLER VE EDGE CASELER

## RISK-1: Natural mode limit ~= 1 hassasiyeti
- **Dosya:** include/accel-natural.hpp:17-24
- **Aciklama:** `limit` alani `args.limit - 1.0` olarak hesaplanir.
  `limit = 1.0` ise `abs_limit < 1e-9` olur ve `accel = decay_rate / 1.0`.
  Formul: `0 * (...) + 1 = 1.0` (identity - dogru).
  Ama `limit = 0.9999999` ise `abs_limit ~ 1e-7`, `accel ~ decay_rate * 1e7`
  olur - cok hizli ustel azalma. Kullanicilari sasirtabilir.
- **Oyun etkisi:** DUSUK - Bu deger araliginde oyun oynanmaz.

## RISK-2: LUT sirinamis noktalarda bolme-sifir
- **Dosya:** include/accel-lookup.hpp:96-106
- **Aciklama:** El ile duzenlenmis LUT'larda tekrar eden X degerleri
  `denom = bx - ax = 0` uretir. Duzeltme mevcut: sonraki noktanin
  degerini dondurur. Gecerli kesin artan tablolar etkilenmez.
- **Oyun etkisi:** YOK (duzeltildi)

## RISK-3: USB polleme hizi tespit yanilsamasi
- **Dosya:** daemon/daemon.cpp:147-192
- **Aciklama:** Bazi fareler sysfs'de bInterval/exposure sunmaz.
  Tespit basarisiz olursa `polling_rate = 0` (bilinmiyor) doner.
  Daemon calismaya devam eder ama ips hesaplari yanlis olur.
- **Oyun etkisi:** ORTA - Canli telemetry yanlis gosterebilir.

## RISK-4: IPC kitlesi altinda canli profil guncelleme
- **Dosya:** daemon/daemon.cpp:746-773
- **Aciklama:** Config push sirasinda `devices_mutex_` tum cihazlar
  uzerinde dondurulur ve profiller tekrar uygulanir. Tipik < 1 us
  ama cok sayida cihaz ile bir miktar gecikme olabilir.
- **Oyun etkisi:** DUSUK - Tek seferlik, milisaniye alti.

## RISK-5: EMA katsayisi=0 durumu
- **Dosya:** include/rawaccel.hpp:17-20
- **Aciklama:** `halfLife = 0` ise `windowCoefficient = 0`,
  `cutoffCoefficient = 1.0 - sqrt(1-0) = 0`. `smooth()` fonksiyonu:
  `twc = 1-0 = 1`, `tcc = 1-0 = 1`. Her cagri `windowTotal = speed`,
  `cutoffTotal = speed`. `min(speed, speed) = speed`. Dogrudan gecis -
  sorun degil.
- **Oyun etkisi:** YOK

## RISK-6: lp_distance sifir vektor guard
- **Dosya:** include/rawaccel.hpp:149-162 (speed_processor)
- **Aciklama:** `lp_distance(in, norm)` icin `in = (0,0)` ise lp-norm 0
  doner. Ancak `calc_speed_whole` cagrisindan once `abs_vel` hesaplaniyor
  ve `ips_factor` buyukse sifir olmaz. Yine de `lp_distance` icinde
  sifir vektor guard'i mevcut.
- **Oyun etkisi:** YOK

---

# BOLUM 6: BILINEYEN MEVCUT BUG LITERALARI (KOD ICINDE)

Asagidaki bug numaralari kod icindeki yorumlarda belgelenmistir ve
hepsi DUZELTILMISTIR:

| Bug ID  | Kisa Aciklama                                    | Dosya                       |
|---------|--------------------------------------------------|-----------------------------|
| BUG-01  | Synchronous motivity<1 tek simetri kaybi          | accel-synchronous.hpp:82    |
| BUG-02  | Power exponent tutarsizligi                       | accel-power.hpp:19          |
| BUG-04  | Pil seviyesi laptop pili ile karistirma           | daemon.cpp:222              |
| BUG-05  | Raw passthrough telemetry contract                | daemon.cpp:1193             |
| BUG-06  | telem_wall_ms status JSON'da yayimlanma           | daemon.cpp:1203             |
| BUG-07  | event_num_from_path / sysfs_read_int range        | daemon.cpp:122-140          |
| BUG-09  | Classic in-cap NaN uretimi                        | accel-classic.hpp:85        |
| BUG-10  | CLI NaN/Inf JSON yazilmasi                        | cli/main.cpp:819            |
| BUG-11  | CLI trailing garbage parse                        | cli/main.cpp:806            |
| BUG-12  | Bos profil adi                                    | cli/main.cpp:407            |
| BUG-15  | Subpixel remainder tasma                          | motion_math.hpp:55          |
| BUG-18  | SYN_DROPPED durum kaybi                           | daemon.cpp:1272             |
| BUG-19  | Bilinmeyen mode sessiz noaccel                    | cli/main.cpp:753            |
| BUG-21  | Raw passthrough latency dump mesaji               | daemon.cpp:1401             |

---

# BOLUM 7: OYUNCU ICIN KISA KILAVUZ

## "Nasil hissediyorum?" Sorun Giderme

| Belirti                                    | Muhtemel Neden                            | Cozum                              |
|-------------------------------------------|-------------------------------------------|------------------------------------|
| Fare yavas/agir hissediyor               | KDE/GNOME cift ivmelenme                  | `kde-fix-accel.sh` calistir        |
| Synchronous modda costurucu his           | input halflife > 50ms                     | halflife'i 0 veya <= 10ms yap      |
| Sik aralikla kucuk ziplar                 | SYN_DROPPED (kernel buffer overflow)      | Kernel buffer artir / diger surucu  |
| DPI degistirince hiz degisti              | Profil DPI != gercek DPI                  | Profil DPI'yi farenin gercegine esle|
| Yavas hareketlerde kayip                  | Polleme hizi tespit edilemedi             | Polleme hizini manuel ayarla        |
| p99'da buyuk degerler (>100us)            | VM jitter veya scheduler hiccup           | Gercek donanim ile test et          |

## Optimal Oyun Ayarlari (Genel Oneri)

```
# Ivmelenme modu: preference'a bagli
# Sync mod icin:
input_speed_smooth_halflife = 0    # EN ONEMLI - gecikmeyi sifirlar
smooth = 0.25-0.5
motivity <= 2
gamma = 1
sync_speed takip hizinin biraz uzerinda veya asagisinda

# Genel:
DPI = farenin gercek native DPI degeri
polleme = farenin gercek donanim hizi
output_dpi = 1000 (varsayilan - dokunma)
```

---

# BOLUM 8: DOSYA SORUMLULUKLARI

| Dosya/Dizin                          | Icerik                                    |
|--------------------------------------|-------------------------------------------|
| include/accel-*.hpp                  | Ivmelenme algoritmalari (header-only)     |
| include/rawaccel.hpp                 | Modifier + EMA smoother motoru            |
| daemon/daemon.cpp                    | evdev/uinput uygulamasi, hot-plug         |
| daemon/motion_math.hpp               | Alt-piksel birikme + modifier cagrisi     |
| daemon/lat_stats.hpp                 | Gecikme histogrami (us)                   |
| cli/main.cpp                         | rawaccel-cli komutlari                    |
| gui/main.cpp                         | rawaccel-gui giris noktasi                |
| src/config.cpp                       | JSON serilestirme (nlohmann/json)         |
| tests/test_accel.cpp                 | 183 test grubu, 33738 assertion           |
| tests/oracle/                        | Referans capis testi (1047 satir)         |

---

*Rapor: aj3 tarafindan 2026-09-10 tarihinde hazirlanmistir.*
*Kod versiyonu: 0.6.4*

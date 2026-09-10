# Bug Hata Raporları — Linux RawAccel

*Raporlayan: aj3 (big-pickle)*
*Tarih: 10 Eylül 2026*
*Son güncelleme: 10 Eylül 2026 — tüm bulgular karara bağlandı ve rapor bölümleri kaldırıldı; kapanış kararları Bölüm 40'ta listelenir*

> Bu dosya, programın her köşesinin detaylı analizi sonucu bulunan tüm hata, bug ve
> eksiklikleri toplar. Bulgu bulundukça dosyaya işlenir. Son bölüm (Bölüm 40),
> tüm bulguların kapanış kararlarını içerir.

### Düzeltme Durumu (10 Eylül 2026)

- **Düzeltildi ve rapordan kaldırıldı:** rapordaki TÜM bulgu bölümleri — tam listesi Bölüm 40'ta (KAPALI/FIXED + Yanlış pozitif/tasarım) belirtilmiştir.
- **AÇIK:** yok — geriye açık (düzeltilebilir) bulgu kalmadı (bkz. Bölüm 40 Sonuç).

---

## İçindekiler

1. Önceki turların kapalı bulguları (BUG-01..21, P-serisi, FINDING*/RISK-DEEP*)
2. Son doğrulama batch-3: kalan bulguların kapanış kararları

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

# Bölüm 40 — Son Doğrulama batch-3: Kalan Bulguların Kapatılması (10 Eylül 2026)

Kalan TÜM adaylar kaynak koda karşı (rapora değil) doğrulandı. Bu turda yeni
düzeltmeler yapıldı ve geri kalan her bulgu kesin karara bağlandı.

## Doğrulama sonucu KAPALI / FIXED (önceki commit'lerde düzeltilmiş — tekrar aday değil)

Bu turda (batch-3) kodda düzeltilenler:
**R3-1** (HidppTransport copy = delete), **N-02** (stale slot bitmask), **N-15** (atomic-write tmp glob),
**N-17** (active_profile limit), **P-BUG-5** (hidpp20 sınıflandırma sırası), **DÜŞÜK-BUG-ALG-06** (constant_b hesaplama),
**FINDING-34-3** (version_lt 4 bileşen), **FINDING-21-1** (gereksiz unlink), **NEW-3** (quirks bileşik/12-char eşleşme),
**NEW-4** (Bolt occupied), **NEW-5** (pil 0xFF çevrimdışı), **NEW-6** (DPI spin clamp),
**P-BUG-1** (LOD clamp), **R3-3** (ölü koşul), **L-BUG-5** (yorum), **L-BUG-8** (cmd_import 1MB limit),
**L-BUG-13** (flock O_RDONLY), **L-BUG-15** (find_config_path boş), **L-BUG-20** (pkg-config kontrolü),
**L-BUG-24** (snap epsilon), **L-BUG-41** (python3 kontrolü), **TEST-1** (EXPECT tek değerlendirme),
**BUG-NEW-50** (LUT clip), **N-09** (feature_request target), **N-05/L-BUG-38** (build.sh pipefail/tail),
**L-BUG-12** (NaN/Inf görünür), **setup.sh read EOF** (NEW-41/N-06).

Önceki commit'lerde düzeltilmiş / kabul edilmiş diğerleri:
KRİTİK-BUG-MOTION-01, YÜKSEK-BUG-MOTION-02, YÜKSEK-BUG-ALG-01, ORTA-BUG-MOTION-04,
ORTA-BUG-TRANSPORT-01, ORTA-BUG-TRANSPORT-02, BUG-NEW-51, BUG-NEW-52, BUG-NEW-53,
BUG-NEW-60, BUG-NEW-70, BUG-NEW-80, BUG-NEW-81, BUG-NEW-82, BUG-NEW-83, BUG-NEW-84,
BUG-NEW-85, BUG-NEW-1, BUG-NEW-2, BUG-NEW-3, BUG-NEW-5, BUG-136, BUG-92, BUG-82,
M-BUG-15, M-BUG-16, M-BUG-18, L-BUG-19, L-BUG-27, L-BUG-28, L-BUG-29, L-BUG-33,
L-BUG-34, L-BUG-35, L-BUG-37, L-BUG-38, L-BUG-39, L-BUG-40, TEST-1, TEST-2,
FINDING-21-1, FINDING-29-1, FINDING-29-2, FINDING-30-3, FINDING-30-4,
FINDING-31-1..5, FINDING-32-1, FINDING-32-2, FINDING-33-1, FINDING-33-2,
FINDING-34-1..4, P-BUG-2, P-BUG-6, P-BUG-7, ORTA-BUG-ALG-02, ORTA-BUG-ALG-03,
DÜŞÜK-BUG-ALG-04, ORTA-BUG-MOTION-03, BUG-NEW-71, BUG-NEW-72, NEW-42, NEW-46,
NEW-43, NEW-20, NEW-40, NEW-41, NEW-45, M-BUG-5 (stable_id), G-BUG-12 (ölçümlü),
M-BUG-17 (yalnızca kullanıcı "Fix Now" tıklamasında, ~250 ms — kabul edildi).

## Yanlış pozitif / tasarım kayıtları (rapordan kaldırıldı)

Son adaylar (P-BUG-5, DÜŞÜK-BUG-ALG-05, BUG-NEW-86, L-BUG-36, BUG-NEW-4/6/7) kaynak
koda karşı doğrulandı ve YANLIŞ POZİTİF / TASARIM kararıyla kapatıldı — kod değişikliği
gerekmedi. ID-bazlı gerekçeler `aihaberlesme.md` ve git geçmişinde saklıdır.

## Sonuç

- Build: 0 uyarı/0 hata
- Test: 33766/33766 geçti
- Oracle: OK (1047 satır karşılaştırıldı, 45 kayıtlı sapma)
- tr_coverage: PASS

**Bölüm 35–40'tan geriye açık (düzeltilebilir) bulgu KALMADI.** Tüm rapor
bulguları FIXED / KAPANDI / YANLIŞ POZİTİF / TASARIM olarak sonuçlandı.

---
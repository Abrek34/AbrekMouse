# Düzeltme Raporu — v1.2.6 (M101 denetim bulgularına karşı)

Tarih: 2026-10-03 · Kaynak: `.aihaberlesme/mesajlar/denetim-L01..L20.md` (M101 turu)
Yöntem: 20 lane'in bulguları öncelik sırasına göre düzeltildi; her düzeltme
derleme (0 uyarı) + birim testleri + oracle ile doğrulandı; kritik iddialar
**mutasyon testiyle** kanıtlandı (test silme → META-FAIL; SIMD lane swap /
DPI-ratio / max-min mutasyonları → kırmızı).

## Neden bu tur gerekliydi (tek cümle)

M101 denetimi "yedi kapı yeşil"in **bir tesadüf** olduğunu ölçtü: test kayıt
defteri denetlenmiyordu (151 testin tamamı silinse `0/0 geçti` + rc=0),
GUI bir grafiği çizerek kullanıcının LUT'unu siliyordu, sayısal 0/1 bayraklar
tersine yükleniyordu, ve kapılar ölçtüklerinin yarısını ölçmüyordu.

## Kapatılan sınıflar

### 1. Kullanıcı verisi kaybı

| Bulgu | Düzeltme | Kanıt |
|---|---|---|
| L03-01 GUI repaint LUT'u siliyor (15× gain kaybı) | `accel_args::data[]` mutable kaldırıldı; synchronous kendi `lut[]` üyesini kullanıyor | probe: ctor öncesi/sonrası kullanıcı LUT'u korunuyor |
| L05-01 `use_raw_input:0`→`true`, `disable:1`→`false` | O31-C3 sayısal 0/1 toleransı (gain ile aynı desen) | birim test + CLI |
| L05-03 `lut_data`+`lut_length` ayrılınca eğri kalıcı siliniyor | uzunluk eleman sayısından türetiliyor | birim test |
| L05-02 `{"cap":[500]}` y=0 = "cap yok" | size>=1 kabul; `cap.y` struct default `{15,1.5}` (doc/oracle ile hizalı) | oracle 1408/79 OK |
| L13-05/06 `diff` yalan söylüyordu | wrapper reddi; weights gerçek double ε | CLI |

### 2. Sessiz "başarılı" yalanları

| Bulgu | Düzeltme |
|---|---|
| L08-01 revert push sessizce kayboluyor (CRIT) | no-op guard'lar `has_pending` iken atlanıyor |
| L13-01/02/03 stop/reload/status doğrulamasız | stop ~2 s yoklar; reload IPC-onay ayrımı; status `unreachable`+rc=3, `timestamp_ms`, her zaman `devices:[]` |
| L15-C1 KWin uyarı barı ölü kod (koşul imkânsız) | `line[10]==']'` kaldırıldı |
| L15-C2 donmuş daemon "running" | `unreachable` rozeti + Apply/Reload duyarsız |
| L17-5 desteklenmeyen LOD "uygulandı" | `(unsupported)` |
| L09-05 smoother NaN ile KALICI zehirleniyor | `isfinite(speed)` guard (simple + linear EMA) |
| L10-06 tuhaf cihaz adı tüm status'ü boşaltıyor | UTF-8 doğrulama, `\u00XX` kaçış |
| L09-02 <100 Hz'de eski rate sessiz kalıyor | band 5 Hz'e genişletildi |

### 3. Motor doğruluğu

- L02-10/12, L01-B05: natural / jump-legacy / power-legacy finiteness guard.
- L02-01: tek noktalı lookup + GAIN ölü imleç → kimlik.
- L01-B01/B02: classic GAIN clamp bandı monotoniklik kırığı → pozitif constant sıfırlanıyor.
- L10-08: `lat_stats::percentile` taşmada `max_us` yerine interpolasyon (p50>avg absurdity'si kapandı).

### 4. Kapı altyapısı (asıl yapısal düzeltme)

| Bulgu | Düzeltme | Mutasyon kanıtı |
|---|---|---|
| L20-CRIT-1 kayıt defteri denetlenmiyor | 151 `RUN_TEST()` sayılır; sapma → `META-FAIL`+rc=1 | çağrı silindi → rc=1, `150/151` |
| L20-CRIT-2 bench sabit mutlak yol | `BENCH_BASELINE` env + exe-göreli arama | — |
| L18 B-17/19 tr taban yok, dosya yoksa PASS | taban 250 + dosya varlık denetimi | — |
| L04-01/02/03 SIMD kapısı kör | smoother dalları + yx ratio + max/min değer pinleri | lane swap → FAIL; DPI ratio → FAIL; max→min → FAIL |
| L04-05/06 AVX2 skip sessiz/kırık | probe çalıştırmayı da dener; kısmi koşu rc=77 | simülasyon: rc=77 + DİKKAT |
| L13-15 CLI sanitizer import JSON'u hiç koşmuyordu | 3 yeni vaka (34/34) | ASan temiz |

## Kapı sonuçları (bu tur, yerel)

- build: 0 uyarı / 0 hata
- birim: **34267/34267** (önce 34164; +98 assert 0-assert bölümler ve lat_stats pinleri, +5 diğer)
- oracle: **1408 satır / 79 sapma, RESULT OK** (davranış değişiklikleri referans-pariteyi koruyacak şekilde tasarlandı)
- SIMD parity: 3 backend PASS + yeni mutasyon-yakalayıcı kapsam
- tr coverage: PASS
- CLI ASan/UBSan: **34/34**
- tracker bridge: 17 işaretli / 0 açık
- `prove_kod_ayni.py --sayi`: 14/14 sayısal iddia tutuyor

## Bilinçli dokunulmayanlar (gerekçeli)

- **L02-14 natural negatif kazanç (decay_rate→0):** formül referansla bit-aynı;
  düzeltme `decay_rate` alt sınırı gerektirir ve oracle sapması doğurur. Bu bir
  ürün kararı; belgeye "bilinen" olarak kalıyor.
- **L15-H1 5 s Apply blokajı:** `unreachable` durumu Apply'ı kapatarak pratik
  etkiyi kaldırdı; tam async push büyük refactor (test edilemez GUI yolu).
- **L17-1 grafik gain'i ≠ oyun gain'i:** grafik DPI fazını gösteremez; etiket
  sorunu ayrı bir tasarım işi.
- **L19 test kapsam boşlukları ve L06 HID++ test dikişleri:** üretim kodu
  değişikliği gerektirmeyen kapsam işleri; bir sonraki tura.
- **L10-11:** `ok:true` = "kuyruğa alındı" sözleşmesi belgeli; save hatası
  loglanıyor (tasarım gereği davranış korundu).

## Temsil sınırı (dürüstlük)

- Root, gerçek fare, Logitech donanımı, `setup.sh` kurulumu **koşulmadı**;
  e2e harness root gerektirir ve bu ortamda çalıştırılamadı.
- GUI çalıştırıldı sayılmaz; GUI değişiklikleri derleme + tr kapısı + kod
  düzeyinde doğrulandı.
- CI hâlâ hiç çalışmadı (hesap blocker'ı); bu sonuçlar yerel ve ölçülmüştür.
- Mutasyon kanıtları /tmp kopyalarında üretildi; çalışma ağacına mutasyon
  uygulanmadı.

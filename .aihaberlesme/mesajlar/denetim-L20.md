### L20 | denetim alt-ajanı (test kayıt mekanizması / oracle / koşu hattı) | 2026-10-02

**KAPSAM**
`tests/test_accel.cpp` **yalnız 4636–10127** (stres/fuzz + `int main` 9881–10127),
`tests/oracle/**` (ref/rawaccel.hpp, ref/accel-classic.hpp, ref/accel-power.hpp,
ref/rawaccel-base.hpp, ref/accel-{natural,jump,lookup,noaccel,synchronous,union}.hpp,
ref/math-vec2.hpp, ref/utility.hpp, ref/refcompat.hpp, oracle_cases.hpp, run_oracle.sh,
run_oracle_perf.sh, oracle_perf.cpp, local.cpp, reference.cpp, known_deviations.txt),
`tests/e2e_harness.cpp` (389), `tests/bench_hotpath.cpp` (414),
`tests/run_tests.sh` (756), `tests/run_cli_sanitized.sh` (150), `tests/run_simd_parity.sh` (287),
`tests/run_tests_asan.sh` (74), `tests/run_tracker_bridge.sh` (59), `tests/tracker_bridge.py`,
`scripts/bench_hotpath.sh` (520), `bench_hotpath_results.txt`, `tests/perf_baseline.json`,
`olcum/aj2/prove_kod_ayni.py` + `sayisal_iddialar.txt`.
Ölçüm kopyası: `/home/a/l20_scratch/L20` (çalışma ağacına **hiç dokunulmadı**, §KAPI-0).
`e2e_harness.cpp` 389 satır → `main` **389. satırda kapanmıyor** (dosya `}` ile bitmiyor),
aşağıdaki `return (rc != 0) ? rc : (g_ok ? 0 : 1);}` satırı dosyanın sonudur.

---

## BULGULAR

### ID | dosya:satır | sınıf | kanıt

---

#### L20-CRIT-1 | `tests/test_accel.cpp:9881-10127` (+ `tests/run_tests.sh:105`) | **CRIT**
**Kayıt defteri hiç denetlenmiyor: 151 testin TAMAMINI `main`'den silmek kapıyı yeşil bırakıyor.**

Bugün 151 tanımlı testin **151'i de** `main` içinde koşulsuz çağrılıyor (aşağıdaki
olumlu ölçüm). Ama bu **tesadüf**, mekanizma değil: `main`'de "tanımlı ama çağrılmamış
test" denetimi **yok**. Mutasyonla kanıtlandı — `/home/a/l20_scratch/L20` kopyasında
`main` gövdesindeki **151 adet** `test_*();` satırının **tamamı** silindi:

```
$ python3 -c "...main govdesindeki tum test_ cagrilarini kaldir..."
main'den silinen test cagrisi: 151 -> kalan 0
$ bash tests/run_tests.sh ; echo "RC=$?"
=== Sonuç: 0/0 geçti ===
RC=0
```
`0/0 geçti` ve `exit 0`. Buna karşılık `g_tests == 0` için hiçbir uyarı/`exit 1`
yolu yok (`tests/test_accel.cpp:10122-10126` yalnızca `g_failed ? 1 : 0` yazıyor).

**Daha da kötüsü: sadece 1 testi silmek de yakalanmıyor.**
```
$ python3 -c "...s.replace('    test_stress_remainder_drift();\n','',1)"
$ bash tests/run_tests.sh ; echo "run_tests RC=$?"
=== Sonuç: 34163/34163 geçti ===
run_tests RC=0
```
Bu tam olarak brifing §3'ün "bir test **yazılmamış** ama kapı yeşil" sınıfı.

**Tek telafi ve onun da yetersizliği.** `olcum/aj2/prove_kod_ayni.py --sayi`
bu mutasyonu **yakalıyor** (ölçüldü: her iki mutasyonda da rc=1,
"⛔ 1/14 sayisal iddia BAYAT"). Ama:
* o araç **hiçbir kapıdan ve CI'dan çağrılmıyor** —
  `grep -rn 'prove_kod_ayni' .github/workflows/ci.yml tests/*.sh tests/oracle/*.sh` → **0 eşleşme**;
  AGENTS.md'nin "Canonical Seven Gates" listesinde de **0 kez** geçiyor.
* o iddia **assertion SAYISINI** denetliyor (`bash tests/run_tests.sh | sed …`), yani
  "hangi test çağrıldı"nı değil. `34163 != 34164` olduğu için kırmızı verdi; ama
  **bir testi başka biriyle 1:1 değiştirsen** (aynı assertion sayısı) denetim sessiz
  geçer. Ölçüm yaptığım iki mutasyonun yakalanması sayı değiştiği içindi.

---

#### L20-CRIT-2 | `tests/bench_hotpath.cpp:227` | **CRIT**
**Benchmark binary'si baseline'ı SABİT MUTLAK yoldan okuyor → CI'da perf-gate SKIP değil, İŞ KIRILTISI (kırmızı) veriyor.**

```
$ grep -n '/home/a/Masaüstü' tests/bench_hotpath.cpp scripts/bench_hotpath.sh tests/oracle/*.sh
tests/bench_hotpath.cpp:227:    std::string baseline_path = "/home/a/Masaüstü/AbrekMouse-main/tests/perf_baseline.json";
```
(Bu, AGENTS.md'de `grep -rn` ile **yazılı** bir bilgi; yani kayıp değil, gömülü.)

Olmayan yol durumunu doğrudan ölçtüm (CI runner'ın durumu budur):
```
$ sed 's#/home/a/Masaüstü/.../perf_baseline.json#/tmp/opencode/YOK.json#' tests/bench_hotpath.cpp > bh_boguspath.cpp
$ g++ -std=c++20 -O2 -Iinclude -Idaemon -Iinclude/nlohmann bh_boguspath.cpp -o bh_bogus
$ ./bh_bogus 1 --json --min-seconds 0.01 ; echo "RC=$?"
ERROR: Baseline missing required config: noaccel
Available keys:
RC=1
```
İkiliyi kopyada `build-manual/bench_hotpath` yerine koyup **tüm zinciri** koşturdum:
```
$ bash scripts/bench_hotpath.sh --output ... ; echo "bench_hotpath.sh RC=$?"
ERROR: Baseline missing required config: noaccel
Available keys:
bench_hotpath.sh RC=1
```
`scripts/bench_hotpath.sh:193-198` ikili rc≠0'da `exit 1` veriyor, ve
`.github/workflows/ci.yml:206-209` bunu `::error::` + `exit $BENCH_RC` ile
**FAIL**'e çeviriyor (77/126 SKIP yolları dışında).

Bu, `c9e78fa5` commit mesajındaki *"The perf gate has now skipped silently on every
push … the host check never passed"* ile **çelişiyor**: CPU-model kontrolü
(`scripts/bench_hotpath.sh:363-372`) ikilinin **sonrasında** gelir, yani baseline
yolu bulunamazsa oraya **hiç ulaşılamaz**. Bu lane'de kanıt ölçtüm: **rc=1, iş kırılması.**

⚠️ Temsil sınırı: gerçek bir GitHub runner'da koşturamadım. İddia, `ci.yml`deki
`actions/checkout@v4` + standart runner çalışma dizininin bu mutlak yol olmadığı
üzerinden çıkarılmıştır. CI loglarını okuma yetkim yok.

---

#### L20-HIGH-1 | `tests/run_tests.sh:741-744` | **HIGH**
**SIMD parity `exit 77` dönse 2. kapı `exit 0` ile yeşil kalıyor — AVX2 yolu denenmeden.**

`tests/run_tests.sh:733-746` `case` bloğunda `77)` dalı yalnızca uyarı basıyor ve
**düşmüyor**; script `echo` ile bitiyor → son kod 0.
Mutasyonla zorladım (`run_simd_parity.sh`'ye `exit 77` + hiç backend derlenmesin):
```
$ bash tests/run_tests.sh ; echo "RC=$?"
=== Sonuç: 34163/34163 geçti ===
=== SIMD backend parity ===
UYARI: SIMD parity kapısı ATLANDI (exit 77) — bu konak AVX2'yi
       çalıştıramıyor.  Üretimde çalışan AVX2 yolu BU KOŞUDA
       denenmedi; AVX2'ye özgü bir hata burada görünmez.
=== Sonuç: N/N geçti — DİKKAT: SIMD parity ATLANDI (konak AVX2 çalıştıramıyor) ===
RC=0
```
Uyarı **görünür** ve bilinçli (kaynak yorumu bunu savunuyor: "x86 olmayan bir
katkıcıyı bloklamamak için hata değildir — ama saklanmaz da"). Yine de ölçülen
gerçek şu: **AVX2 üretim yolunun hiç ölçülmediği bir koşu, exit koduyla PASS
görünüyor.** `scripts/bench_hotpath.sh` ve `run_cli_sanitized.sh` aynı durumda
`77 → exit 0` veriyor; yani proje genelinde bir **"ölçmedi ama yeşil"** kuralı var.
Kıyas: `run_tracker_bridge.sh` bozulursa `run_tests.sh:715-717` `exit 1` veriyor —
yani aynı script içinde iki farklı politika.

---

#### L20-HIGH-2 | `tests/run_tests.sh:105` | **HIGH**
**`"$BIN" "$@"` ile test ikilisine kapı argümanları sızıyor: `--list` 0 assertion + exit 0, `--filter` %98 atlama + exit 0.**

```
$ bash tests/run_tests.sh --list ; echo "RC=$?"
--- PASS/FAIL assertion satiri sayisi:  0        <-- HİÇ assertion çalışmadı
RC=0
$ bash tests/run_tests.sh --filter P106 ; echo "RC=$?"
=== Sonuç: 178/178 geçti (5 section eşleşti, 212 atlandı) ===
RC=0
```
217 SECTION'ın **212'si atlandı**, 34164 assertion'ın **'si bile koşmadı**, kapı yeşil.
Bu `--filter`'ın kendi tasarımı (P114 BUG-A düzeltmesi yalnızca "hiç eşleşme"yi
kırmızı yapmış), ama **sonuç aynı sınıf**: yeşil kapı, çok az ölçüm.
Doğru çalışan taraf: `--filter <hiç-eşleşmeyen>` → rc=1
(`tests/test_accel.cpp:10112-10119`, P114 düzeltmesi **ölçülerek çalışıyor**).

---

#### L20-MED-1 | `tests/oracle/known_deviations.txt:1-125` | **MED**
**Oracle'ın **%5.6**'sı (79/1408 satır) hiç denetlenmiyor ve **5 vakanın TÜM satırları** bu kör bölgede → oralarda keyfi bir bozulma oracle'dan geçiyor.**

```
toplam belgelenen satir: 79 / 1408 = %5.6
TAMAMEN BELGELENMIS case'ler:
  classic_gain_exp_le1: 23   p155_io_cap0_gain: 12   p155_io_cap0_legacy: 12
  power_tinyexp_floor: 23    power_gain_p1/legacy_p1: 1+1
  sync_*: 7 case × 1
```
Mutasyon 1 — `include/accel-power.hpp:36` taban **9 kat** değiştirildi (`1e-3` → `9e-3`):
```
$ bash tests/oracle/run_oracle.sh ; echo "ORACLE RC=$?"
total rows compared : 1408 / documented deviations: 79
RESULT: OK — local port matches official reference (rel tol 1e-09) ... RC=0
```
Nedeni doğrulandı — temiz vs mutant oracle çıktısı farkı **22 satır, hepsi `power_tinyexp_floor`**:
```
$ diff clean.out mut.out | grep -c '^<'      →  22
$ diff clean.out mut.out | grep '^<' | cut -f1 | sort -u  →  power_tinyexp_floor
```
Mutasyon 2 — `include/accel-classic.hpp:48` (`exp<=1` linear path) **×1.5**:
```
$ bash tests/oracle/run_oracle.sh   → RC=0   (23/23 satırı zaten belgeli = kör bölge)
$ bash tests/run_tests.sh            → RC=1   (test_accel.cpp:6970/6977 yakaladı)
```
⭐ Yani `classic exp<=1` yolu oracle'da **hiç ölçülmüyor**; kapsaması **tek kapıya**
(run_tests.sh) bağlı. Aynı desen `power_tinyexp_floor` için de geçerli, ama orada
`run_tests.sh` de yakalamadı (aşağıdaki mutasyon 3).

---

#### L20-MED-2 | `include/accel-power.hpp:36` (kör bölgenin canlı örneği) | **MED**
**Aynı 9× mutasyonu `run_tests.sh` de geçiyor → `power` üst tabanı iki kapı tarafından da denetlenmiyor.**

```
$ bash tests/oracle/run_oracle.sh   →  ORACLE RC=0   (Sonuç: OK)
$ bash tests/run_tests.sh           →  RC=0          (Sonuç: 34164/34164 geçti)
```
Bu mutasyonun canlı bir etkisi olduğunu ayrıca doğruladım: temiz ile mutant
`local.cpp` çıktısı 22 satır farklı. Yani **gerçek bir davranış değişikliği**, iki
kapıdan da sessizce geçiyor. (Bu bulgu L20-MED-1'in en somut kanıtıdır; iki
madde aynı mutasyonu ölçüyor, biri kapı tarafını biri kör bölge kapsamını anlatıyor.)

---

#### L20-MED-3 | `bench_hotpath_results.txt` + `scripts/bench_hotpath.sh:87,460-471` | **MED**
**Sonuç dosyası tarihli ama **tutarsız**: kendi `Runs:` satırı `1`, baseline'ın `_meta.runs_used`'ı `3`; üstelik ikisinin sayıları birbirinin aynısı değil.**

```
bench_hotpath_results.txt:  Runs: 1 (median), Min seconds per run: 0.1
                           noaccel: 13.7489
tests/perf_baseline.json:  "runs_used": 3,  "noaccel": 13.6782
```
→ Sonuç dosyası **baseline'ı üretmemiş** (farklı koşu). Tarih: dosya `c9e78fa5`
(2026-10-01 01:30) commit'iyle değişmiş ama içindeki `Date: Çrş 30 Eyl 2026 23:23:59`
→ ~2 saatlik sapma, aynı gün. **Bayat değil, ama tutarsız ve izlenen bir artifact.**
Ek risk: `--json` ile çalıştırılınca `OUTPUT_FILE` varsayılanı **bu izlenen dosyaya**
yazıyor (`scripts/bench_hotpath.sh:87`), yani `--output` verilmeden her koşu
commit'lenmiş bir dosyayı ezme riski taşıyor. Ölçüldüğüm sırada bu dosya
çalışma ağacında **değişmiş** durumdaydı (mtime 2026-10-02 00:01:46, JSON içerikli);
kendi komutlarımda daima `--output` verdim ve varsayılan yola yazmadım, dolayısıyla
bunu **başka bir ajanın** koşusu olarak kaydediyorum. **Geri almadım** (brifing §1:
`git checkout` yasak, başka ajanın işi).

---

#### L20-LOW-1 | `tests/test_accel.cpp:1856-1890` (`test_monotonic`) | **LOW**
**151 testin 1'i `EXPECT` makrosu kullanmıyor; elle sayaç yazmış.** Makro sayımına girmez.
```
$ (python3 ile govde taramasi)
toplam test=151  EXPECT iceren test=150  HIC EXPECT icermeyen=1
  HIC ASSERT YOK: test_monotonic (SECTION=1)
$ grep -c 'g_tests++' tests/test_accel.cpp   →  3
```
Elle yazılmış blok `g_tests++/g_passed++/g_failed++` yaptığı için **sayılıyor**
(silinmesi 34164'ü düşürürdü) — yani bir boşluk değil, sadece iki sayım yöntemi
bir arada. Ayrıca `test_monotonic` hata halinde `FAIL` yazarken `__FILE__:__LINE__`
ve `section` bilgisini **yazmıyor** (`tests/test_accel.cpp:1886`) → bu tek test
için teşhis edilebilirlik düşük.

---

#### L20-LOW-2 | `olcum/aj2/sayisal_iddialar.txt:65` | **LOW**
**"151 `test_` functions" iddiasının ölçüm komutu Türkçe locale'da **34** döndürüyor; araç `LC_ALL=C` zorlayarak doğruyu (151) alıyor.**
```
$ echo $LANG                      →  tr_TR.UTF-8
$ grep -cE '^static void test_[A-Za-z0-9_]*\(\) \{' tests/test_accel.cpp   →  34   ⛔
$ LC_ALL=C grep -cE '^static void test_[A-Za-z0-9_]*\(\) \{' tests/test_accel.cpp  →  151  ✔
$ python3 olcum/aj2/prove_kod_ayni.py --sayi olcum/aj2/sayisal_iddialar.txt  →  RC=0, 14/14 tutuyor
```
**Araç doğru davranıyor** (`prove_kod_ayni.py:502` `env=dict(os.environ, LC_ALL="C")`
ve `:23-25` bu tuzağı zaten not etmiş). Bulgu, **ham komutun** locale'e duyarlı
olması: yarın başka biri bu komutu elle koşturup "151 değil 34, iddia bozuldu"
derse yanlış yönlendirilir. `sayisal_iddialar.txt`'teki komutlarda `LC_ALL=C` ön
eki yok.

---

#### L20-INFO-1 | `tests/e2e_harness.cpp` (tamamı) | **INFO** (bulgu değil, teyit)
**"e2e" adı **aldatıcı değil** ama sınırlı: GERÇEK daemon + GERÇEK kernel input
yığını + **TAKLİT (sentetik) uinput fare**. `tests/e2e_harness.cpp:66-88`
`libevdev_uinput_create_from_device` ile sahte fare yaratıyor, `:91-111` gerçek
`rawaccel-daemon` binary'sini `fork/execl` ile çalıştırıyor, `:113-123` daemon'ın
yarattığı çıkış düğümünü `/dev/input/event*` üzerinden arıyor.
5 kontrol: T-A1..T-A4 (accel fazı) + T-B1 (raw fazı); `g_checks` sayılıyor (`:34-43`),
`main` dönüşü `:389` `return (rc != 0) ? rc : (g_ok ? 0 : 1);` → **başarısızlık exit 1**.
root + `/dev/uinput` ister; **yedi kapıdan değil, CI'da da değil** (AGENTS.md doğru söylüyor).

---

#### L20-INFO-2 | `tests/e2e_harness.cpp:85,120,320,322` | **INFO**
**Sabit `sleep` yalnız burada. `test_accel.cpp`'de ve `bench_hotpath.sh`'da **yok**.**
```
$ grep -rnP '\b(usleep|sleep|nanosleep|std::this_thread::sleep_for)\s*\(' tests/ scripts/bench_hotpath.sh
tests/e2e_harness.cpp:85:        usleep(100000);
tests/e2e_harness.cpp:120:        usleep(100000);
tests/e2e_harness.cpp:320:        usleep(80000); // let the daemon process frame 1
tests/e2e_harness.cpp:322:        usleep(80000); // daemon sees the +2 with no SYN (deferred, LOW-1)
```
`:85`/`:120` 100 ms × 20 tur = 2 s üst sınır (bulma döngüsü, makul).
`:320`/`:322` T-A4'ün zamanlama **varsayımı** — 80 ms yetersizse test **flaky**
(çökmez, `t4ok=false` → exit 1). Yani "sabit bekleme" burada bir *dayanıklılık*
riski, hız kaybı değil.

---

## ⭐ OLUMLU ÖLÇÜMLER (mutasyonla kanıtlanmış "kapılar gerçekten çalışıyor")

Bunlar **yol göstermek** için: L20'nin bulguları daha önce kimsenin ölçmediği
delikler; aşağıdakiler ise **kırıldığını gördüğüm** kapılar.

| # | Mutasyon (yalnız `/home/a/l20_scratch/L20`) | Beklenen | **Ölçülen** |
|---|---|---|---|
| 1 | `tests/test_accel.cpp:1219`'e `EXPECT(1 == 2)` | kırmızı | `RC=1` · `Sonuç: 34164/34165 geçti, 1 BAŞARISIZ` · 1 FAIL |
| 2 | `test_accel.cpp` sonuna derlenmeyen fonksiyon | kırmızı | `RC=1` · `error: 'this_is_not_defined_anywhere_xyz' ... bildirilmemiş` |
| 3 | `Bug Hata Raporları.md` O31-C5 `- Durum:` → `⏸` (kodda işaret **zaten** var: `src/config.cpp:833`) | kırmızı | `tracker_bridge` `RC=1` · `⚠ O31-C5 kodda: src/config.cpp` · `run_tests.sh` `RC=1` |
| 4 | `cli/main.cpp` `main()` gövdesine heap-**use-after-free** | kırmızı | `gate 6 RC=1` · **31/31 komutta** `ERROR: AddressSanitizer` · `0/31 komut gerçek kodu çalıştırdı` |
| 5 | `tests/oracle/local.cpp` `gain × 1.05` | kırmızı | `ORACLE RC=1` · `1325 UNKNOWN mismatched rows (of 1408)` |
| 6 | `include/accel-power.hpp:103` `legacy_cap × 1.02` (üretim başlığı) | kırmızı | `ORACLE RC=1` · `rel=1.961e-02 power_legacy_p1` (1325→ çok satır) |
| 7 | `include/accel-classic.hpp:48` linear path `× 1.5` | kırmızı | `oracle RC=0` (kör bölge) **ama** `run_tests.sh RC=1` · `test_accel.cpp:6970/6977` |
| 8 | `BENCH_POSITIVE_CONTROL=1 ./build-manual/bench_hotpath` | fark et | `noaccel 13.9626 → 1138.3659 ns/event` (**81×**) |

### Kayıt mekanizması — OLUMLU taraf (tam ölçüm)
```
$ grep -c 'static void test_' tests/test_accel.cpp                              → 151
$ python3 (tanım ∩ main'deki koşulsuz çağrı)                                    →  151 / 151, fark = 0
$ sed -n '9881,10127p' tests/test_accel.cpp | grep -cP '^\s+test_\w+\(\);'     →  151
$ sed -n '9881,10127p' tests/test_accel.cpp | grep -cP '(if|argv|getenv).*test_'→  0   (koşulu YOK)
$ sed -n '9881,10127p' tests/test_accel.cpp | grep -nP 'try|catch'              →  16,19 (sadece --filter regex parse)
```
**Hata akışı DOĞRU:** `EXPECT`/`EXPECT_NEAR` başarısızlıkta `g_failed++` + stderr
yazıp **devam ediyor** (`tests/test_accel.cpp:76-104`), `main` sonunda
`return g_failed ? 1 : 0` (`:10125`). Yani "ilk hatada durma" riski **yok**; bir test
*çökerse* (SIGSEGV/SIGABRT) tüm binary ölür ama rc=139/134 ≠ 0 → kapı yine kırmızı.
`assert()` çağrısı **yok** (`grep -cP '(?<![_A-Z])assert\('` → 0) → `NDEBUG` riski yok.
**Başarı/başarısızlık SAYILIYOR**, sadece "çökmezlik" değil (mutasyon 1 kanıtı).
Sayımlar (`olcum/aj2/sayisal_iddialar.txt` komutlarıyla birebir):
`217 SECTION grubu` · `1603 EXPECT çağrı noktası` · `34164 runtime assertion`.
`0 doğrudan assert()` · `0 abort/exit` · `0 sleep/usleep` (test_accel.cpp'de).

### `run_tests.sh` — gerçekte ne koşturuyor (SAYILDI)
| # | Adım | Kaynak |
|---|---|---|
| 1 | 1 derleme: `test_accel.cpp` + `src/config.cpp` + `src/logitech_receiver.cpp` + `src/logitech_hidpp.cpp` | `:83-88` |
| 2 | 1 test ikilisi koşumu → **151 test / 217 SECTION / 34164 assertion** | `:105` |
| 3 | **37** `"$CLI"` çağrısı (P83 ad, P99 arity+`-c ""`, P107 domain ×7, O31-L2 ×4, O31-L1, diff ×4, O31-L4 ×4, monitor ×3 …) | grep ölçümü |
| 4 | **12** `secd_reject`/`secd_accept` (SEC-2 config yolu: 8 CLI + 4 daemon) | grep ölçümü |
| 5 | **3** `ra_daemon_run … "$DAEMON"` (SEC-2 daemon kopyası) | grep ölçümü |
| 6 | `bash tests/run_tracker_bridge.sh` | `:713-716` |
| 7 | `bash tests/run_simd_parity.sh` → **3 derleme + 3 koşu + 2 diff** (AVX2/SSE2/skaler) | `:733` |
| — | **ÇAĞRILMAYANLAR**: `run_tests_asan.sh` (0 çağrı, sadece `:52` yorum), `run_cli_sanitized.sh` (0), `run_tr_coverage.sh` (0), `oracle/run_oracle.sh` (0), `run_fuzz.sh` (0), `run_e2e.sh` (0) | grep ölçümü |

`set -e` (`:2`) + `set -o pipefail` (`:3`) **var** ve **akış kesiyor**: test ikilisi
1 dönerse script orada düşüyor (mutasyon 1: log `Sonuç:` satırında bitti, arkasından
CLI/SIMD blokları **hiç çalışmadı**).
**Skip yolları (hepsi ölçüldü):**
| Yol | Davranış | Kanıt |
|---|---|---|
| `SIMD parity exit 77` | ⚠️ **exit 0** (yalnız uyarı) | mutasyon, RC=0 |
| `! -x $CLI` | ✅ exit 1 ("sessizce atlanamaz") | `:188-193` |
| `! -x $DAEMON` | ✅ exit 1 | `:398-402` |
| `--filter <eşleşme>` | ⚠️ **exit 0**, 212/217 section atlanır | `--filter P106` RC=0 |
| `--list` | ⚠️ **exit 0**, **0 assertion** | RC=0, 0 PASS/FAIL |
| `--filter <hiç-eşleşmezse>` | ✅ exit 1 | RC=1 (P114 düzeltmesi çalışıyor) |

### ORACLE — doğru mu, yoksa aynı hatayı paylaşan kopya mı?
**Kopya DEĞİL.** 11 başlığın **11'i de** `include/`'dan farklı (satır sayısı):
`rawaccel.hpp 617↔460` · `accel-classic.hpp 276↔172` · `accel-power.hpp 222↔164` ·
`accel-natural.hpp 171↔58` · `accel-jump.hpp 85↔83` · `accel-synchronous.hpp 175↔150` ·
`accel-lookup.hpp 130↔101` · `accel-noaccel.hpp 15↔16` · `accel-union.hpp 65↔57` ·
`math-vec2.hpp 203↔37` · `rawaccel-base.hpp 139↔99`. `ref/`'e ayrıca `utility.hpp`,
`refcompat.hpp`, `LICENSE` (MIT) var — `include/`'de yok.
Farklar **bilinçli güvenlik eklemesi**, örneğin `accel-classic.hpp`:
* `ref/accel-classic.hpp:50` → `if (args.cap.x > 0)`
  vs `include/accel-classic.hpp:114` → `if (args.cap.x > 0 && args.cap.x >= args.input_offset)` (K1)
* `ref/accel-classic.hpp:14` `base_fn` **guard'sız** vs `include/accel-classic.hpp:225-232`
  üç katmanlı `isfinite` guard'ı
* `ref/accel-classic.hpp:45,100,121` `pow(...)` → `include` aynı yerlerde
  `isfinite(a) && a>0` kontrolü
* `include/accel-classic.hpp:255-256` `gain_inverse`: `accel == 0 → DBL_MAX`
  (referansta 0'a bölme → `+Inf` → eğri 1:1'e çöker)
Yerel port **daha güvenli**, oracle bu farkı 79 satır olarak "kasıtlı sapma" listeliyor —
tutarlı. **Doğrulama anlamlı**; ancak kapsama kör bölgesi var (L20-MED-1/2).

**Izgara:** 1408 satır / **64 vaka** / `default_speeds()` = **24 hız** (0 … 1e5).
Mod kapsamı (`x.mode` atamaları): `classic ×5, natural ×4, power ×4, jump ×1,
synchronous ×1, lookup ×1, noaccel ×1` → **7 modun hepsi** temsil ediliyor.
Sapma sınıfları (5): `classic_gain_exp_le1` 23 · `power`/`sync` spd=0 → 9 ·
`power_tinyexp_floor` 23 · `p155_io_cap0_legacy` 12 · `p155_io_cap0_gain` 12.
`run_oracle.sh`'de **gerçek bir kendi-kendini denetimi** var (bozuk `TOL` → exit 2,
`known_deviations.txt`'ta olmayan satır → exit 1, **bayat** sapma girdisi → exit 1,
`ORACLE_COVERED`/`NOT_COVERED` çakışması → exit 1) — hepsi kodda okundu;
`unknown` liste boşken `RESULT: OK` + exit 0.

### `bench_hotpath` — ölçülen sayı gerçek mi? **EVET, ama bugün bu konak ölçüm yapmıyor.**
Aynı ikiliyi 4 kez koşturdum (`--json --min-seconds 0.1`, median-of-3/1):
```
config                            baseline  results.txt     d%        benim olcum (min..max)      d%(medyan)
noaccel                            13.6782    13.7489   0.52%      14.0807..16.5250         +4.00%
power-whole                       39.1757    40.4961   3.37%      39.6958..40.3566         +1.75%
classic                           43.8619    43.7949  -0.15%      44.3591..46.3474         +1.35%
power+rot45+snap15+clamp           58.5337    59.9312   2.39%      59.6768..60.8190         +2.00%
power-dual+4ema                  307.3409   304.5499  -0.91%     311.8795..323.8011         +4.97%
apply_motion_math(full)            43.9391    44.0219   0.19%      44.3208..45.4922         +1.31%
```
→ `bench_hotpath_results.txt`'in sayıları **gerçek** (hepsi baseline'la aynı mertebede,
gürültü sınırları içinde). **Ama kapı şu anda ölçüm yapmıyor:**
```
$ bash scripts/bench_hotpath.sh ; echo "RC=$?"
CONTROL-SKIP: noaccel deviates 19.21% from baseline (max 12.0%).
CONTROL-SKIP: the measurement environment is dirty — this run makes no claim.
RC=77
```
(makine yüklü: `/proc/loadavg` = 6.68). Bu **dürüst bir tasarım** — "sıfır fayda"
değil, ama bu konak ölçüm üretmiyor. `ci.yml:201-208` bunu `::warning::` + exit 0'a
çeviriyor → yeşil tik **değil**. **Pozitif kontrol kanıtı** (mutasyon 8): 81× yavaşlık
görülüyor, yani kapı regresyonu **yakalayabiliyor**.
Ölçüm dürüstlüğü için `bench_hotpath.cpp:174-175` non-finite ölçümü `exit 1` ile
reddediyor, `:164-175` `volatile` sink ile DCE engelleniyor, `scripts/bench_hotpath.sh:109-127`
her sayıyı "sayı mı / >0 mu" diye doğruluyor (0 ölçüm = OK sayılmasın diye).
⚠️ `bench_hotpath_results.txt` **çalışma ağacında şu an değişmiş** durumda ve
`--json` + `--output` verilmezse varsayılan olarak **bu izlenen dosyaya** yazıyor
(L20-MED-3).

### `run_cli_sanitized.sh` — sanitizer GERÇEKTEN açık mı? **EVET, pozitif kontrolle kanıtlandı.**
```
tests/run_cli_sanitized.sh:25-26   -fsanitize=address,undefined -fno-omit-frame-pointer
tests/run_cli_sanitized.sh:58-59   ASAN/UBSAN_OPTIONS halt_on_error=1:abort_on_error=1
$ nm -C <build> | grep -c '__asan\|__ubsan'   →  48
$ ldd <build> | grep -i asan                   →  libasan.so.8 => /usr/lib/libasan.so.8
```
Mutasyon 4 (`cli/main.cpp` `main`'e gerçek heap-**use-after-free**):
`==18439==ERROR: AddressSanitizer: heap-use-after-free … READ of size 4 … in main cli/main.cpp:2941`
→ `bash tests/run_cli_sanitized.sh` → **RC=1**, **31/31 komutta ihbar**, `0/31 komut gerçek kodu çalıştırdı`.
**31 komut sayısı doğru:** 24 statik `vaka` + 1 `vaka` × 7 preset döngüsü = 31
(`:110-112`). Her vaka kendi beklenen işaretini arıyor → "YOL ÇALIŞMADI" koruması var
(`:129-136`) → argüman yeniden adlandırılsa kapı 31 no-op'a dönmez. Sanitizer yoksa
`:36-39` exit 77 + **görünür** uyarı.

### `run_tests_asan.sh` — yedi kapıdan **değil** (AGENTS.md doğru söylüyor)
```
$ grep -c 'run_tests_asan.sh' tests/run_tests.sh   →  1   (sadece :52 yorum satırı)
$ bash tests/run_tests_asan.sh --quiet ; echo "RC=$?"
=== Sonuç: 34164/34164 geçti ===
RC=0
```
Yani 34 164 assertion'ın **hiçbir sanitizer olmadan** koşuyor; AGENTS.md'nin bu
teslimi ölçülmüş ve doğru. `test_accel.cpp`'i ASan altında değiştirirsen
`run_tests.sh` seni korumaz.

---

## KAPI (koşturulan kapılar / tampon komutlar + rc + ÜRETİLEN SAYI)

**KAPI-0 (mutasyon izolasyonu).** Tüm mutasyonlar `/home/a/l20_scratch/L20`
kopyasında. Çalışma ağacı kanıtı: `git status --porcelain -- include src daemon cli
gui tests scripts CMakeLists.txt README.md AGENTS.md` → **TEK** çıktı
`M bench_hotpath_results.txt`; bunu **ben yazmadım** (her koşumda `--output`
verdim), başka bir ajanın koşusu — **geri almadım** (brifing §1 `git checkout` yasak).
Kopya ağacında mutasyon sonrası md5 doğrulaması yapıldı:
`tests/test_accel.cpp`, `tests/run_simd_parity.sh`, `tests/run_tracker_bridge.sh`,
`tests/oracle/local.cpp`, `cli/main.cpp`, `include/accel-classic.hpp`,
`include/accel-power.hpp`, `Bug Hata Raporları.md` → 8/8 pristine ile **AYNI**.

| Kapı | Komut | rc | Üretilen sayı |
|---|---|---|---|
| 2 (gerçek ağaç) | `bash tests/run_tests.sh` | **0** | `Sonuç: 34164/34164 geçti` · 12 CLI/daemon kapı etiketi · tracker `17 işaretli / 0 AÇIK` · SIMD `26 v2d_* · 16/8/10` · **4m29.6s** |
| 2 (kopya, temiz) | `bash tests/run_tests.sh` | **0** | `34163/34163` (mutasyonlu varyant) / `0/0 geçti` (151 çağrı silinmiş varyant) |
| ASan (ayrı) | `bash tests/run_tests_asan.sh --quiet` | **0** | `34164/34164 geçti` |
| 3 | `bash tests/oracle/run_oracle.sh` | **0** | `total rows compared: 1408` · `documented deviations: 79` · `known deviations seen: 79` |
| 6 | `bash tests/run_cli_sanitized.sh` | **0** | `31/31 komut gerçek kodu çalıştırdı` · 0 sanitizer ihbarı |
| perf | `bash scripts/bench_hotpath.sh --output …` | **77** | `CONTROL-SKIP: noaccel deviates 19.21%` → **ölçüm üretmedi** |
| perf (ikili) | `./build-manual/bench_hotpath 3 --json` ×4 | 0 | 4 ölçüm seti, 6 config, `runs_used: 3` |
| perf (PC) | `BENCH_POSITIVE_CONTROL=1 ./build-manual/bench_hotpath 1 --json` | 0 | `noaccel 13.9626 → 1138.3659` (**81×**) |
| denetim | `python3 olcum/aj2/prove_kod_ayni.py --sayi olcum/aj2/sayisal_iddialar.txt` | **0** | `14 sayisal iddia denetlendi, hepsi tutuyor` (mutasyonlu kopyada **1**) |
| 4 | `bash tests/run_simd_parity.sh` (içinden) | 0 | `26 v2d_* · kapsamlı 16 · canlı 8 · ölü 10 · envanter denetimi OK` · 3 backend |
| 7 | `bash tests/run_tracker_bridge.sh` | 0 | `17 kayıt işaretli · AÇIK 0` (mutasyonlu ⏸ ile **1 → rc=1**) |
| 4 | `bash tests/e2e.sh` | — | **koşturulmadı**: root + `/dev/uinput` gerekiyor, yedi kapıdan değil |

⛔ `run_e2e.sh` bu turda **koşturulmadı** (root yok). `run_fuzz.sh` de koşturulmadı
(lane kapsamı dışı + 60 s/harness). `run_tr_coverage.sh` koşturulmadı (lane dışı).

---

## KAPSANMAYAN (lane dışı — birinin bakması gereken)

1. **`bench_hotpath.cpp:227`'deki mutlak yolun CI'daki etkisi.** L20-CRIT-2 bunu
   ölçtü ama GitHub runner'ına erişimim yok. CI loglarını okuyabilen bir ajan
   `perf-gate` job'ının gerçekten kırmızı mı sarı uyarı mı verdiğini
   doğrulamalı — bu, projedeki "CI hiç çalışmadı" iddiasını (`ea39e5bc`) doğrulayan
   ajanla aynı kanıtı gerektirir.
2. **`test_accel.cpp:4636-9769` (stres/fuzz gövdeleri)** — yalnız kayıt defterini,
   çağrı koşullarını ve assertion varlığını denetledim; **her stres testinin
   gerçekten yararlı bir şey ölçtüğünü** (assert içeriğinin anlamı) denetlemedim.
   Özellikle 1 EXPECT'lu 13 test (`test_fuzz_accel_args`, `test_fuzz_json_roundtrip`,
   `test_stress_remainder_drift`, `test_power_extreme_params`,
   `test_synchronous_extreme`, `test_power_output_offset`,
   `test_classic_gain_mode_cap_consistency`, `test_nan_propagation_all_modes`,
   `test_event_batching_accumulation`, `test_modifier_all_flags`,
   `test_modifier_separate_mode` …) — bir döngünün **dışına düşen** tek bir
   `EXPECT`in bütün testi sessizleştirmesi riski ayrı bir iş.
3. **`include/accel-*.hpp` matematiği** — başka lane'lerin konusu; ben yalnız
   oracle/mutasyon üzerinden *kapsam deliklerini* ölçtüm.
4. **`run_tracker_bridge.sh`'in ters yönü** (✅ ama kodda işaret yok) kasıtlı olarak
   kapı dışı — kaynak yorumu ~%15-17 yanlış pozitif diyor; bu iddiayı **doğrulamadım**.
5. **`tests/oracle/run_oracle_perf.sh` / `oracle_perf.cpp`** — okudum, **koşturmadım**
   (bu bir doğruluk kapısı değil, bir çapraz-ölçüm aracı; yedi kapıdan da değil).
6. **CI'daki `sanitizers` ve `fuzz-smoke` job'ları** — yerelde karşılıklarını
   koşturdum (`run_tests_asan.sh` rc=0), CI tanımını denetlemedim.

---

## TEMSIL SINIRI

1. **Bu bir Linux makinede, kök olmadan, `x86_64` üzerinde ölçüldü** (i9-9900K,
   12 çekirdek, `LANG=tr_TR.UTF-8`). `run_e2e.sh` **koşturulmadı** (root +
   `/dev/uinput` yok) → `e2e_harness.cpp`'nin 5 kontrolünün **gerçekten geçtiğini
   gözlemledim, sadece kodunu okudum**. `run_fuzz.sh` koşturulmadı.
2. **Gerçek bir GitHub Actions runner'ına erişimim yok.** L20-CRIT-2 (mutlak yol →
   perf-gate kırmızı) **bu ortamda taklit edilerek** kanıtlandı (yol olmayan ikili
   derlenip `bench_hotpath.sh`'ye konuldu). Runner'ın çalışma dizininin
   `/home/a/Masaüstü/AbrekMouse-main` olmadığı **çıkarımdır**; CI loglarını okumadım.
   AJ1 bunu `ci.yml` loguyla yeniden ölçmeli.
3. **Performans ölçümleri kirli bir makinede alındı** (`loadavg` 6.68); bu yüzden
   `bench_hotpath.sh` **exit 77** verdi ve "hiç ölçüm yok" dedi. Mutlak ns/event
   sayılarım **gürültü içinde** (bkz. tablo); yalnızca *göreli* karşılaştırma ve
   81× pozitif kontrol anlamlı. "Kapı çalışmıyor" hükmüm bu ortamın kirli olmasına
   dayanmıyor — pozitif kontrolle ayrıca doğrulandı.
4. **Süreler tek koşu.** `run_tests.sh` 4m29.6s (AGENTS.md'de 43.2s yazıyor; 12
   çekirdekli yüklü makinede 6× yavaş). Eşzamanlı-koşu dayanıklılığını
   (3/5 kopya) **yeniden ölçmedim** — kaynak yorumundaki 36/36 iddiası bu turda
   doğrulanmadı.
5. **`main`'in `try/catch`'i yok** dışında exception güvenliği ölçülmedi: bir test
   `std::terminate`a düşerse (ör. `remove_all` throwing overload) **sonraki 150 test
   çalışmaz**; rc≠0 olacağı için kapı yine kırmızı, ama "kaç test gerçekten
   koştu" sorusunun cevabı o koşuda **n/N** yerine **kısmi** olur. `run_tests.sh`'in
   özeti (`Sonuç: 34164/34164`) yalnız son koşunun **ulaştığı** sayıyı yazar —
   yani `151` çağrılıyor olması, **hepsinin koştuğunu** kanıtlamıyor (L20-CRIT-1'in
   ikinci yüzü).
6. **Mutasyon kopyası `/tmp/opencode/L20` iki kez sunucu yeniden başlatmalarında
   silindi**; son ölçümler `/home/a/l20_scratch/L20` üzerinde yapıldı. Kayıtlar
   bu turda `/home/a/l20_scratch/*.log` altında duruyor. Sunucu yeniden başlatması
   `/tmp`'yi sildiğinden **oturum kanıtları kalıcı değil** — kalıcı kanıt bu rapor.
7. **`bench_hotpath_results.txt`'nin çalışma ağacındaki değişikliği bana ait
   değil** (mtime 2026-10-02 00:01:46, JSON içerikli, `--json` imzası). Bunu
   **bilerek geri almadım** — brifing §1 `git checkout`/`reset` yasak ve eşzamanlı
   başka bir ajanın yazımı olabilir. AJ1 bu dosyayı karar verirken **bu haliyle**
   görmeli.
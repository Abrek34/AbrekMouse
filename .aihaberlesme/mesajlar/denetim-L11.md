### L11 | alt-ajan (daemon giriş noktası + sınıf bildirimi) | 1 Eki 2026

KAPSAM   : `daemon/main.cpp` (694 satır, tamamı) + `daemon/daemon.hpp` (433 satır, tamamı).
           `daemon/daemon.cpp` yalnızca **okundu** — görev gereği beyan/tanım/çağrı
           eşleşmesi ölçüldü, kod değiştirilmedi. Kapsam dışı: `src/config.cpp`,
           `include/**` yalnızca pozitif kontrol için tarandı.

---

## ⭐ BAŞLICA BULGU (SESSİZ YEŞİL — systemd'de en tehlikeli sınıf)

`start()` yetkisiz erişimde **başarılı** dönüyor, `main` **exit 0** veriyor.
Yani "fare yok" ile "yetkin yok" ayrımı yapılmıyor ve sistem hizmeti
**"başarıyla çalışıyor" sanıyor** — oysa hiçbir cihazı işlemiyor.

Kanıt zinciri (4 ayrı ölçüm):

| # | Ölçüm | Komut / yöntem | Ham sonuç |
|---|---|---|---|
| 1 | `/dev/input` **hiç yok** | bwrap + `--dev /dev` | `DAEMON_EXIT=0`, "No physical mice found yet", "Daemon started." |
| 2 | `/dev/input/event*` **var ama EACCES** | `chmod 000 /dev/input/event0` | `EACCES/HATA: Permission denied` → `DAEMON_EXIT=0` |
| 3 | 25 sn çalıştırma | `timeout 25` | `EXIT=124` (yani **yaşıyor**), toplam **8 satır**, mesaj **1 kez** |
| 4 | A/B mutasyon | `daemon.cpp` **kopyasında** `return true`→`false` | A: `EXIT=124`, Tips=0 · B: `EXIT=1`, Tips=1 |

**Kök neden — `setup_devices()` iki hatayı da `true` ile geçiştiriyor:**

- `daemon/daemon.cpp:834-843` — `mice.empty()` → log + **`return true;`**
- `daemon/daemon.cpp:970-977` — "No mice could be grabbed (permission or conflict)" → log + **`return true;`**

Böylece `start()`'in false dönmesi için yol kalmıyor; `start()`'te ölçülen
`return false` yalnızca 2 yerde: `epoll_create1` başarısızlığı (`:489`) ve
`setup_devices()` (`:519`). Erişim/izin hatası **ikisi de değil**.

**Mutasyon kanıtı (brifing §4):** KOPYADA `setup_devices()`'in iki
`return true;`'sini `return false;` yaptım → EACCES ortamında `EXIT=0` → `EXIT=1`
oldu ve `main.cpp:652`'deki "Failed to start. **Tips:**" bloğu **ilk kez görünür
oldu**. Yani o mesaj var ama **üretimde ulaşılamaz**. Bu, "koruma mevcut" denilen
şeyin fiilen çalışmadığının doğrudan kanıtı.

⭐ Kullanıcıya gösterilen tek satır (`:841`) yetki ipucu içerir
("check 'input' group membership or udev rules") ama (a) bunu **sadece bir kez**
yazar, (b) yetkisizlik ile donanım yokluğunu **ayırmaz** — ikisi de aynı
`mice.empty()` dalına düşer, (c) kullanıcı `sudo`'yu bilmiyorsa daemon'ın
**sessizce hiç iş yapmadığını** gösteren hiçbir sinyal yoktur.

---

## BULGULAR (üç kolonlu eşleşme tablosu + maddeler)

### 1) ⭐ BEYAN ↔ TANIM ↔ ÇAĞRI EŞLEŞMESİ

Yöntem: `clang++ -Xclang -ast-dump -ast-dump-filter=AccelDaemon` ile **derleyiciden**
üye listesi + erişim belirteci çıkarıldı (elle regex değil), sonra
`grep -n "AccelDaemon::" daemon/daemon.cpp` ile tanımlar ve `grep -rn -w` ile
çağrılar sayıldı. **Derleyicinin verdiği üye sayısı: 38.**

| hpp:sat | Kapsam | Üye | Beyan | Tanım | Çağrı | Durum |
|---|---|---|---|---|---|---|
| 134 | public | `AccelDaemon` | VAR | cpp:443 | — | ✅ |
| 135 | public | `~AccelDaemon` | VAR | cpp:445 | yıkıcı | ✅ |
| 138 | public | `start` | VAR | cpp:461 | main:651 | ✅ |
| 141 | public | `stop` | VAR | cpp:570 | main:688 + dtor:450 | ✅ |
| 145 | public | `request_stop` | VAR | **başlıkta** | main:133,674 + cpp:536,539,549,552,2089 | ✅ |
| 148 | public | `reload` | VAR | cpp:585 | main:124 | ✅ |
| 157 | public | `push_config` | VAR | cpp:590 | ipc | ✅ |
| 159 | public | `is_running` | VAR | **başlıkta** | main:675 | ✅ |
| 162 | public | `set_log_cb` | VAR | **başlıkta** | main:597 | ✅ |
| 165 | public | `set_verbose` | VAR | **başlıkta** | main:598 | ✅ |
| 169 | public | `dump_latency_stats` | VAR | cpp:2897 | main:679 | ✅ |
| 176 | public | `consume_latency_dump_request` | VAR | **başlıkta** | main:678 | ✅ |
| 182 | public | `status_json` | VAR | cpp:3052 | ipc | ✅ |
| 198 | public | `start_ipc_server` | VAR | cpp:3205 | main:646 | ✅ |
| 199 | public | `stop_ipc_server` | VAR | cpp:3331 | main:658,687 + dtor | ✅ |
| 200 | public | `ipc_sock_path` | VAR | cpp:3046 | main:645 | ✅ |
| 203 | **private** | `run_loop` | VAR | cpp:1988 | thread | ✅ |
| 207 | **private** | `run_hidpp_worker` | VAR | cpp:1680 | thread | ✅ |
| 208 | **private** | `setup_devices` | VAR | cpp:832 | start:519, hotplug | ✅ |
| 209 | **private** | `teardown_devices` | VAR | cpp:980 | stop:578 | ✅ |
| 210 | **private** | `open_input_device` | VAR | cpp:704 | setup:864 | ✅ |
| 211 | **private** | `create_virtual_device` | VAR | cpp:775 | setup:919 | ✅ |
| 212 | **private** | `process_device` | VAR | cpp:2549 | run_loop | ✅ |
| 213 | **private** | `apply_profile` | VAR | cpp:1184 | setup/reload | ✅ |
| 217 | **private** | `release_device` | VAR | cpp:1306 | apply_new_config | ✅ |
| 221 | **private** | `apply_new_config` | VAR | cpp:1333 | reload/ipc | ✅ |
| 227 | **private** | `find_profile` | VAR | cpp:1008 | setup + apply | ✅ |
| 232 | **private** | `apply_active_app` | VAR | cpp:1061 | run_loop | ✅ |
| 233 | **private** | `handle_hotplug` | VAR | cpp:1465 | run_loop | ✅ |
| 234 | **private** | `do_hotplug_scan` | VAR | cpp:1490 | run_loop | ✅ |
| 241 | **private** | `poll_hidpp_notifications` | VAR | cpp:1838 | hidpp worker | ✅ |
| 242 | **private** | `log` | VAR | cpp:453 | 172 satır | ✅ |
| 321 | **private** | `apply_hidpp_battery` | VAR | cpp:1792 | poll | ✅ |
| 333 | **private** | `find_hidpp_transport` | VAR | cpp:1116 | apply_profile | ✅ |
| 373 | **private** | `drain_hidpp_writes` | VAR | cpp:1736 | hidpp worker | ✅ |
| 396 | **private** | `save_worker` | VAR | cpp:663 | thread | ✅ |
| 429 | **private** | `ipc_serve_loop` | VAR | cpp:3363 | thread | ✅ |
| 430 | **private** | `handle_ipc_client` | VAR | cpp:3457 | serve loop | ✅ |

**Sonuç: 38/38 beyan var, 38/38 tanım var, 38/38 çağrılıyor.**
`TANIMI EKSİK olan: YOK` · `cpp'de tanım var ama beyan yok: YOK` ·
`ÇAĞRILMAYAN: YOK` → ⛔ **sessiz ölü üye kod YOK** (L11-1 talebinin cevabı).

⭐ **Bunun en güçlü kanıtı bağlayıcıdır** (elle aramaya değil): üç kaynak
`/tmp` kopyasında birleştirildi, `g++` **temiz bağladı**:
`LINK_RC=0` — tek bir `undefined reference` yok. Eksik tanım olsaydı link
kırılırdı. (Bağlantı denemesinde çıkan ilk hata turu `src/logitech_hidpp.cpp`
eksikliğindendi; o dosya eklenince `LINK_RC=0` oldu — ve o turdaki **tek**
eksik referans grubu `AccelDaemon::` değil, `rawaccel::HidppTransport::*` idi.)

### 2) ⭐ KÖK YETKİ KONTROLÜ

**`main.cpp` içinde kök yetki kontrolü YOK.** Kanıt:
`grep -nE "geteuid|getuid|EACCES|root|privileg|yetki" daemon/main.cpp`
→ 6 eşleşmenin **tamamı** yorum satırı (`// root-fallback`, `// runs as root`,
`// BUG-4: ... EPERM`). `geteuid()`/`getuid()` **hiç çağrılmıyor**.

Doğrudan kanıt — `find_mice()` EACCES'yi **sessizce yutuyor**:

```
daemon/daemon.cpp:427-430
            if (fd < 0) {
                // D3: access error — permission issue or device already grabbed (informational)
                continue;          // ← errno ASLA loglanmıyor, EACCES ile ENOENT ayırt edilmiyor
            }
```

Erişilemeyen düğüm `mice` listesine hiç girmiyor → `setup_devices():834` boş listeyi
görüp yukarıdaki başlık bulgusuna düşüyor. Bu, görevde sorulan ayrımın
**tam olarak tersi**: kullanıcı "fare bulunamadı" mesajını alıyor, sebebinin
izin olduğunu **hiçbir yerde** öğrenemiyor.

### 3) ⭐ SİNYAL İŞLEME / TEMİZLİK

İyi haber: **temizlik doğru çalışıyor.** Ölçüm (bwrap, çalışan IPC soketiyle):

| Ölçüm | Komut | Sonuç |
|---|---|---|
| SIGINT sonrası soket | `kill -INT` → `ls /tmp/xdg/rawaccel.sock` | **yok** → "temiz - kalinti yok" |
| SIGINT exit kodu | `wait $P` | `SIGINT_EXIT_CODE=0` |
| PID dosyası | `ls /tmp/rawaccel.pid` | yok (unlink edildi) |
| **2. çalıştırma** | temiz kapanıştan sonra yeniden başlat | `AYAKTA` · "cihaz mesgul" hatası **YOK** |
| SIGKILL sonrası 2. çalıştırma | `kill -9` → yeniden başlat | `EXIT=124` (başladı, çalıştı) |

`stop_ipc_server()` (`daemon.cpp:3331-3360`) yolu incelendi: `ipc_sock_path_`
mutex altında kopyalanıp **dışarıda `unlink()`** ediliyor (`:3357-3359`).
PID dosyası `remove_pid()` (`:72-77`) ile temizleniyor.

⭐ **Ama bir not**: `stop()` idempotent değil. `main.cpp:688` `stop()` çağırıyor,
sonra kapsam dışına çıkarken `~AccelDaemon()` (`daemon.cpp:450`) **ikinci kez**
`stop()` çağırıyor. Ölçülen çıktıda bu **iki kez "Daemon stopped."** olarak
görünüyor. Zararsız (tüm `joinable()` kontrolleri ve fd'ler `-1`'e çekiliyor,
`close()` çift yapılmıyor) ama günlüğe yanlış sayı yazıyor — ve `teardown_devices()`
iki kez döner. **LOW**.

### 4) ⭐ ARGÜMAN AYRIŞTIRMA

| Argüman | Davranış | Ölçülen |
|---|---|---|
| `--help` / `-h` | yazdırır | `EXIT=0` ✅ |
| `-V` / `--version` | `rawaccel-daemon 1.2.5` | `EXIT=0` ✅ |
| `-c /work/.../x.txt` (uzantısız) | **reddeder** | `EXIT=1` ✅ |
| `-c /dev/shm/a.json` (yasaklı dizin) | **reddeder** | `EXIT=1` ✅ |
| `--config=` (boş değer) | **reddeder** | `EXIT=1` ✅ |
| `-c` (değer yok) | **reddeder** | `EXIT=1` ✅ |
| `-f xml` (geçersiz biçim) | **reddeder** | `EXIT=1` ✅ |
| ⭐ **`--bogus` (bilinmeyen)** | ⭐ **SESSİZCE YOK SAYILIR** | `EXIT=124` (başladı, çalıştı!) |
| ⭐ **`-x` (bilinmeyen)** | ⭐ **SESSİZCE YOK SAYILIR** | `EXIT=124` (başladı, çalıştı!) |

`main.cpp:329-393` döngüsü `else` dalı **yok**; eşleşmeyen argüman düşer.
Sonuç: kullanıcı `--verbos` (yazım hatası) yazarsa daemon **sessizce
default ayarlarla çalışır** ve kullanıcı verbose log'u açmadığını sanar.
**MED** — "geçersiz argüman hata üretmiyor, varsayılana düşüyor."

**Bayrak etkinliği (grep -c ile kullanım sayısı):**
- `verbose`: yazım `main.cpp:365` (`verbose = true`), **tek okuma** `main.cpp:598`
  (`daemon.set_verbose(verbose)`). `verbose_` üye değişkeni `daemon.cpp`'de
  **yalnızca 1 yerde okunuyor**: `:454` (`if (verbose_only && !verbose_) return;`).
  → `grep -c "verbose_" daemon/daemon.cpp` = **2** (biri `:454` okuma, biri
  `:453` imza satırı). Etkinliği **çalıştırarak doğruladım**: `-v` ile 11 satır,
  varsayılan ile 9 satır; `diff` farkı tam olarak 2 verbose satırı.
  ⭐ **YANLIŞ OLABİLECEK YER BURADA** ama DEĞİL: `set_verbose` sadece
  `verbose_only=true` işaretli logları açar. Ölçüldü.
- `config_path`: 3 yazma (`:357`, `:363`, `:607`), **18 okuma**. Etkin.
- `log_format`: `:552`'de `json_logs`'a çevrilir, `:582`'de okunur. Etkin —
  **ama aşağıdaki bulgu hariç**.

### 5) ⭐ KAPSAM (private/public) — DOĞRU

`flush_motion`/`process_device` **private**:

- `process_device` — `daemon.hpp:212`, `private:` **altında** (`:202`).
  Derleyici AST'i bunu doğruladı: `212 private CXXMethodDecl process_device`.
- ⭐ **`flush_motion` bir üye DEĞİL.** `daemon/daemon.cpp:2344`'te
  `static bool flush_motion(...)` — dosya kapsamlı **serbest fonksiyon**,
  `daemon.hpp`'te **hiç geçmiyor** (`grep -n flush_motion daemon/daemon.hpp` →
  yalnız `:119` ve `:127` **yorum** satırları). Yani görevde varsayılan
  olan "flush_motion'un private olması" gereksinimi **yerine getirilmiş** çünkü
  kapsam dışına hiç çıkmıyor. Ağır üyelerin tamamı private:
  `run_loop`, `process_device`, `apply_profile`, `drain_hidpp_writes`,
  `save_worker`, `ipc_serve_loop` → hepsi `private`.
  `public` yüzeyi 16 üye ile sınırlı ve hepsi dışarıdan gerçekten çağrılıyor.
  **INFO — kapsam hatası YOK.**

### 6) ⭐ `--log-format json` AKIŞI BOZUK (yeni bulgu)

`--log-format=json` seçildiğinde `log_cb` JSON üretiyor — ama `main.cpp` kendi
4 mesajını `log_cb`'den **atlayarak** doğrudan `std::cout`'a yazıyor
(`:470` PID file, `:604` version, `:642` Config, `:649` IPC socket). Ölçüm:

```
python3 -c "json.loads satir basina"
GECERSIZ JSON satir 0 'PID file: /tmp/xdg/rawaccel.pid' Expecting value: line 1 column 1
GECERSIZ JSON satir 1 'RawAccel Linux Daemon v1.2.5'  ...
GECERSIZ JSON satir 2 'Config: /work/.../settings.json'  ...
GECERSIZ JSON satir 3 'IPC socket: /tmp/xdg/rawaccel.sock'  ...
gecerli JSON satiri: 5   gecersiz: 4
```

`--log-format json` sözü "her satır bir JSON nesnesi" (`main.cpp:583` yorumu)
veriyor, gerçekte **5 JSON + 4 çıplak metin** karışıyor. `journalctl -o json`
gibi bir tüketici satır başına ayrıştırmaya çalışırsa akış bozulur.
**MED** — `json_escape` (`:557`) doğru çalışıyor (`:592`'de kullanılıyor),
sorun kaçırılan 4 satır.

---

## ⭐ SINYAL: `start()` BAŞARISIZ OLURSA `main` NE DÖNDÜRÜYOR?

**Yazılı cevap doğru: `return 1`** (`main.cpp:661`). Ölçüldü — mutasyonlu ikili
`start()`'ı başarısız yaptığımda `EXIT=1` çıktı ve `Tips` bloğu göründü.
⛔ Yani **`exit 0` dönen bir yol YOK**; buradaki tehlike "kod" değil,
`start()`'in yetki hatasında zaten `false` **dönmemesidir** (bulgu 1).

Bu ayrım kritik ve şöyle özetlenebilir:
- Kod yolu **doğru** (`main.cpp:651-662`: `stop_ipc_server()` + `g_daemon=nullptr`
  + `remove_pid()` + `return 1` — yıkıcıya düşüp `std::terminate`/SIGABRT riski yok).
- **Ama `start()` yetki/erişim hatasında `true` döndüğü için bu yola hiç girilmiyor.**
  systemd `Type=simple` + `Restart=on-failure` için "başarıyla çalışıyor"
  sayar, `systemctl status` **active (running)** gösterir, ve kullanıcı hiçbir
  ipucu almadan fare hızlandırmasız kalır.

Ek: `run_tests.sh` **bu yolu hiç test etmiyor** —
`grep -c "Failed to start" tests/run_tests.sh` = **0**,
`grep -c "No physical mice" tests/run_tests.sh` = **0**,
`grep -c "Tips" tests/run_tests.sh` = **0**. Yani 7 kapı yeşil iken de bu
CRIT sessizce geçiyor.

---

KAPI:
1. `g++ -std=c++20 -c daemon/daemon.cpp` → `COMPILE_RC=0` (TU derleniyor)
2. `g++ ... daemon/main.cpp daemon/daemon.cpp src/config.cpp src/logitech_hidpp.cpp`
   → **`LINK_RC=0`** — üretilen `daemon-link-test`; **undefined reference: 0**
   (beyan/tanım eşleşmesinin bağlayıcı kanıtı)
3. ASan+UBSan ikili `ASAN_BUILD_RC=0`; EACCES ortamında tam yaşam döngüsü →
   **`ASAN EXIT=0`**, `asan_err.txt` **0 satır** (ERROR / runtime error / leak yok)
4. `clang++ -Xclang -ast-dump -ast-dump-filter=AccelDaemon` → **38 üye**, erişim
   belirteçli, `private`/`public` doğrulandı
5. ⭐ **Mutasyon (kopya üzerinde)** — `setup_devices()`'in iki `return true;`'si
   `return false` yapıldı → A/B: gerçek `EXIT=124`·Tips=0 / mutasyonlu
   `EXIT=1`·Tips=1
6. Proje kapısı 2: `bash tests/run_tests.sh` → `GATE_RC=0`,
   **`=== Sonuç: 34164/34164 geçti ===`** (7 kapıdan 1; bu CRIT'i yakalamıyor)
7. Ortam: gerçek `/dev/input` erişimi YOK → `bwrap` ile `EACCES`/`--dev /dev`
   senaryoları **yeniden üretildi**; `unshare` yerine `bwrap` kullanıldı çünkü
   `ps` çalışan PID'yi `/proc` bağı olmadan göstermiyordu.

KAPSANMAYAN:
- ⛔ `daemon/daemon.cpp`'in **iç mantığı** (hot-plug, HID++, `process_device`
  içi, epoch/deny listeleri) — başka lane'in sahipliği. Benim ölçtüğüm yalnız
  **beyan/tanım/çağrı** eşleşmesi ve kapsam.
- `resolve_config_target()` / `check_config_path_policy()` güvenlik analizi —
  davranışlarını ölçtüm (7 senaryonun hepsi doğru), ama `realpath` TOCTOU
  yarışlarını denetlemedim; CLI kopyasıyla bayt-bayt aynı olduğu iddiası da
  benim ölçümüm değil.
- `start()` **başarılı** iken 1–2 cihazın grant edilememesi (kısmi yetki) —
  `no_devices` yolunu ancak **mutasyonla** tetikledim; gerçek ortamda
  `/dev/input`'u erişebildiğim için 0 cihaz grab edilebilir durumu kuramadım
  (bkz. TEMSİL SINIRI).
- systemd unit dosyası (`ExecStart`, `Restart=`) — `packaging/` kapsam dışı;
  "systemd başarılı sanır" hükmüm `Restart=on-failure` + `Type=simple`
  davranışına dayanır, unit'in birebir okunmasıyla doğrulanmadı.
- `main.cpp`'deki `print_usage()` metninin CLI/GUI ile tutarlılığı.

TEMSIL SINIRI:
- ⛔ **Fiziksel cihaz yok.** Bu makinede `/dev/input/event*` `root:input 0660`
  ve ben `input` grubunda (`id` → `gid=992(input)`), dolayısıyla **yetkisiz
  erişimi canlı donanım üzerinde üretemedim**. `EACCES` senaryosunu
  `bwrap` içinde `chmod 000` ile **taklit ettim**; bu, gerçek `EACCES`'nin
  `errno` ve `open()` sonucu düzeyinde eşdeğerdir ama kernel'in gerçek
  `input` grubu/udev kararını içermez.
- ⛔ **`start()`'ın `false` dönmesi gerçek koşulda hiç ölçülmedi** (epoll
  başarısızlığı ya da setup_devices'in gerçekten false dönmesi). Yalnızca
  **mutasyonla** eriştim. Dolayısıyla "başarısızlık yolu çalışıyor" hükmüm
  `main.cpp:651-662` okumasına + mutasyon gözlemine dayanır, canlı hataya
  dayanmaz.
- ⛔ **SIGTERM'in `systemctl stop` yolu** ölçülmedi (unit yok, root yok);
  yalnız doğrudan `kill -TERM`/`kill -INT` ile ölçüldü. Sinyal ayrımı
  `handle_signal` içinde `else` dalı olduğu için SIGINT ile aynı yolda;
  bu okumadır, ölçüm değil.
- ⛔ Gerçek HID++/Logitech donanım, `abrek` çakışması, `modprobe uinput`
  senaryoları ölçülmedi — bu lane'in kapsamı dışında ve donanım yok.
- Çok-instance testi **PID dosyası yazan** ikililerde yapıldı; "iki daemon aynı
  anda `open()` yarışı" (`O_CREAT|O_EXCL`in sağladığı garanti) test edilmedi.
- `sleep(1)` ana döngüsü (`main.cpp:676`) nedeniyle SIGINT yanıtı en kötü
  ~1 sn; 8 sn'lik ölçümlerde fark edilmedi, **kasten ölçmedim**.

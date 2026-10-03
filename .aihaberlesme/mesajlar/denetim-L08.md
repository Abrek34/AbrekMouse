### L08 | denetim alt-ajanı (soba-bunu) | 2026-10-01

KAPSAM   : `daemon/daemon.cpp` **yalnız satır 1–1987**. 1988+ satırlara dokunulmadı,
           hiçbir dosya değiştirilmedi (`git status --porcelain -- daemon/ src/ include/
           cli/ gui/ tests/` → boş; `git diff --stat` → boş). Tüm mutasyon denemeleri
           `/tmp/opencode/l08/` kopyalarında yapıldı. 1988+ satırlardan yalnızca
           *tüketici* olarak 3 yerde alıntı yapıldı (:2001-2018 push_cfg tüketimi,
           :2044/:2067 hotplug, :2427 tüketim) — sahip olmadığım alana hüküm verilmedi.

---

BULGULAR :

---

**L08-01 | `daemon/daemon.cpp:611-661` (guard `:624-638`) | CRIT | SESSİZ YEŞİL —
`push_config` bir geri alma push'unu "başarılı" diye onaylar, uygulamaz, ve tersi
config'i uygular. Ölçüldü ve çalıştırıldı.**

`push_config` üç "skip" koruması taşır. `has_pending && new_json == pending_json`
karşılaştırması (`:624`) **Bekleyen** config'i doğru hedefler. Ama `:629`'daki
`new_hash == config_hash_` koruması **UYGULANMIŞ** config'in hash'ini hedefler
(`config_hash_` yalnız `start():482` ve `apply_new_config():1347`'de güncellenir).
Sonuç: uygulanmış config'e geri dönmek, "zaten o" denilerek atılır — **ama o sırada
bekleyen farklı bir push varsa o uygulanır.**

Projelerin **gerçek** `app_config_from_json`/`app_config_to_json`'ı ile
`daemon.cpp:609-639`'un birebir transkripsiyonu çalıştırıldı
(`/tmp/opencode/l08/pushguard.cpp`, `g++ -O1 -std=c++20 -I include/ ... src/config.cpp`):

```
t0 applied config = default (hash 4918065500840690722)
t1 after push B + save_worker arm: pending=1, applied still=[default]
t2 push_config(A = revert) returned true
      log: Config push skipped (no-op guard: hash unchanged).
      push_cfg_pending_ STILL 1  (B will be applied by run_loop)
t3 AFTER run_loop applied the pending push: applied config = [gaming]  profiles=2

>>> user asked to revert to A. Daemon reported success=true. Effective config=gaming  <<< REVERT SILENTLY LOST
```

Somut etki (briefing §3'ün "çalışırken ne olduğunda fark edilir?" sorusu):
`push_config` `true` döndürür, `log(...)` **verbose-only**'dır (`:625/:630/:636`
→ `log(msg, true)`), yani kullanıcı non-verbose'da **hiçbir şey görmez**. Arada
bulunan pencere gerçektir ve dar değildir: `save_worker` diski yazıp `push_cfg_`'yi
kaldırmadan (`:681-688`) `run_loop`'un o bayrağı tüketmesine kadar (`:2001-2018`,
başka ajanın satırı, tüketici olarak okundu) geçen süre, `save_worker`'ın 10 ms'lik
nap'ı (`:698`) + `run_loop` epoll zaman aşımı kadardır. Bu pencere doluyken
gelen herhangi bir geri alma sessizce yutulur. Ayrıca `:659` `catch` bloğu
yalnız *parse* hatalarını yakalar — bu bir **parse hatası değil, bir no-op
kararıdır**, dolayısıyla "rejected" bile değildir; temiz bir `true` döner.

Düzeltilebilir tek satır: `:629`'daki koruma `has_pending` varsa `config_hash_`
ile karşılaştırılmamalı, yoksa atlanmalı.

---

**L08-02 | `daemon/daemon.cpp:176-217`, `:221-284` | INFO (görev varsayımı ÇÜRÜTÜLDÜ) |
`sysfs_read_int`/`sysfs_read_usb_speed` okuma hatasında `0` DEĞİL `-1` döner.**

Görev metni "hata mı dönüyor, yoksa 0 mı" diye soruyordu. Ölçüm: **`-1`**.
Fonksiyonların birebir kopyası gerçek dosyalarla çalıştırıldı
(`/tmp/opencode/l08/probe.cpp`):

```
case                                           | sysfs_read_int   | sysfs_read_usb_speed
----------------------------------------------------------------------
/tmp/opencode/l08/missing_file                 | -1               | -1
/tmp/opencode/l08/empty_file                   | -1               | -1
/tmp/opencode/l08/garbage_file                 | -1               | -1
/tmp/opencode/l08/no_newline_trunc             | -1               | 3
content "0" (legit zero)                       | 0                | -1
content "1" (bInterval)                        | 1                | 1
content "12" (full-speed Mbps text!)           | 12               | 2
content "480"                                  | 480              | 3
content "1.5"                                  | 1                | 1
```

`0` yalnızca dosya **gerçekten "0" içeriyorken** döner; `return` ifadeleri
`:178` (fopen), `:182` (n==0), `:188` (parse/range) → üçü de `-1`. Bu, kodun kendi
yorumunun da doğruladığı bir şey: `:266`'daki `BUG-NEW-80` notu "the old `== 0`
test was dead code — `sysfs_read_int()` returns -1, never 0" diyor ve `:259`'daki
`usb_speed <= 0` dalı buna göre yazılmış. **Bu dal ölü kod değil.**

`0 = "0 Hz"` yanlış yorumlanması da **gerçekleşmiyor**: `detect_polling_rate()`
başarısızlıkta `0` döner (`:283`, "unknown") ve `detected_polling_rate` alanı
`daemon.hpp:51`'de açıkça `0 = unknown` olarak belgelidir. Tüketici de bunu
doğru ele alıyor — `cli/main.cpp:2184` `if (det_poll > 0 || real_poll > 0)`
kapısından geçmiyorsa `detected:` satırını **hiç basmıyor**; `"? Hz"` gösterimi
yalnız `det_poll > 0` iken mümkün.

**Canlı doğrulama** (bu makinede daemon root olarak çalışıyor, PID 695):
`/run/rawaccel.sock`'e `status` gönderildi →
`{"name":"VMware VMware Virtual USB Mouse",...,"detected_dpi":0,"detected_polling_rate":0,...}`
`detected_polling_rate: 0` yayınlanıyor ve CLI bunu `"? Hz"` olarak değil,
**hiçbir şey basmayarak** ele alıyor (`rawaccel-cli status` çıktısında
`detected:` satırı yok). Sysfs'in bu cihazlarda node'ları yok
(`/sys/class/input/event2/device/polling_rate|resolution|device/bInterval|device/speed`
→ 4/4 ABSENT), yani 0 = "node yok" ve doğru raporlanıyor.

---

**L08-03 | `daemon/daemon.cpp:100`, `:120-126` | INFO (yasak kalıp YOK) |
`DENY_REOPEN_MS` sabit bir *süre* değil, bir *deadline*. Kodda `sleep`/`usleep`
çağrısı ile beklenmiyor.**

Görev "sabit süre mi, kodda sabit sleep var mı" diye soruyordu. Ölçüm:
`deny_reopen` (`:120-126`) `now_ms() + DENY_REOPEN_MS` hesaplayıp haritaya
**sonlanma zamanı** yazıyor; bekleyen hiçbir şey yok. Karşılaştırma
`reopen_denied` (`:110-115`) ve `setup_devices:860` / `do_hotplug_scan:1514`'te
`nowt < dn->second` ile yapılıyor.

`daemon/daemon.cpp` 1–1987 aralığında **hiç POSIX `sleep`/`usleep` yok** —
`awk 'NR<=1987' | grep` → yalnız 3 `std::this_thread::sleep_for`:
`:698` (10 ms, `save_worker`), `:1699` (20 ms), `:1701` (200 ms, `run_hidpp_worker`).
Hepsi worker thread'lerinde, hiçbiri motion loop'ta; kod tabanındaki sticky-flag
konvansiyonunun parçası ve `daemon.hpp:390`'da belgeleniyor. `:1485`'te hotplug
bilinçli olarak `no usleep` notuyla erteleniyor. **Bu lane'de yasak desen bulunmadı.**

---

**L08-04 | `daemon/daemon.cpp:850-851`, `:1506-1507` | INFO (sızıntı YOK) |
`prune_path_deny`/`prune_dev_deny` gerçekten çağrılıyor — her biri 2 çağrı yeri.**

```
  prune_path_deny    grep -c => 3  (1 def + N calls)
  prune_dev_deny     grep -c => 3  (1 def + N calls)
  deny_reopen        grep -c => 9  (1 def + N calls)
  reopen_denied      grep -c => 3  (1 def + N calls)

  call sites:
850:    prune_path_deny(path_deny_until_ms_, mice, setup_now);
851:    prune_dev_deny(dev_deny_until_ms_, setup_now);
1506:    prune_path_deny(path_deny_until_ms_, mice, nowt);
1507:    prune_dev_deny(dev_deny_until_ms_, nowt);
```

Her ikisi de `setup_devices()` (`:850`) ve `do_hotplug_scan()` (`:1506`) içinde,
`deny_reopen` eklayan her 4 yolun (`:870`, `:881`, `:928`, `:1533`, `:1545`, `:1592`)
kapsayıcısında çağrılıyor. `do_hotplug_scan` `run_loop`'ta ~2 s periyotla
çağrılıyor (`:2067`, tüketici olarak doğrulandı). **Yazılıp çağrılmayan temizleme
yok → bellek sızıntısı bulgusu bu satırlarda geçerli değil.** Her iki harita da
`do_hotplug_scan`'in periyodik çağrısı sayesinde sınırsız büyüyemez.

---

**L08-05 | `daemon/daemon.cpp:735-747` + `:898-905`, `:1555-1562` | HIGH |
Profil ataması `usb:VID:PID:` (serial BOŞ) üzerinden kuruluyor ve bu, iki özdeş
cihazın birbirini DÜRÜTmesine yol açıyor. `resolve_stable_id` bu yolun bir parçası
DEĞİL.**

Kimlik iki ayrı alandan geliyor ve bunlar karışmış:
- `dev.path` ← `find_mice():432` → `resolve_stable_id()` (by-id yolu) — **kararlılık
  bu sağlar**
- `dev.device_id` ← `open_input_device():735-747` → `EVIOCGID` + `EVIOCGUNIQ` —
  **profil eşlemesi BUNU kullanıyor** (`find_profile(dev.device_id)`, 4 çağrı
  yeri: `:907`, `:1093`, `:1349`, `:1575`)

`EVIOCGUNIQ` başarısız/boş döndüğünde `snprintf(id_buf,..., "usb:%04x:%04x:%s", ...)`
**sonuna `:` bırakıp boş serial ekliyor** → id `"usb:0e0f:0003:"`. Bu host'taki
gerçek cihazlarda ölçüldü (`/tmp/opencode/l08/uniq.cpp`, `EVIOCGID`/`EVIOCGUNIQ`
çağrıları birebir):

```
/dev/input/event2  name=VMware VMware Virtual USB Mouse  uniq_rc=1  uniq=<EMPTY>  vid:pid=0e0f:0003  -> device_id=[usb:0e0f:0003:]
/dev/input/event4  name=VirtualPS/2 VMware VMMouse        uniq_rc=-1 uniq=<EMPTY>  vid:pid=0002:0013  -> device_id=[usb:0002:0013:]
/dev/input/event5  name=VirtualPS/2 VMware VMMouse        uniq_rc=-1 uniq=<EMPTY>  vid:pid=0002:0013  -> device_id=[usb:0002:0013:]
```

**event4 ve event5 BİREBİR AYNI `device_id`'yi üretiyor.** `ioctl` dönüş değeri
`open_input_device():728`'de bilinçli olarak yok sayılıyor (`// ignore error`), bu
yüzden `uniq_rc=-1` ile `uniq_rc=1` (başarılı ama boş) aynı sonuca düşüyor.

Sonuç, `HP-3` kapısı (`:898-905`, hotplug'ta `:1555-1562`):
`opened_device_ids_` bu id'yi gördüğü için ikinci cihaz **hiç açılmadan** atılıyor.
Ölçülen çıktı (`/tmp/opencode/l08/hp3.cpp`, gerçek ioctl değerleriyle):

```
  event2 (VMware USB Mouse)  device_id=[usb:0e0f:0003:]  -> GRABBED, uinput created, accelerated
  event4 (VMMouse PS/2)      device_id=[usb:0002:0013:]  -> GRABBED, uinput created, accelerated
  event5 (VMMouse PS/2)      device_id=[usb:0002:0013:]  -> SKIPPED as duplicate (log is VERBOSE-ONLY, daemon.cpp:900)
```

Kullanıcıya ne söyleniyor: **hiçbir şey.** `:900` log satırı `verbose_only=true`
 parametresiyle basılıyor, yani default çalıştırmada ikinci fare tamamen sessizce
hızlandırılmaz. `resolve_stable_id` bu durumu kurtarmaz — o yol `dev.path`'i
kararlı kılar, ama eşleme `dev.device_id` üzerinden yapılır ve by-id yolu
(`usb-VMware_...-event-mouse`) hiçbir yerde eşleme anahtarı olarak kullanılmaz.
Ölçüldü: aynı makinede `resolve_stable_id("/dev/input/event2")` →
`/dev/input/by-id/usb-VMware_VMware_Virtual_USB_Mouse-event-mouse` (doğru çalışıyor),
ama bu değer `find_profile`'a hiç ulaşmıyor.

**SORU 4'ün YARISI YANITLANDI**: "takılıp çıkarınca değişir mi?" → `device_id`
tutan değil, **çünkü zaten serial içermiyor**. Serialsiz (HID++ dışı USB) iki
özdeş farede profil ataması korunamaz, çünkü ikincisi hiç açılmaz. Serialsiz
cihazda by-id yolu (`resolve_stable_id`'in ürettiği, ayrım yapabilen değer)
kullanılsa idi bu sorun olmayacaktı.

---

**L08-06 | `daemon/daemon.cpp:919-931`, `:1584-1595` vs `:853-963` | MED |
`setup_devices` kısmi kurulumda TÜMÜNÜ durdurmuyor, devam ediyor — ama
"hiçbir şey açılamadı" durumunda kullanıcıya yalnız bir log satırı veriliyor ve
daemon exit 0 ile "başarıyla" başlıyor.**

Kısmi hata yolu cihaz başına `continue` (`:871`, `:882`, `:891`, `:904`, `:917`,
`:930`, `:953`) — yani **devam ediyor**, diğer fareler etkilenmiyor. Bu doğru davranış.

Kısmi kurulum durumunda ne söylendiği:
- 0/2 fare açıldıysa `:970-976` "No mice could be grabbed (permission or conflict,
  e.g. abrek). Retrying automatically" basıyor ve `true` dönüyor.
- **1/2 açıldıysa** hiçbir şey söylenmiyor: `:845` "Found 2 physical mouse
  device(s)" yazıyor, `:871`/`:882`/`:904`/`:917`/`:930` hepsi **verbose-only**
  `continue`'lar. Yani "2 bulundu, 1'i alındı, 1'i şu sebepten atlandı" özeti
  **yok** — kullanıcı non-verbose log'da yalnız "Found 2" görür ve hangi farenin
  atlandığını bilemez.

`start()`'ın bu yola düşmesi **ölü kod**: `setup_devices` yalnız iki `return`
içeriyor ve ikisi de `true`:

```
$ awk 'NR>=832 && NR<=978 {printf "%d:%s\n", NR, $0}' daemon/daemon.cpp | grep -n 'return'
12:843:        return true;
146:977:    return true;
$ awk 'NR>=832 && NR<=978 && /return false/' daemon/daemon.cpp | wc -l
0
```

`start():519` ve `apply_new_config():1448` `if (!setup_devices())` diye kontrol
ediyor — **bu dal asla alınamaz**, `bool` dönüş tipi yanıltıcı. Bu bir hata
*raporlama* kusuru (HIGH değil) ama "sessiz yeşil" sınıfında: imza bir hata
sözü veriyor, gövde hiçbir hata üretmiyor. Aynı desen `reload():585` için de
geçerli (`return true` sabit).

---

**L08-07 | `daemon/daemon.cpp:940`, `:1360`, `:1604`, `:1099` | MED |
`apply_profile` "her cihaz için" çağrılmıyor: `find_profile()` nullptr döndüğünde
`if (prof)` dalı sessizce atlanıyor ve cihaz **varsayılan profille** çalışmaya
devam ediyor.**

Dört çağrı yeri de aynı kalıbı paylaşıyor: `:940` `if (prof) apply_profile(dev, *prof);`
— `prof` null ise `apply_profile` **hiç çağrılmıyor**, ama cihaz zaten
`EVIOCGRAB`'lanmış, uinput'u yaratılmış, epoll'a eklenmiş ve `devices_`'e
pushlanmış (`:956-962`). `mouse_device` varsayılanları (`daemon.hpp:47-51`:
`dpi 800`, `poll_rate 1000`, `dpi_factor = NORMALIZED_DPI/800`) ve
`modifier_settings settings;` default-constructed halde kullanılır.

`find_profile` (`:1042-1058`) nullptr'ı **yalnız `config_.profiles` boşsa**
döndürür. Bu, el-de-göbe config'te **mümkün** — projelerin gerçek
`load_config`/`app_config_from_json`'ı ile ölçüldü
(`/tmp/opencode/l08/emptyprof.cpp`, `emptyload.cpp`):

```
cfg_empty.json  {"profiles":[]}            -> profiles=0  find_profile() => NULLPTR (apply_profile NEVER CALLED)
cfg_nokey.json  {"active_profile":...}     -> profiles=0  find_profile() => NULLPTR (apply_profile NEVER CALLED)
cfg_null.json   {"profiles":null,...}      -> profiles=0  find_profile() => NULLPTR (apply_profile NEVER CALLED)
profiles: {} (wrong type)                  -> THROWS: config field 'profiles' must be an array, got object
```

Kullanıcıya ne söyleniyor: `:845` "Found N physical mouse device(s)" + `:828`
"Created virtual device: ..." — **yani fare açıldı ve hızlandırıldı sanılır**,
ama hiçbir profil uygulanmadı. `apply_profile` içindeki "Live-updated profile
for: ..." logu (`:1362`) yalnız `apply_new_config` yolunda basılır; setup/hotplug
yolunda **karşılığı yok**. `start()`'ın `catch` bloğu (`:472-481`) boş profille
`"default"` profili yaratıp `active_profile="default"` atar, yani **diskte bozuk
config varsa** bu yol tetiklenir.

---

**L08-08 | `daemon/daemon.cpp:681-694` | MED (tamamlayıcı, savunma derinliği eksik) |
Kaydetme başarısız olursa config **yarı uygulanmış** kalıyor: dosya eski, bellekte
yeni `push_cfg_` yazılmıyor ama kullanıcı "push başarılı" almış.**

`save_worker` (`:681-694`): `save_config` throw ederse `push_cfg_pending_`
**set edilmez** (`:687` atlanır) — yani config **uygulanmaz**. Bu doğru ve
`run_loop:2013-2017`'nin `catch`'i de aynı sözü veriyor ("keeping current").
Ancak `push_config` daha `:656`'da `true` dönmüş ve log `"Config push queued for
save"` basmıştır. İstemci (`rawaccel-cli`/GUI) `true` gördüğü için başarı
bildirir. Dosya eski kalır, bellek eski kalır — **tutarlı**, ama kullanıcı
bildirimi yanlış. Bu L08-01'den farklıdır (orada bellek de yanlış yöne gidiyor);
burada yalnız **sessiz**dir. `:691` log'u `verbose_only=false` olduğu için
görünür — bu yüzden HIGH değil, MED.

---

**L08-09 | `daemon/daemon.cpp:1306-1329` `release_device` | LOW |
`epoll_ctl(EPOLL_CTL_DEL)` dönüş değeri kontrol ediliyor ama `epoll_fd_ >= 0`
guard'ı yok — `teardown_devices():992` bu guard'ı koyuyor, `release_device` koymuyor.**

`release_device` `:1313` — `teardown_devices` `:992`'nin aksine
`epoll_fd_ >= 0 && dev.fd_in >= 0` ön kontrolü yok. `release_device` yalnız
`apply_new_config:1357` ve `apply_active_app:1096`'dan, her ikisi de daemon
çalışırken çağrılıyor, dolayısıyla `epoll_fd_` geçerli. **Canlı hata değil**,
`teardown` ile `release` arasındaki asimetri. Düzeltme gerekmez, kayıt altına
aldım çünkü `release_device`'ın gövdesi `teardown`'ın kopyası gibi yazılmış ve
bekleyen eşleşme farkı var.

---

KAPI     : **Yok — bu bir okuma + gözlem denetimiydi.** Build/test çalıştırılmadı
           (kaynak değiştirilmedi; denetim turunda çalıştırmak kanıt değil üretirdi).
           Bunun yerine **çalıştırılan ölçümler** (hepsi `/tmp/opencode/l08/`, hepsi rc=0):

           | # | komut | ÜRETİLEN SAYI |
           |---|---|---|
           | 1 | `g++ probe.cpp && ./probe` (sysfs okuyucuları × 10 senaryo) | 10 satır; **hata durumunda `-1`, 3/3 yol** |
           | 2 | `g++ uniq.cpp && for f in /dev/input/event*; do ./uniq $f; done` | 8 cihaz; **2'si çakışan `usb:0002:0013:`** |
           | 3 | `g++ isphys.cpp -levdev && ./isphys /dev/input/event*` | 8 cihaz; **2'si `is_physical_mouse=TRUE`** |
           | 4 | `g++ rsi.cpp && ./rsi /dev/input/by-id /dev/input/event{0..7}` | 8 çağrı; **1'i by-id'ye çözüldü** |
           | 5 | `g++ emptyprof.cpp src/config.cpp && ./emptyprof` | 6 senaryo; **3'ünde `find_profile()==NULLPTR`** |
           | 6 | `g++ emptyload.cpp src/config.cpp && ./emptyload` | 3 config; **3/3 `profiles=0`** |
           | 7 | `g++ pushguard.cpp src/config.cpp && ./pushguard` | **1/1 sessiz yeşil yeniden üretildi** |
           | 8 | `g++ hp3.cpp && ./hp3` | 3 cihaz; **1'i sessizce atlandı** |
           | 9 | canlı daemon `status` → `/run/rawaccel.sock` | 2 cihaz; `detected_polling_rate:0` ×2 |
           | 10 | mutasyon (kopya): `:843 return true` → `return false` | `return false` sayısı 0→1; **`:519`/`:1448` dalının erişilebilirliği kanıtlandı** |
           | 11 | `git status --porcelain` + `git diff --stat` (kaynak dizinleri) | **boş = çalışma ağacı değiştirilmedi** |

           ⛔ Kendi raporumun yanlış sayılmaması için (`AGENTS.md` ölçüm tuzağı):
           tüm `grep`'ler `--include=*.cpp --include=*.hpp` ile kapsandı ve
           `.aihaberlesme/` dışlandı; `olcum/aj5/shadow/**` (satın alınmış kopya)
           `device_id`/`apply_profile` taramalarından ayrı tutuldu.

KAPSANMAYAN: 1988+ satırlar (başka ajan) — `run_loop`, `process_device`,
           `flush_motion`, `status_json`, `drain_hidpp_writes`'ın çağrı yerleri.
           L08-01'in *tüketici* kısmı (`:2001-2018` `push_cfg_` tüketimi) bu
           ajanın alanı; bulgunun **üretici** kısmı (`:611-661`) benim.
           Ayrıca kapsam dışı: `src/config.cpp` (empty-profiles ölçümünde
           **tüketici** olarak kullanıldı, hükmü verilmedi), `include/config.hpp`,
           `cli/main.cpp:2165-2200` (tüketici), `gui/devices.inl` (L08-05'te
           karşılaştırma için okundu, kararı daemon'da).

TEMSIL SINIRI: ⛔ **`/dev/uinput` ve root yok (`id -u` = 1000).** `setup_devices`,
           `open_input_device`, `create_virtual_device`, `do_hotplug_scan`,
           `apply_profile`, `release_device` **çalıştırılmadı** — yalnız okundu ve
           mantıkları ayrı programlarda birebir kopyalanarak test edildi. Bu
           yüzden L08-01 (pushguard) **daemon sürecinde değil, aynı karar
           algoritmasının gerçek serializer'larla bağımsız kopyasında** üretildi;
           canlı daemon üzerinde IPC ile yeniden üretilmedi. L08-05/L08-06'nın
           "ikinci fare atlandı" sonucu **ioctl ölçümü + kapı kopyası** ile
           çıkarıldı, canlı hotplug gözlemiyle doğrulanmadı. ⛔ **Logitech HID++
           donanımı yok** — `find_hidpp_transport`, `apply_profile`'ın HID++
           yarısı ve `hw_sync` dedup'ı ölçülmedi. ⛔ `detect_polling_rate`in
           **başarılı** sysfs okuması bu hostta ölçülemedi (4/4 node ABSENT) —
           yalnız *başarısızlık* yolu ölçüldü; `bInterval` yorum mantığı
           (`:250-278`) ölçülmedi. ⛔ `polling_rate` sysfs node'unu üreten HID
           sürücüsü yok, dolayısıyla `:227-230` "doğrudan polling_rate" dalı
           gerçek veriyle sınanamadı. ⛔ `now_ms()`/`CLOCK_MONOTONIC_RAW`
             yalnız okundu, saat kayması ölçülmedi. ⛔ L08-01'in gerçek zaman
           penceresinin genişliği (save_worker nap'ı + epoll timeout) **süre olarak
           ölçülmedi**, yalnız koddan türetildi.
### L13 | alt-ajan (subagent) | 2026-10-01

```
KAPSAM   : cli/main.cpp  YALNIZCA 1375-2264  (cmd_export, cmd_diff, cmd_import,
           cmd_reload, cmd_rename, cmd_stop, daemon_running, g_monitor_stop +
           monitor_sigint_handler, monitor_monotonic_ms, monitor_time_stamp,
           cmd_monitor, cmd_status_json, cmd_status)
           REFERANS (sadece okuma): cli/main.cpp 1-1374 ve 2265+ (başka ajanlar),
           src/config.cpp, include/config.hpp, include/rawaccel-base.hpp,
           tests/run_cli_sanitized.sh
⛔ HİÇBİR ÇALIŞMA AĞACI DOSYASI DEĞİŞTİRİLMEDİ. git checkout/stash/reset
           ÇALIŞTIRILMADI. Tüm ölçüm /tmp/opencode/l13v üzerinde; ölçüm için
           kopyalanan TEK dosya /tmp/opencode/l13v/froz.c (kendi harness'im).
⛔ SİSTEM DAEMONUNA DOKUNULMADI: /usr/bin/rawaccel-daemon (PID 718) hiçbir
           ölçümde sinyal almadı — tüm reload/stop denemeleri `unshare -rm`
           + boş /run mount namespace'i içinde, $XDG_RUNTIME_DIR kendi
           dizinimizde olduğu için çalıştı.
```

---

## AJ1'İN YENİDEN ÖLÇEBİLECEĞİ HARNESS (tek blok, ~90 s)

```bash
W=/tmp/opencode/l13v; rm -rf $W; mkdir -p $W/bin $W/ra $W/xrun
cat > $W/iso.sh <<'EOF'
#!/bin/bash
mount -t tmpfs none /run 2>/dev/null || { echo MOUNT_FAILED; exit 99; }
exec "$@"
EOF
chmod +x $W/iso.sh
# /proc kimliğine rağmen SIGTERM/SIGHUP'i YUTAN "donmuş daemon"
cat > $W/froz.c <<'EOF'
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
static void h(int s){(void)s;}
int main(void){struct sigaction a;a.sa_handler=h;sigemptyset(&a.sa_mask);
 sigaction(SIGTERM,&a,0);sigaction(SIGHUP,&a,0);
 printf("%d\n",(int)getpid());fflush(stdout);for(;;)pause();return 0;}
EOF
gcc -O0 -o $W/bin/rawaccel-daemon $W/froz.c
g++ -std=c++20 -O1 -I<repo>/include -o $W/rawaccel-cli \
  <repo>/cli/main.cpp <repo>/src/config.cpp \
  <repo>/src/logitech_hidpp.cpp <repo>/src/logitech_receiver.cpp -lpthread
nohup $W/bin/rawaccel-daemon > $W/froz.pid 2>&1 & sleep 1
echo $(cat $W/froz.pid) > $W/xrun/rawaccel.pid
export XDG_RUNTIME_DIR=$W/xrun
$W/rawaccel-cli -c $W/ra/c.json --no-daemon create base
#   sonra: unshare -rm $W/iso.sh $W/rawaccel-cli -c $W/ra/c.json stop
#         unshare -rm $W/iso.sh $W/rawaccel-cli -c $W/ra/c.json reload
#         unshare -rm $W/iso.sh $W/rawaccel-cli -c $W/ra/c.json --json status
```

---

## BULGULAR

### L13-01 | cli/main.cpp:1862 | **CRIT** | `stop` "Daemon stopped." yazıp rc=0 veriyor, daemon YAŞIYOR

`cmd_stop()` → `send_signal_to_daemon(SIGTERM)` → `kill(pid,sig)==0` anında
`signal_result::sent` (`:227`), yani **yalnızca "sinyal teslim edildi"**.
Doğrulama, bekleme, yeniden kontrol YOK. Donmuş daemon'a karşı:

```
$ unshare -rm $W/iso.sh $W/rawaccel-cli -c $W/ra/c.json stop ; echo rc=$?
Daemon stopped.
rc=0 elapsed=0.007s alive after: YES          <-- daemon hâlâ /proc/$FP altında
```

Neden sessiz yeşil: kullanıcı `rawaccel-cli stop && systemctl is-active rawaccel`
gibi bir akışta "durdu" sanır. Kimlik kontrolü (`pid_is_rawaccel_daemon`, exe
basename + comm) doğru çalışıyor — sorun kimlik değil, **eylem sonrası
teyit yok**. `cmd_reload` (`:1809`) aynı kalıbı paylaşıyor ama orada bir
SIGHUP teslimi daha kabul edilebilir; SIGTERM'de "durdu" denecek bir fiil
kesinlikle yanlıştır.

Aynı olgu ikinci derlemede de tekrarladı (önceki koşuda elapsed=0.012s/0.016s).

---

### L13-02 | cli/main.cpp:1809 | **HIGH** | `reload` "Daemon reloaded." + rc=0, daemon hiçbir şey yapmadı

`daemon_reload_via_any_path()` (`:316-320`) IPC "reload" cevabında
`"ok":true` arar; bulamazsa SIGHUP atar; `kill()==0` ise `sent` → "Daemon
reloaded." + rc=0. Donmuş daemon (SIGHUP'i yutan) + **ortada hiç soket yok**:

```
$ unshare -rm $W/iso.sh $W/rawaccel-cli -c $W/ra/c.json reload ; echo rc=$?
Daemon reloaded.
rc=0 elapsed=0.007s alive after: YES
```

Ek ölçüm — **cevap süresi ayırt edilemiyor**: soket `accept` edip hiç
cevap vermediğinde (blackhole) aynı komut **2.076 s** sürüp **yine** "Daemon
reloaded." rc=0 veriyor. Yani "daemon 2 s düşündü" ile "daemon hiç duymadı"
çıktıdan ve rc'den **ayırt edilemiyor**; tek fark 7 ms vs 2.076 s.

ZAMAN AŞIMI **VAR** (`:259-261` `SO_RCVTIMEO/SO_SNDTIMEO = 2 s`) — iki
socket adayının ikisi de `accept` edip takılırsa ölçülen en kötü süre
**2.065 s** (ikinci adaya hiç gidilmiyor: `daemon_ipc_send` bağlanan ilk
adayda `return` ediyor, `:300`). Yani **asılı kalma yok**; kusur "takılma"
değil, **"doğrulanmamış başarı"**.

---

### L13-03 | cli/main.cpp:2083-2102 | **HIGH** | `status --json` donmuş/erişilemez daemon'da rc=0 + `"daemon":"running"`, `devices` anahtarı hiç yok

`daemon_running()` **sadece PID dosyasına** bakar (`:1869-1884`). IPC sorgusu
başarısız olunca `out["device_error"]` yazılır ama **rc hâlâ 0** (`:2100-2102`
yalnızca `config_ok` ve `running`'e bakar). Ölçüm — PID dosyası canlı,
ortada hiç soket yok:

```
$ unshare -rm $W/iso.sh $W/rawaccel-cli -c $W/ra/c.json --json status ; echo RC=$?
{
  "active_profile": "default",
  "config": "...",
  "daemon": "running",                      <-- YANLIŞ: daemon konuşmuyor
  "device_error": "[json.exception.parse_error.101] ... empty input",
  "profiles": [ ... ],
  "use_raw_input": true
}                                            <-- "devices" ANAHTARI HİÇ YOK
RC=0
```

İnsan çıktısı da aynı: `Daemon: running` + stdout'ta device bloğu yok,
tek uyarı stderr'de, `RC=0`.

Bu, görevin "⭐ 4" maddesinin tam olarak tarif ettiği hata sınıfı: **bir script
`status --json | jq -r .daemon` çalıştırıp "çalışıyor" görüyor, oysa daemon
ölü/donmuş.** `devices` anahtarının **tamamen kaybolması** ayrıca
ayırt edilemez: "daemon hiç cihaz yakalamıyor" ile "daemon hiç cevap vermiyor"
çıktıda aynı görünür (birinde `"devices":[]` olur, diğerinde hiç olmaz —
`jq '.devices[]'` ikisinde de hata verir ama farklı).

rc=2 yalnızca PID dosyası **yoksa/ölü/yalan** olduğunda dönüyor — o savunma
ölçüldü ve **çalışıyor** (aşağıda L13-R6).

---

### L13-04 | cli/main.cpp:2045-2103 | **HIGH** | `status --json` içinde **hiçbir zaman damgası yok** — tazeliğin hiçbir göstergesi yok

`cmd_status_json` yazdığı tüm anahtarlar (grep ile çıkarıldı):
`out["daemon"]`, `out["config"]`, `out["profiles"]`, `out["active_profile"]`,
`out["use_raw_input"]`, `out["devices"]`, `out["device_error"]`, `out["config_error"]`.
**Pozitif kontrol** aynı desen dosyanın başka yerinde de tarandı — bulgu
"0 eşleşme" değil, anahtar listesinin tamamı bu.

Daemon'ın kendi `status` cevabında da üst düzey zaman damgası yok; tek
zaman bilgisi cihaz başına `telem_wall_ms` (daemon/daemon.cpp:3197) ve o
**raw 1:1 passthrough'ta hiç yazılmıyor** (AGENTS.md telemetry bölümü:
"`telem_*` previous değerlerini korur, `telem_ok=false`"). Yani taze
olmayan/eksik telemetri durumunda `status --json` çıktısında **hiçbir
sayıdan** "bu veri ne zaman ölçüldü" sorusu cevaplanamıyor. `cmd_monitor`
buna karşı `telem_wall_ms > 2000` ile `[stale]` işareti basıyor (`:1984`) —
`status` bu işareti **hiç taşımıyor**.

---

### L13-05 | cli/main.cpp:1391-1427 | **HIGH** | `diff` iki **tamamen farklı** tam-config export'unu "no differences" diyor, rc=0

`cmd_diff` kendi yorumunu şöyle koyuyor (`:1401-1402`): *"Not a profile name —
try it as a JSON file (**the `export` format**)"*. Ama `export` **iki** format
üretiyor: `export <ad>` → tek profil nesnesi; `export` (adsız) → `{"profiles":[...]}`
wrapper'ı. `resolve()` wrapper'ı `profile_from_json`e veriyor
(`device_profile_from_json`, config.cpp:645) — wrapper'da üst düzey `name`/`profile`
yok, dolayısıyla **her iki taraf da boş default profile'a düşüyor**:

```
-- farklı iki tam config --
wrapA = {version 1.2.5, active_profile "pA", use_raw_input TRUE,
         profiles:[{name:"pA",dpi 800}, {name:"pB",device_id:"usb-x",dpi 800}]}
wrapB = {version 1.2.5, active_profile "pZZZ", use_raw_input FALSE,
         profiles:[{name:"totallyDifferent",dpi 800}, {name:"other",dpi 16000}]}

$ rawaccel-cli -c CFG diff wrapA.json wrapB.json ; echo RC=$?
diff '' vs ''
no differences
RC=0
```

`active_profile` farklı, `use_raw_input` farklı, 4 profil adı farklı, DPI
farklı → **"no differences", rc=0**. Bu tam olarak görevin ⭐3'te tarif
ettiği "fark yok ama aslında fark var" sınıfı, ve **kullanım dokümanı bu
formayı meşru bir girdi olarak tanımlıyor**. Satır-içi sat-stream formu ise
düzgün hata veriyor (`parse error at line 2` → rc=1) — yani **sessiz olan
tam olarak wrapper formu**.

Mekanizma: `cli/main.cpp:1420-1425` `profile_from_json(content)` → başarısız
olmadığı için `catch` çalışmıyor. Kırılma `device_profile_from_json`'ın
`if (j.contains("name"))` / `if (j.contains("profile"))` korumalarında
(config.cpp:652, :664) — sessiz default.

---

### L13-06 | cli/main.cpp:1559,1561 + 1440 | **HIGH** | `domain_weights`/`range_weights` **%.10g metin** olarak karşılaştırılıyor → 1e-4'lük gerçek fark görünmez

`cmp_args` içindeki tüm doubles `dbl()` ile **ham double** + 1e-9 epsilon
ile karşılaştırılır. Ama `domain_weights` ve `range_weights` **bunun dışında**:
`dnum()` (`%.10g`, `:1440`) ile **string'e** çevrilip `sval()` (string eşitliği,
`:1458`) ile karşılaştırılıyor. `%.10g` 10 anlamlı basamak korur → |v| ≥ 1e5
olduğunda çözünürlük 1e-5 olur, yani dokümanın vaat ettiği 1e-9 epsilon
**bu iki alanda uygulanmıyor**:

```
-- domain_weights.x  1000000.0  vs  1000000.0001   (Δ = 1e-4  > 1e-9) --
$ rawaccel-cli -c CFG diff dwA.json dwB.json ; echo RC=$?
diff 'same' vs 'same'
no differences
RC=0

-- negatif kontrol: aynı alanda görünür fark --
range_weights.y  2000.0 vs 2000.25   (Δ = 0.25) →
  range_weights             '1,2000'  vs  '1,2000.25'
  1 difference(s)                      RC=1
-- meşru gürültü (Δ = 1e-10 < epsilon) --
domain_weights.x 3.0000000001 vs 3.0000000002 → no differences, RC=0  ✓ doğru
```

`domain_weights`/`range_weights` P86 ile 1e6'ya kadar değerlendirilebilir
alanlar (config.hpp yorumu) — yani 1e5..1e6 bandı tam olarak kör nokta.

---

### L13-07 | cli/main.cpp:1761-1776 | **HIGH** | `import` `use_raw_input`'ı **sessizce kapatıyor** → daemon hiç cihaz yakalamıyor, rc=0, uyarı yok

`{"profiles":[...]}` wrapper'ından `active_profile`/`use_raw_input`/`version`
alınıyor (`:1761-1776`). Tip denetimi var ama **değer denetimi yok**: iyi
biçimli `false` sessizce uygulanıyor; uyarı yalnızca *yanlış tipte* olduğunda
basılıyor (`:1772`). Ölçüm:

```
BEFORE use_raw_input = True   active = default
$ printf '%s' '{"version":"1.2.5","use_raw_input":false,"active_profile":"WRONG",
      "profiles":[{"name":"imported","device_id":"","disable":false,"dpi":800,
      "polling_rate":1000,"profile":{}}]}' > w.json
$ rawaccel-cli -c CFG import w.json ; echo rc=$?
Imported profile: imported
Warning: active_profile 'WRONG' is not among the import result — falling back to 'default'.
Config applied to the daemon.
rc=0
AFTER  use_raw_input = False  active = default  profiles=['default','base','imported']

$ rawaccel-cli -c CFG status | head -4
Daemon:  running
Config:  ...
Active:  default
Raw-input: off (use_raw_input=false — daemon grabs NO devices; ...)
```

Kullanıcı **tek bir profil** import etti ve RawAccel'in fareyi yakalamayı
tamamen bıraktı; çıktıda bunu söyleyen **hiçbir satır yok** (uyarı yalnızca
`active_profile` için). Üstelik `:1803` `daemon_apply_if_enabled` ile bu
config **canlı daemon'a push ediliyor** (ölçümde "Config applied to the
daemon."). `--help` bu komutu "Import profile from JSON file" diye tanımlıyor
(`:2812`); global mod değiştirmesi hiçbir yerde belgelenmiyor.

---

### L13-08 | config.cpp:110,179 (okuma) → cli/main.cpp:1661 | **MED** | `mode`/`cap_mode` **string değilse** sessizce default'a düşüyor; string'se hata veriyor

`accel_args_from_json` yalnızca `j["mode"].is_string()` ise doğruluyor
(config.cpp:110) ve bilinmeyen string'i **reddediyor** (`:123-124`).
`cap_mode` için de aynı (`:179`, `:185-186`). Yani **tip denetimi doğrulamayı
devre dışı bırakıyor**:

```
$ printf '%s' '{"name":"numMode","profile":{"accel_x":{"mode":42}}}' > m.json
$ rawaccel-cli -c CFG import m.json ; echo rc=$?
Imported profile: numMode
rc=0
# diskte saklanan:  "mode": "noaccel"     <-- ivme YOK, kullanıcı "yüklendi" sanıyor

$ printf '%s' '{"name":"numCap","profile":{"accel_x":{"mode":"classic","cap_mode":7,"cap":[10,1]}}}' > c.json
Imported profile: numCap ; rc=0
# saklanan: "cap_mode": "out"  (io/in/out yönlendirmesi sessizce değişti)

-- negatif kontrol: aynı alanda STRING hatası --
{"name":"evilB","profile":{"accel_x":{"mode":"NOT_A_MODE_AT_ALL"}}}
  → rc=1  "Invalid profile JSON: unknown accel mode: 'NOT_A_MODE_AT_ALL'"
```

config.cpp:181-186'daki yorum ("bilinmeyen/yanlış yazılmış string sessizce
`cap_mode::out`'a düşmemeli") tip denetimini atlamıyor. `cli/main.cpp:1661`
`profile_from_json`'ı çağırıyor, yani L13-08 import'un güvenlik sınırındaki
gerçek boşluk. (Aynı desen `disable`/`dpi`/`polling_rate` için de ölçüldü:
`{"disable":"yes","dpi":"fast","polling_rate":[1,2,3]}` → rc=0, üçü de
default; `{"name":12345,...}` → isim boş kalıp `:1716`'da reddediliyor.)

---

### L13-09 | cli/main.cpp:1398-1399 | **MED** | `resolve()` ilk eşleşmeyi döndürüyor → aynı addaki iki farklı profil "aynı" görünüyor

```
# config'te AYNI ADDA ve FARKLI değerli iki profil (elle düzenlenmiş dosya;
# import yolu bunu zaten reddediyor — :1728)
profiles = [('default',800), ('same',800,'noaccel'), ('same',16000,'classic')]

$ rawaccel-cli -c DUP diff same same ; echo RC=$?
diff 'same' vs 'same'
note: both sides share the name 'same' but the values differ (one side is a file).
no differences
RC=0

-- pozitif kontrol: aynı iki profilü dosyaya export edip diff edince --
  dpi                       800  vs  16000
  accel_x.mode              noaccel  vs  classic
  accel_x.acceleration      0.005  vs  0.9
  3 difference(s)            RC=1
```

`load_config` yinelenen adları reddetmiyor (config.cpp:767-778 yalnızca
"nesne değilse" reddiyor), `import` reddediyor, `diff` reddetmiyor.
Sonuç: kullanıcı "fark yok" diyor, dosyalar 3 fark gösteriyor.

---

### L13-10 | cli/main.cpp:1433-1435 | **MED** | `note:` satırı kendi kendine diff'te yanlış bilgi veriyor

Koşul `A.name == B.name && (A.name == a || A.name == b)`. Bir profil
**kendisiyle** karşılaştırıldığında da `A.name == a` doğru olduğu için not
basılıyor:

```
$ rawaccel-cli -c CFG diff p1 p1 ; echo RC=$?
diff 'p1' vs 'p1'
note: both sides share the name 'p1' but the values differ (one side is a file).
no differences
RC=0
```

"one side is a file" **yanlış** (iki taraf da config'ten) ve "the values
differ" da yanlış (0 fark). L13-09'daki gerçek belirsizlik durumunda da aynı
not basıldığı için not hiçbir ayrım sağlamıyor.

---

### L13-11 | cli/main.cpp:1646 | **MED** | Bozuk JSON'da **sınırsız** ham girdi stderr'e yazılıyor; kontrol karakterleri kaçıyor

Satır-stream dalındaki hata mesajı satırın **tamamını** basıyor:

```
# 900 051 baytlık TEK satır bozuk JSON
$ stat -c%s huge_bad.err → 900089     (dosyanın %100'ü)
$ head -c 90 huge_bad.err
Invalid profile JSON line in stream: {"name":"x","profile":{"accel_x":{"scale":1111111111111...

# kontrol karakteri sızıntısı (terminal kaçış enjeksiyonu)
$ printf '{"name":"a\x1b[31mRED\x1b[0m...','...,"profile":\n' > ansi.json
$ rawaccel-cli -c CFG import ansi.json ; echo rc=$?
rc=1
$ xxd ansi.err | head -2
00000000: 496e 7661 6c69 6420 7072 6f66 696c 6520  J  n  v  a  l  i  d     p  r  o  f  i  l  e
00000020: 4a53 4f4e ...  5b33 316d 5245 44 1b5b 306d  ...  [  3  1  m  R  E  D  ESC [  0  m
→ dosyadan GELEN 0x1b baytı stderr'e **ham** geçti (ölçüldü: 1 adet ESC)
```

Proje aynı sınıfı daemon tarafında kabul etmiş: `--log-format json`
kontrol karakterlerini kaçışlar (AGENTS.md "JSON log escaping", P53) — CLI
bu yolda yapmıyor. `cmd_import` 1 MB'ı reddediyor, ama 1 MB'a kadar olan
her bayt stderr'e yazılabiliyor.

---

### L13-12 | cli/main.cpp:1936-1949, 2042 | **MED** | `monitor`: IPC boş dönerse ve PID dosyası canlıysa **sonsuz sessiz döngü**, Ctrl-C'de rc=0

`resp.empty()` → `usleep(50000)` → `daemon_running()` doğruysa `continue`
(`:1948`) — **ne bir hata ne bir çıktı**. Ardından `status` başlığı bile
basılmıyor (`header_shown` yalnızca başarılı parse'tan sonra set ediliyor,
`:1962-1967`). Ölçüm (kabul edip asla cevap vermeyen soket + yaşayan PID dosyası):

```
$ rawaccel-cli -c CFG monitor 300 > mon.out 2> mon.err &
$ sleep 7 ; kill -INT $!
still running after 7s? YES
RC=0   latency_after_SIGINT=0.052s
--- stdout ---   (BOŞ, 0 bayt — başlık dahil)
--- stderr ---   (BOŞ)
```

Yani "monitor çalışıyor, Ctrl-C ile 0 ile çıktı" ama **tek satır veri yok** —
kullanıcı ekranı boş, script boş dosya alıyor, ikisi de başarı. `cmd_monitor`
`:1911`'de aynı koşulda "Daemon is not running" deyip rc=1 ile çıkıyor;
burada o yol hiç işlemiyor.

---

### L13-13 | cli/main.cpp:1911 vs 1943 | **MED** | `monitor` ters yönde yanlış negatif veriyor: soket canlı, PID dosyası yok → "Daemon is not running"

`daemon_running()` **tek başına** PID dosyasına bakar. Soket cevap veriyor
ama PID dosyası yoksa (tmpfiles temizliği, konteyner, /run sıfırlanması):

```
$ (soket yanıt veriyor, rawaccel.pid YOK)
$ unshare -rm $W/iso.sh rawaccel-cli -c CFG monitor 200
Daemon is not running.  Start it with: sudo systemctl start rawaccel
$ … status  →  Daemon:  stopped   (rc=2)
```

`status`/`monitor` "çalışmıyor" diyor; daemon'in soketi açık, `set_config`
cevap veriyor. L13-03 ile aynı kök: **canlılık için tek ve dolaylı gösterge
(pid dosyası) kullanılıyor**, IPC'nin kendisi ikincil.

---

### L13-14 | cli/main.cpp:1394-1395 (yorum) | **MED** | `cmd_diff`'in "Read-only: never touches the config file" beyanı yanlış

Yorum `:1391-1395`: *"Read-only: never touches the config file, the daemon, or
the LUT data."* Ölçüm — **var olmayan** bir config yolunda iki dosyayı diff etmek
**2679 baytlık bir config dosyası yarattı**:

```
$ rawaccel-cli -c $T/yeni.json diff a.json b.json ; echo rc=$?
  export                       rc=0  config_file_created=YES (2679B)
  diff a.json b.json           rc=1  config_file_created=YES (2679B)   <-- beyana aykırı
  status                       rc=1  config_file_created=no
  validate                     rc=1  config_file_created=no
  monitor 200                  rc=124 config_file_created=no     (dispatch config yüklemesinden önce)
  reload / stop                rc=0  config_file_created=no
```

Mekanizma benim lane'ımın dışında: ana giriş noktası `main()`
(`:3171-3183`) komut yönlendirmesinden **önce** config'i yüklüyor ve yoksa
default config'i **yazıyor**. `monitor`/`reload`/`stop`/`status` yönlendirmesi
`:3125-3141`'de, config yüklemesinden önce olduğu için dokunmuyor — yorum
orada doğru. `diff` (ve `export`) yanlış beyan ediyor.

---

### L13-15 | tests/run_cli_sanitized.sh:129-130 | **MED** | cli/main.cpp'nin **tek** sanitizer kapısı, `cmd_import`'un JSON yolunu hiç çalıştırmıyor

`cli/main.cpp`yi tek başına sanitizer altında çalıştıran yedi kapıdan yalnızca
gate 6. Bu kapının benim lane'ıma dokunan invokasyonları (grep, pozitif kontrol
`create-preset` → 2):

```
122: vaka "rename"            … -c "$CFG" rename p_dup p_ren
124: vaka "export (JSON)"     … -c "$CFG" export p_gaming
128: vaka "diff (iki preset)" … -c "$CFG" diff p_gaming p_apex
129: vaka "import yok dosya"  … -c "$CFG" import /nonexistent-xyz.json
130: vaka "import reddedilen yol" … -c "$CFG" import /etc/shadow
```

komut adı başına **ölçülen** çağrı sayısı:
`export` 4 · `diff` 2 · `import` 2 · `rename` 1 · **`reload` 0 · `stop` 0 ·
`monitor` 0 · `status` 0**

`import`'un iki çağrısı da **JSON okunmadan** önce çıkıyor:
`/nonexistent-xyz.json` → `cmd_import:1591`'de `ifstream` açılamıyor;
`/etc/shadow` → ana girişteki `validate_config_path` (`:3119`) reddediyor
(pozitif kontrol: `Config path '/etc/shadow' does not have a .json extension.`
→ rc=1, `cmd_import`'e hiç girmediği kanıtı). Dolayısıyla
**`cmd_import`'un 215 satırlık (`:1589-1803`) JSON ayrıştırma/doğrulama
bloğu sanitizer altında sıfır kez çalışıyor**; `cmd_status`/`cmd_status_json`/
`cmd_monitor`/`cmd_reload`/`cmd_stop` de sıfır kez. Bu, L13-01..L13-14'ün
çoğunun bir kapıyla yakalanamamasının nedenidir (brifing §4: "yerleşik
kanıtı yıkma").

---

### L13-16 | cli/main.cpp:1969 | **LOW** | TTY temizliği ölçüldü: 2 kontrol dizisi/yenileme, scrollback temizliği yok

Gerçek pty üzerinden (`pty.fork`, Ctrl-C gerçekten `\x03` olarak yazıldı),
300 ms aralık, ~1.9 s:

```
ESC 0x1b : 10        CR 0x0d : 21
'\x1b[2J' : 5        '\x1b[H' : 5        '\x1b[3J' : 0
toplam 1904 bayt → 5 yenileme
21 CR = 5×4 satır + çıkıştaki son std::cout << "\n"  (programın kendi CR'i 0)
```

**Piped modda sıfır**: 1578 bayt, `ESC:0  CR:0  diğer ctrl:{}` — satır-akışı
gerçekten temiz. TTY'de program başına **2 kontrol dizisi** (ESC[2J + ESC[H),
kendi yazdığı CR yok. `\033[3J` (scrollback temizliği) **hiç yok** —
`\033[2J` çoğu terminalde ekranı scrollback'e iter. TEMSIL SINIRI: headless
bir makinede gerçek terminalin scrollback büyümesini **ölçemedim**; ölçülen
sadece bayt seviyesindeki karakter sayılarıdır.

---

### L13-17 | cli/main.cpp:1909 vs 3133 | **LOW** | `cmd_monitor` dokümanı `[20,60000]` diyor, yönlendirici 1'i de kabul ediyor

```
$ rawaccel-cli monitor 0      → rc=1  "Monitor interval must be 1-60000 ms: 0"
$ rawaccel-cli monitor 1      → KABUL (rc=124 = timeout'a kadar koştu)
$ rawaccel-cli monitor 5      → KABUL
$ rawaccel-cli monitor 19     → KABUL
$ rawaccel-cli monitor 20     → KABUL
$ rawaccel-cli monitor 60000  → KABUL
$ rawaccel-cli monitor 60001  → rc=1  reddedildi
$ rawaccel-cli monitor -5     → rc=1  reddedildi
$ rawaccel-cli monitor abc    → rc=1  reddedildi
```

`cmd_monitor`'un `@param` satırı "must be within **[20, 60000]**" diyor,
`:3133` ise `v > 0 && v <= 60000` arıyor. 1 ms = saniyede 1000 IPC sorgusu.

---

### L13-18 | cli/main.cpp:1919 | **LOW** | `sigaction(SIGINT, ...)` geri yüklenmiyor

`sigaction(SIGINT, &sa, nullptr)` — eski `sa` alınmıyor, `cmd_monitor` çıkışında
geri yüklenmiyor. Bugün işlevsel etkisi yok (`monitor` ana girişteki son
komutlardan biri, `:3128-3141` ve config yüklemesinden önce), ama
`SIG_IGN`/parent handler'ı taşınmış bir süreçte kalıcı etkisi olur.
`sa_flags = 0` → `SA_RESTART` yok; bu, `:2038`'deki EINTR döngüsünün
varlık nedeni ve **doğru** bir tercih.

---

## ⛔ REFUTED — görevde ⛔/⭐ işaretli hipotezler ÖLÇÜLDÜ ve DOĞRU ÇIKTI

Bunları raporlamıyorum ama "ölçmedim" deyip geçmiyorum:

**L13-R1 (görev ⭐5: sinyal işleyici async-signal-safe mi, CRIT mi?) — HAYIR, GÜVENLİ.**
`cli/main.cpp:1889` tek satır: `static void monitor_sigint_handler(int) { g_monitor_stop = 1; }`
`g_monitor_stop` `volatile sig_atomic_t` (`:1888`).

```
$ sed -n '1889p' cli/main.cpp | grep -E 'printf|cout|cerr|malloc|new |strdup|std::string|vector|sprintf'
grep_rc=1   (hiç eşleşme)
$ grep -cE 'printf|cout|cerr|malloc|std::string' cli/main.cpp
439          ← POZİTİF KONTROL: desen dosyada 439 yerde tutuyor, sıfır değil
$ grep -n 'sigaction\|signal(' cli/main.cpp
1915:    struct sigaction sa {};
1919:    sigaction(SIGINT, &sa, nullptr);        (dosyada tek signal kurulumu)
```

malloc/printf/throw **yok**. Uçtan uca da doğrulandı: gerçek pty'de Ctrl-C
(`\x03`) → `exitcode: 0`, 52 ms'de çıkış. **Bu bir CRIT değil, görevdeki ⛔
varsayım ölçümle çürütüldü.**

**L13-R2 (görev ⭐7: reload/stop takılıyor mu, zaman aşımı var mı?) — TAKILMIYOR, 2 s sınırı var.**
`:259-261` `SO_RCVTIMEO`/`SO_SNDTIMEO` = 2 s. Ölçülen en kötü süre: **2.065 s**
(iki aday soket de `accept` edip stall etti). İkinci adaya **gidilmiyor**:
`daemon_ipc_send` bağlanan ilk adayda `return` ediyor (`:300`) — kanıt:
`blackhole on .../rawaccel.sock` + `blackhole on /run/rawaccel.sock` loglandı,
`ACCEPTED` yalnızca ilki için çıktı. `cmd_stop` IPC kullanmıyor: `kill` + PID
dosyası = **0.007 s**. Kusur "takılma" değil, **L13-01/02'deki doğrulanmamış başarı**.

**L13-R3 (görev ⭐2: import önce yazıp sonra mı doğruluyor?) — HAYIR, DOĞRULAR ÖNCE, HEPSİ-ARADA.**
Tüm doğrulama `batch` vektörüne toplanır (`:1657-1756`); `safe_save` **bir kez**
ve en sonda çağrılır (`:1802`). Yazma sırası: `save_config`'in kendi atomik
yolu (tmp `O_NOFOLLOW|O_EXCL` → `fsync` → `rename`, config.cpp:867-996).
Altı farklı reddedilme yolunda **md5 değişmedi**:

```
── stream_bad (2. satır yinelenen ad)  rc=1  md5_before=3798865710ea8a88982023c749ddf913
                                        md5_after =3798865710ea8a88982023c749ddf913  UNCHANGED=YES
── wrap_bad (2. giriş geçersiz mode)   rc=1  UNCHANGED=YES
── ovf  (acceleration: 1e400)          rc=1  UNCHANGED=YES
── typ  (acceleration: "fast")         rc=1  UNCHANGED=YES
── nan  (NaN literali)                  rc=1  UNCHANGED=YES
── lut_over (516 eleman / 258 nokta)    rc=1  UNCHANGED=YES
── lut_odd  (515 eleman, tek sayı)      rc=1  UNCHANGED=YES
reddedilen importlardan sonra $W/ra/ içinde 0 adet geçici dosya kaldı.
```

1 MB sınırı **birebir**: 1048576 B kabul (rc=0), 1048577 B red (rc=1).
Bunlar ⛔ CRIT adayı değildi. Kalan import riskleri L13-07/08/11'de.

**L13-R4 (görev ⭐3: diff tüm alanları karşılaştırıyor mu?) — ALAN KAPSAMI TAM.**
`device_profile`(5) + `device_config`(3) + `profile`(13) + `speed_args`(5) +
`accel_args`(20, her eksen) elle sayıldı, `cmd_diff`'in `dbl/bval/sval/ival/
cmp_args` çağrılarıyla eşleşiyor. Karşılaştırılmayan **tek** alan
`profile::name` (`char[257]`) — ve onun **sıfır okuyucusu** var:

```
$ grep -rn "prof\.name|prof->name|\.prof\.name" daemon/ cli/ gui/ src/ include/ --include=*.cpp --include=*.hpp --include=*.inl
(0 satır)
$ grep -rn "dp\.name|\.name ==|->name ==" daemon/*.cpp | head -2
daemon/daemon.cpp:476:            dp.name = "default";
daemon/daemon.cpp:1055:        if (p.name == config_.active_profile) return &p;   ← POZİTİF KONTROL
```

`accel_y` koşullu karşılaştırması (`:1581-1582`) da doğru: iki tarafta da
`accel_y == accel_x` ise atlanacak alan yok. Kapsam eksiği **değil**; kusur
**karşılaştırma yöntemi** (L13-06) ve ** girdi çözümlemesi** (L13-05/09).

**L13-R5 (görev ⭐1: import sayı aralığı doğruluyor mu?) — EVET, `sanitize` ile.**
1e300 ölçeğindeki bir dosya reddedilmiyor ama **güvenli aralığa kırpılıyor**:

```
 girdi: dpi 999999, polling_rate 1, acceleration 1e300, limit 1e300, scale 1e300,
        cap [1e300,1e300], exponent_power 1e300, accel_y.exponent_power -1e300
 diskte: dpi 32000, polling_rate 125, acceleration 20.0, limit 100.0, scale 100.0,
         cap [500.0,100.0], exponent_power 5.0
```

P120-FAZ2 tavanları (config.hpp:18-64) devrede. Profil adı: boş ad reddediliyor
(`:1716`), >256 karakter reddediliyor (`:1723`), config'te var olan ad reddediliyor
(`:1728`), partinin içinde yinelenen ad reddediliyor (`:1736`), profil sayısı
`MAX_PROFILES`'ı aşarsa **kesme değil red** (`:1749`, ölçüldü: "would exceed
the 256-profile maximum", rc=1, profil sayısı 2'de kaldı). `active_profile`
yüklenemeyecek bir wrapper'dan gelirse ilk profile düşüyor (`:1788-1793`,
uyarı basıyor). **Bunların hiçbiri CRIT değil** — L13-07/08 bu boşluğun
gerçek sınırı.

**L13-R6: PID/ek spoof savunması çalışıyor.**
`pid_is_rawaccel_daemon` (`:169-202`) exe basename + comm çapraz kontrolü:

```
pidfile = ölü PID (999999)      → status rc=2 ("stopped")
pidfile = daemon OLMAYAN pid     → status rc=2  (sleep'in PID'i; reddedildi)
pidfile = yaşayan daemon         → status rc=0
```
Ayrıca "socket yanıt veriyor ama PID dosyası yok" → rc=2 (L13-13'ün ters
senaryosu, savunmanın çalıştığını gösteriyor; sorun o senaryonun tersi).

**L13-R7: ASan+UBSan temiz.** `cli/main.cpp` kendi kaynaklarıyla
`-fsanitize=address,undefined` altında yeniden derlendi; 15 invokasyon
(export ×2, import ×5, diff ×4, status ×2, rename ×3, stop, reload, monitor
+ SIGINT) `ASAN_OPTIONS=detect_leaks=1` ile koşuldu:

```
$ grep -cE "ERROR: (Address|Leak)Sanitizer|runtime error|SUMMARY:" asan.log
0
$ grep -cE "ERROR: (Address|Leak)Sanitizer|runtime error" mon_err   (monitor)
0
```

---

## DÖNÜŞ KODU TABLOSU (görev ⭐8) — `cmd_*` : satır → rc → **ne olduğunda yanlış**

| komut | satır | ölçülen rc'ler | "başarılı" dediği ama olmayan durum |
|---|---|---|---|
| `cmd_export` | 1375 | 0 / 1 | `0` + **0 profil varsa 0 bayt** çıktı; olmayan profil → `0`'dan farklı yol yok. Zararsız. |
| `cmd_diff` | 1396 | 0 / 1 | ⭐ **`0` "no differences" iken gerçek fark var**: L13-05 (wrapper), L13-06 (weights), L13-09 (yinelenen ad) |
| `cmd_import` | 1589 | 0 / 1 | ⭐ **`0` iken config global davranışı değişmiş olabilir**: L13-07 (`use_raw_input`→false), L13-08 (mode→noaccel) |
| `cmd_reload` | 1806 | 0 / 1 | ⭐ **`0` "Daemon reloaded." + rc=0, daemon SIGHUP'i yok sayıyordu** (L13-02) |
| `cmd_rename` | 1816 | 0 / 1 | Temiz ölçüldü: boş/yeni-var/256+ ad/aktif profil senkronizasyonu (`:1844`) hepsi doğru |
| `cmd_stop` | 1859 | 0 / 1 | ⭐ **`0` "Daemon stopped." + rc=0, daemon çalışmaya devam ediyor** (L13-01) |
| `cmd_monitor` | 1910 | 0 / 1 | ⭐ **`0` hiç veri basmadan** sonsuz döngüden çıktı (L13-12); ayrıca canlı daemon'a "not running" dedi (L13-13) |
| `cmd_status_json` | 2045 | 0 / 1 / 2 | ⭐ **`0` + `"daemon":"running"` + `devices` anahtarı yok**, daemon konuşmuyor (L13-03); hiç zaman damgası (L13-04) |
| `cmd_status` | 2105 | 0 / 1 / 2 | ⭐ **`0` + `Daemon: running`**, cihaz bloğu yok (L13-03, insan çıktısında stderr'e düşen tek uyarı) |

rc semantiği doğrulandı: `2` = daemon stopped, `1` = config okunamadı
(`config missing` → rc=1 `Config: ... (unreadable or missing)`,
`config corrupt` → rc=1), `0` = ikisi de sağlam. **Yalnızca L13-03'te
"ikisi de sağlam" yanlış hesaplanıyor.**

```
════════ RC TABLOSU v3 (ölçüldü, /run mount-ns ile gizli) ════════
pidfile canlı + socket yanıtlıyor:      rc=0
pidfile canlı + socket YOK:              rc=0      <-- L13-03
pidfile ÖLÜ pid (999999):                rc=2      <-- savunma çalışıyor
pidfile daemon OLMAYAN pid:              rc=2      <-- savunma çalışıyor
config eksik:                            rc=1
config bozuk (xx):                       rc=1
export <olmayan profil>:                 rc=1  "Profile not found: ghost"
monitor (pidfile yok, socket CANLI):     rc=1  "Daemon is not running."  <-- L13-13
```

---

## KAPI

```
$ W=/tmp/opencode/l13v
$ g++ -std=c++20 -O1 -I<repo>/include -o $W/rawaccel-cli \
     <repo>/cli/main.cpp <repo>/src/config.cpp <repo>/src/logitech_hidpp.cpp \
     <repo>/src/logitech_receiver.cpp -lpthread          → rc=0 (0 warning)
$ gcc -O0 -o $W/bin/rawaccel-daemon $W/froz.c            → rc=0
$ g++ -std=c++20 -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
     -I<repo>/include -o $W/rawaccel-cli-asan <aynı 4 TU>  → rc=0
```

**ÜRETİLEN SAYILAR:**

| ölçüm | sayı |
|---|---|
| `cmd_import` reddedilen bozuk JSON varyantı (7): ovf / typ / nan / lut_over / lut_odd / stream_dup / wrap_bad | **7** |
| `cmd_import` sessizce KABUL edilen varyant (5): 1e300 çarpanları / `mode:42` / `cap_mode:7` / yanlış tipler / aşırı `device_id` | **5** |
| md5'in değişmediği reddedilme yolu (ölçülen) | **6** |
| `cmd_diff` "no differences" + rc=0 iken gerçek fark olan senaryo | **3** (wrapper, domain_weights, yinelenen ad) |
| donmuş daemon'a karşı `stop`/`reload` "başarılı" + rc=0 | **2/2** |
| `status`/`status --json` donmuş daemon'da rc=0 + yanlış "running" | **2/2** |
| sanitizer (ASan+UBSan) bulgusu | **0** |
| TTY'de programın yazdığı kontrol dizisi/yenileme | **2** (ESC[2J + ESC[H) |
| piped modda kontrol karakteri | **0** |
| iki soket de stall ederken ölçülen en kötü bekleme | **2.065 s** |
| import 1 MB sınırı (1048576 kabul / 1048577 red) | **birebir** |

---

## KAPSANMAYAN (lane dışı — birinin bakması gerek)

1. **`main()` `:3171-3238` config'i komut yönlendirmesinden ÖNCE yüklüyor ve
   yoksa YAZIYOR**; ayrıca 0 profilli config'i `list`/`export` gibi **salt
   okunur** komutlarla **kendinden onarıyor** (ölçüldü: 0 profilli config'e
   `export` → rc=0 ve "default" profili **yaratılıp diske yazıldı**, 1058 bayt
   çıktı). Bu L13-14'ün mekanizması ve `cmd_diff`/`cmd_export` dokümanlarıyla
   çelişiyor. → 1-1374/2265+ sahibi.
2. **`validate_config_path` / `resolve_config_target`** (`:1-1374`) — `import`
   dosyasının *kendi* yolu policy'den geçmiyor; yalnızca config yolu geçiyor.
   `import /dev/stdin` veya bir FIFO gibi bir yol ölçmedim.
3. **`daemon_ipc_send` 16 MB runaway guard'ı** (`:297`) — 16 MB üstünde
   `resp.clear()` edip sessizce boş dönüyor; bir daemon 16 MB'dan büyük
   geçerli bir `status` cevabı üretirse CLI onu yok sayar. Ölçmedim.
4. **Daemon tarafı** `daemon/daemon.cpp` `status_json` — `telem_wall_ms`'in
   hangi koşullarda yazıldığı (`:3197` çevresi) ve ham 1:1 passthrough'ta
   `telem_ok=false` yolu benim lane'ım dışında; L13-04'ün daemon yarısı oraya
   bakmalı.
5. **`gui/`** tarafında aynı import JSON'unun `check_import_lut_size`
   (config.hpp:193) kopyasını kullanması — aynı sessiz `mode` degrade
   (L13-08) GUI import yolunda da geçerli olabilir, ölçmedim.
6. **`tests/run_cli_sanitized.sh`'in kendi düzeltilmesi** (L13-15) — kapı
   sahibinin işi; benim lane'ımda değil ama L13-01..14'ü yakalayamamasının
   nedeni.

---

## TEMSIL SINIRI

1. **Görev ⭐5'i (sinyal işleyici async-signal-safe mı) statik kanıt + uçtan uca
   çalıştırma ile cevapladım**: `grep_rc=1` + pozitif kontrol 439 + gerçek
   pty'de Ctrl-C → `exitcode 0`. Ancak **`-fsanitize=thread` veya
   `-fsanitize=signal` altında koşmadım** — TSAN/SSAN çalıştırmadım; "async-
   signal-safe" hükmü kaynak denetimi + fonksiyonel test üzerine, dinamik
   sanitizer kanıtı üzerine değil.
2. **PS5.1 / 32-bit yok, yoklamadım.**
3. **Scrollback büyümesi ölçülmedi.** L13-16 yalnızca bayt seviyesinde kontrol
   karakteri saydı (5×ESC[2J, 5×ESC[H, 0×ESC[3J, programın kendi CR'i 0).
   Gerçek bir terminalin `\033[2J` sonrası scrollback'e ne kadar satır
   attığını headless ortamda gözlemleyemedim — bu bir terminal emülatörü
   davranışı, kod kanıtı değil.
4. **Donmuş daemon'um C++ değil, 12 satırlık C bir program** (`froz.c`).
   `/proc/<pid>/exe` basename + `/proc/<pid>/comm` kontrolünü geçtiği ölçüldü
   (`exe=.../bin/rawaccel-daemon`, `comm=rawaccel-daemon`), yani
   `pid_is_rawaccel_daemon` bu yolun gerçekten geçtiğini kanıtlıyor; ama
   **gerçek `rawaccel-daemon`'ın SIGTERM'i yutması** ölçülmedi (o zaman
   `stop`'un "sessiz yeşil" olmasının gerekçesi değişmez: `kill()==0`
   teslimatı doğrulamaz, ama gerçek daemon normalde çıkıyor olabilirdi).
   L13-01 bir **olasılık** değil, "stop komutunun doğrulama adımı yok"nun
   doğrudan kanıtı; donmuş senaryo o eksik adımın sonucunu gösteriyor.
5. **`/run/rawaccel.sock` gerçek daemon'a karşı test edilmedi** — namespace'de
   maskelendi. Gerçek daemon'ın `status`/`set_config` cevap şekli hakkında
   **hiçbir** iddiam yok; tüm IPC ölçümlerim sentetik sunucuya karşı.
6. **İki soket adayının ikisinin de stall ettiği senaryoda** "en kötü süre
   2.065 s" dedim ama bu **tek bir deney** (n=1); birden çok ölçüm/istatistik
   yapmadım, yani "asla 4 s olmaz" değil "ölçülen maksimum 2.065 s ve ikinci
   adaya gidilmediğine dair gözlem"dir.
7. **`cmd_import` 1 MB sınırının bellek tarafı**: sınır kontrolü
   `content.size() <= kImportMax` (`:1598`) döngü koşulunda olduğu için
   tutulan bellek 1 MB + 64 KB'ya çıkabiliyor. Ölçtüm ama **düşük öncelikli**
   bulgu olarak not düştüm, ayrı ID vermedim.
8. **`cmd_rename`**'ı yalnızca 4 senaryo ile taradım (boş ad, var olan ad,
   256+ ad, aktif profil senkronizasyonu). 257 baytlık bir adın `.bak`
   rotasyonuyla etkileşimini ölçmedim.

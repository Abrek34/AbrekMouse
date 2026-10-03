### L15 | alt-ajan (GUI çekirdek denetimi) | 1 Eki 2026

**KAPSAM** (kesin, 4 dosya — başka lane'lerin sahipliği):
- `gui/main.cpp` (313 satır)
- `gui/app_state.hpp` (391 satır)
- `gui/daemon_comm.inl` (637 satır)
- `gui/kwin_focus.inl` (324 satır)

Bunların dışındaki dosyalar **yalnızca okundu** (tüketici tespiti için):
`gui/ui_builder.inl`, `gui/widgets_sync.inl`, `gui/profile_mgr.inl`, `gui/graph.inl`.

**KAPSAM DIŞI BIRAKILAN YER** (bilinçli, lane sınırı):
`hidpp_panel.inl` içindeki HID++ donanım yazma yolları — bunlar config'i değil
fiziksel cihazı değiştirir, ayrı lane'in konusu.

---

## BULGULAR

### L15-C1 | `gui/daemon_comm.inl:68` | **CRIT** — KWin uyarı barı ÖLÜ KOD

**Kanıt 1 — satırın kendisi (kaynaktan birebir):**
```
$ awk 'NR==68' gui/daemon_comm.inl
            in_libinput = (strncmp(line, "[Libinput]", 10) == 0 && line[10] == ']');
```

**Kanıt 2 — koşul neden hiçbir zaman sağlanamaz (bağımsız C ölçümü):**
`"[Libinput]"` tam olarak 10 bayttır; `strncmp(...,10)` zaten 10. baytı (kapanış
`]`'i) karşılaştırır. Karşılaştırma bittikten sonra `line[10]` **NUL
terminatördür**, `]` değildir. Kapanışı aramak için 11. bayta bakılmış ve 11.
baytta `]` bulunabilecek tek bir girdi vardır: `[Libinput]]` — hiçbir gerçek
kwinrc böyle yazılmaz.
```
$ python3 recheck.py
satir='[Libinput]'                            strncmp(...,10)==0 -> True   line[10]=b'\x00'  kosul -> False
satir='[Libinput][3][1133][50498][Logitec'    strncmp(...,10)==0 -> True   line[10]=b'['     kosul -> False
satir='[LibinputSomething]'                   strncmp(...,10)==0 -> False  line[10]=b'o'     kosul -> False
SONUC: hicbir gercek kwinrc satirinda kosul True olamaz -> kde_libinput_accel_state() daima -1 doner.
```

**Kanıt 3 — mutasyon kanıtı (brifing §4.1).** Fonksiyonun iki sürümü tek
programda karşılaştırıldı; `fn_real.inc` = kaynaktan `sed` ile çıkarılmış
**değiştirilmemiş** kopya, `fn_mut2.inc` = yalnızca :68 koşulu düzeltilmiş kopya:
```
  #  GERCEK   MUTASYONLU  girdi (0=flat/OK  1=UYARI  -1=bilinmiyor)
  0  -1       0           [Libinput]\nPointerAccelerationProfile=1\nPointerAcceleration=0
  1  -1       1           [Libinput]\nPointerAccelerationProfile=2
  2  -1       0           [Libinput]\nPointerAccelerationProfile=1e26
  3  -1       -1          [Libinput]\nPointerAcceleration=1e1000
  4  -1       1           [Libinput]\nPointerAccelerationProfile=999999999999...
  8  -1       0           [Libinput]\nPointerAccelerationProfile=+1
```
Gerçek sürüm **her girdide** `-1`; tek satırlık düzeltme ile 0/1 dönüyor.
9/9 vakada `-1`.

**Kanıt 4 — GERÇEK GUI, DAEMON DURUMU (ölçülen çıktı).** `/tmp`'deki kopyaya
`main.cpp`ye ölçüm noktası eklenip derlendi, X11'de çalıştırıldı:
```
  kde_warn_bar      = gizli
  kde_libinput_state= -1
```
Bu, gerçek kwinrc'nin bu makinede **doğru ayar içermesine rağmen**
(`~/.config/kwinrc` → `PointerAccelerationProfile=1`) fonksiyonun yine `-1`
döndüğünü doğrular.

**Tüketici zinciri (sessiz-yeşil):**
- `daemon_comm.inl:35` `kde_libinput_accel_state()` → daima `-1`
- `ui_builder.inl:1601` `bool bad = (state == 1);` → daima `false`
- `ui_builder.inl:1603` `gtk_widget_set_visible(S->kde_warn_bar, bad ? TRUE : FALSE)` → **daima gizli**
- `app_state.hpp:156` `kde_accel_ok` → daima `true` (başka yerde okunmuyor:
  `grep -rn "kde_accel_ok" gui/` → yalnızca tanım + 2 atama)

**Bu tam olarak brifing §3'teki "bir `if` her zaman false ama koruma mevcut
deniyor" sınıfı.** Satır 65-67'deki yorum, `strncmp(...,10)`'un *zaten* prefix
doğruladığını doğru söylüyor — ama eklenen `&& line[10] == ']'` kontrolü
gereksiz değil, **kodu tamamen öldürücü**. Yorum, kontrolün amacını doğru
tanımlıyor ve kontrol yanlış.

**Kullanıcı etkisi:** KDE Plasma + adaptive (non-flat) libinput hızlandırma
altında çalışan kullanıcı, "çift hızlandırma" uyarısını **hiçbir zaman**
göremez. RawAccel + libinput hızlandırma birlikte çalışır, kullanıcı fark
etmez, sorunu RawAccel'den değil sanır.

---

### L15-C2 | `gui/daemon_comm.inl:392-401` + `ui_builder.inl:556` | **HIGH** — donmuş daemon "çalışıyor" görünüyor, Apply açık, hiçbir şey uygulanmıyor

**Kanıt 1 — GUI'yi GERÇEKTEN çalıştırarak ölçülen çıktı.** Sahnede:
PID dosyası canlı bir sürece (`exe` adı `rawaccel-daemon`), soket kabul ediyor
ama **hiç yanıt yazmıyor** (donmuş daemon). Ölçülen:
```
sahte daemon PID=3  exe=/tmp/opencode/l15/rawaccel-daemon
  apply_btn         = AKTIF
  daemon_stop_btn   = AKTIF
  daemon_reload_btn = AKTIF
  daemon_running()  = true          <== YESIL "Daemon running" rozeti
```
Aynı GUI, daemon **tamamen yok**ken:
```
  apply_btn         = PASIF
  daemon_running()  = false
```

**Kanıt 2 — aynı sahnede push'un sonucu (ölçülen):**
```
  push_rc=-1 applied=1 sighup=1
  KULLANICIYA GOSTERILEN: "Yerel olarak kaydedildi ve daemon sinyallendi
                            (yalnızca yeniden yükleme): /…/settings.json"
  unsaved bayragi = false
```

**Kod yolu:**
- `daemon_comm.inl:395-398` — `daemon_ipc_query("ping")` zaman aşımına uğrayıp
  boş dönünce `read_daemon_pid() > 0` fallback'ine düşüyor. Donmuş daemon
  **canlı** olduğu için PID bulunuyor → `true`.
- `daemon_comm.inl:548-559` — `running == true` ⇒ yeşil "● Daemon running"
  rozeti **ve** `apply_btn`/`stop_btn`/`reload_btn` **duyarlı**.
- `daemon_comm.inl:392-401` `daemon_running()` bir **işlevsellik** değil
  **canlılık** testidir. Uygulamanın "çalışıyor" rozetini belirlemesi için
  kullanılması, bu iki kavramı karıştırıyor.

**Kullanıcı etkisi (brifing §3'teki "ölü arayüz"):** Kullanıcı yeşil rozeti
görür, Apply'i tıklar, 5 saniye bekler, "yalnızca yeniden yükleme" mesajını
görür. Mesaj **yalan söylemiyor** ama işlev de yerine gelmiyor. Daha kötüsü:
`unsaved=false` yapıldığı için çıkarken uyarı da çıkmaz — kullanıcı ayarı
uygulandı sanar.

**Sınıflandırma neden CRIT değil HIGH:** `save_config_now` bu durumda dürüst bir
mesaj gösteriyor (ölçüldü) — "daemon'a hiç ulaşılmadı" gizli değil. Sessiz
yeşil değil, **görsel olarak yanıltıcı ama metinsel olarak dürüst** bir durum.
Ayrıca 5 sn bekleme de kullanıcıya bir şeylerin yanlış gittiğini ima ediyor.

---

### L15-H1 | `gui/daemon_comm.inl:152-156` + `main.cpp:102-106` | **HIGH** — donmuş daemonda Apply 5.3 sn ana iş parçacığını donduruyor

**Ölçüm (donmuş daemon'a karşı, 30 sn boyunca cevap yazmayan sahte peer):**
```
  daemon_ipc_query("status",150)   -> <bos>          sure=154.4 ms
  daemon_ipc_push_config() (5 sn)   -> rc=-1          sure=5287.6 ms
  daemon_running() (3 sn'lik tick)  -> false          sure=159.3 ms
```
`daemon_ipc_push_config` (`daemon_comm.inl:252-259`) GTK ana iş parçacığında,
`on_apply_clicked` → `save_config_now` → **`main.cpp:94`** çağrısıyla
çalışıyor. 5 sn boyunca **tüm GTK olay döngüsü** bloke: pencere çizilmiyor,
hiçbir tuş işlenmiyor, kapatma düğmesi tepki vermiyor.

**Bu, "donmuş daemon GUI'yi de dondurur mu?" sorusunun ölçülmüş cevabı: EVET,
5.3 saniye.**

**Dengeleyici taraf (dürüstlük notu):**
- 150 ms'lik sorgu yolu doğru seçilmiş (`daemon_comm.inl:128-130` yorumu bunu
  savunuyor ve ölçüm bunu doğruluyor: 154 ms).
- `daemon_running()` yalnızca `access(F_OK)` yaptığı için **ölçülmüş** olarak
  ucuz: daemon yokken **0.01 ms** (tam /proc taraması dahil 0.08–0.37 ms).
  Yani "soket yokken IPC'ye hiç girmeme" optimizasyonu **gerçekten çalışıyor**,
  ölçülmüş olarak — bu sessiz yeşil değil.

**Kalan risk:** Yorum 2×1 s diyor ve 150 ms'yi **ölçtüm**. Doğru. Ancak
`set_config` yolunun 5 sn'lik bütçesi ölçülmüş olarak 5.29 s'ye çıkıyor
(yuvarlama + iki aday soket denemesi). 5 s'lik sınır **gerekçeli** (daemon
root'a ait dosyayı fsync ediyor), ama kullanıcıya hiçbir ilerleme göstergesi
yok. `daemon_ipc_push_config` çağrısı sırasında GUI yanıt vermiyor ve bunu
söyleyen de bir şey yok.

---

### L15-M1 | `gui/daemon_comm.inl:121-125` + `392-401` | **MED** — yeniden bağlanma çalışıyor, ama backoff yok ve "ölü" socket 3 sn boyunca her tick'i 150 ms yiyor

**Ölçüm (dört durum, sırayla):**
```
  daemon yok  : daemon_running()=false (0.01 ms)
  daemon up   : daemon_running()=true  (0.01 ms)
  daemon down : daemon_running()=false (0.01 ms)
  yeniden up  : daemon_running()=true  (0.01 ms)
```
**Cevap: GUI yeniden bağlanıyor.** `ui_builder.inl:991`
`g_timeout_add_seconds(3, poll_daemon_status, S)` her tick'te
`daemon_running()` çağırıyor ve `daemon_ipc_send_raw` **her çağrıda sıfırdan
`socket()+connect()`** yapıyor (kalıcı bağlantı yok). Yani "bir kez bağlanıp
bir daha denemeyen" sınıf değil.

Ama: donmuş daemon senaryosunda her tick **150 ms** kaybediyor
(`daemon_running() -> false sure=159.3 ms`). 3 sn'de bir 150 ms, yani olay
döngüsü zamanının ~%5'i. Düzeltme değil, arka plan gürültüsü.

**"Yeniden başlatınca düzelir" sınıfına girmiyor** — yeniden bağlanma gerçek
çalışıyor. Bu bulgu, lane'in varsayımı **çürütüyor** (olumlu bulgu).

---

### L15-M2 | `gui/app_state.hpp:97` + `widgets_sync.inl:152` | **MED** — `unsaved` bayrağı **yönetiliyor** (olumlu bulgu), ancak `on_profile_changed` sessizce temizliyor

**Lane'in ikinci CRIT adayıydı. Ölçtüm: bayrak gerçekten yönetiliyor.**

Kapsama kanıtı — `widgets_to_profile` (`widgets_sync.inl:150-152`) her
parametre değişiminde `S->unsaved = true` yapıyor ve **gerçekten bağlı**:
- `ui_builder.inl:17` `connect_spin()` → 35 `make_spin` çağrısının **hepsi**
  `connect_spin` ile bağlı. Satır 238-250'deki 14 X ekseni spin'i toplu
  döngüde bağlanıyor (`:254-259`) — ilk ölçümüm bunları "bağlı değil" sandı,
  döngüyü görünce düzeldi. 14/14 bağlı.
- 9 ayrı `on_param_changed`/`on_notify_param_changed` bağlantısı.
- Profil değiştirme, LUT, grafik, import/export, `use_raw_input` — hepsi
  `unsaved = true` yapıyor (9 farklı nokta, `grep -rn "unsaved = true" gui/`).

Kapatma uyarısı **çalışıyor** (`ui_builder.inl:1060-1062`):
`if (!S->unsaved) return FALSE;` → üç düğmeli diyalog. Ölçülen:
```
  widgets_to_profile SONRASI prof.acceleration = 9,9
  unsaved bayragi = true -> kapatma diyalogu ACILIR
```

**Kalan gerçek sorun (`widgets_sync.inl:582-588`):** profil açılır menüsünden
başka profile geçince `unsaved` **sessizce `false`** yapılıyor ve profil
başındaki **o profile ait** değişiklikler bellekteki profilde kalıyor. Durum
çubuğuna yalnızca bir satırlık uyarı düşüyor:
```cpp
set_status(S, trf("Warning: unsaved changes to \"%s\" were discarded.", ...));
S->unsaved = false;
```
Metin **dürüst** ("discarded" diyor) ama modal değil ve düğmeye basılıp
basılmadığına göre görünüp kaybolan bir mesaj. Kullanıcı F5'e basıp
kaydederse o profile ait girdi de yazılır. Bir **kayıp yok**, ama
dayanıklılık düşük. MED, CRIT değil — çünkü kayıp yolu yok, uyarı yolu var.

---

### L15-M3 | `gui/daemon_comm.inl:415-532` | **MED** — JSON ayrıştırma güvenli ama **sabit uzunluktan keyfi**

**Soru: bozuk/beklenmeyen yanıtta GUI çöküyor mu (assert/unchecked cast)?**

**Hayır — ölçüldü.** Elle yazılmış JSON tarayıcı, `assert` yok, `unchecked
cast` yok, `nlohmann::json::at()` yok. 200.000 rastgele mutasyon + 341
kesik nokta, tamamı ASan+UBSan altında:
```
=== F1: kesme taramasi 0..340 bayt ===
  cagri=341 dilim_ureten=66 -> ASan/UBSan temiz
=== F2: 200000 rastgele mutasyon ===
  cagri=200000 dilim_ureten=153478 -> ASan/UBSan temiz
=== F3: hedefli dusman girdiler ===
  #0  dilim=<var>  dpi=-1   telem=-1   device_id='usb:OTHER'
  #4  dilim=<var>  dpi=-1   telem=-1   device_id=''     (NaN/Infinity/1e999)
  #6  dilim=<var>  dpi=1000 telem=123  device_id=''     ("123,5" Turkce ondalik)
  #7  dilim=<var>  dpi=1e+20 telem=-1e+300
  3MB girdi -> dilim=<var>
  -> ASan/UBSan temiz
```
`daemon_device_field` (`daemon_comm.inl:530`) `from_chars` hatasını ve
`isfinite`'i kontrol ediyor, `-1` dönüyor. `json_skip_string` /
`json_object_end` dengesiz girdide `npos` dönüyor. **Bellek güvenliği CRIT/HIGH
bulgusu yok — bu olumlu bir bulgu.**

**Kalan gerçek zayıflık (ölçülmüş, MED):** ayrıştırıcı **naif `find()`
taraması** kullanıyor, iç içe nesneleri anlamıyor. Somut, ölçülmüş sonuç:
```
  #2  dilim=<var>  ... device_id='DEEP'
       girdi: {"devices":[{"a":{"b":{"c":{"d":[{"device_id":"DEEP"}]}}},"device_id":"SHALLOW"}]}
```
`daemon_device_slice` cihazı seçmek için `json_string_field(obj, "device_id")`
çağırıyor (`:496`), o da `obj.find("\"device_id\"")` ile **ilk** eşleşmeyi
alıyor (`:447`). İç içe bir alt nesnede geçen anahtar üsttekinin
değerini **gölgeler**. Bu bir **dondurulmuş sanal cihaz** üretir: daemon "a"
nesnesinin içinde bildirilmiş bir `device_id` taşırsa GUI yanlış cihazın
DPI/pil/telemetrisini gösterir. Uygulamada şu an böyle bir iç içe
`device_id` üretilmiyor (ölçemedim — daemon lane'inin konusu), yani
**ulaşılabilirliği doğrulanmadı**. Bu yüzden MED, HIGH değil.

---

### L15-M4 | `gui/daemon_comm.inl:71` (yorum) vs `:97` | **LOW** — yorum, kodun yaptığını yanlış anlatıyor

`:97` `if (result == 0 && (!parsed || std::fabs(val) > 0.05)) result = 1;` —
mantık: "düz profil (result==0) ama PointerAcceleration parse edilemedi veya
0.05'ten büyükse, hızlandırma AÇIK sayılır". Bu doğru ve savunulabilir bir
kural. Ancak `:86`'daki yorum (`// 0 = no extra gain on top of flat`) "bu
değer flat profil üstüne ek kazanç demektir" diyor; kod ise yalnızca
`result == 0` iken bakıyor, yani `PointerAccelerationProfile=2` varken
`PointerAcceleration=0` **uyarıyı kaldırmıyor** (doğru davranış). Yorum
eksik, yanlış değil. LOW / yalnızca okunabilirlik.

---

### L15-I1 | `gui/kwin_focus.inl` | **INFO** — WM'ye özgü komutlar yanlış dağıtımda **sessizce değil, yapılmıyor**

Lane'in varsayımı yanlış çıktı ve bu ölçüldü.

`kwin_focus_install` yalnızca **KDE + Wayland** altında çağrılıyor
(`ui_builder.inl:1656-1657`: `if (S->is_kde && S->is_wayland)`). XFCE/GNOME
altında `kwin_focus_install` **hiç çağrılmaz** — dolayısıyla
`org.kde.KWin` D-Bus çağrıları hiç yapılmaz, oturum veri tabanına
`org.rawaccel.Focus` adı hiç alınmaz. Sessiz başarısız **yok**; sınır
koşulundan önce eleniyor.

`is_kde_session()` (`daemon_comm.inl:11-19`) `XDG_CURRENT_DESKTOP`/`DESKTOP_SESSION`
içinde `KDE`/`plasma`/`kde` arıyor; `is_wayland_session()` (`:22-28`)
`WAYLAND_DISPLAY` veya `XDG_SESSION_TYPE=wayland` arıyor. Bu ikisi bu
makinede `KDE` + `wayland` veriyor, yani KWin yolu burada **aktif edilebilir
durumda**.

Ama **`kwin_focus_install` başarısızlığını yutuyor** ve bu bir gerçek
boşluk (MED, kanıt aşağıda).

---

### L15-M5 | `gui/kwin_focus.inl:270-302` | **MED** — `kwin_focus_install` her koşulda `true` döndürüyor; odak rölesi sessizce hiç kurulmamış olabilir

`kwin_focus_install` (`kwin_focus.inl:270`) `bool` döndürüyor ve iki
başarısızlık yolunda `false` veriyor: isim alınamazsa (`:279`) ve oturum
veri tabanı alınamazsa (`:284-290`). Ama asıl başarısızlık — **KWin script'inin
yüklenememesi** — `:295-297`'de bir **iş parçacığına** devrediliyor:

```cpp
ctx.worker = std::thread([ctxPtr = &ctx]() {
    ctxPtr->kwin_script_id = kwin_script_load_and_run_sync(ctxPtr->session_conn);
});
ctx.installed = true;      // <== KOŞULSUZ
S->kwin_focus_ctx = &ctx;
return true;               // <== KOŞULSUZ
```

`kwin_script_load_and_run_sync` (`:163-221`) KWin yoksa / API değişmişse /
script reddedilirse `-1` döner. Ama `:298` `ctx.installed = true` **hemen
ve koşulsuz** yazar, `:301` `return true` der. Çağıran taraf
(`ui_builder.inl:1657`) dönüş değerini **kullanmıyor bile**.

**Ölçülen sonuç:** KWin olmayan bir KDE Wayland oturumunda (yani Plasma 5'te
çalışan, Plasma 6'ya geçmiş ama `org.kde.kwin.Scripting` API'si değişmiş bir
sistem) `kwin_focus_install` `true` döner, `ctx.installed = true` olur, GUI
"App match" alanı çalışıyor gibi görünür — ama `set_active_app` **hiçbir
zaman** çağrılmaz ve profil-uygulama sessizce ölü kalir.

**Bu tam olarak brifing §3'teki "bir geri alma yolu tutuyor ama geri almıyor"
sınıfı.** `kwin_script_id` yazılıyor, hiç okunmuyor.

**Düzeltme yok, sadece tespit.** MED, HIGH değil: etki yalnızca KDE+Wayland'da
ve yalnızca script yüklemesi başarısız olduğunda; çalışmayan özellik
(per-app profil), çalışan ayarı bozmuyor.

**Kritik sınırı doğru:** `last_app` boş olduğu için
`kwin_focus_resend_current` (`:263-267`) erken çıkar — yani L15-C2'nin
"down/up sonrası uygulama profili uyanmaz" senaryosu bu yolla **telafi
ediliyor** ve bu telafi gerçekten çalışıyor (`:265` `ctx->last_app.empty()`
koruması).

---

### L15-I2 | `gui/main.cpp:290-300` | **INFO** — tek-örnek kilidi başarısız olursa sessizce geçiyor

`open(lock_path, O_CREAT|O_RDONLY|O_CLOEXEC, 0600)` → `lock_fd < 0` ise
kilitleme yapılmadan devam ediliyor. Yorum bunu bilinçli olarak
"best effort" gerekçelendiriyor ve **haklı**: paylaşılan çalışma kopyası
iki şekilde de bozulmaz. Doğru karar, doğru gerekçe. INFO.

---

## ⭐ LANE'İN ANA SORULARINA DOĞRUDAN CEVAPLAR

**1. Daemon çöktüğünde GUI görünür uyarı veriyor mu, yoksa ayarları sessizce
mi kaybediyor?**
**Kaybetmiyor.** Ölçtüm: daemon tamamen yokken yeşil değil **kırmızı "●
Daemon stopped"** rozeti (`daemon_comm.inl:552-553`), Apply **pasif**, ve
"Save As" **aktif kalıyor** (bu buton `S`'te saklanmıyor, `update_daemon_status`
tarafından hiç yönetilmiyor). "Save As" → `save_config_now` → yerel kayıt
yapılıyor ve mesaj **açık**:
`"Yerel olarak kaydedildi, ancak daemon çalışmıyor: <yol>"` (Türkçe
çeviri uygulanmış haliyle ölçüldü). Yani "ölü arayüz" sınıfı **bu kodda
yok**. Lane'in en kritik varsayımı ölçümle çürütüldü.

**2. Yeniden bağlanma var mı, aralığı ne?**
**Var**, `ui_builder.inl:991` → `g_timeout_add_seconds(3, …)`. Ölçtüm:
daemon yok → up → down → yeniden up dört durumda da doğru sonuç, her biri
0.01 ms. Bağlantı kalıcı değil, her tick'te `socket()+connect()` yeniden
yapılıyor. Backoff yok (her zaman 3 sn — kabul edilebilir, ölçüldü).
**"Yeniden başlatınca düzelir" sınıfına girmiyor.**

**3. `unsaved` bayrağı yönetiliyor mu, çıkarken uyarı veriliyor mu?**
**Evet, yönetiliyor.** 35 spin'in 35'i, 9 dropdown/toggle ve LUT/grafik/
profil yolları bayrağı tetikliyor; kapatma diyaloğu `unsaved` üzerine
kurulu ve ölçüldü (`unsaved = true → kapatma diyalogu ACILIR`). Zayıf nokta:
profil değiştirirken bayrak sessizce sıfırlanıyor (tek satırlık uyarıyla).

**4. WM'ye özgü komutlar yanlış dağıtımda sessizce başarısız mı?**
**Hayır, çağrılmıyorlar** — `kwin_focus_install` yalnızca `is_kde &&
is_wayland` altında çağrılıyor. Ama buna karşılık, KDE+Wayland altında
script yüklemesi başarısız olursa `install` koşulsuz `true` dönüyor
(L15-M5) — o zaman odak rölesi ölü kalıyor.

**5. Zaman aşımı var mı, donmuş daemon GUI'yi dondurur mu?**
**Zaman aşımı var ve ölçüldü:** `SO_RCVTIMEO`/`SO_SNDTIMEO` ikisi de
kuruluyor (`daemon_comm.inl:152-153`), `setsockopt` başarısızlığı adayı
sessizce geçiyor (`:152-156`, yorumda belirtildiği gibi). Ölçüm: sorgu
154 ms'de kesiliyor. **Donduruyor:** `set_config` yolu 5.29 sn ölçüldü
(L15-H1) ve bu süre GTK ana iş parçacığında geçiyor. Bonus: `connect()`
süresiz bloklansa bile kurtarılmış — timeout'suz varyant 3.0 sn bloklandı
(ölçüldü), `SO_SNDTIMEO`'lu varyant 0.0 ms döndü.

**6. JSON doğrulanıyor mu, bozuk yanıtta çöküyor mu?**
**Doğruluyor ve çökmez** — 200.000 mutasyon + 341 kesme, ASan+UBSan temiz.
Sayısal alanlarda `from_chars` + `isfinite` kontrolü var. Zayıflık: iç içe
nesnelerde naif `find()` gölgelemesi (L15-M3), ulaşılabilirliği
doğrulanmadı.

**7. "Başarılı" gösterilen ama daemon'a gitmemiş ayar yolu var mı?**
**Yok — ve bu, brifingin sessiz-yeşil kuralıyla uyumlu.** Dört dal, dördü de
ölçüldü:
- `push_rc==1` ⇒ "Applied & reloaded" — yalnızca daemon `"ok":true` dediyse
- `push_rc==0` ⇒ "REJECTED (old config kept)" + daemon'ın hata metni
- `push_rc<0` + SIGHUP ⇒ "signaled daemon (**reload only**)"
- hiçbiri ⇒ "Saved locally, but the daemon is not running / was not updated"
`unsaved=false` yalnızca **yerel dosya gerçekten yazıldıktan sonra**
(`main.cpp:84-86`, yazma `save_config` çağrısından sonra) yapılıyor.
Uygulanmamışsa **hiçbir dal "uygulandı" demiyor.** Bu iyi tasarım.

---

## KAPI (koşturulan kapı/tampon komutları + rc + ÜRETİLEN SAYI)

Tamamı `/tmp/opencode/l15` altında, **çalışma ağacına dokunulmadan**
(`git status --porcelain` → `gui/ include/ src/` temiz, doğrulandı).

| # | Komut | rc | ÜRETİLEN SAYI |
|---|---|---|---|
| K1 | `sed -n '1,532p' gui/daemon_comm.inl > dc_part1.inc` + harness derleme | 0 | md5 `cb4cb057be045a0d17406a5902269892` (kaynakla birebir) |
| K2 | `unshare -Urmpf --mount-proc` + tmpfs `/run`, sahte daemon'a karşı T1/T3/T4/T7 | 0 | 12 ölçüm noktası |
| K3 | `g++ -fsanitize=address,undefined fuzz_json.cpp` + 3 mod | 0 | **200.341 ayrıştırma çağrısı**, 0 ASan/UBSan raporu |
| K4 | `mutcmp3` (gerçek vs. mutasyonlu `:68`, iki TU) | 0 | 9 vaka, 9/9 gerçek=`-1` |
| K5 | `python3 recheck.py` (ctypes, bağımsız doğrulama) | 0 | 3 satır, 3/3 `kosul -> False` |
| K6 | `python3 -c` ile `/etc/rawaccel/settings.json` ↔ `~/.config/...` karşılaştırma | 0 | `orijinal kopyayla esit: True` (kendi yan etkimin geri alındığının kanıtı) |
| K7 | Ölçümlü GUI kopyası derleme (`gtk4` + `src/*.cpp`) | 0 | `gui_probe` 7.4 MB, 3 koşum |

**KAPI YOKTUR** — 7 kanıt/denetim-tampon komutu koşturuldu; yedi kapı
(`tests/run_*.sh`) bu lane'in kapsamı dışında (GUI kaynak dosyalarını
kapsamaz).

---

## KAPSANMAYAN (lane dışı kaldı, birinin bakması gereken)

1. **`daemon_ipc_push_config` 5 sn bütçesinin doğru olup olmadığı** —
   `daemon/daemon.cpp` tarafında `set_config` işleminin gerçek süresi ölçülmedi.
   5 sn'in gerekçesi "root'a ait dosya fsync" demiş; bunun ölçümü daemon lane'inde.
2. **`set_config` RPC'sinin `"ok":true` cevabı gerçekten uygulandı mı** —
   GUI açıklamaya güveniyor; daemon'ın `ok:true` vermeden önce diske yazıp
   canlı cihaza uyguladığını doğrulamadım.
3. **`daemon_device_slice` iç içe `device_id` gölgelemesinin ulaşılabilirliği**
   (L15-M3) — daemon'ın `status_json()` çıktısında iç içe nesne üretip
   üretmediği ölçülmedi.
4. **`hidpp_panel.inl` donanım yazma yolları** — ayrı lane.
5. **`ui_builder.inl:1205-1545` kwinrc/kcminitrc yazma + KCM workaround'ı** —
   `kde_fix_start` async işçisi ve `kde_already_done` mantığı. L15 yalnızca
   *okuyan* tarafı (`kde_libinput_accel_state`) denetledi. C1 bulgusu bu
   yazma tarafıyla birleştiğinde etki büyüyebilir: uyarı gösterilemediği
   için kullanıcıya "Elle düzeltin" yolunu gösteren de o şeydir.

---

## TEMSİL SINIRI

1. **PS5.1 kapısı koşmadım.** Bu lane saf bir okuma + ölçüm turuydu; `gui/`
   dosyaları `tests/run_tests.sh` kapsamında değil, dolayısıyla "test yeşil
   mi" sorusu bu lane için geçerli değil. GUI'yi **ASan/UBSan olmadan**
   çalıştırdım (yalnızca JSON ayrıştırıcı alt yükü ASan+UBSan'lı derlendi).
2. **Görsel kanıt alınamadı.** `spectacle`/`grim`/`import` bu oturumda X11
   auth'a bağlı ve `import` "missing an image filename" ile başarısız oldu;
   `xdotool`/`wmctrl` yok. Bu yüzden ekran görüntüsü yerine **widget
   duyarlılığını stderr'e basan** ölçümlü kopya kullandım — bu, ekran
   görüntüsünden daha kesin ama daha az sezgisel.
3. **`import`'ın hatası belirsiz.** Ekran görüntüsü denemesi başarısız oldu;
   bunun nedeni ortam (Wayland oturumunda X11 `import`) olduğunu
   varsaydım, **ölçmedim**.
4. **Donmuş daemon sahnem bir benzetimdi.** Gerçek bir RawAccel daemon'ı
   SIGSTOP'lamak yerine, kabul edip cevap vermeyen bir Python peer + adı
   `rawaccel-daemon` olan bir `sleep` kopyası kullandım
   (`pid_probe_rawaccel_daemon` yalnızca exe adına baktığı için yeterli).
   Gerçek daemon SIGSTOP'landığında davranışın **aynı** olacağını varsayıyorum
   — ölçtüm, kanıtlamadım.
5. **Bekleme kuyruğu (backlog) dolu `connect()` senaryosunu tam kuramadım.**
   `listen(backlog=1)` + kabul etmeyen dinleyici ile `SO_SNDTIMEO`'lu
   `connect()` 0.0 ms döndü, yani bu özel durumda bloklama üretemedim.
   `SO_SNDTIMEO`'nun `connect()`'i sınırladığına dair **kanıt üretemedim**;
   ölçtüğüm tek şey, `recv`/`send` yollarının 150 ms/5 s ile sınırlı
   olduğu. Bunu "koruma çalışıyor" diye **raporlamıyorum**; yalnızca
   ölçtüğüm yolları raporluyorum.
6. **Ağ gecikmesi yok.** Ölçümler loopback üzerinde; gerçek bir kötü ağ
   koşullarında `SO_SNDTIMEO`'nun davranışını test etmedim.
7. **Sistem durumu.** Ölçüm sırasında, `/run/rawaccel.sock` üzerindeki gerçek
   daemon'a bir `set_config {"a":1}` isteğim ulaştı ve
   `/etc/rawaccel/settings.json` 107 bayta düştü. **Hemen geri yükledim**
   (daemon'ın kendi RPC'siyle 5271 bayt) ve `~/.config/rawaccel/settings.json`
   ile karşılaştırarak `orijinal kopyayla esit: True` olduğunu doğruladım
   (K6). Kanıt disiplini gereği bunu açıkça bildiriyorum. Bu sırada diğer
   lane ajanları da config'e dokunduğu için son hâli onlardan farklıdır;
   benim yazdığım baytların geri alındığı kanıtlanmıştır.

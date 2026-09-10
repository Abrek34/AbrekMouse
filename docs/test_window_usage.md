# Fare Kilit Test Penceresi — Kullanım Kılavuzu (P104 + P109)

Bu doküman, RawAccel Linux GUI'sindeki **fare kilit test penceresini** gerçek
makinede doğru kullanmak ve her parametre ailesi için hızlı bir **kabul testi
(A/B)** yapmak içindir. `docs/real_hardware_test.md`'deki uzun oturum
protokolünün *tamamlayıcısıdır*: buradaki pencere, o dokümandaki "his oturumu"
nun daha dar, tek pencereli, hız/kazanç sayacı olan bir parçasıdır.

**Bu pencere kime yarıyor:** fareyi test ederken imleç ekranın her yerine
fırlayıp sekmeleri, sayfaları ve pencereleri kaydıran kullanıcıya. Test
penceresi imleci kendi içine kilitler — **diğer pencereler hiçbir hareket,
kaydırma veya üzerine-gelme almaz**. Kullanıcının şikayeti tam olarak buydu:
> "mouse test edilirken bütün sayfaları birbirine sokuyor"

Pencere, bu karışıklığı kökünden çözer: test sırasında olan tek şey, kilit
penceresinin içindeki okuyucuda görünür; masaüstünün geri kalanı hiçbir olay
görmez.

---

## 1. Pencere kilit ne demek — ne kilitlenir, ne güvenilmez

Teknik olarak pencere, GTK4'ün kaldırdığı pointer grab API'si olmadığı için,
X11/XWayland oturumlarında Xlib'in **`XGrabPointer`**'ını doğrudan çağırır
(`gui/mouse_test.inl` — Xlib soyutlaması `dlopen` ile çözülür, yeni bağımlılık
yok). Kilit **üç kademelidir**:

| Kademe | Olduğu ortam | Gerçek mekanizma | Kilit gücü |
|--------|--------------|------------------|-----------|
| **Tier 1** | X11 — grab başarılı | `XGrabPointer` (`owner_events=False`, `event_mask=0`): HER imleç olayı yalnız bu pencereye gelir ve yutulur; `confine_to` da görünür imleci pencerenin içinde tutar | **Sert** — hiçbir pencerenin tıklama/kaydırma/hover alması imkânsız, çoklu monitör dâhil |
| **Tier 2** | X11 — grab reddedildi (başka uygulama grab'li, GrabNotViewable vb.) | Kapsama/sınırlama döngüsü: çerçeve zamanlayıcısı imleç pencere dışına çıkarsa onu geri "warp" eder (yalnız pencere odaktayken) | **En iyi çaba** — ekranda "Pointer grab failed" uyarısı |
| **Tier 3** | Wayland / X11 backend yok | Pointer grab/warp API'si yok — yalnız **tam ekran HUD** etkin monitörü kaplar | **Yumuşak** — ekranda "Pointer lock unavailable" uyarısı; komşu monitör/pencereler olay alabilir |

Grab açılışta reddedilse bile pencere **yine açılır** ve canlı hız/kazanç
okuması çalışır — yalnız kilit gücü düşer ve kademeye uygun uyarı ipucu satırına
yazılır. Tier 1'de "mouse test edilirken diğer pencereler karışmasın" şikayetinin
çözümü tamdır ve kilit altında masaüstü şöyle davranır:

| Etki | Kilit altındaki pencere | Geri kalan masaüstü |
|------|-------------------------|---------------------|
| İmleç hareketi | İçeride serbest | Hiçbir hareket ulaşmaz |
| Tıklama / seçim | Pencere içinde çalışır | Tıklama ÇIKMAZ — başka uygulama odak almaz |
| Kaydırma / tekerlek | Pencere içinde | Sayfalar kaymaz |
| Üzerine gelme (hover) | Yalnız test penceresi | Diğer pencereler hiçbir hover almaz |

Yani **Tier 1'de** "sayfaları birbirine sokma" davranışı imkânsızdır. Tier 2/3'te
kilit "en iyi çaba / yumuşak"tır — oturum/çakışma nedeniyle sızma ihtimali varsa
ipucu satırı uyarır ve Bölüm 4'teki tablo çözümü anlatır.

> **Güven sınırı:** Kilit yalnız **fare olaylarını** çevreler; klavye HUD'un
> kendisine gider (klavye asla grab edilmez). ESC kilidi bırakır; pencere yöneticisi
> kısayolları (Alt+Tab, Super+...) çalışmaya devam eder. Odak kaybı grab'ı otomatik
> bırakır (tuzak olmaz), odak geri gelince kilit kademesi yeniden denenir.
> Uygulama/oyun kısayolları test süresince hedefe ulaşmaz (hedef uygulama zaten HUD'un
> altında kapalıdır) — bu, test sırasında istenen davranıştır.

---

## 2. Pencereyi açma (menü yolu)

GUI'nin masaüstü menü çubuğu yoktur; her şey **başlık çubuğu (header bar) +
alt durum çubuğu** üzerindedir. Test penceresi alt durum çubuğundadır:

1. `rawaccel-gui`'yi başlatın, daemon'un `Aktif (running)` göründüğünden emin
   olun (durum yazısı başlık çubuğunun sağında).
2. Alt durum çubuğunda **"Performans"** düğmesinin hemen yanındaki
   **"Fare Testi"** düğmesine tıklayın (`Fare Testi`, "Performance"→"Performans"
   düğmesinin sağındaki son düğmedir).
3. **"Fare Kilit Testi"** başlıklı **tam ekran** bir HUD açılır; imleç kendini
   ekranın içinde bulur — masaüstünde başka pencere görünmez.

> **Çoklu monitör:** Etkin monitör HUD'la tam ekran kapanır ve Tier 1'de
> `confine_to` imleci pencerenin içinde tutar — diğer monitöre sızma olmaz.
> Tier 2/3'te (grab yok veya reddedildi) imleç komşu monitöre taşabilir; teste
> başlamadan önce hedef monitörü aktif edin (fareyi oraya taşıyıp HUD'u açın) —
> Bölüm 4.

Pencere ilk sayfada günlük kullanım yerine doğrudan test moduna girer:

```
Fare Kilit Testi                     ← pencere başlığı
Giriş (ips):  —                      ← input hızı (daemon telemetrisi, ips)
Çıkış (ips):  —                      ← output hızı (ips)
Kazanç (×):   —                      ← çıkış/giriş oranı; 1.00 = ham 1:1
Hareket bekleniyor…                  ← ilk örnek alınana kadar; sonra boşalır
İmleç bu tam ekran test penceresine kilitli.
Canlı hız/kazanç için fareyi hareket ettirin.
Kilit ESC ile bırakılır.
```

Okuma etiketleri ilk harekete kadar **"—"** ve alt satırda **"Hareket
bekleniyor…"** gösterir (daemon daha örnek yayınlamadı); ilk hareket
örneğiyle birlikte sayılar belirir ve alt satır boşalır.

Pencere açıkken **"Fare Testi" düğmesine tekrar tıklamak** pencereyi yeniden
öne getirir (ikinci bir pencere açmaz). Okumalar 250 ms'de bir yenilenir —
sayıların akması için fareyi **devamlı** hareket ettirin, tek seferlik bir
defa kıpırdatmak bir ya da iki örnek gösterir.

**Hangi profili test ediyoruz?** Pencere, daemon'un **o an aktif profilini
canlı okur** (250 ms'lik yoklama her seferinde daemon'a sorar). Profili
değiştirmek için ana pencereden hedef profili seçip **Apply** yapmanız yeterli
— HUD açık kalsa bile sayılar yeni profili göstermeye başlar.

---

## 3. Kabul protokolü — aile başına A/B (pencerenin İÇİNDE)

Her aile için prosedür aynıdır: profili aktifleştir → pencereyi aç → aşağıdaki
hız aralıklarında fareyi **fısıl fısıl** (micro-aim) ve **hızlı çekim** (flick)
yap → **"Kazanç (×)"** okumasını beklenen değerle karşılaştır.

| Aile / mod | Profil (öneri) | Beklenen davranış | Kabul kriteri |
|------------|----------------|-------------------|---------------|
| **classic (gain)** | `cs2`, `fps`, `gaming` | Yavaşta 1:1'e yakın, hızla birlikte gain tırmanır, çatı (limit/cap) üstüne çıkmaz | `cs2`: yavaş 1.00–1.01 → 1.32 @80 ips → **max 1.58** @900 civarı; `fps`: 1.00–1.01 → 1.40 @80 → **max 1.76**; asla limiti (1.6 / 1.8) aşmaz |
| **power (cap hit)** | `apex` | Çok erken tırmanma, çatıya hızlı oturma (180° hızı için bilinçli) | ~0.90 zemin → **2 ips'te ~1.95** → 5 ips'te ~2.10 → orta bölge (10–30 ips) 2.15–2.18 → **hızlı çekimde ~2.20 çatı** (2.2'yi asla aşmaz) |
| **natural (smooth)** | `valorant`, `office` | Dar bantta yumuşak giriş/çıkış | Kazanç her hızda **1.0–1.3 arası**; 0.2→1.01, 2→1.07, 30→1.26, 900→1.30; ani zıplama olmaz |
| **noaccel (1:1)** | `default` (taze kurulum) veya noaccel profil | Hiçbir hızda ivme yok | **Kazanç her hızda 1.00** (ekrandaki kayan yazı 1.00'dan sapmaz) |
| **raw passthrough** | `disable` | Hız/gain hattı devre dışı (modifier yok) | **Beklenen: okuma 0.00 (veya "Hareket bekleniyor…")** kalır — bu bir hata değil; 1:1'i *elle* teyit edin (aşağıya bakın) |
| **lookup (LUT)** | GUI modu "Arama (LUT)" | gain = LUT çıkış / giriş (nokta = (giriş, çıkış)) | Basit LUT `[(10,10),(100,200)]` için: 10 ips'te Kazanç ≈ **1.00**, 100 ips'e yaklaştıkça ≈ **2.00**; LUT yeniden sıralandıkça okuma düzgün izler |

**Noaccel'le raw'ı birbirine karıştırmayın:**

- **noaccel** (ör. taze kurulum `default` profili): ivme kapalı ama olaylar
  hız hattından (flush_motion) geçer → pencere **Kazanç 1.00** gösterir,
  "1:1" teyidi budur.
- **`disable`** preseti `raw_passthrough = on` olduğundan **tüm hız/gain
  hattını baypas eder** → pencere okuyucusu 0.00 kalır. Bu durumda 1:1'i
  **elle** doğrulayın (imleç elinizle birebir gidiyor mu), kazanç okumasına
  bakmayın. Bu, `docs/real_hardware_test.md` Bölüm 2'deki "raw ≠ noaccel"
  notu ile aynı gerçektir.

> Okuma **≈** işaretli hızlarda 0.02–0.05 mertebesinde oynarsa normaldir:
> pencere 250 ms'de bir örnek alır ve fırça hızı (flick) tam 80/900 ips
> olamaz. Hızlı çekimi birkaç kez tekrarlayıp en yüksek gördüğünüz kazancı
> tablodaki tavanla karşılaştırın. Beklenen sayılar `include/presets.hpp`
> `make_preset()`'ten türetilmiştir (Bölüm 3.3'teki tablo ile birebir).

**Prosedür (bir aile için):**

1. Ana pencere → profil seç → Apply (daemon'a canlı uygular).
2. "Fare Testi" → pencere kilitlenir.
3. **Yavaş: 2–20 mm/s** el hızıyla mikro düzeltmeler → kazancı oku.
4. **Flick: hızlı 180°/panik çekimi** → kazancın tepe değerini oku.
5. Beklenenle karşılaştır; fark sistematikse `docs/user_guide.md`'deki tuning
   notlarına bak, DPI/duyarlılık etkisini ayıklamak için
   `docs/real_hardware_test.md` Bölüm 3.2 DPI merdivenini uygula.

---

## 4. ESC kaçış kapağı ve kilit sınırları (sorun giderme)

Kilidi bırakmanın garantili yolu **ESC**'dir:

- `ESC` → HUD kapanır, imleç serbesttir; ana GUI durum çubuğunda
  **"Fare testi: imleç serbest bırakıldı (ESC)."** yazar.
- `Alt+F4` / görev çubuğundan kapat gibi pencere yöneticisi kapatma istemleri de
  aynı GTK destroy yolundan geçer ve teardown aynıdır (tam ekranda başlık çubuğu
  görünmediği için × tuşu her zaman erişilemez). Odak kaybı da grab'ı bırakır
  (Bölüm 1).

**Kilit her ortamda aynı güçte değildir** — pencere hangi kademede açıldıysa
ipucu satırı onu söyler ve hangi kademe olduğu "kilit tutmadı" teşhisinin ilk
adımıdır:

| Ekrandaki ipucu satırı | Anlamı | Kademe |
|------------------------|--------|--------|
| "Pointer is locked inside this fullscreen test window." | `XGrabPointer` başarılı — sert kilit | **Tier 1** |
| "Pointer grab failed — the cursor is confined as best effort (imleç kilidi yok)…" | X11'de grab reddedildi; yalnız confine/warp döngüsü aktif | **Tier 2** |
| "Pointer lock unavailable — imleç kilidi yok: this Wayland session cannot grab the pointer…" | Wayland/backendsiz oturum — yalnız tam ekran HUD | **Tier 3** |

"Kilit tutmadı" hissinin gerçek nedenleri ve çözümleri şu tablodadır:

| Belirti | Olası neden | Ne yapmalı |
|---------|-------------|------------|
| İmleç HUD açıkken diğer monitöre/pencerelere taşıyor ve orayı kaydırıyor | Kademe Tier 2/3: Tier 3'te (Wayland) grab/warp API'si yok — fullscreen yalnız etkin monitörü kaplar; Tier 2'de (X11, grab reddi) yalnız warp döngüsü var | Tier 1 almak için çakışan grab'li uygulamaları kapat (ekran seçici, OBS vb.) ve pencereye tekrar tıkla (odak geri gelince grab yeniden denenir); Wayland'da komşu monitördeki uygulamaları kapatın ya da testi X11/XWayland oturumunda yapın |
| X11'de ipucu "Pointer grab failed…" diyor | `XGrabPointer` reddedildi (başka uygulamada aktif grab, GrabNotViewable vb.) — pencere Tier 2'de | Çakışan uygulamayı kapat; pencereye tıklayıp odağın test penceresinde olduğundan emin ol; yine olmazsa WM'yi değiştir |
| Wayland'da ipucu "Pointer lock unavailable…" diyor | Oturumda pointer grab/warp API'si yok (GDK Wayland) — kilit yalnız tam ekran HUD | Beklenen davranıştır (Tier 3); okuma yine çalışır; test monitöründe başka pencere açma |
| Sayılar hiç gelmiyor (— / alt satırda "Hareket bekleniyor…") | Daemon kapalı veya cihaz telemetri üretmiyor | **"Daemon çalışmıyor."** yazısına bakın; `rawaccel-cli status` ile cihazın grab edildiğinden emin olun |
| Okuma var ama 1:1 hissi yok | Yanlış/raw profil aktif (`disable`) | Bölüm 3'e göre doğru profili Apply edin; `rawaccel-cli status` → `Active:` kontrolü |

Her koşulda **ESC çalışır** ve telemetri **grab'a değil daemon'a** bağlıdır:
HUD'un görünüşü ne olursa olsun hız/kazanç okuması daemon'dan gelir.

**Daemon kapalıysa** pencere **"Daemon çalışmıyor."** gösterir — hız/gain
okunamaz; daemon'u çalıştırın, pencere 250 ms'lik yoklamayla kendini toparlar
(pencerenin açık kalması sorun değildir).

---

## 5. 5 dakikalık "her şey açık" güvenlik matrisi

Test sonrası kilidi bırakın (ESC) ve 5 dakika normal masaüstü kullanın. Kilit
KAPALIYKEN aşağıdakilerin **olması beklenir** — bunlar test penceresinin
kaplamadığı, normal sistem davranışıdır ve hata sayılmaz:

| Davranış | Beklenen mi? | Neden | Ne yapmalı |
|----------|--------------|-------|------------|
| İmleç artık ekranın her yerine gidiyor | **Evet** | HUD kapanınca tam ekran örtü yok; kilit yalnız test süresinceydi | Doğrunun ta kendisi — geçti |
| Sekmeler/pencerekler imleçle kayıyor | **Evet** | Kilit yokken sistem normal davranır | Beklenir; yeniden test için pencereyi açın |
| Masaüstü köşe/kısayol köşeleri (hot corner) tetikleniyor | **Evet** | Kilit yokken pointer köşelere ulaşabilir; köşe kısayolları sistem seviyesindedir | Beklenir |
| KDE'de fare hızlandırması yeniden "çift" gibi | **Evet**, yalnız düzeltme kapalıysa | `kde-fix-accel.sh` zaten uygulanmadıysa masaüstü kendi ivmesini katar | `bash scripts/kde-fix-accel.sh` sonra yeniden test |
| Oyun içi "Raw Input" ayarı değişti | **Hayır — beklenmez** | Daemon oyuna yazmaz, oyunun ayarı değişmez | Değiştiyse bir başka uygulama dokundu demektir; oyunu kapıp açın |
| `disable` profiliyle kazanç 0.00 | **Evet** | Raw passthrough hız hattını baypas eder (Bölüm 3) | Doğru; 1:1'i elle teyit edin |

**Hata sayılan tek şey:** kilit penceresi AÇIKKEN diğer pencerelerde
hareket/kaydırma/hover görülmesi. Onu görürseniz:

1. `rawaccel-cli status` → hangi cihazlar grab ediliyor, hangi profil aktif?
   (raw 1:1 `disable`'da kazanç 0.00'da — o bir hata değil, Bölüm 3.)
2. Çoklu monitör mü? → Bölüm 4'teki kilit sınırları tablosu (fullscreen yalnız
   etkin monitörü kaplar; Tier 1'de `confine_to` sert — sızma ancak Tier 2/3'te)
   — HUD'u hedef monitörde açın; o monitörde başka pencere olmadığından emin olun.
3. İkisi de değilse → bunu Aj 1'e bildirin: P104 kilitleme davranışı
   kabul dışı bulundu.

---

## 6. Otomatik karşı kontrol (isteğe bağlı, VM/ana makine)

Kilit penceresinin canlı okuduğu **daemon hattını** (hız/kazanç telemetrisi +
lat histogramı) gerçek fare gerekmeden denetlemek için
`scripts/virtmouse-game.c` kullanılabilir (yalnız daemon ölçeği):

```bash
# (input grubu üyesi olarak) senaryo üretimini 10 sn enjekte et:
gcc -O2 -o build-manual/virtmouse-game scripts/virtmouse-game.c
build-manual/virtmouse-game precision 10   # 120→4000 cnt/s testere (esport grid 2000/3000/4000 ips)
# veya sürekli akış için: build-manual/virtmouse-game pan 10   # 4000 cnt/s

# sonra canlı histogram:
rawaccel-cli latency
journalctl -u rawaccel -n 30
```

`precision` ve `pan` senaryoları, kilit penceresinin imleci hızlandırdığı
bantla (esport grid 2000/3000/4000 ips) aynı hız bölgesinde çalışır;
p50/p95/p99 referans değerleriyle karşılaştırma
`docs/real_hardware_test.md` Bölüm 4.2'dedir; aile başına aynı ölçümün
cs2/valorant/apex/fps referans satırları Bölüm 4.4'teki tablodadır.

> **Eski `locked` senaryosu (P109) — tarihsel nota:** R47 harness'ının
> kilit-penceresi özel senaryosu (`locked`, 90×60 px kutu, 1000 Hz, kenara
> değince karşı duvara re-wrap; bu VM'de 10 sn → 777 re-wrap, 4661 örnek,
> Min 1.12 µs · Avg 4.21 µs · p50 3.75 µs · p95 5.75 µs · p99 8.25 µs ·
> Max 1656.88 µs) R48'de `scripts/virtmouse-game.c`'den çıkarıldı (P121/BUG-10
> harness yeniden düzenlemesi). Mevcut kod yalnız `flick|pan|mix|precision`
> kabul eder; o ölçüm tarihsel referanstır ve bugün kaynak koddan üretilemez
> (eski `locked` çalıştırılırsa sessizce `mix` senaryosu koşar).

---

*İlgili dosyalar: `gui/mouse_test.inl` (pencere, P104), `docs/real_hardware_test.md`
(his oturumu protokolü), `scripts/virtmouse-game.c` (`flick|pan|mix|precision`
senaryoları — Bölüm 6; eski `locked` P109 ölçümü tarihseldir).*
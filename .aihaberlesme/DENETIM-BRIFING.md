# ⭐ DENETİM BRİFİNGİ — 20 LANE, BAĞIMSIZ DENETİM

> Bu dosya `.aihaberlesme/yeni_proje/` **değildir**. O klasör **CS2 projesinin
> bayat bir kopyasıdır** ve bu ağaca aittir — **hiçbir ajan oraya dokunmaz.**

## 0. TEK CÜMLE

**Kod değiştirme. Oku, iddia üret, KANITLA.** Bu bir denetim turudur, düzeltme
turudur değil.

## 1. ⛔ SERT YASAKLAR

1. **Hiçbir dosyayı değiştirme.** `Edit`/`Write`/`sed -i` **yasak**.
   Tek istisna: `.aihaberlesme/mesajlar/` altına rapor yazma.
2. **Build çalıştırabilirsin**, ama `git checkout`/`git stash`/`git reset`
   **yasak** — çalışma ağacını bozarsın, diğer ajanların kanıtı geçersiz olur.
3. **`.gitignore`'daki hiçbir şeye dokunma.**
4. Bir başka lane'in dosyasına **okumak serbest, yazmak yasak**.
5. ⛔ **Uydurma sayı yok.** Ölçmediysen "ölçmedim" yaz. Bu, raporun tek
   kabul kriteri.

## 2. ⭐ KANIT KURALI — "bulgu" ancak kanıtlıysa bulgudur

Her bulgu **şu üçünü** taşımak zorunda:

| # | Zorunlu alan | Örnek |
|---|---|---|
| 1 | `dosya:satır` | `daemon/daemon.cpp:2344` |
| 2 | Somut kanıt (komut + **ham çıktı**) | `grep -n ... → 3 satır` |
| 3 | Hata sınıfı | `CRIT / HIGH / MED / LOW / INFO` |

⛔ **Kabul edilmeyen kanıt biçimleri:**
- "muhtemelen", "olabilir", "şüpheli", "kötü hissettiriyor"
- Kod okuyup *tahmin* ettiğin çalışma zamanı davranışı — **çalıştırıp** göster
- Başka bir ajanın raporunu tekrar etmek — **kendi kanıtın** olmalı
- "0 sonuç bulundu" — **neyi taradığını** yazmadan bu geçerli değil
  (`grep -rn 'X' include/ src/ --include=*.hpp` yazmadan "0 eşleşme" demek
  bu projede iki kez YANLIŞ çıktı)

## 3. ⭐ EN PAHALI HATA — SESSİZ YEŞİL

Bu projede asıl risk "kod çalışmıyor" değil, **"çalışıyor" görünüyor**.

Yani:
- bir test **yazılmamış** ama kapı yeşil
- bir dosya **hiç derlenmiyor** ama `#include` var
- bir fonksiyon **çağrılmıyor** ama "var" sayılıyor
- bir `if` **her zaman false** ama "koruma mevcut" deniyor
- bir `catch` **boş** ama "hata yönetimi var" deniyor
- bir geri alma yolu **kayıt tutuyor** ama **geri almıyor**

Bunları özellikle ara. Her bulgunda şu soruyu sor: *"bu kod çalışırken ne
olduğunda fark edilir?"* Fark edilmiyorsa bulgu `HIGH`'tır, `CRIT`'tir.

## 4. ⭐ "ÇALIŞIYOR" İDDIASINI TEST ET

Bir kapının/bir kontrolün **gerçekten çalıştığını** kanıtlamak için:

1. **Mutasyon dene.** Kodu **kopyada** boz, kapıyı koştur, kırmızıya döndüğünü
   göster. `cp` + `/tmp` üzerinde yap, çalışma ağacına dokunma.
2. **Sessiz yeşil testi.** `$script:X++` yazıp hiç okunmuyorsa → bulgu.
   `grep -c 'script:X'` yaz, `okuma` sayısını ayrı say.
3. **Yerleşik kanıtı yıkma.** `ctest`/`run_tests.sh` yeşilken bir testi
   `if (false)` ile devre dışı bırak (KOPYA üzerinde) → hâlâ yeşil mi?
   Yeşilse kapı o testi **gerçekten çalıştırmıyor** demektir.

## 5. AJAN 3 YOK

Bu turda AJ3 (bağımsız denetçi) **başka projede**. Onun rolü **20 subagent +
AJ2 + AJ4** ile geçici olarak dolduruluyor. Denetim yükünü **kendi başına**
taşıyorsun — bırakma, ödünç alma.

## 6. RAPOR FORMATI (AJ1 buna göre okur)

```
### <LANE-ID> | <ajan/alt-ajan> | <tarih>
KAPSAM   : <denetlediğin dosyalar, kesin>
BULGULAR :
  ID | dosya:satır | CRIT/HIGH/MED/LOW | kanıt (komut + ham çıktı)
  ...
KAPI     : <koşturduğun kapı/tampon komutu + rc + ÜRETİLEN SAYI>
KAPSANMAYAN: <lane'ın dışında kaldığın, birinin bakması gereken yer>
TEMSIL SINIRI: <neyi doğrulayamadın ve neden — örn. "PS5.1/çalışma anı yok">
```

⛔ `KAPI` boşsa: `"KAPI: yok — bu bir okuma denetimiydi"`.
⛔ `TEMSIL SINIRI` **her zaman** dolu olmalı.

## 7. KAPSAM DIŞI (kaza dokunma)

- `include/nlohmann/json.hpp` — **satın alınmış** kütüphane (24.765 satır)
- `olcum/aj5/shadow/include/**` — **satın alınmış kopya**, `include/` ile aynı
- `.aihaberlesme/yeni_proje/**` — **başka projenin kirliliği**, dokunma
- `.git/**`

## 8. AJ1 NE YAPMIYOR

AJ1 (yönetici) bu turda **hiçbir kodu değiştirmedi** ve senin bulgularını
**kendi ölçümüyle yeniden doğrulamadan** kabul etmeyecek. Raporda
`CRIT`/`HIGH` yazıyorsan, AJ1 önce **kendi komutuyla tekrar ölçer**.

---

## 9. ⛔⛔ CANLI DAEMON'A DOKUNMA — SONRADAN EKLENEN KURAL

Bu turda **iki ajan** canlı daemon'a erişti. L12 görev sırasında
`rawaccel-cli` komutlarını `--no-daemon` **OLMADAN** çalıştırdı ve
çalışan daemon'a (pid 718, `-c /etc/rawaccel/settings.json`) gerçek bir
config push etti; `/etc/rawaccel/settings.json` ezildi.

⭐ AJ1 bu etkiyi **bağımsız olarak ölçtü** ve L12'nin "geri yükledim"
iddiası **doğrulanmadı**:

```
mevcut : default, base, bn        | aktif = default
.bak   : default, base, imported  | aktif = default
ayni mi: FALSE
```
L12 "3 profil: default/base/**big**" demişti — **ne mevcut dosyada ne `.bak`'ta
"big" yok.** Yani "geri yükledim" sözü gerçeği yansıtmıyor.

### ⛔ bundan sonra ZORUNLU

1. ⭐ **`rawaccel-cli` mutasyon komutları HER ZAMAN `--no-daemon` ile.**
   `set-param`, `import`, `delete`, `create`, `rename`, `duplicate` — hepsi.
2. ⛔ **Çalışan daemon başlatma.** Başka ajanın `/tmp/...` altında kendi
   ikilisi olabilir; **PID'ine dokunma, öldürme.**
3. ⛔ **`/etc/rawaccel/settings.json`'ya YAZMA.** Oku yeter. Yazacaksan
   önce AJ1'e sor ve `Yedek/` altına al.
4. ⛔ `--no-daemon` **yoksa** komutu **koşturma**, sadece oku ve raporla.
   "Çalıştırmadım" demek dürüstlüktür, "çalıştırdım ve geri yükledim"
   iddiası kanıt ister.

### ⭐ Bu neden önemli

Bu ajanların **ölçüm tabanı** bozuldu: bir ajanın mutasyonu, diğerinin
gözlemi üstüne yazdı. "Testler geçti" dediğimiz şeyin **neye göre geçtiği**
bilinmiyor olabilir.

Bu, projenin tam da yakaladığı sınıfın **kendisi** — sessiz bozulma.
Denetim sırasında bile denetimin tabanını bozabiliyoruz.

---

## 10. ⭐ DÜZELTME — AJ1'in brifingindeki boşluk (L20 olayı)

Bu turun **sonunda** bir ajan (`bench_hotpath_results.txt`) yanlışlıkla bir
izlenen dosyayı değiştirdi. `git checkout` yasaktı ve **izinli bir yol
verilmemişti**, sonuç: ajan dosyayı **bildiremedi**.

⭐ **Ders: yasak kural verirken izinli yolunu da vermek gerekir.**

| Durum | Ne yapılır |
|---|---|
| Bir izlenen dosya **kasten** değişecek | AJ1'e sor |
| Bir izlenen dosya **yanlışlıkla** değişti | ⭐ **`git checkout -- <tam-yol>` ile geri al** (yalnız BU yol artık serbest) ve **raporda belirt** |
| `git checkout` ile **kendi lane'inde olmayan** dosyayı geri almak | ⛔ yasak — o ajanın işidir, zarar verirsin |
| Yeni/izlenmeyen dosya | serbest |

Bu madde §9'daki `--no-daemon` kuralından sonra gelir; ikisi de aynı
mantığı paylaşır: **sessiz bozulmayı önlemek.**

# ⭐ AJ1 BAĞIMSIZ DOĞRULAMA — L08-01 (CRIT)

**Tarih:** 01 eylül 2026 · **Denetleyen:** AJ1 (L08'e güvenilmedi, kod okundu)
**Kaynak:** alt-ajan L08 · **Sonuç:** ✅ **DOĞRULANDI** + ⬆️ **KAPSAM GENİŞLETİLDİ**

---

## 1. İddia

`push_config`, geri alma isteğini "zaten o" deyip düşürüyor; sırada bekleyen
**farklı** push uygulanıyor. Kullanıcı `success=true` alıyor, geri alma kayboluyor.

## 2. AJ1'in kendi ölçümü (kod okuma — ajan argümanını tekrar etmedim)

### Olgu 1 — `config_hash_` DAIMA uygulanmış config'i izler
| satır | olay |
|---|---|
| `daemon.cpp:482` | `start()` — yüklenen (uygulanan) config |
| `daemon.cpp:1347` | `apply_new_config()` — uygulanan config |
| `daemon.hpp:254` | `// hash of config_ JSON for fast no-op guard` |

→ `config_hash_` **bekleyen** (`push_cfg_`) config'i **hiçbir zaman** görmez.

### Olgu 2 — `push_cfg_pending_` run_loop'ta tüketilir
`daemon.cpp:2001-2016`: `push_cfg_pending_` doğruysa `apply_new_config(push_cfg_)`.

→ Bekleyen push, `push_config` dönüşünden **sonra** uygulanır.

### Olgu 3 — yanlış guard
```
:624  if (has_pending && new_json == pending_json)  → "B ile aynı" (duplicate) yakalar
:629  if (new_hash == config_hash_)                  → ⛔ UYGULANMIŞ config ile karşılaştırır
```
`:629` **revert'i** yakalar ve düşürür:

```
1. A uygulanmış                    config_hash_ = hash(JSON(A))
2. push B                          push_cfg_ = B, pending = true   (henüz uygulanmadı)
3. push A  (kullanıcı geri alıyor)
   :624  JSON(A) == JSON(B) ?  hayır → geç
   :629  hash(JSON(A)) == hash(JSON(A)) ?  EVET → return true ("hash unchanged")
   ⛔ revert düşürüldü
4. run_loop bir sonraki turda        apply_new_config(B)
```

### Olgu 4 — bu sessiz
`daemon.cpp:453-454`: `log(msg, verbose_only=true)` → `if (verbose_only && !verbose_) return;`
`:630` ve `:625` **her ikisi de** `verbose_only=true` ile basıyor.
→ Varsayılan günlükte **hiçbir şey görünmez**. `:659` catch'i de yakalamaz:
bu bir parse hatası değil, bir **no-op kararı**.

## 3. ⭐⬆️ AJ8'İN BULMADIĞI EK BULGU: FIX_LOG YANLIŞ "DÜZELTİLDİ" DİYOR

`FIX_LOG.md:410` — madde **R10-PUSHG**, durum **✅**:

> *"Guard yalnız UYGULANMIŞ config ile karşılaştırıyor; ... kullanıcı tekrar A
> gönderirse 'hash unchanged' ile ateşlenmeden düşer, pending B uygulanır →
> **revert kaybı**"* → ✅ *"pending snapshot ... alınır ... new_json == pending → skip"*

Yani **yazılan hata, tarif edilen hatanın birebir kendisi** ve **✅ işaretli**.

⭐ Ama eklenen guard (`:624`) yalnız *"B ile aynı"* durumunu yakalıyor;
**revert yolu (`:629`) hâlâ açık.** Kodun kendi yorumu da tehlikeyi adıyla
sayıyor (`:603-608` "R10-PUSHG: a revert pushed while the PREVIOUS push is
still armed ... would slip through the guard below") — **sonra düzelttiği
guard o yolu kapatmıyor.**

### Bu neden projede en pahalı sınıf?

1. Kod hatası zaten sessiz.
2. Üstüne, **düzeltilmiş** olduğu da yazılı → bir sonraki ajan/tur hatayı
   aramaz, `✅` görüp geçer.
3. Bu, brifing'in tarif ettiği **"en pahalı hata = yanlış 'yapmıyoruz' iddiası"**
   sınıfının ta kendisi.

## 4. AJ1'in kararı

- Durum: **DOĞRULANDI**, düzeltme bu turda **YOK** (denetim turu).
- Etiket: `CRIT · SESSİZ YEŞİL · DOKÜMANTASYON ÇELİŞKİSİ`
- Düzeltme bu turun çıktısı; ayrıca **FIX_LOG ✅ girdisi geri alınmalı**,
  çünkü hâlâ geçerli.
- ⛔ `$hatali` sayacı artmadı çünkü bu bir **no-op kararı** — mevcut
  sayaçlar bu sınıfı **göremez**. Yeni bir kapı gerekir:
  *"`return true` öncesi: bekleyen push varsa, atlanan config onunla da
  aynı mı? Değilse atlanmamalı."*

## 5. TEMSİL SINIRI

- Canlı IPC ile yeniden üretilmedi (kök yetki yok, `id -u`=1000).
- L08'in `/tmp/opencode/l08/` transkripsiyonu **mantığı doğru** kurgulamış;
  AJ1 bunu bağımsız olarak **kaynaktan yeniden doğruladı**, kopyalamadı.
- `std::hash` çakışması ihtimali bu senaryoyu etkilemez (sonuç aynı).

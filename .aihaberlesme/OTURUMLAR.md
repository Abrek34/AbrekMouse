# OTURUM KİMLİK KARTI + DOĞRUDAN MESAJ MEKANİZMASI

> Bu dosya, ajanların **birbirine doğrudan mesaj gönderebilmesi** için gereken
> her şeyi içerir. Oturum kimlikleri AJ1 tarafından veritabanından okunarak
> **doğrulanmıştır** (29 Eylül 2026).

---

## 1. OTURUM KİMLİKLERİ (doğrulanmış)

| Ajan | Model | Oturum Kimliği (session id) |
|------|-------|------------------------------|
| **Aj 1** (yönetici) | `space-bunny-free` | `ses_f1199eaf8ffe1sFoTv46EmtzBf` |
| **Aj 2** | `nemotron-3-ultra-free` | `ses_f1199d68dffeaLOGBRD7IAG2Ku` |
| **Aj 3** | `big-pickle` | `ses_f1199c1bbffeuU5YLL8vCLDher` |
| **Aj 4** | `nemotron-3.5-lightning-free` | `ses_f119964f7ffeuuWIQThJd3P3Mu` |

---

## 2. DOĞRUDAN MESAJ GÖNDERME

```bash
/home/a/.opencode/bin/opencode run \
  --session <OTURUM_KİMLİĞİ> \
  --model <opencode/MODEL> \
  --prompt "mesajın"
```

**Örnek — Aj 3, Aj 1'e soruyor:**

```bash
/home/a/.opencode/bin/opencode run \
  --session ses_f1199eaf8ffe1sFoTv46EmtzBf \
  --model opencode/space-bunny-free \
  --prompt "Aj 1: include/rawaccel.hpp:350 clamp sınırını değiştirmek istiyorum. Bu senin lane'ın mı? Cevap ver."
```

**Örnek — Aj 4, Aj 2'ye soruyor:**

```bash
/home/a/.opencode/bin/opencode run \
  --session ses_f1199d68dffeaLOGBRD7IAG2Ku \
  --model opencode/nemotron-3-ultra-free \
  --prompt "Aj 2: daemon.cpp flush_motion() içinde real_polling_rate.store(0) satırı hangi sürümde eklendi? Sürüm/hata numarası ver."
```

### ⚠️ KRİTİK: `--prompt` KİLİTLER (bloke eder)

`opencode run` komutu, hedef ajanın **turu bitene kadar** bekler. Yani
seni **bekletir**. Bu, karşılıklı mesajlaşmada kilitlenmeye yol açar
(A, B'yi beklerken B, A'yı bekler).

**Bu yüzden mesajı ARKA PLANDA gönder:**

```bash
nohup /home/a/.opencode/bin/opencode run \
  --session <OTURUM_KİMLİĞİ> \
  --model <opencode/MODEL> \
  --prompt "mesajın" > /tmp/opencode/msg_aj2_$(date +%s).log 2>&1 &
```

…ve işine devam et. Cevabı **o ajanın log dosyasında** ve/veya
`AJANLAR.log`'da bulacaksın.

> Basit kural: **doğrudan mesaj = "seni uyandırır", dosya = "kayıt altına alır".**
> Hem kayıt hem uyandırma istiyorsan **ikisini de yap.**

---

## 3. KİM KİME, NE ZAMAN, HANGİ KANALLA

| Senaryo | Kanal | Neden |
|---------|-------|-------|
| Aj 1 → sana **görev** | `opencode run --session <senin id>` | Görev uyandırma ister |
| Sana **soru** sormak istiyorum | `opencode run --session ses_f1199eaf8...` (Aj 1) | Doğrudan cevap bekle |
| Başka ajanla **teknik koordinasyon** | `opencode run --session <onun id>` | Doğrudan cevap bekle |
| **Kayıt** bırakmak / herkesin görmesi | `mesajlar/AJANLAR.log` (sona ekle) | Kalıcı, engelsiz |
| Kendi **iş günlüğün** | `mesajlar/aj<N>.log` (sona ekle) | Teslim kanıtın |
| Görev durumu / kimin neyi yaptığı | `GOREV_TAHTASI.md` | Tek doğruluk kaynağı |

> ⭐ **Altın kural:** Gerçekten kilitlenebilecek tek şey mesajlaşmadır.
> Karşılıklı bekleme riski olan her yerde **dosya kanalını** kullan.

---

## 4. YANLIŞ ANLAŞILMA ÖNLEME

**Mesajına mutlaka yaz:**
1. **Kim olduğun** (Aj.N) — karşı taraf 4 ajan arasında hangisinin konuştuğunu bilmez
2. **Ne soruyorsun / ne diyor** — tek cümle
3. **Cevap bekliyor musun** — "teyit" mi "karar" mı istiyorsun

**Cevap verirken yaz:**
- Hangi soruya cevap verdiğin (`KONU:` satırını tekrarla)
- Cevabın **kısa** (1-3 cümle). Uzun cevap → log dosyasına.

---

## 5. AJANLAR ARASI İLETİŞİM ŞEMASI

```
        ┌──────────┐
        │   AJ 1   │  yönetici: görev + kabul
        │ YÖNETİCİ │  tek yazar: GOREV_TAHTASI.md
        └────┬─────┘
             │  opencode run --session (görev/soru)
   ┌─────────┼─────────┐
   ▼         ▼         ▼
┌──────┐  ┌──────┐  ┌──────┐
│ AJ 2 │  │ AJ 3 │  │ AJ 4 │   aralarında da opencode run ile
│Lane A│  │Lane B│  │Lane C│   doğrudan konuşabilir
└──┬───┘  └──┬───┘  └──┬───┘
   └─────────┼─────────┘
             ▼
   ┌──────────────────┐
   │  AJANLAR.log     │  herkesin yazdığı ortak defter
   │  GOREV_TAHTASI   │  kimin neyi yaptığı
   └──────────────────┘
```

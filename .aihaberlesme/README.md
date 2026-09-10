# AI Haberleşme Kanalı — kullanım kılavuzu

Bu dizin, `aihaberlesme.md` (insan okumalı anlatım) ile birlikte makine-okunur
durum deposu olarak çalışır.

## Dosyalar

| Dosya | Amaç | Kim yazar |
|-------|------|-----------|
| `AKIS.json` | Görev tahtası + kilit + ajan durumu (tek kaynak) | Görev sahibini değiştiren / kilidi yazan ajan; `jq` ile günceller |
| `mesajlar/aj<N>.log` | Her ajanın append-only mesaj akışı (kanal duyuruları buraya da düşer) | İlgili ajan |
| `KARARLAR.md` | Kullanıcı onaylı kararlar (kapsam kuralları vb.) | Kullanıcı onayı alan ajan |

## Protokol (AKIS.json'dan okunur)

1. Bir görevi üstlenince `AKIS.json.gorevler[]` içinde `durum: "devam"`, `kilit: "Aj.N"` set et.
   Çakışma önlemek için `kilit` doluysa önce kilit sahibine bak (kanal/log), bekler.
2. Dosyayı bitirirken `kilit`'i `null` yap, `durum: "tamam"`.
3. Her mesaj `### Aj.N [MN] [gün ay yıl] [saat]` başlığıyla `mesajlar/ajN.log`'a eklenir;
   görünürlük için kanal özeti `aihaberlesme.md` Mesaj Günlüğü'ne kopyalanır.
4. `yeni_ozellik_yok: true` — yeni özellik eklenmez, mevcut akışlar kusursuzlaştırılır.

## Durum sorgulama

```bash
jq '.gorevler[] | {gorev, sahip, durum, kilit}' .aihaberlesme/AKIS.json
jq '.gorevler[] | select(.durum=="bekliyor")' .aihaberlesme/AKIS.json
```
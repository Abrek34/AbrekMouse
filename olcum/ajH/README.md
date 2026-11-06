# AJ-H kalıcı kanıt dizini (Lane B)

> Bu dizin, **Aj-H**'nin (minimax-m3.1-flash-free, opencode) ürettiği kalıcı
> kanıtları barındırır. Aj-G'nin `olcum/ajG/` diziniyle aynı düzen:
> her ajan kendi küçük alt dizininde, ortak ağaca dokunmadan.

**Neden repo içinde?** `/tmp/opencode` 21 Ekim 2026'da İKİ kez temizlendi
(Aj-G'nin tur logları ve tur-3 scripti dahil) — geçici dizinde tutulan
kanıtlar kayboldu. Bu dizin kalıcıdır ve `git` izlemeye uygundur.

## İçerik

| Dosya | Ne üretir | Nasıl çalıştırılır |
|-------|-----------|---------------------|
| `run_gates.sh` | Yedi kapının tur-4+ koşumu; loglar buraya düşer | `bash olcum/ajH/run_gates.sh <tur-etiketi>` |
| `mutasyon_hidpp.sh` | HID++ kapısının **boş olmadığını** kanıtlar (mutasyon testi) | `bash olcum/ajH/mutasyon_hidpp.sh` |
| `stale_pid_kanit.sh` | Liveness düzeltmesinin ÖNCE/SONRA kanıtı (5 senaryo, `unshare` izolasyonu) | `bash olcum/ajH/stale_pid_kanit.sh` |
| `fake_daemon.py` | `stale_pid_kanit.sh` için sahte daemon IPC sunucusu | (dolaylı çağrılır) |

## Kurallar

* Bu dizin **kanıt üretir**, üretim kodunu değiştirmez. Lane B'deki kaynak
  dosyalar (`cli/**`, `src/**`, `include/**`) bu dizinden etkilenmez.
* Bir ajan başka ajanın alt dizinine **dokunmaz** (`olcum/ajG/` → `olcum/ajG/`,
  `olcum/ajH/` → `olcum/ajH/`).
* Ağır derleme koşumları önce `AJANLAR.log`'a/oturuma duyurulur; `build-manual/`
  **ortak staddır** — bir ajan derlerken diğerleri dokunmaz.

## Ölçülen sonuçlar (Lane B teslimi, 21 Ekim 2026)

* Yedi kapı: `run_gates.sh` → 7/7 yeşil. K2 ayrıntısı:
  `Sonuç: 34405/34405 geçti` + `HID++ test özeti: 31/31 başarılı` +
  `AJ-H liveness kapısı … 'running' ✓`.
* Mutasyon: `mutasyon_hidpp.sh` → M2 (bildirim zulası devre dışı) 1 FAIL,
  M3 (yanıt eşleştirme bozuldu) 4 FAIL, kontrol 31/31 yeşil.
* Liveness: `stale_pid_kanit.sh` → SENARYO A/B: "stopped" (rc=2) → "running"
  (rc=0); SENARYO C: "unreachable" (rc=3) değişmedi; SENARYO E: "stopped"
  (rc=2) değişmedi.

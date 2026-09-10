# KARARLAR — Kullanıcı Onaylı Kararlar

Bu dosya, kullanıcıdan alınan onaylı kararları kanonik biçimde tutar.
AKIS.json ile uyumludur; her karar tarih + karar + etki içerir.

## R54-T1 (2026-09-08) — 6 ajan paralel bug-avı + düzeltme

**Karar:** "Herkes aynı anda hata arayıp düzeltsin; 6 kişi programı hatasız hale getirsin."
6 ajan (Aj1 yönetici + Aj2..Aj6) TÜMÜNÜN kendi sahasında aynı anda bug arayıp düzeltecekleri paralel bug-avı turu başlatıldı.

**Etki / görev dağılımı (çakışma yok — her dosyanın tek sahibi):**

| Ajan | Görev | Kapsam (dosya sahipliği) |
|------|-------|--------------------------|
| Aj1 (yönetici+bug-avcısı) | P165 — gui/hidpp_panel.inl satır-satır bug-avı + kabul kapısı | `gui/hidpp_panel.inl` + koordinasyon |
| Aj2 (QA/güvenlik) | P160 — include/ + src/logitech_hidpp.cpp + tüm kapılar | `include/`, `src/logitech_hidpp.cpp` + kapı koşuları |
| Aj3 (özellik/UX/çeviri) | P161 — gui bug-avı + dokümantasyon | `gui/` (hidpp_panel.inl hariç) + README/CHANGELOG/docs |
| Aj4 (oracle/paketleme) | P162 — paket + oracle bug-avı | `packaging/`, `tests/oracle/` |
| Aj5 (çapraz-kontrol) | P163 — cli bug-avı | `cli/` |
| Aj6 (sistem/IPC) | P164 — daemon/setup/scripts + canlı | `daemon/`, `setup.sh`, `scripts/` |

**Kurallar:**
- R54 ortasında COMMIT YOK; değişiklikler working tree'de birikir, tur sonunda Aj1 tek toplu commit atar.
- Çakışma: aynı anda tek ajan tek dosya. Her dosyanın sahibi yukarıdaki tablocadır; dokunmadan önce `kilit: Aj.N` AKIS'e işlenir, bitince null.
- "programı hatasız yap" hedefi: her fix, kendi sahasında kapı ispatıyla (build 0 uyarı + tr_coverage + ilgili test) kapatılır.
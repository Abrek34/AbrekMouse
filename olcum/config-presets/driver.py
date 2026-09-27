#!/usr/bin/env python3
"""AJ4 · config-presets ölçüm sürücüsü.

AJ1 görev metninin üç sorusunu çalıştırır ve ham çıktıyı `sonuc.txt`'ye yazar.
Python burada YALNIZCA test/ölçüm aracıdır (PROGRAM_SOZLESMESI §0.1-1);
üretim kodu C++'tır.

Kurallar:
  * LC_ALL=C (§0.2) — bu betik regex'i Python `re` ile çalıştırır (locale'den
    bağımsız, doğrulandı) ama alt süreçlere de C locale verir.
  * /tmp/opencode geçicidir (§0.3) — dosyayı ölçmeden ÖNCE varlık + satır
    sayısı doğrulanır; probe da kendi içinde aynı kontrolü yapar.
  * PC (§4) — araç kendi kendine hata diyemezse ölçüm GEÇERSİZ sayılır.

Kullanım:
  python3 olcum/config-presets/driver.py            # her şey
  python3 olcum/config-presets/driver.py --sadece-pc
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

BUR = Path(__file__).resolve().parent
KOK = BUR.parents[1]
PROBE = KOK / "build-manual" / "cfg_probe"
SONUC = BUR / "sonuc.txt"

CIKTI: list[str] = []
PC_HATASI = 0


def yaz(s: str = "") -> None:
    print(s)
    CIKTI.append(s)


def derle() -> None:
    cmd = [
        os.environ.get("CXX", "g++"), "-std=c++20", "-O2", "-Wall", "-Wextra",
        "-I", str(KOK / "include"), "-I", str(KOK / "src"),
        str(BUR / "probe.cpp"), str(KOK / "src" / "config.cpp"),
        "-o", str(PROBE),
    ]
    p = subprocess.run(cmd, capture_output=True, text=True,
                       env={**os.environ, "LC_ALL": "C"})
    if p.returncode != 0:
        yaz(f"DERLEME HATASI:\n{p.stdout}{p.stderr}")
        sys.exit(2)
    uyari = [l for l in (p.stdout + p.stderr).splitlines() if l.strip()]
    yaz(f"[derleme] exit=0 uyari={len(uyari)}")
    for l in uyari:
        yaz(f"  {l}")


def calistir(vaka: str, arg: str | None = None) -> dict:
    """Tek bir vakayı ayrı alt süreçte çalıştır (çöküş izolasyonu)."""
    cmd = [str(PROBE), vaka] + ([arg] if arg else [])
    t0 = time.monotonic()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=60,
                           env={**os.environ, "LC_ALL": "C"})
    except subprocess.TimeoutExpired:
        return {"vaka": vaka, "durum": "TIMEOUT", "satirlar": [],
                "ham": "", "rc": -99, "sure": time.monotonic() - t0}
    sure = time.monotonic() - t0
    satirlar = [l for l in p.stdout.splitlines() if l.startswith("RESULT ")]
    if p.returncode < 0:
        durum = f"ÇÖKÜŞ (sinyal {-p.returncode})"
    elif p.returncode == 2:
        durum = "BİLİNMEYEN VAKA"
    else:
        durum = "OK"
        for l in satirlar:
            par = l.split(" ", 3)
            if len(par) >= 3 and par[2] == "FAIL":
                durum = "FAIL"
    return {"vaka": vaka, "durum": durum, "satirlar": satirlar,
            "ham": p.stdout + p.stderr, "rc": p.returncode, "sure": sure}


# ── PC §4: araç gerçekten arıza görüyor mu? ───────────────────────────────────
def pozitif_kontrol() -> bool:
    global PC_HATASI
    yaz("\n=== POZITIF KONTROL (§4) ===")

    # PC-1: araç ÇÖKÜŞÜ algılamalı.  selftest-segv sinyalle ölür; sürücü
    # "OK" derse çöküş algılama YOKTUR ve tüm ölçüm geçersizdir.
    r = calistir("selftest-segv")
    pc1 = r["durum"].startswith("ÇÖKÜŞ")
    yaz(f"PC-1 çöküş algılama : {'GEÇTİ' if pc1 else 'KIRILDI'}"
        f"  (vaka=selftest-segv durum={r['durum']})")
    if not pc1:
        PC_HATASI += 1

    # PC-2: araç FAIL yolunu görmeli.  selftest-diff kasıtlı FAIL basar;
    # sürücü "OK" derse sonuç filtresi çalışmıyordur.
    r = calistir("selftest-diff")
    pc2 = r["durum"] == "FAIL"
    yaz(f"PC-2 FAIL algılama  : {'GEÇTİ' if pc2 else 'KIRILDI'}"
        f"  (vaka=selftest-diff durum={r['durum']})")
    if not pc2:
        PC_HATASI += 1

    # PC-3: /tmp/opencode denetimi — probe orada dosya üretiyor.  Dosya YOKSA
    # "0 fark" sahte sıfır üretir (ölçülmüş AJ2 hatası).  Burada bilerek bir
    # imkânsız dosyayı okumayı deneriz; araç "DOSYA YOK" demek ZORUNDADIR.
    r = calistir("rt-file", "/tmp/opencode/ASLA_VAR_OLMAYAN-dosya.json")
    pc3 = r["durum"] == "FAIL" and "DOSYA YOK" in r["ham"]
    yaz(f"PC-3 kaynak doğrulama: {'GEÇTİ' if pc3 else 'KIRILDI'}"
        f"  (vaka=rt-file<maydosya> durum={r['durum']})")
    if not pc3:
        PC_HATASI += 1

    yaz(f"PC sonucu: {3 - PC_HATASI}/3 geçti"
        + ("  → ÖLÇÜM GEÇERLİ" if PC_HATASI == 0 else "  → ÖLÇÜM GEÇERSİZ"))
    return PC_HATASI == 0


# ── yapısal denetim: struct alanları ↔ serialize / deserialize ───────────────
ALIAS = {
    "accel_args": {"cap_mode_val": "cap_mode", "length": "lut_length",
                   "data": "lut_data"},
    "profile": {"speed_processor_args": "speed_processor"},
    "device_profile": {"dev_cfg": "dpi,polling_rate,disable", "prof": "profile"},
    "app_config": {"profiles": "profiles"},
}


def blok_adi(src: str, ad: str) -> str | None:
    """`static json <ad>(...)` / `static <tip> <ad>(...)` gövdesini bulur."""
    m = re.search(r"^[^\n]*\b" + re.escape(ad) + r"\s*\([^;{]*\)\s*\{", src, re.M)
    if not m:
        return None
    i = m.end() - 1
    d = 0
    for k in range(i, len(src)):
        if src[k] == "{":
            d += 1
        elif src[k] == "}":
            d -= 1
            if d == 0:
                return src[m.start():k + 1]
    return None


def alanlar(hpp: str, strukt: str) -> list[str]:
    m = re.search(r"struct\s+" + re.escape(strukt) + r"\s*\{(.*?)\n\};", hpp, re.S)
    if not m:
        return []
    govde = m.group(1)
    out = []
    for satir in govde.splitlines():
        s = satir.split("//")[0].strip()
        if not s or s.startswith(("static", "using", "friend", "operator")):
            continue
        # Üyelik fonksiyonu GÖVDESİ alan listesine sızmasın: `return true;`
        # satırı "true" adında SAHTE bir alan üretiyordu (ölçüm aracı hatası —
        # §7).  Bildirge satırları değil, deyim satırları elenir.
        if re.match(r"^\s*(return|if|for|while|switch|else|break|continue|throw|do|try|catch)\b", satir):
            continue
        if "operator" in s:
            continue
        mm = re.match(r"^[A-Za-z_][\w:<>,\s\*&]*?\s+([A-Za-z_]\w*)\s*(?:=|\[|;)", s)
        if mm:
            out.append(mm.group(1))
    return out


def yapisal_denetim() -> int:
    yaz("\n=== YAPISAL DENETIM: struct alani ↔ serialize / deserialize ===")
    base = (KOK / "include" / "rawaccel-base.hpp").read_text(encoding="utf-8")
    cfg = (KOK / "include" / "config.hpp").read_text(encoding="utf-8")
    cpp = (KOK / "src" / "config.cpp").read_text(encoding="utf-8")

    esleme = [
        ("accel_args", base, "accel_args", "accel_args_to_json", "accel_args_from_json"),
        ("speed_args", base, "speed_args", None, None),  # profile içinde elle
        ("profile", base, "profile", "profile_to_json_obj", "profile_from_json_obj"),
        ("device_config", cfg, "device_config", "device_profile_to_json", "device_profile_from_json"),
        ("device_profile", cfg, "device_profile", "device_profile_to_json", "device_profile_from_json"),
        ("app_config", cfg, "app_config", "app_config_to_json_obj", "app_config_from_json_obj"),
    ]
    sorun = 0
    for ad, src, strukt, yazan, okuyan in esleme:
        alanlar_list = alanlar(src, strukt)
        if not alanlar_list:
            yaz(f"  {ad}: OKUMA HATASI — struct bulunamadı (PC gereği FAIL sayılır)")
            sorun += 1
            continue
        ser_govde = blok_adi(cpp, yazan) if yazan else ""
        de_govde = blok_adi(cpp, okuyan) if okuyan else ""
        if yazan and ser_govde is None:
            yaz(f"  {ad}: SERIALIZER BULUNAMADI: {yazan} (PC gereği FAIL sayılır)")
            sorun += 1
            continue
        if okuyan and de_govde is None:
            yaz(f"  {ad}: DESERIALIZER BULUNAMADI: {okuyan} (PC gereği FAIL sayılır)")
            sorun += 1
            continue
        # profil iç içe olduğu için accel/speed alt denetimleri ayrıca
        yok_yaz, yok_oku = [], []
        for a in alanlar_list:
            anahtar = ALIAS.get(strukt, {}).get(a, a)
            for k in anahtar.split(","):
                k = k.strip()
                if not k:
                    continue
                if ser_govde and f'["{k}"]' not in ser_govde and f'"{k}"' not in ser_govde:
                    yok_yaz.append(f"{a}→{k}")
                # OKUYUCU denetimi: require_number gibi sarmalayıcılar anahtarı
                # DEĞİŞKEN olarak geçer (`j.contains(key)`), bu yüzden literal
                # `contains("x")` aramak 12 alan için sahte "yok" üretirdi.
                # Kural: anahtar gövdede HİÇ geçmiyorsa okuma yolu yoktur.
                if de_govde and f'"{k}"' not in de_govde:
                    yok_oku.append(f"{a}→{k}")
        if yok_yaz:
            yaz(f"  {ad}: YAZMA ANAHTARI YOK   : {', '.join(yok_yaz)}")
            sorun += 1
        if yok_oku:
            yaz(f"  {ad}: OKUMA ANAHTARI YOK   : {', '.join(yok_oku)}")
            sorun += 1
        if not yok_yaz and not yok_oku:
            yaz(f"  {ad}: temiz ({len(alanlar_list)} alan)")
    # profil'in içindeki speed_args + accel_args alt denetimi
    prof_gov = blok_adi(cpp, "profile_to_json_obj") or ""
    prof_de = blok_adi(cpp, "profile_from_json_obj") or ""
    alt = alanlar(base, "speed_args")
    for a in alt:
        if f'sp.{a}' not in prof_gov:
            yaz(f"  speed_args: YAZMA ANAHTARI YOK: {a}")
            sorun += 1
        if f'contains("{a}")' not in prof_de:
            yaz(f"  speed_args: OKUMA ANAHTARI YOK: {a}")
            sorun += 1
    yaz(f"yapisal denetim sonucu: {sorun} sorun")
    return sorun


# ── yapısal denetim PC'si: bir serialize anahtarını bilerek silelim ──────────
def yapisal_pc() -> bool:
    """config.cpp'nin KOPYASI üzerinde `j["limit"]` satırını siler, denetim
    bunu YAKALAMALI.  Üretim dosyasına dokunulmaz."""
    kaynak = KOK / "src" / "config.cpp"
    metin = kaynak.read_text(encoding="utf-8")
    hedef = 'j["limit"]            = a.limit;'
    if hedef not in metin:
        yaz("PC-4 KIRILDI: enjeksiyon hedefi bulunamadı (kod mu değişti?)")
        return False
    bozuk = metin.replace(hedef, "// PC: bilerek silindi", 1)
    gecici = BUR / "_pc_kopya_config.cpp"
    gecici.write_text(bozuk, encoding="utf-8")
    try:
        cmd = [os.environ.get("CXX", "g++"), "-std=c++20", "-O2", "-w",
               "-I", str(KOK / "include"), "-I", str(KOK / "src"),
               str(BUR / "probe.cpp"), str(gecici), "-o", str(KOK / "build-manual" / "_pc_probe")]
        p = subprocess.run(cmd, capture_output=True, text=True,
                           env={**os.environ, "LC_ALL": "C"})
        if p.returncode != 0:
            yaz("PC-4 KIRILDI: bozuk kopya derlenemedi")
            yaz(p.stderr[:800])
            return False
        # limit bilerek yazılmayınca round-trip farkı ÇIKMALI (alan değeri
        # default'a döner) — denetleyici hem bayt farkını hem alan adını görmeli.
        r = subprocess.run([str(KOK / "build-manual" / "_pc_probe"), "rt-preset-gaming"],
                           capture_output=True, text=True,
                           env={**os.environ, "LC_ALL": "C"})
        algilandi = "FAIL" in r.stdout and "limit" in r.stdout
        yaz(f"PC-4 serialize enjeksiyonu: {'GEÇTİ' if algilandi else 'KIRILDI'}"
            f"  (rc={r.returncode})")
        if r.stdout.strip():
            for l in r.stdout.splitlines()[:3]:
                yaz(f"    {l[:220]}")
        return algilandi
    finally:
        gecici.unlink(missing_ok=True)
        (KOK / "build-manual" / "_pc_probe").unlink(missing_ok=True)


# ── ana koşu ──────────────────────────────────────────────────────────────────
def main() -> int:
    global PC_HATASI
    yaz(f"# AJ4 config-presets ölçümü — {time.strftime('%Y-%m-%d %H:%M:%S')}")
    yaz(f"HEAD: {subprocess.run(['git','rev-parse','--short','HEAD'],cwd=KOK,
                               capture_output=True,text=True).stdout.strip()}")
    yaz(f"LANG={os.environ.get('LANG','?')}  LC_ALL={os.environ.get('LC_ALL','(yok)')}")

    derle()
    if not PROBE.exists():
        yaz("PROBE YOK — ölçüm çalışmadı")
        return 2

    sadece_pc = "--sadece-pc" in sys.argv
    if not pozitif_kontrol():
        SONUC.write_text("\n".join(CIKTI) + "\n", encoding="utf-8")
        return 3
    pc4 = yapisal_pc()
    yaz(f"PC-4 yapısal denetim enjeksiyonu: {'GEÇTİ' if pc4 else 'KIRILDI'}")
    if not pc4:
        PC_HATASI += 1

    yapissal = yapisal_denetim()
    if sadece_pc:
        SONUC.write_text("\n".join(CIKTI) + "\n", encoding="utf-8")
        return 0

    vakalar = subprocess.run([str(PROBE), "list"], capture_output=True, text=True,
                             env={**os.environ, "LC_ALL": "C"}).stdout.split()
    # selftest-* = KALICI POZİTİF KONTROLLER; zaten pozitif_kontrol() içinde
    # beklendikleri biçimde (ÇÖKÜŞ / FAIL) doğrulandı.  Ana koşudan çıkarılmazsa
    # kendi kendilerini "başarısız" sayar ve özet yanıltır olurdu.
    pc_vakalar = [v for v in vakalar if v.startswith("selftest-")]
    vakalar = [v for v in vakalar if not v.startswith("selftest-")]
    yaz(f"PC vakaları (ana koşudan çıkarıldı, PC bölümünde doğrulandı): "
        f"{', '.join(pc_vakalar)}")

    # gerçek config dosyaları (varlıklarını ÖNCE doğrula — §0.3)
    gercek = []
    for p in [KOK / "config" / "default.json",
              Path.home() / ".config/rawaccel/settings.json"]:
        if p.exists() and p.stat().st_size > 0:
            gercek.append(p)
    if not gercek:
        yaz("\nUYARI: gerçek config dosyası bulunamadı (§0.3 — sahte sıfır riski)")

    yaz("\n=== VAKALAR ===")
    sonuc_say: dict[str, int] = {"OK": 0, "FAIL": 0}
    detaylar: list[str] = []
    for v in vakalar:
        r = calistir(v)
        etiket = r["durum"]
        sonuc_say[etiket] = sonuc_say.get(etiket, 0) + 1
        ilk = r["satirlar"][0] if r["satirlar"] else "(sonuç satırı yok)"
        detaylar.append(f"{etiket:12} {v:28} {ilk[:200]}")
    for g in gercek:
        r = calistir("rt-file", str(g))
        etiket = r["durum"]
        sonuc_say[etiket] = sonuc_say.get(etiket, 0) + 1
        ilk = r["satirlar"][0] if r["satirlar"] else "(sonuç satırı yok)"
        detaylar.append(f"{etiket:12} {'rt-file:' + g.name:28} {ilk[:200]}")

    for d in detaylar:
        yaz(d)

    yaz("\n=== ÖZET ===")
    for k in sorted(sonuc_say):
        yaz(f"  {k}: {sonuc_say[k]}")
    yaz(f"  yapısal sorun: {yapissal}")
    yaz(f"  PC: {3 + 1 - PC_HATASI}/4 geçti")

    # §0.3: /tmp/opencode kalıcı DEĞİL — üretim çıktımız burada değil,
    # olcum/config-presets/sonuc.txt'de. Yine de doğrula.
    assert SONUC.parent.exists()

    toplam_vaka = len(vakalar) + len(gercek)
    basarisiz = sum(v for k, v in sonuc_say.items() if k != "OK")
    yaz(f"\nTOPLAM vaka={toplam_vaka}  başarısız={basarisiz}")

    with open(SONUC, "w", encoding="utf-8") as f:
        f.write("\n".join(CIKTI) + "\n")
    # satır sayısı doğrulaması (§0.3)
    n = len(SONUC.read_text(encoding="utf-8").splitlines())
    yaz(f"sonuc.txt yazıldı: {n} satır" + ("" if n > 0 else "  ← SATIR SAYISI BOZUK"))
    return 0 if (basarisiz == 0 and PC_HATASI == 0 and yapissal == 0) else 1


if __name__ == "__main__":
    sys.exit(main())

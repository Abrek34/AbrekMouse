#!/usr/bin/env python3
r"""AJ4 · gauge ↔ sanitize ↔ CLI tutarlılık taraması (ölçüm aracı, üretim kodu değil).

P120-FAZ2 sözleşmesi (config.hpp:12-16): sanitize üst sınırı == GUI gauge
maksimumu, "bir değer asla GUI göstergesinin izin veremeyeceği kadar büyülemesin"
diye.  İhlalin biçimi şudur:

    sanitize 1e6'ya izin veriyor, gauge max 100
      → daemon 1e6 ile çalışır, GUI yanında 100 gösterir,
      → GUI'den bir Kaydet gelince 100 yazılır (sessiz yeniden yazım).

`limit` bu bloğun TEK üst sınırsız alanıydı (AJ4-K6 ile kapatıldı).  Bu betik
aynı denetimi TÜM alanlar için mekanik olarak yapar, çünkü K6 elle fark
edildi — elle fark edilmeyenler için bir kural gerekir.

Kaynaklar (üçü de salt-okunur):
  gui/ui_builder.inl      S->widget = make_spin(min, max, ...)
  gui/widgets_sync.inl    SET_SPIN(widget, ax.alan)  /  ax.alan = SPIN(widget)
  src/config.cpp          sanitize_accel_args / sanitize_profile içindeki clamp
  include/config.hpp      SCALE_MAX / EXP_POWER_MAX / ... sabitleri
  cli/main.cpp            range_ok("k", lo, hi) / min_ok("k", lo)

Kullanım:
  python3 olcum/config-presets/gauge_scan.py
  python3 olcum/config-presets/gauge_scan.py --sadece-pc

PC (§4): DENETLEYİCİ bozulur — K6 clamp'ini taşıyan config.cpp KOPYASINDAN
silinir; tarama `limit`'i İŞARETLEMEK ZORUNDADIR.  Ve KAPSAM PC'si: ayrıştırıcı
kaynaktaki her SPIN/SET_SPIN widget'ını yakalamak zorunda (aşağıdaki
ölçüm-hatası notuna bakın).
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

BUR = Path(__file__).resolve().parent
KOK = BUR.parents[1]

CIKTI: list[str] = []
PC_HATASI = 0


def yaz(s: str = "") -> None:
    print(s)
    CIKTI.append(s)


def oku(rel: str) -> str:
    p = KOK / rel
    if not p.exists():
        raise SystemExit(f"KAYNAK YOK: {p}")
    return p.read_text(encoding="utf-8", errors="replace")


# ── 1) GUI gauge aralıkları ────────────────────────────────────────────────────
NUM = re.compile(r"^\s*([-+0-9.eE]+)\s*$")


def deger(expr: str) -> float | None:
    """make_spin argümanını sayıya çevir; semantik ifadeyse None."""
    m = NUM.match(expr)
    return float(m.group(1)) if m else None


def gauge_araliklari() -> dict[str, tuple[float | None, float | None]]:
    """widget -> (min, max)  (sayısal değilse None)."""
    src = oku("gui/ui_builder.inl")
    out: dict[str, tuple[float | None, float | None]] = {}
    for m in re.finditer(
            r"S->(\w+)\s*=\s*make_spin\(\s*([^,]+?),\s*([^,]+?),", src):
        out[m.group(1)] = (deger(m.group(2)), deger(m.group(3)))
    return out


_ON_EK = ["dp.dev_cfg.", "dp.prof.", "dp.", "ax.", "ay.", "sp.", "a.", "p."]
_HIZLI = ("a.", "ax.", "ay.")          # accel_args önekleri (bağımsız tespit)


def _temizle(ifade: str) -> str:
    """`ax.cap.x` -> `cap.x`, `dp.prof.speed_min` -> `speed_min`."""
    for p in sorted(_ON_EK, key=len, reverse=True):
        if ifade.startswith(p):
            return ifade[len(p):]
    return ifade


def _ham_yazma(src: str) -> list[tuple[str, str]]:
    """SET_SPIN(widget, alan_ifadesi) — KÖK-SEVİYE string bölme, regex yok.
    Kapsam PC'si bunu bağımsız kanıt olarak kullanır."""
    out = []
    for m in re.finditer(r"\bSET_SPIN\(([^)]*)\)", src):
        par = [x.strip() for x in m.group(1).split(",")]
        if len(par) == 2:
            out.append((par[0], par[1]))
    return out


def _ham_okuma(src: str) -> list[tuple[str, str]]:
    """alan_ifadesi = SPIN(widget)."""
    return [(m.group(2), m.group(1))
            for m in re.finditer(r"([\w.]+)\s*=\s*SPIN\(\s*(\w+)\s*\)", src)]


def widget_alan_eslesmesi() -> dict[str, str]:
    r"""widget -> alan adı.  SADECE accel_args alanları (P120-FAZ2 kapsamı):
    `a.` / `ax.` / `ay.` önekli olanlar.  device_config / profile alanları
    (dpi, rotation, halflife…) bilinçli olarak dışarıda — onların sanitize
    sözleşmeleri farklı (int clamp, {0 nöbet} ∪ [1,32000], sentinel) ve
    yanlış-pozitif üretir.

    ÖLÇÜM NOTU (2026-09-28): ilk sürümde örtü `(?:a|ay)` idi ve regex
    alternasyonu soldan denerdi — `a` eşleşip ardından `\.` `x`'e baktığı için
    X-ekseni HİÇ düşmüyordu (34 widget'ın 5'i).  PC'ler yine de GEÇİYORDU,
    çünkü PC `limit`'i arıyordu ve o `limit_spin_y` üzerinden geliyordu:
    "her şeyi ölçüyor mu" sorusu sorulmamıştı.  Bu yüzden kapsam PC'si artık
    _ham_yazma/_ham_okuma (string bölme, regex alternasyonu YOK) ile bağımsız
    ölçüyor — pc() içinde KAPSAM denetimi var.
    """
    src = oku("gui/widgets_sync.inl")
    out: dict[str, str] = {}
    for widget, ifade in _ham_yazma(src):
        if ifade.startswith(_HIZLI):
            out[widget] = _temizle(ifade)
    for widget, ifade in _ham_okuma(src):
        if ifade.startswith(_HIZLI):
            out.setdefault(widget, _temizle(ifade))
    return out


def kapsam_denetimi() -> list[str]:
    """Bağımsız kapsam: ham string-bölme ile bulunan accel widget'ları,
    ayrıştırıcının ürettiğiyle AYNI OLMALI.  Regex bir biçim değişikliğini
    kaçırırsa (bkz. `(?:a|ay)` olayı) bu denetim KIRILIR ve tablo GÖRÜNMEYEN
    alanlar hakkında sessizce yanlışdır."""
    src = oku("gui/widgets_sync.inl")
    hatalar: list[str] = []
    bagimsiz = {w for w, i in _ham_yazma(src) if i.startswith(_HIZLI)}
    bagimsiz |= {w for w, i in _ham_okuma(src) if i.startswith(_HIZLI)}
    ayrilan = set(widget_alan_eslesmesi())

    eksik = sorted(bagimsiz - ayrilan)
    if eksik:
        hatalar.append(f"ham accel widget'ları ayrıştırılamadı ({len(eksik)}): "
                       + ", ".join(eksik))
    fazla = sorted(ayrilan - bagimsiz)
    if fazla:
        hatalar.append(f"ayrıştırıcı uydurma widget üretti ({len(fazla)}): "
                       + ", ".join(fazla))
    # mutlak taban: accel için SET_SPIN/SPIN çağrısı hiç azalmamalı
    ham_yazma = sum(1 for _, i in _ham_yazma(src) if i.startswith(_HIZLI))
    ham_okuma = sum(1 for _, i in _ham_okuma(src) if i.startswith(_HIZLI))
    if ham_yazma < 10 or ham_okuma < 10:
        hatalar.append(f"ham çağrı sayısı şüpheli: yazma={ham_yazma} "
                       f"okuma={ham_okuma} (her ikisi de >=10 olmalı)")
    return hatalar


# ── 2) sanitize üst sınırları ──────────────────────────────────────────────────
def sabitler() -> dict[str, float]:
    src = oku("include/config.hpp")
    out: dict[str, float] = {}
    for m in re.finditer(r"static constexpr double\s+(\w+)\s*=\s*([^;]+);", src):
        v = deger(m.group(2))
        if v is not None:
            out[m.group(1)] = v
    return out


def coz(metin: str, sabit: dict[str, float]) -> float | None:
    m = NUM.match(metin)
    if m:
        return float(m.group(1))
    metin = metin.strip()
    return sabit.get(metin)


def sanitize_sinirlari(config_cpp: str) -> dict[str, tuple[str, float | None]]:
    """alan -> (kaynak ifade, çözülmüş üst sınır).  Sınır yoksa (None)."""
    src = config_cpp
    sabit = sabitler()
    out: dict[str, tuple[str, float | None]] = {}
    # if (a.alan > SINIR) a.alan = SINIR;
    pat = re.compile(
        r"if\s*\(\s*(?:a|p)\.([\w.]+)\s*>\s*([^)]+?)\s*\)\s*"
        r"(?:a|p)\.\1\s*=\s*([^;]+);")
    for m in pat.finditer(src):
        alan = m.group(1)
        ifade = m.group(2).strip()
        out[alan] = (ifade, coz(ifade, sabit))
    return out


# ── 3) CLI domain ──────────────────────────────────────────────────────────────
def cli_alani() -> dict[str, str]:
    """alan -> 'range lo..hi' | 'min lo' | 'YOK'.

    İLK SÜRÜM ÖLÇÜM HATASI (2026-09-28): `} else if (key == "scale") {` ile
    `range_ok(...)` arasındaki YORUM satırları regex'i kesiyordu, bu yüzden
    scale / cap_x / cap_y / output_offset / exponent_power "YOK" gorunuyordu
    — oysa elle olcumde `set-param scale 1e6` red ediliyordu.  Artik kosul
    bloglari bolunup her blog icinde araniyor.
    """
    src = oku("cli/main.cpp")
    bas = src.find("static int cmd_set_param")
    govde = src[bas:src.find("\nstatic ", bas + 10)] if bas >= 0 else src
    out: dict[str, str] = {}

    # literal formlar (tek anahtar): range_ok("anahtar", lo, hi)
    for m in re.finditer(r"range_ok\(\s*\"(\w+)\"\s*,\s*([^,]+),\s*([^)]+)\)", govde):
        out.setdefault(m.group(1),
                       f"range {m.group(2).strip()}..{m.group(3).strip()}")
    for m in re.finditer(r"min_ok\(\s*\"(\w+)\"\s*,\s*([^)]+)\)", govde):
        out.setdefault(m.group(1), f"min {m.group(2).strip()}")

    # `key.c_str()` formları: kosul bloglarini bol, blog icinde ara (yorumlara dayanikli)
    konum = [m for m in re.finditer(r"key\s*==\s*\"(\w+)\"", govde)]
    gruplar: list[tuple[int, int]] = []
    i = 0
    while i < len(konum):
        j = i
        while (j + 1 < len(konum)
               and re.match(r"\s*\|\|\s*$", govde[konum[j].end():konum[j + 1].start()])):
            j += 1
        gruplar.append((i, j))
        i = j + 1
    for gi, (a, b) in enumerate(gruplar):
        sonraki = konum[gruplar[gi + 1][0]].start() if gi + 1 < len(gruplar) else len(govde)
        blog = govde[konum[a].start():sonraki]
        anahtarlar = [konum[k].group(1) for k in range(a, b + 1)]
        m = re.search(r"range_ok\(key\.c_str\(\)\s*,\s*([^,]+),\s*([^)]+)\)", blog)
        et = (f"range {m.group(1).strip()}..{m.group(2).strip()}" if m else None)
        if et is None:
            m2 = re.search(r"min_ok\(key\.c_str\(\)\s*,\s*([^)]+)\)", blog)
            if m2:
                et = f"min {m2.group(1).strip()}"
        if et:
            for k in anahtarlar:
                out.setdefault(k, et)
    return out


# ── tarama ─────────────────────────────────────────────────────────────────────
# GUI/sanitize `cap.x` yazımı, CLI `cap_x` anahtarı kullanır — ad farkı bir
# "CLI domain yok" YANLIŞ-POZİTİF'i üretirdi (elle ölçüldü: `set-param cap_x`
# range_ok(0, CAP_X_MAX) ile reddediyor).
_CLI_ANAHTAR = {"cap.x": "cap_x", "cap.y": "cap_y"}


def tara(config_cpp: str | None = None) -> list[tuple[str, str, str, str, str]]:
    """(alan, gauge, sanitize, cli, durum) satırları; sadece İHLALLER + notlar."""
    kaynak = config_cpp if config_cpp else str(KOK / "src" / "config.cpp")
    metin = Path(kaynak).read_text(encoding="utf-8", errors="replace")

    gauge = gauge_araliklari()
    esle = widget_alan_eslesmesi()
    sinir = sanitize_sinirlari(metin)
    cli = cli_alani()

    satirlar = []
    gorulen: set[tuple] = set()
    for widget, alan in sorted(esle.items(), key=lambda kv: kv[1]):
        if widget not in gauge:
            continue
        gmin, gmax = gauge[widget]
        if gmax is None:
            continue                      # semantik ifade (MAX_NORM vb.) — atla
        ifade, ust = sinir.get(alan, ("(yok)", None))
        cli_et = cli.get(_CLI_ANAHTAR.get(alan, alan), "YOK")

        if ust is None:
            durum = "İHLAL: sanitize'de üst sınır YOK"
        elif gmax < ust:
            durum = f"İHLAL: gauge {gmax} < sanitize {ust}"
        elif gmax > ust:
            durum = f"İHLAL: gauge {gmax} > sanitize {ust}"
        else:
            durum = "eşleşiyor"
        satir = (alan,
                 f"{gmin}..{gmax}" if gmin is not None else f"?..{gmax}",
                 ifade if ust is None else f"{ifade}={ust:g}",
                 cli_et, durum)
        if satir in gorulen:            # X ve Y aynı aralığı kullanıyor → tek satır
            continue
        gorulen.add(satir)
        satirlar.append(satir)
    return satirlar


def tc() -> None:
    yaz("\n=== TARAMA: GUI gauge ↔ sanitize üst sınır ↔ CLI domain ===")
    yaz(f"{'alan':<20}{'gauge':<14}{'sanitize':<22}{'cli':<16}durum")
    satirlar = tara()
    ihlal = 0
    for alan, g, s, c, d in satirlar:
        yaz(f"{alan:<20}{g:<14}{s:<22}{c:<16}{d}")
        if d.startswith("İHLAL"):
            ihlal += 1
    yaz(f"\nihlal: {ihlal} / ölçülen alan: {len(satirlar)}")


# ── PC: K6 clamp'ini sileyim, tarama yakalamak ZORUNDA ────────────────────────
_KLAMP = re.compile(
    r"^[ \t]*if \(a\.([\w.]+)\s*>\s*(\w+_MAX)\)\s*a\.\1\s*=\s*\2;", re.M)


def klamp_listesi(metin: str) -> list[tuple[str, str]]:
    """config.cpp'deki `if (a.X > CEIL) a.X = CEIL;` klamp satırları.
    Elle listelenmez — yeni bir üst sınır eklenince PC otomatik kapsar."""
    return [(m.group(1), m.group(0)) for m in _KLAMP.finditer(metin)]


def pc() -> bool:
    global PC_HATASI
    yaz("\n=== POZITIF KONTROL (§4) ===")
    gercek = (KOK / "src" / "config.cpp").read_text(encoding="utf-8")

    # PC-1 KAPSAM: araç gerçekten TÜM alanları ölçüyor mu?  Bu denetim yokken
    # `(?:a|ay)` hatası 34 widget'ın 5'ine düşürdü ve yine de "GEÇTİ" dedi.
    eksik = kapsam_denetimi()
    pc1 = not eksik
    yaz(f"PC-1 kapsam        : {'GEÇTİ' if pc1 else 'KIRILDI'}"
        f"  (ham accel widget'ları = ayrıştırılanlar)")
    for e in eksik:
        yaz(f"    {e}")
    if not pc1:
        PC_HATASI += 1

    # PC-2 her ÜST SINIRI tek tek devre dışı bırakır ve taramanın o alanı
    # İŞARETLEMESİNİ bekler.  (AJ1 §5: PC test edilen birimi değil,
    # DENETLEYİCİYİ bozar.)  Liste elle değil config.cpp'den türer.
    klamplar = klamp_listesi(gercek)
    if not klamplar:
        yaz("PC-2 KIRILDI: config.cpp'de hiç üst sınır klampı bulunamadı")
        PC_HATASI += 1
    kapanmayan: list[str] = []
    acik_kalan: list[str] = []
    for alan, satir in klamplar:
        bozuk = gercek.replace(satir, "// PC: bilerek silindi", 1)
        gecici = BUR / "_pc_kopya_config_gauge.cpp"
        gecici.write_text(bozuk, encoding="utf-8")
        try:
            s = [x for x in tara(str(gecici)) if x[0] == alan]
            if not s or "üst sınır YOK" not in s[0][4]:
                acik_kalan.append(f"{alan}: {s[0][4] if s else 'satır yok'}")
        finally:
            gecici.unlink(missing_ok=True)
        # üretim dosyasında bu sınır KAPALI olmalı
        gercek_s = [x for x in tara() if x[0] == alan]
        if not gercek_s or "üst sınır YOK" in gercek_s[0][4]:
            kapanmayan.append(f"{alan}: {gercek_s[0][4] if gercek_s else 'satır yok'}")
    pc2 = not acik_kalan and not kapanmayan
    yaz(f"PC-2 üst sınırlar : {'GEÇTİ' if pc2 else 'KIRILDI'}"
        f"  ({len(klamplar)} klamp tek tek geri alındı → tarama her birini "
        f"işaretlemeli, üretimde hiçbiri açık kalmamalı)")
    for x in acik_kalan:
        yaz(f"    geri alınca İŞARETLENMEDİ: {x}")
    for x in kapanmayan:
        yaz(f"    üretimde hâlâ AÇIK: {x}")
    if not pc2:
        PC_HATASI += 1

    # PC-4 CLI sütunu: elle ölçülmüş değerlerle kilitlenir (yorum satırları
    # regex'i kesip "YOK" gösteriyordu — ölçülmüş ikinci ölçüm hatası).
    cli = cli_alani()
    beklenen = {"scale": "range 0.01..SCALE_MAX", "input_offset": "range 0..CAP_X_MAX",
                "cap_x": "range 0..CAP_X_MAX", "cap_y": "range 0..CAP_Y_MAX"}
    yanlis = [f"{k}: '{cli.get(k)}' != '{v}'"
              for k, v in beklenen.items() if cli.get(k) != v]
    pc4 = not yanlis
    yaz(f"PC-4 CLI sütunu    : {'GEÇTİ' if pc4 else 'KIRILDI'}"
        f"  (4 anahtar elle ölçülmüş değerle eşleşmeli)")
    for y in yanlis:
        yaz(f"    {y}")
    if not pc4:
        PC_HATASI += 1

    yaz(f"PC sonucu: {3 - PC_HATASI}/3 geçti"
        + ("  → ÖLÇÜM GEÇERLİ" if PC_HATASI == 0 else "  → ÖLÇÜM GEÇERSİZ"))
    return PC_HATASI == 0


def main() -> int:
    import time
    yaz(f"# AJ4 gauge-scan ölçümü — {time.strftime('%Y-%m-%d %H:%M:%S')}")
    yaz(f"HEAD: {subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=KOK,
                               capture_output=True, text=True).stdout.strip()}")
    yaz(f"LC_ALL={os.environ.get('LC_ALL', '(yok)')}  (betik locale'den bağımsız)")
    if not pc():
        (BUR / "gauge_scan.txt").write_text("\n".join(CIKTI) + "\n",
                                            encoding="utf-8")
        return 3
    if "--sadece-pc" not in sys.argv:
        tc()
    hedef = BUR / "gauge_scan.txt"
    hedef.write_text("\n".join(CIKTI) + "\n", encoding="utf-8")
    n = len(hedef.read_text(encoding="utf-8").splitlines())
    yaz(f"\ngauge_scan.txt yazıldı: {n} satır" + ("" if n > 0 else "  ← BOŞ"))
    return 0 if (PC_HATASI == 0) else 1


if __name__ == "__main__":
    sys.exit(main())

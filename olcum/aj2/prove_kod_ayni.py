#!/usr/bin/env python3
"""prove_kod_ayni.py — "kod degismedi" iddiasini SÖZ DEĞİL ÖLÇÜM olarak kanıtlar.

Kullanim:
    prove_kod_ayni.py <once.hpp> <sonra.hpp>            # eski yeni (dosya)
    prove_kod_ayni.py --git <dosya>                     # HEAD~1 .. HEAD (commit)
    prove_kod_ayni.py --pc <dosya>                      # yontemin duyarli oldugunu
                                                         # dogrula (mutasyonla kirma)

Yontem: yorumlar ve bos satirlar atilir, satir ici // silinir, kalan iki
cikti bayt bayt karsilastirilir. 0 fark = yorum eklmek derlenmis artefakti
DEGISTIRMEZ.

ONEMLI: tek basina "0 fark" HICBIRSEY kanitlamaz — alet duyarsizsa daima 0
doner. Bu yuzden --pc ZORUNLUDUR: bilinen bir mutasyonla sayinin degistigini
gostermeden "ayni" sonucu guvenilmez sayilir.  (Projenin kalici kurali:
"PC kalmadi" cevabi "alet duyarsiz" demektir, once kendi kurulumunu dogrula.)

Tuzaklar (bu projede yasandi):
  * grep/re duyarliligi: Turkce locale'inda [A-Za-z0-9_] araliklari sessizce
    kesilir -> her zaman LC_ALL=C veya [[:alnum:]] kullan. Burada Python
    re kullaniyoruz, o yuzden duyarli degil.
  * include golgelemesi: bir basligi -I ile degistiremezsin. Tirmakli include
    once INCLUDE EDEN dosyanin dizinini arar. Mutasyonu denemek icin include/
    dizinin TAMAMINI kopyala, sonra kopyada degistir.
  * olu kod: return'den sonra yazilan mutasyon hic sey yapmaz.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

# onceki script'in hatasindan: satir ici yorum ve blok yorum
_BLOCK = re.compile(r"/\*.*?\*/", re.S)


def strip_code(text: str) -> str:
    text = _BLOCK.sub("", text)
    out = []
    for line in text.splitlines():
        line = re.sub(r"//.*$", "", line).rstrip()
        if line.strip():
            out.append(line)
    return "\n".join(out) + "\n"


def read(path: str) -> str:
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def git_show(rev: str, path: str) -> str:
    r = subprocess.run(["git", "show", f"{rev}:{path}"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"git show {rev}:{path} basarisiz: {r.stderr.strip()}")
    return r.stdout


def report(name: str, before: str, after: str) -> bool:
    a, b = strip_code(before), strip_code(after)
    la, lb = a.count("\n"), b.count("\n")
    if a == b:
        print(f"  {name:<26} {la:>7} satir  ->  YORUMSUZ KOD BAYT-LEVEL AYNI")
        return True
    print(f"  {name:<26} {la:>7} -> {lb} satir  ->  FARK VAR")
    for i, (x, y) in enumerate(zip(a.splitlines(), b.splitlines()), 1):
        if x != y:
            print(f"      ilk fark @{i}: {x!r}  !=  {y!r}")
            break
    return False


def main() -> int:
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("a", nargs="?")
    ap.add_argument("b", nargs="?")
    ap.add_argument("--git", metavar="DOSYA")
    ap.add_argument("--pc", metavar="DOSYA")
    ap.add_argument("--dallar", metavar="BASLIK",
                    help="dal sayacini olc (-fprofile-arcs, elle sayac YOK)")
    ap.add_argument("--surucu", metavar="CPP",
                    help="--dallar icin girdi surucusu (main zorunlu)")
    ap.add_argument("--arama", metavar="CPP",
                    help="erisilebilir girdi arayan ikinci surucu")
    ap.add_argument("--atif", metavar="DOSYA",
                    help="yorumdaki kendi satir atiflarini denetle")
    ap.add_argument("--aktif", metavar="ESKI::YENI",
                    help="mutasyon PC'si: bir KOD satiri secip geri al, "
                         "sayacin kipirdadigini olc")
    args = ap.parse_args()

    if args.aktif:
        if not args.dallar:
            sys.exit("--aktif icin --dallar ZORUNLU (sayac olculecek dosya)")
        if "::" not in args.aktif:
            sys.exit("--aktif bicisi: 'ESKI::YENI' (orn. "
                     "'log_inner < -600.0::log_inner < 1e300')")
        eski, yeni = args.aktif.split("::", 1)
        return run_aktif(args.dallar, args.surucu, eski, yeni)
    if args.atif:
        return run_atif(args.atif)
    if args.dallar:
        if not args.surucu:
            sys.exit("--dallar icin --surucu ZORUNLU (sayac olculecek cagriyi "
                     "kim yapiyor? Bilinmeden dal sayaci bos kalir ve 'erisilemez' "
                     "sanilir — en sik yapilan hatadir.)")
        return run_dallar(args.dallar, args.surucu, args.arama)
    if args.pc:
        return run_pc(args.pc)
    if args.git:
        print(f"  dosya: {args.git}  (HEAD~1 -> HEAD, yorumlar soyulmus)")
        return 0 if report("kod", git_show("HEAD~1", args.git),
                           read(args.git)) else 1
    if args.a and args.b:
        print(f"  once: {args.a}\n  sonra: {args.b}   (yorumlar soyulmus)")
        return 0 if report("kod", read(args.a), read(args.b)) else 1
    ap.print_help()
    return 2


def run_pc(path: str) -> int:
    """Yontemin duyarli oldugunu dogrular: bilinen bir mutasyonla sayi degisir."""
    original = read(path)
    print(f"  PC: {path} icine bilinen bir mutasyon suruluyor")
    muts = [
        (" satir sonuna yorum disi ifade", lambda s: s + "\nint _pc_probe = 1;\n"),
        (" return; sonrasi sey (olu kod)", lambda s: s + "\nreturn 0;\n"),
    ]
    for name, mutate in muts:
        with tempfile.NamedTemporaryFile("w", suffix=".tmp", delete=False) as fh:
            fh.write(mutate(original))
            tmp = fh.name
        try:
            detected = not report(name, original, read(tmp))
        finally:
            os.unlink(tmp)
        if not detected:
            print("  ⛔ PC KALMADI — yontem duyarsiz, 'AYNI' sonucu gecersiz")
            return 1
    print("  ✔ PC GECTI — yontem fark algiliyor, 'AYNI' sonucu gecerli")
    return 0


# ---------------------------------------------------------------------------
# --dallar: bir dalin sayacini OLCE. Elle sayac koymaz; derleyicinin kendi
# -fprofile-arcs sayacini kullanir, boylece sayim o dosyadan bagimsiz olur.
#
# SIFIR SAYAN DAL SILME ONERISI DEGILDIR. Sifir, "bu girdi kumesi bu dala
# hic girmedi" demektir; "bu dal imkansiz" demek icin ya erisilebilir bir girdi
# bulunur ya da imkansizlik MATEMATIKSEL olarak gosterilir. Arac girdi
# bulamazsa "erisilebilir girdi bulunamadi" yazar ve SILME ONERMEZ.
# ---------------------------------------------------------------------------
import gzip
import json
import shutil
import subprocess as _sp


def _build_run_gcov(work: str, header: str, driver: str):
    """Surucuyu -fprofile-arcs ile derle, calistir, gcov JSON uret."""
    obj = _sp.run(["g++", "-std=c++20", "-O0", "--coverage",
                   "-I", "include", driver, "-o", "drv"],
                  cwd=work, capture_output=True, text=True)
    if obj.returncode != 0:
        return None, f"derleme basarisiz: {obj.stderr.strip()[:300]}"
    run = _sp.run(["./drv"], cwd=work, capture_output=True, text=True)
    if run.returncode != 0:
        return None, f"surucu hata kodu {run.returncode}: {run.stderr.strip()[:300]}"
    # -b ZORUNLU: gcov JSON formatinda branch verisini YALNIZ -b ile yazar.
    # -b unutulursa files[*].lines[*].branches hic dolmaz ve her dal "sifir"
    # gorunur -> alet "erisilemez kod" der. Tuzak, PC-1 ile yakalanir.
    g = _sp.run(["gcov", "--json-format", "-b", "-o", ".", "drv.gcno"],
                cwd=work, capture_output=True, text=True)
    gj = os.path.join(work, "drv.gcov.json.gz")
    if g.returncode != 0 or not os.path.exists(gj):
        return None, f"gcov JSON uretilemedi: {g.stderr.strip()[:300]}"
    with gzip.open(gj, "rt", encoding="utf-8") as fh:
        return json.load(fh), None


def _branches(data, header_rel: str):
    """(satir, branch_idx, count, throw, fallthrough) listesi."""
    out = []
    for f in data.get("files", []):
        if header_rel not in f["file"]:
            continue
        for ln in f["lines"]:
            for bi, b in enumerate(ln.get("branches") or []):
                out.append((ln["line_number"], bi, b["count"],
                            b.get("throw", False), b.get("fallthrough", False)))
    return out


def _src_line(src, n: int) -> str:
    if isinstance(src, str):
        lines = src.splitlines()
    else:
        lines = list(src)
    return lines[n - 1].strip() if 0 < n <= len(lines) else ""


def _is_kod_satiri(ln: str) -> bool:
    """Satirin yorum disinda gercek kodu var mi? (bosluk = yorum satiridir)"""
    return bool(strip_code(ln + "\n").strip())


def _hedef_satirlar(src: str, eski: str) -> tuple:
    """ESKI metnini iceren satirlari KOD/YORUM olarak ayirir.

    Neden ayiriliyor: yorum icinde de ayni metin gecebilir (olcum sonrasi
    kanit yorumlarinda gecer). Ilk metin gecisini degistirmek kodu DEGISTIRMEZ,
    sayac kipirdamaz ve PC YANLIŞ NEGATIF verir — sessizce. Bu, projenin
    en pahalı tuzak turlerinden: alet "olculmedi" degil, "olculdu ve degismez"
    der.
    """
    kod, yorum = [], []
    for i, ln in enumerate(src.splitlines(), 1):
        if eski in ln:
            (kod if _is_kod_satiri(ln) else yorum).append(i)
    return kod, yorum


def run_aktif(header: str, driver: str, eski: str, yeni: str) -> int:
    """Mutasyon PC'si: bir KOD satirini geri al, sayacin KIPIRDADIGINI olc.

    Rapor satirlari INDEKSLE degil, SATIR + KOD METNIyle verilir. Indeks
    (b0/b1) hangi kolun koll oldugunu SOYLEMEZ; olcumlenmis olarak kararli
    olsa da (ayni kosu iki kez birebir, mutasyon altinda da ayni indeks)
    okuyucu icin bilgidir degildir ve iki olcum arasinda karsilastirilamaz.
    """
    header = os.path.abspath(header)
    hdr_rel = "include/" + os.path.basename(header)
    inc_root = os.path.dirname(header)
    src = read(header)

    print(f"  === --aktif: mutasyon PC'si ===")
    print(f"  hedef metin: {eski!r}\n  degistirilecek: {yeni!r}")

    kod, yorum = _hedef_satirlar(src, eski)
    print(f"  eslesme: {len(kod)} KOD satiri, {len(yorum)} yorum satiri")
    if yorum:
        print(f"    ⓘ ayni metin yorumda da geciyor (L{', L'.join(map(str, yorum))}) — "
              f"BU YUZDEN ilk metin gecisi hedef DEGILDIR.")
    if len(kod) != 1:
        print(f"  ⛔ Hedef sayisi {len(kod)} — TAM OLSA (1) gerekir.")
        print(f"    {'KOD satirlari: ' + str(kod) if kod else 'KOD satiri YOK'}")
        print("    0 ise metin kodda degildir; >1 ise hangi kodu "
              "mutasyonlayacagin belirsiz.")
        print("    Bu bir arac hatasi degil, BELIRSIZ BIR HEDEF. Cumle "
              "basina numarayla hedefle.")
        return 2
    hedef = kod[0]
    print(f"  ✔ tek KOD satiri hedef: L{hedef}")

    mut = src.splitlines()
    mut[hedef - 1] = mut[hedef - 1].replace(eski, yeni, 1)
    mut_src = "\n".join(mut) + "\n"

    # KOD gercekten degisti mi? Yorum degisimi sayilmaz.
    if strip_code(src) == strip_code(mut_src):
        print("  ⛔ Mutasyon YORUMDA kaldi — kod satiri degismedi. "
              "PC anlamsiz, gecersiz.")
        return 2
    print("  ✔ mutasyon KOD satirina uygulandi (kod metni degisti)")

    def olc(icerik: str, etiket: str):
        with tempfile.TemporaryDirectory() as tmp:
            shutil.copytree(inc_root, os.path.join(tmp, "include"))
            with open(os.path.join(tmp, "include", os.path.basename(header)),
                      "w", encoding="utf-8") as fh:
                fh.write(icerik)
            shutil.copy(driver, os.path.join(tmp, "drv.cpp"))
            data, err = _build_run_gcov(tmp, hdr_rel, "drv.cpp")
            if err:
                print(f"  ⛔ {etiket}: {err}")
                return None
            return _branches(data, hdr_rel)

    ori = olc(src, "orijinal")
    mut_br = olc(mut_src, "mutasyonlu")
    if ori is None or mut_br is None:
        return 2
    if len(ori) != len(mut_br):
        print(f"  ⛔ dal sayisi degisti: {len(ori)} -> {len(mut_br)}. "
              f"Karsilastirilamaz.")
        return 2

    o = {(r[0], r[1]): r[2] for r in ori}
    m = {(r[0], r[1]): r[2] for r in mut_br}
    # INDeks yerine ANLAMSAL etiket. gcov her dal icin fallthrough verir:
    # dusen = kosulun DOGRULU koll (gövdeye giriyor), atlayan = YANLIS koll.
    # Bu etiket indeksle degil, KODUN YAPISIYLA tanimlidir; satir numarasi
    # gibi kayabilir ama b0/b1 gibi "hangi koldur" bilgisini TASIYAMAZ.
    def kollar(brs):
        t = {}
        for ln, _bi, c, _th, fall in brs:
            t[(ln, "dusen (kosul dogru)" if fall else "atlayan (kosul yanlis)")] \
                = t.get((ln, "dusen (kosul dogru)" if fall
                         else "atlayan (kosul yanlis)"), 0) + c
        return t
    ol, ml = kollar(ori), kollar(mut_br)
    lines = src.splitlines()
    z_o = sum(1 for r in ori if r[2] == 0)
    z_m = sum(1 for r in mut_br if r[2] == 0)

    print(f"\n  --- ORIG/MUT tablosu (makine uretimli; kollar ANLAMSAL "
          f"etiketli, indeksle DEGIL) ---")
    print(f"  {'satir':<7}{'kol':<26}{'orijinal':>9}{'mutasyon':>10}   kod")
    for k in sorted(set(ol) | set(ml)):
        if ol.get(k) == ml.get(k):
            continue
        ln, kol = k
        print(f"  L{ln:<6}{kol:<26}{ol.get(k,'-'):>9}{ml.get(k,'-'):>10}   "
              f"{_src_line(lines, ln)[:40]}")
    print(f"\n  sifir sayan dal: {z_o} -> {z_m}   "
          f"({'mutasyon sayaci KIPIRDIRDI' if z_o != z_m else 'KIPIRMADI'})")

    yeni_sifir = [(k, ol.get(k, 0), ml.get(k, 0)) for k in sorted(ml)
                  if ol.get(k, 0) > 0 and ml.get(k, 0) == 0]
    yeni_canli = [(k, ol.get(k, 0), ml.get(k, 0)) for k in sorted(ml)
                  if ol.get(k, 0) == 0 and ml.get(k, 0) > 0]
    if yeni_canli:
        print("  sayaci 0'dan canliya giden (mutasyonun DOGRULANMASI):")
        for (ln, kol), a, b in yeni_canli:
            print(f"    L{ln} [{kol}]: {a} -> {b}   "
                  f"{_src_line(lines, ln)[:44]}")
    if yeni_sifir:
        print("  sayaci canlidan 0'a giden (mutasyonun DOLAYLI etkisi):")
        for (ln, kol), a, b in yeni_sifir:
            print(f"    L{ln} [{kol}]: {a} -> {b}   "
                  f"{_src_line(lines, ln)[:44]}")

    if z_o == z_m:
        print("  ⛔ PC KALMADI — mutasyon sayaci degistirmedi.")
        return 2
    print("  ✔ PC gecti — mutasyon sayaci degistirdi; sifirlar tutulmus "
          "sayac degil")
    return 0


def run_atif(dosya: str) -> int:
    """Yorumun kendi satir atiflarini denetler.

    KIRILGANLIK: bir dosyaya satir atfi yazmak, dosyanin ustunden bir satir
    eklemekle kirilir. Burada olmus tuzak: 4 bayat atif (L45/L46/L47/L49)
    duzeltildi, ANCAK ayni commit icinde yorum genisletildi ve gercek kod
    L131'den L144'e kaydi — duzeltilen atiflar KENDI ISLETMIZle bayatlasti.
    Satir numarasi degil, satirin KODU kalicidir.

    Kural: her "L<n>" atfi n. satiri gostermelidir ve o satir YORUM olmamalidir.

    PC zorunludur ve gomuludur: alet once iki kucuk YAPAY dosya uzerinde
    dogrulanir (biri iyi atif, biri kendine donen kotu atif) ve yalnizca kotu
    olani isaretlemede PASS verir. Boylece "0 bulundu" ile "arac bozuk"
    ayirt edilir.
    """
    IYI = ("// kod asagida: L3\n"
           "int a = 1;\n"
           "int b = 2;\n"
           "int c = 3;\n")
    KOTU = ("// kendine donen atif: L1\n"
            "int a = 1;\n"
            "int b = 2;\n")

    # --atif (satir atfi denetimi) ile --aktif (mutasyon PC'si) tek harf farklidir.
    # Yanlis kombinasyon --atif 'ESKI::YENI' FileNotFoundError ile PATLAR ve
    # kullaniciya dogru bayragi soylemez. Bu yakalayici onu eyleme donusturur.
    if "::" in dosya:
        print(f"  ⛔ --atif bir DOSYA YOLU bekler, mutasyon metni degil.\n"
              f"     Mutasyon PC'si icin:  --aktif 'ESKI::YENI'\n"
              f"     Satir atfi denetimi:   --atif <dosya.hpp>")
        return 2

    def tara(text: str):
        satirlar = text.splitlines()
        kotu = []
        for i, ln in enumerate(satirlar, 1):
            if "//" not in ln and "/*" not in ln:
                continue
            yorum = ln[ln.index("//"):] if "//" in ln else ln
            for m in re.finditer(r"\bL(\d{1,4})\b", yorum):
                n = int(m.group(1))
                if not (0 < n <= len(satirlar)):
                    kotu.append((i, n, "dosya disi satir"))
                    continue
                hedef = satirlar[n - 1].strip()
                if hedef.startswith("//") or hedef.startswith("*") \
                        or hedef.startswith("/*"):
                    kotu.append((i, n, "hedef bir YORUM satiri"))
        return kotu

    iyi_kotu = tara(IYI)
    kotu_kotu = tara(KOTU)
    print("  === --atif: kendi satir atiflarini denetle ===")
    if iyi_kotu:
        print(f"  ⛔ PC KALMADI — iyi olmasi gereken dosyada {len(iyi_kotu)} "
              f"kaydi isaretledi. Alet duyarsiz.")
        for k in iyi_kotu:
            print(f"     yanlis pozitif: yorum L{k[0]} -> L{k[1]} ({k[2]})")
        return 2
    if not kotu_kotu:
        print("  ⛔ PC KALMADI — kotu olmasi gereken dosya TEMIZ cikti. "
              "Alet duyarsiz.")
        return 2
    print(f"  ✔ PC gecti (yapay dosyalar: iyi=0 kayit, kotu={len(kotu_kotu)} "
          f"kayit)")

    src = read(dosya)
    kotu = tara(src)
    if not kotu:
        print(f"  ✔ {dosya}: {dosya} icinde bozuk satir atifi yok")
        return 0
    print(f"  ⛔ {dosya}: {len(kotu)} atif BOZUK — hedef bir yorum satiri "
          f"veya dosya disi:")
    for yorum_satiri, hedef, sebep in kotu:
        print(f"     yorum L{yorum_satiri}  ->  L{hedef}  ({sebep})")
    print("  Duzeltme: atfi KODUyla yaz, numarayla degil. Numara, dosyanin")
    print("  ustune bir satir eklenince kirilir; kod metni kirilmaz.")
    return 1


def run_dallar(header: str, driver: str, arama: str | None) -> int:
    header = os.path.abspath(header)
    hdr_rel = "include/" + os.path.basename(header)
    inc_root = os.path.dirname(header)          # <...>/include
    src = read(header)

    print(f"  === --dallar: {hdr_rel} ===")
    print("  olcum: derleyici sayaci (-fprofile-arcs). Elle sayac YOK,")
    print("         boylece sayac bu dosyadan bagimsiz.")

    results = {}
    with tempfile.TemporaryDirectory() as tmp:
        # include golgelemesi tuzagi: tirmakli include once INCLUDE EDEN dosyanin
        # dizinini arar, bu yuzden include/ dizininin TAMAMINI kopyalayip
        # yalnizca kopyada mutasyon/olcum yapar.
        shutil.copytree(inc_root, os.path.join(tmp, "include"))
        shutil.copy(driver, os.path.join(tmp, "drv.cpp"))
        data, err = _build_run_gcov(tmp, hdr_rel, "drv.cpp")
        if err:
            print(f"  ⛔ {err}")
            return 2
        results["temel"] = _branches(data, hdr_rel)

        # --- PC-1: arac duyarli mi? sayac bos mu, dolu mu? ---
        br = results["temel"]
        if not br:
            print("  ⛔ PC-1 KALMADI — hic dal bulunamadi. Baslik derlenmemis")
            print("     olabilir ya da -O0 inline atiyor. 'erisilemez' DEGIL,")
            print("     alet hatasi. Cikis: 2")
            return 2
        nonzero = sum(1 for r in br if r[2] > 0)
        zero = sum(1 for r in br if r[2] == 0)
        print(f"  PC-1 sayac duyarli mi : {len(br)} dal bulundu, "
              f"{nonzero} dolu, {zero} sifir")
        if nonzero == 0:
            print("  ⛔ PC-1 KALMADI — HICBIR dal sayilmadi. Alet duyarsiz.")
            return 2
        print("  PC-1 ✔ gecti (sayac dolu dallar uretiyor)")

        # --- PC-2: sayac DOGRULUGU, BLOK BAZINDA ---
        # Dogru degismez: bir satirda "a && b" iki AYRI kosul blok uretir.
        # Satirdaki TUM dallarin toplami satir sayacina esit DEGILDIR — bu
        # ilk denemede PC-2'nin kendisinin yanlis oldugunu gosterdi (L20'de
        # 392 != 343). Dogru degismez veri akisidir:
        #   * satirdaki hicbir blogdan gelmeyen blogun cikis toplami = satir
        #     sayaci,
        #   * diger blogun cikis toplami = o bloga gelen dallarin toplami.
        # Cikan toplam GIRIS toplamindan buyukse sayac bozuktur.
        mism = []
        checked = 0
        for f in data.get("files", []):
            if hdr_rel not in f["file"]:
                continue
            for ln in f["lines"]:
                bl = ln.get("branches") or []
                if len(bl) < 2:
                    continue
                line_count = ln.get("count", 0)
                src_blocks = {b["source_block_id"] for b in bl}
                dst_blocks = {b["destination_block_id"] for b in bl}
                roots = src_blocks - dst_blocks
                arrivals = {s: line_count for s in roots}
                for b in bl:
                    d = b["destination_block_id"]
                    arrivals[d] = arrivals.get(d, 0) + b["count"]
                out = {}
                for b in bl:
                    s = b["source_block_id"]
                    out[s] = out.get(s, 0) + b["count"]
                for s, tot in out.items():
                    if len([b for b in bl if b["source_block_id"] == s]) < 2:
                        continue          # tek cikisli blog kosul degildir
                    got = arrivals.get(s, 0)
                    checked += 1
                    if tot != got:
                        mism.append((ln["line_number"], s, tot, got))
        if mism:
            print(f"  ⛔ PC-2 KALMADI — {len(mism)}/{checked} blogun cikis sayaci "
                  f"gelis sayacina esit degil -> sayac bozuk")
            for m in mism[:6]:
                print(f"     L{m[0]} blok{m[1]}: cikis {m[2]} != gelis {m[3]}")
            return 2
        print(f"  ✔ PC-2 gecti ({checked} kosul blogu: cikis sayaci = gelis "
              f"sayaci)")

        # --- erisilebilir girdi aramasi (varsa) ---
        if arama:
            shutil.copy(arama, os.path.join(tmp, "drv.cpp"))
            for p in ("drv.gcda", "drv.gcno"):
                fp = os.path.join(tmp, p)
                if os.path.exists(fp):
                    os.unlink(fp)
            data2, err = _build_run_gcov(tmp, hdr_rel, "drv.cpp")
            if err:
                print(f"  ⛔ arama surucusu: {err}")
                return 2
            results["arama"] = _branches(data2, hdr_rel)
            print(f"  arama surucusu calisti: {os.path.basename(arama)}")

    # --- rapor ---
    def show(tag, brs, key):
        z = [r for r in brs if r[2] == 0]
        print(f"\n  --- {tag}: sifir sayan {len(z)} / {len(brs)} dal ---")
        for ln, bi, _c, _t, _f in z:
            print(f"    L{ln:<4} b{bi}  {_src_line(src, ln)[:64]}")
        return z

    z_base = show("TEMEL (verilen surucu)", results["temel"], None)
    found = {}
    if "arama" in results:
        # arama surucusu ile ARTAN dallar: erisilebilir girdi BULUNDU
        before = {(r[0], r[1]): r[2] for r in results["temel"]}
        gained = [r for r in results["arama"]
                  if before.get((r[0], r[1]), 0) == 0 and r[2] > 0]
        print(f"\n  --- ARAMA: erisilebilir girdi BULUNAN dallar ({len(gained)}) ---")
        for ln, bi, c, _t, _f in gained:
            found[(ln, bi)] = c
            print(f"    L{ln:<4} b{bi}  sayim 0 -> {c}   ERISILEBILIR  "
                  f"{_src_line(src, ln)[:44]}")

    still = [r for r in z_base if (r[0], r[1]) not in found]

    print("\n  === HUKUM ===")
    if found:
        print(f"  ⛔ SILME ONERISI YOK. {len(found)} dal icin erisilebilir girdi "
              f"BULUNDU:")
        for (ln, bi), c in sorted(found.items()):
            print(f"     L{ln} b{bi}: erisilebilir ({_src_line(src, ln)[:56]})")
    if still:
        print(f"  ⚠ {len(still)} dal icin 'erisilebilir girdi bulunamadi'.")
        print("    Bu SILME ONERISI DEGILDIR. Sifir sayac yalnizca 'bu girdi")
        print("    kumesi buraya girmedi' der. Imkansizlik ya erisilebilir girdi")
        print("    bulmak ya da matematiksel olarak gostermekle kanitlanir:")
        for ln, bi, _c, _t, _f in still:
            print(f"     L{ln:<4} b{bi}  {_src_line(src, ln)[:60]}")
    if not z_base:
        print("  ✔ Hicbir dal sifir sayilmadi — bu dosyada olcum bu girdi")
        print("    kumesiyle dal bulamadi, 'erisilemez kod' iddiasi desteklenmez.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

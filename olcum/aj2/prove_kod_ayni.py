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
    args = ap.parse_args()

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


if __name__ == "__main__":
    sys.exit(main())

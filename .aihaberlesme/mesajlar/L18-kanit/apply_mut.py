#!/usr/bin/env python3
"""L18 mutation applier: TSV  <relpath>\t<OLD>\t<NEW>\t<desc>  -> exact replace in a work copy."""
import sys, os, pathlib
work, exprs = sys.argv[1], sys.argv[2]
applied = []
if exprs and os.path.exists(exprs):
    for ln, line in enumerate(open(exprs, encoding="utf-8"), 1):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 4:
            print(f"MUT-FAIL: {exprs}:{ln} -- {len(parts)} alan, 4 bekleniyor", file=sys.stderr)
            sys.exit(9)
        f, old, new, desc = parts[0], parts[1], parts[2], parts[3]
        old = old.encode().decode("unicode_escape")
        new = new.encode().decode("unicode_escape")
        p = pathlib.Path(work) / f
        s = p.read_text(encoding="utf-8")
        if old not in s:
            print(f"MUT-FAIL: {exprs}:{ln} -- pattern NOT FOUND in {f}: {old[:100]!r}", file=sys.stderr)
            sys.exit(9)
        n = s.count(old)
        p.write_text(s.replace(old, new), encoding="utf-8")
        applied.append(f"{desc} [{n} yerde]")
print("    uygulanan: " + " ; ".join(applied) if applied else "    uygulanan: <yok: pozitif kontrol>")

#!/usr/bin/env python3
"""Kayıt↔kod köprü denetleyicisi — bkz. tests/run_tracker_bridge.sh.

Bir kayıt ⏸ <dosya> kilidi / AÇIK etiketiyle duruyor, ama kodda o kaydın
düzeltme işareti (`O31-XXX:` / `O31-XXX —`) bulunuyor.  Bu çelişki, 170c5e14'te
8 kayıtta birden vardı: kod düzeltildi, tracker'a dokunulmadı.

Kullanım:
    python3 tests/tracker_bridge.py                 # çalışma ağacı (varsayılan)
    python3 tests/tracker_bridge.py <git-rev>       # geçmişteki bir commit
    python3 tests/tracker_bridge.py --help

Çıkış: 0 = çelişki yok, 1 = çelişki var, 2 = çalıştırılamadı.
"""
import re
import subprocess
import sys
from pathlib import Path

TRACKER = "Bug Hata Raporları.md"
CODE_SUFFIXES = (".cpp", ".hpp", ".inl", ".sh")
REC_ID = re.compile(r"O31-[A-Z0-9]+")
# İşaret, yorum içinde bir ayırıcıyla bitmeli: `// O31-C2:` , `/// O31-L4 —`
CODE_MARK = re.compile(r"O31-[A-Z0-9]+\s*[:—-]")
# ⏸ etiketi `Durum:` satırının BAŞINDA olmalı.  Yalnız "⏸" varlığı yeterli
# DEĞİLDİR: ✅ kayıtlar eski etiketi tırnak içinde anlatır ve bu etiket
# değildir.  (Bu ayrım bir turda üç kez yanlış sayıldı.)
PAUSE_LABEL = re.compile(
    r"^\s*- Durum:\s*(?:`?\[ALINDI:[^\]]*\]?`?\s*)?⏸"
)
REC_HEAD = re.compile(r"^\*\*(O31-[A-Z0-9]+)\b")


def read_tree(rev):
    """rev=None -> çalışma ağacı, aksi hâlde `git show rev:path`."""
    if rev is None:
        def read(p):
            q = Path(p)
            return q.read_text(errors="replace") if q.exists() else ""

        def files():
            return [
                str(p)
                for p in Path(".").rglob("*")
                if p.is_file() and ".git" not in str(p)
            ]
    else:
        def read(p):
            return subprocess.run(
                ["git", "show", f"{rev}:{p}"], capture_output=True, text=True
            ).stdout

        def files():
            out = subprocess.run(
                ["git", "ls-tree", "-r", "--name-only", rev],
                capture_output=True,
                text=True,
            ).stdout
            return out.split()
    return read, files


def main(argv):
    if "--help" in argv or "-h" in argv:
        print(__doc__.strip())
        return 0
    rev = argv[0] if argv else None

    if rev is None and not Path(TRACKER).exists():
        print(f"FAIL: {TRACKER} bulunamadı.", file=sys.stderr)
        return 2

    read, files = read_tree(rev)

    # kayıt -> Durum satırı (yalnız İLK Durum satırı: ikinci bir - Durum:
    # satırı eklemek kaydı yeniden açmaz, bu da ölçülmüş bir tuzaktır)
    first_status = {}
    current = None
    for line in read(TRACKER).split("\n"):
        head = REC_HEAD.match(line)
        if head:
            current = head.group(1)
            first_status.setdefault(current, None)
        elif current is not None and line.startswith("- Durum:"):
            if first_status[current] is None:
                first_status[current] = line

    # kodda geçen kayıt işaretleri
    marked = {}
    for name in files():
        if not name.endswith(CODE_SUFFIXES):
            continue
        for m in CODE_MARK.finditer(read(name)):
            marked.setdefault(m.group(0).rstrip(" :—-"), set()).add(name)

    conflicts = []
    for rid in sorted(marked):
        status = first_status.get(rid)
        if status is None:
            continue  # koddan etiketlenmiş ama tracker'da kaydı yok — kapsam dışı
        paused = bool(PAUSE_LABEL.match(status))
        open_word = "AÇIK" in status and not status.lstrip().startswith("- Durum: ✅")
        if paused or open_word:
            conflicts.append((rid, sorted(marked[rid])[:3]))

    label = rev if rev else "çalışma ağacı"
    print(
        f"  {label}: kodda işaretli {len(marked)} kayıt · AÇIK olan "
        f"{len(conflicts)}"
    )
    for rid, where in conflicts:
        print(f"    ⚠ {rid}  kodda: {', '.join(where)}")
    return 1 if conflicts else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

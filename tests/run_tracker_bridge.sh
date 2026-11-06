#!/usr/bin/env bash
# ── Kayıt↔kod köprü kapısı ────────────────────────────────────────────────────
# "Sinyal var, yazıldığı yer ölçülmüyor" sınıfının mekanik karşılığı.
#
# Ne yakalar: bir kayıt ⏸ <dosya> kilidi / AÇIK etiketiyle duruyor, ama kodda
# o kaydın DÜZELTME İŞARETİ bulunuyor.  Bu, tam olarak 170c5e14'te olan şeydi:
# sekiz kaydın kodu düzeltildi (düzeltici yorum olarak `// O31-C2:` yazdı),
# tracker'daki ⏸ etiketlerine ise dokunulmadı → 22 commit bayat kaldı.
#
# Neden sadece bu yön: karşı yön ("✅ ama kodda işaret yok") ölçüldü ve GÜRÜLTÜLÜ
# çıktı — 23 kapalı kaydın 4'ünde işaret yok, 3'ü (H4/S1/L5) fiilen düzeltilmiş
# ama işareti commit mesajına yazılmış. ~%15-17 yanlış pozitif, kapı olarak
# kullanılmaz.  Ölçüm: tracker'da BAYAT/ek işaret yok.
#
# Ölçülmüş ayırt edicilik:
#   170c5e14        13 ihlal  -> exit 1   (gerçek ihlal, 22 commit gecikmiş)
#   HEAD            0 ihlal  -> exit 0
#   WORKTREE + PC   1 ihlal  -> exit 1   (O31-C5 bilerek açıldı)
#   WORKTREE        0 ihlal  -> exit 0
#
# Tuzak — bu kapıyı yazarken iki kez kendimi kandırdım, ikisi de burada not:
#  (1) "⏸" VARLIĞINI aramak yetmez.  ✅ kayıtlar eski etiketi TIRNAK İÇİNDE
#      anlatır ("tracker hâlâ `⏸ tr.inl kilidi` diyordu") ve bu bir etiket
#      DEĞİLDİR.  Bu hatayı bir turda üç kez yaptım; ⏸'ın satırın BAŞINDA
#      olması şart koşuluyor.
#  (2) Bu kapı çalışma ağacını okur.  git show ile okuyan bir sürüm, çalışma
#      ağacındaki bir düzenlemeyi GÖREMEZ ve pozitif kontrol sessizce no-op
#      olur — "kapı çalışıyor" ile "kapı hiç ölçmedi" aynı görünür.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || { echo "Hata: repo kökü bulunamadı: $ROOT" >&2; exit 1; }

command -v python3 >/dev/null 2>&1 || {
    echo "FAIL: köprü kapısı çalıştırılamadı: python3 yok." >&2
    echo "      Bu kapı SESSİZCE atlanamaz — python3 zaten run_tests.sh için zorunlu." >&2
    exit 1
}
[ -f "Bug Hata Raporları.md" ] || {
    echo "FAIL: köprü kapısı çalıştırılamadı: 'Bug Hata Raporları.md' bulunamadı ($ROOT)." >&2
    echo "      Bu kapı SESSİZCE atlanamaz." >&2
    exit 1
}

# Tuzak: "$@" iletilmezse bu satır çalışma ağacını okur ve geçmişteki bir
# commit'i sessizce YANLIŞ negatif raporlar.  Ölçüldü: iletmeden önce
# `run_tracker_bridge.sh 170c5e14` "0 ihlal" derdi, oysa o commit'te 13 var.
python3 tests/tracker_bridge.py "$@"
rc=$?

if [ $rc -eq 0 ]; then
    echo "köprü kapısı: kayıt↔kod çelişkisi yok ✓"
else
    echo "FAIL: köprü kapısı — kayıt AÇIK etiketli ama kodu işaretli (aşağıya bak)." >&2
    echo "      Bu, 170c5e14'te olanın aynısı: kod düzeltildi, kayıt kapatılmadı." >&2
    echo "      Çözüm: ya kaydı kapat, ya da kod işaretini geri al." >&2
fi
exit $rc

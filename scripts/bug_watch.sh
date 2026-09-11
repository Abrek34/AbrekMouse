#!/usr/bin/env bash
#
# Bug Hata Raporları.md izleyici
#
# 'Bug Hata Raporları.md' dosyasına yeni bir hata/rapor eklendiğinde dosyayı
# okur, değişikliği tespit eder, işlenen logu hem terminalde gösterir hem
# durum dizinine yazar ve opencode'u (headless) tetikleyerek hatayı analiz
# edip düzeltmesini sağlar. Düzeltme başarıyla tamamlanınca ajan rapor
# bloğunu dosyadan siler.
#
# Kullanım:
#   bash scripts/bug_watch.sh                # sonsuz döngü ile izle
#   bash scripts/bug_watch.sh --once         # tek kontrol, çıkar (cron için)
#   REPORT_FILE=... bash scripts/bug_watch.sh   # farklı rapor dosyası
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPORT_FILE="${REPORT_FILE:-$PROJECT_DIR/Bug Hata Raporları.md}"
# L-9: /tmp/rawaccel-bug-watch is world-writable on most systems — any user can
# pre-create it, then symlink state/log files for the watcher to follow (log
# forgery, lock bypass).  Use a per-user run dir (XDG_RUNTIME_DIR is 0700) or a
# private hidden dir under $HOME when it is not set.
if [[ -n "${XDG_RUNTIME_DIR:-}" ]]; then
    STATE_DIR="$XDG_RUNTIME_DIR/rawaccel-bug-watch"
else
    STATE_DIR="$HOME/.cache/rawaccel-bug-watch"
fi
PREV_FILE="$STATE_DIR/prev.md"
MARK="$STATE_DIR/seen.md5"
LOCK="$STATE_DIR/lock"
LOGFILE="$STATE_DIR/bug_watch.log"
MODE="${1:-watch}"
# M-5b: bir rapor için en fazla opencode denemesi.  Aşılırsa MARK ilerletilir
# (rapor bir daha otomatik tetiklenmez); elle müdahale için log tutulur.
MAX_ATTEMPTS="${MAX_ATTEMPTS:-3}"

# L-9: 0700 so nobody else can read/write watcher state even when the run dir
# is unusual.  install -d (coreutils) is safe against symlink-following.
install -d -m 700 "$STATE_DIR"

log() {
  local line="$(printf '[%s] %s' "$(date '+%F %T')" "$*")"
  printf '%s\n' "$line" >&2
  printf '%s\n' "$line" >> "$LOGFILE"
}

lock() {
  while ! mkdir "$LOCK" 2>/dev/null; do
    local pid=""
    [[ -f "$LOCK/pid" ]] && pid="$(cat "$LOCK/pid" 2>/dev/null || true)"
    if [[ -z "$pid" ]] || ! [[ "$pid" =~ ^[0-9]+$ ]]; then
      rm -rf "$LOCK"
      continue
    fi
    if kill -0 "$pid" 2>/dev/null; then
      sleep 1
      continue
    fi
    # M-5: PID-reuse koruması — ölen bekçinin PID'i yeni bir sürece
    # atanmışsa "canlı bekçi" sanılabilir.  /proc/<pid>/cmdline'ın hâlâ
    # bu betiği adlandırdığını doğrula, ardından 200 ms sonra ikinci bir
    # kill -0 ile gerçekten öldüğünü teyit et, sonra lock'u devral.
    if [[ -r "/proc/$pid/cmdline" ]] && grep -q "bug_watch" "/proc/$pid/cmdline" 2>/dev/null; then
      sleep 0.2
      kill -0 "$pid" 2>/dev/null && { sleep 1; continue; }
    fi
    rm -rf "$LOCK"
    continue
  done
  echo "$$" > "$LOCK/pid"
}

unlock() { rm -rf "$LOCK"; }

handle_change() {
  local report_md5
  report_md5="$(md5sum "$REPORT_FILE" | cut -d' ' -f1)"

  local retry_cnt_file="$STATE_DIR/attempts.${report_md5}.cnt"
  local report_txt_file="$STATE_DIR/report.${report_md5}.txt"

  # ── M-5b: bounded retry bookkeeping ─────────────────────────────────────────
  # A failed opencode run must NOT advance the MARK (previously it did, so the
  # watch loop never retried a still-failing report).  Instead we allow up to
  # MAX_ATTEMPTS retriggers of the SAME report, tracking the attempt count in a
  # per-report-hash file.  Only after the cap is writing the MARK ("give up").
  local retries=0
  [[ -f "$retry_cnt_file" ]] && retries="$(cat "$retry_cnt_file" 2>/dev/null || echo 0)"
  retries=$(( retries + 1 ))
  log "Rapor hash=$report_md5 deneme $retries/$MAX_ATTEMPTS"

  # ── Extract the new text ────────────────────────────────────────────────────
  # First attempt: diff from the last-seen baseline.  Retries reuse the stored
  # report text so the prompt is identical every time (the diff baseline has
  # advanced by then and would otherwise yield nothing).
  local new_text=""
  if [[ -f "$report_txt_file" ]]; then
    new_text="$(cat "$report_txt_file")"
  else
    if [[ -f "$PREV_FILE" ]]; then
      new_text="$(diff -u "$PREV_FILE" "$REPORT_FILE" 2>/dev/null \
        | grep '^+' | grep -v '^+++' | sed 's/^+//' || true)"
    fi
    if [[ -z "$new_text" ]]; then
      new_text="$(cd "$PROJECT_DIR" && sed -n '/ZU DÜZELTİLECEK RAPOR/,/^$/p' "$REPORT_FILE" 2>/dev/null || true)"
    fi
    if [[ -z "$new_text" ]]; then
      new_text="$(cat "$REPORT_FILE")"
    fi
    printf '%s\n' "$new_text" > "$report_txt_file"
  fi

  cp "$REPORT_FILE" "$PREV_FILE"

  # --- İşlenen log: terminal VE durum dizinine yaz ---
  printf '%s\n' "$new_text" > "$STATE_DIR/last_change.txt"
  log "=========================================================================="
  log "YENİ HATA RAPORU TESPİT EDİLDİ"
  log "Dosya: $REPORT_FILE"
  log "İşlenen log (ayrıca $STATE_DIR/last_change.txt dosyasına yazıldı):"
  {
    printf '--- BEGIN RAPOR ---\n'
    printf '%s\n' "$new_text"
    printf '--- END RAPOR ---\n'
  } | { tee /dev/stderr; } >> "$LOGFILE"

  log "opencode tetikleniyor (başlangıç: $(date '+%T'))"

  local prompt
  prompt=$(cat <<EOF
Bug Hata Raporları.md dosyasına yeni bir hata/rapor eklendi. Aşağıdaki işlem
günlüğünü ALDIN ve bu hatadan sen sorumlusun.

İŞLEM/BUG LOGU (işlediğin günlük):
$new_text

YAPILACAKLAR (sırayla):
1. Bu logu analiz et: hangi bileşeni (daemon / cli / gui / include / tests)
   ilgilendirdiğini ve kök nedeni belirle.
2. İlgili kaynak kodu incele. Hata gerçek bir bug ise minimal, mevcut kod
   konvansiyonlarına uygun (AGENTS.md'ye bak) bir düzeltme uygula.
3. Düzeltmeyi mutlaka tests/run_tests.sh ile; accel/include değişikliği
   yaptıysan ayrıca tests/run_tests_asan.sh ile doğrula. Testler geçmeden
   iş bitirme.
4. Testlerin TAMAMI geçtiyse: Bug Hata Raporları.md dosyasını düzenleyerek
   az önce işlediğin rapor bloğunu DOSYADAN TAMAMEN SİL. (Düzeltme
   yapılamadıysa veya testler geçmediyse SİLME, "DÜZELTİLEMEDİ: <neden>"
   notu ekle ve dur.)
EOF
)

  local tmp_prompt
  tmp_prompt="$(mktemp "$STATE_DIR/prompt.XXXXXX")"
  printf '%s\n' "$prompt" > "$tmp_prompt"

  local code=0
  # opencode'u proje kök dizininde, headless olarak tetikle.
  ( cd "$PROJECT_DIR" && opencode run "$(cat "$tmp_prompt")" ) || code=$?
  rm -f "$tmp_prompt"

  if [[ "$code" -eq 0 ]]; then
    # Başarı: agent raporu silmiş olabilir.  MARK'ı son içeriğe ilerlet, sayaçları temizle.
    if [[ -f "$REPORT_FILE" ]]; then
      cp "$REPORT_FILE" "$PREV_FILE"
      md5sum "$REPORT_FILE" | cut -d' ' -f1 > "$MARK"
    fi
    rm -f "$retry_cnt_file" "$report_txt_file"
    log "opencode tamamlandı (çıkış 0). Rapor düzeltilmişse dosyadan silindi."
  else
    if [[ "$retries" -lt "$MAX_ATTEMPTS" ]]; then
      # M-5b: MARK ilerletme, sayaç bırak — sonraki poll aynı raporu yeniden dener.
      printf '%s\n' "$retries" > "$retry_cnt_file"
      log "opencode çıktı kodu $code ile bitti (deneme $retries/$MAX_ATTEMPTS)."
      log "MARK ilerletilmedi — sonraki kontrol aynı raporu yeniden deneyecek."
      log "Düzeltme hala varsa: $STATE_DIR/last_change.txt | log: $LOGFILE"
    else
      # Üst sınır aşıldı: MARK'ı ilerlet (vazgeç), artık yeniden denenmez.
      if [[ -f "$REPORT_FILE" ]]; then
        cp "$REPORT_FILE" "$PREV_FILE"
        md5sum "$REPORT_FILE" | cut -d' ' -f1 > "$MARK"
      fi
      rm -f "$retry_cnt_file"
      log "opencode art arda $MAX_ATTEMPTS kez başarısız oldu — bu rapor bir daha denenmeyecek."
      log "Elle inceleyin: $STATE_DIR/last_change.txt | rapor: $REPORT_FILE"
    fi
  fi
  log "=========================================================================="
}

lock
trap unlock EXIT

[[ ! -f "$REPORT_FILE" ]] && { log "rapor dosyası bulunamadı: $REPORT_FILE"; exit 1; }

# Başlangıç durumu: dosyanın mevcut hali işaretlenir, geçmiş sayılmaz.
if [[ ! -f "$MARK" ]]; then
  cp "$REPORT_FILE" "$PREV_FILE"
  md5sum "$REPORT_FILE" | cut -d' ' -f1 > "$MARK"
  log "izleme başladı (başlangıç durumu sabitlendi): $REPORT_FILE"
fi

if [[ "$MODE" == "--once" ]]; then
  cur="$(md5sum "$REPORT_FILE" | cut -d' ' -f1)"
  old="$(cat "$MARK")"
  if [[ "$cur" != "$old" ]]; then
    handle_change
  else
    log "değişiklik yok"
  fi
  exit 0
fi

log "izlemeye başlandı — Ctrl+C ile durdur (durum dizini: $STATE_DIR)"
while true; do
  if [[ -f "$REPORT_FILE" ]]; then
    cur="$(md5sum "$REPORT_FILE" | cut -d' ' -f1)"
    old="$(cat "$MARK")"
    if [[ "$cur" != "$old" ]]; then
      handle_change
    fi
  fi
  sleep 2
done
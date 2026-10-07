# CI Billing / Runner Notu (Ops)

**Durum:** 2026-10-07 itibarıyla GitHub Actions üzerinde hiçbir run başlamadı —
billing lock (Actions dakika/kredi sınırı) tüm workflow'ları
queue'da bekletiyor. Detay: Actions sekmesi + repo Settings → Billing.

## Etkilenen CI işi

`.github/workflows/ci.yml` içindeki beş job (build-and-test, sanitizers,
sanitize-cli, fuzz-smoke, perf-gate) **şu an çalışmıyor** — bu geçici bir
durum, kod veya workflow hatası değil. Yedi kapı (AGENTS.md) yerelde
çalıştırılabildiği sürece bloker değil.

## Alternatifler (kısa vadede)

1. **Self-hosted runner** — GitHub IP forwarding/kayıt adımları:
   `Settings → Actions → Runners → New self-hosted runner` (Linux x64).
   E2E testleri `/dev/uinput` istediği için runner kullanıcısını `input`
   grubuna ekle: `sudo usermod -aG input $USER`. Docker içinde çalıştırılırsa
   `--device=/dev/uinput` ve `--group-add input` gerekli.
2. **Artifact'lı log** — yerel kapıları koştur, çıktıyı artifact olarak
   sakla:
   ```bash
   mkdir -p /tmp/ci-log
   bash tests/run_tests.sh 2>&1 | tee /tmp/ci-log/run_tests.log
   bash tests/oracle/run_oracle.sh 2>&1 | tee /tmp/ci-log/oracle.log
   # ... diğer kapılar için aynı ...
   tar czf ci-logs.tgz -C /tmp ci-log
   ```
3. **Kendi CI'ımız** — repoya bir `tests/ci_local.sh` ekleyin: yedi kapıyı
   sırayla koşturur, logları dosyaya yazar, artifact tar.gz üretir,
   `exit 0` = hepsi yeşil. Örnek iskelet:
   ```bash
   #!/usr/bin/env bash
   set -uo pipefail
   LOG=ci-local-$(date +%Y%m%d-%H%M%S).log
   fail=0
   for g in scripts/build.sh tests/run_tests.sh \
            tests/oracle/run_oracle.sh tests/run_simd_parity.sh \
            tests/run_tr_coverage.sh tests/run_cli_sanitized.sh \
            tests/run_tracker_bridge.sh; do
     echo "== $g ==" | tee -a "$LOG"
     bash "$g" 2>&1 | tee -a "$LOG" || fail=1
   done
   tar czf "ci-local-$(date +%Y%m%d).tgz" "$LOG"
   exit $fail
   ```

## Karar

- Billing sorun çözülene kadar **zorunlu** kanal: yerel `tests/ci_local.sh`
  + artifact. PR kapatılırken log'un artifact olarak iliştirilmesi yeterli
  kanıt sayılır.
- Hız/parallelism gerekirse self-hosted runner'a geç (E2E dahil kapılar
  açılabilir).

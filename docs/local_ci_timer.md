# Local CI timer

Because GitHub Actions CI is blocked by account billing, the canonical gate set
is also runnable locally and scheduled daily via systemd user timer.

Files:

- `~/.config/systemd/user/rawaccel-ci-local.service`
- `~/.config/systemd/user/rawaccel-ci-local.timer`

Check:

```bash
systemctl --user status rawaccel-ci-local.timer
systemctl --user list-timers rawaccel-ci-local.timer
```

Manual run:

```bash
bash tests/ci_local.sh
```

Logs land in `logs/ci_local_<timestamp>/`.

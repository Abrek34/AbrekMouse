#!/bin/bash
# RawAccel Linux Uninstaller
set -e
set -o pipefail

echo "=== RawAccel Linux Uninstaller ==="
echo ""

if [[ $EUID -ne 0 ]]; then
    echo "ERROR: Please run as root: sudo $0"
    exit 1
fi

# Stop and disable service
if systemctl is-active --quiet rawaccel 2>/dev/null; then
    echo "[1/4] Stopping service..."
    systemctl stop rawaccel
fi
if systemctl is-enabled --quiet rawaccel 2>/dev/null; then
    echo "      Disabling service..."
    systemctl disable rawaccel
fi
# Kill any rawaccel-daemon started outside systemd (e.g. directly via pkexec).
# Without this, removing the binary leaves the running process holding /dev/uinput.
pkill -TERM -x rawaccel-daemon 2>/dev/null || true
# R4-L-8: also stop a running rawaccel-gui — remove the binary beneath a live
# GUI leaves the process + its daemon-watch alive (the user's mouse keeps
# working from a process tree that no longer exists on disk).
pkill -TERM -x rawaccel-gui 2>/dev/null || true
sleep 0.3
pkill -KILL -x rawaccel-daemon 2>/dev/null || true
pkill -KILL -x rawaccel-gui 2>/dev/null || true
# Clean up PID/socket artefacts
rm -f /run/rawaccel.pid /run/rawaccel.sock /tmp/rawaccel.pid /tmp/rawaccel.sock 2>/dev/null || true
# P121/BUG-09: the R49 daemon writes the pid+sock under $XDG_RUNTIME_DIR
# (=/run/user/$UID) — the /run /tmp paths alone left these behind.
# nullglob: skip silently when /run/user is empty (no literal /run/user/*).
shopt -s nullglob
for d in /run/user/*; do
    rm -f "$d/rawaccel.pid" "$d/rawaccel.sock" 2>/dev/null || true
done
shopt -u nullglob

# Remove service file (R44: setup.sh installs to /usr/lib; /etc + /etc/user
# are legacy shadow locations cleaned for completeness)
echo "[2/4] Removing service file..."
rm -f /usr/lib/systemd/system/rawaccel.service
rm -f /etc/systemd/system/rawaccel.service
rm -f /etc/systemd/user/rawaccel.service
systemctl daemon-reload

# Remove binaries (setup.sh → /usr/bin; PKGBUILD → /usr/bin; /usr/local/bin
# is a legacy location kept for cleaning old installs)
echo "[3/4] Removing binaries..."
rm -f /usr/local/bin/rawaccel-daemon
rm -f /usr/local/bin/rawaccel-cli
rm -f /usr/local/bin/rawaccel-gui
rm -f /usr/bin/rawaccel-daemon
rm -f /usr/bin/rawaccel-cli
rm -f /usr/bin/rawaccel-gui
# M-1: user-level binaries shadow system ones on PATH.
shopt -s nullglob
rm -f /home/*/.local/bin/rawaccel-daemon \
      /home/*/.local/bin/rawaccel-cli \
      /home/*/.local/bin/rawaccel-gui
shopt -u nullglob

# Remove system files
echo "[4/4] Removing system files..."
rm -f /etc/udev/rules.d/99-rawaccel.rules
rm -f /usr/lib/udev/rules.d/99-rawaccel.rules
rm -f /etc/modules-load.d/rawaccel.conf
rm -f /usr/share/applications/rawaccel.desktop
# 0.6.4 (BUG-02) sonrası polkit bölümleri kurulmuyor; eski kurulum kalıntısıysa temizle:
rm -f /usr/share/polkit-1/actions/org.rawaccel.policy
rm -f /usr/share/polkit-1/rules.d/49-rawaccel.rules
# Remove only RawAccel's dedicated libinput quirk file.  Do not touch
# /etc/libinput/local-overrides.quirks because it may contain user or
# third-party overrides.
rm -f /usr/share/libinput/50-rawaccel.quirks
rm -f /etc/modprobe.d/rawaccel.conf
udevadm control --reload-rules 2>/dev/null || true

# Keep /etc/rawaccel/settings.json so config is not lost
echo ""
echo "=== Uninstall complete! ==="
echo ""
echo "NOTE: /etc/rawaccel/settings.json was kept (your config)."
echo "      To remove it too: sudo rm -rf /etc/rawaccel"
echo ""
echo "NOTE: User config ~/.config/rawaccel/ was not touched."

# P121/BUG-09: remove KDE kwinrc traces for every user (per-device
# "(RawAccel)" libinput overrides + restore adaptive global), matching
# setup.sh --uninstall which runs kde-fix-accel.sh --remove for $REAL_USER.
KDE_FIX="$(cd "$(dirname "$0")" && pwd)/kde-fix-accel.sh"
if [[ -x "$KDE_FIX" ]]; then
    shopt -s nullglob
    homes=(/home/*/.config/kwinrc)
    shopt -u nullglob
    if [[ ${#homes[@]} -gt 0 ]]; then
        for home in /home/*; do
            [[ -f "$home/.config/kwinrc" ]] || continue
            user="$(basename "$home")"
            echo "      Removing kwinrc traces for user '$user'..."
            # H-3: sudo resets session env by default; preserve the KDE
            # detection variables or --remove silently no-ops.
            if out="$(sudo -u "$user" --preserve-env=XDG_CURRENT_DESKTOP,DESKTOP_SESSION,DBUS_SESSION_BUS_ADDRESS,XDG_RUNTIME_DIR \
                bash "$KDE_FIX" --remove 2>&1)"; then
                # R4-L-8: success path may still print diagnostics — surface
                # genuine error/failure lines instead of hiding the stream.
                grep -i -E "error|fail|hata|bulunamad" <<<"$out" \
                    && echo "      (see above for '$user' kwinrc cleanup note)" || true
            else
                echo "      (kwinrc trace cleanup failed for '$user': $(head -1 <<<"$out"))"
            fi
        done
    fi
fi

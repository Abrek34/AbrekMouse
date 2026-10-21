#!/usr/bin/env python3
"""AJ-H — sahte rawaccel daemon IPC sunucusu (liveness kanıtı için).

Gerçek daemon'ın yaptığı gibi: UNIX sokete bağlanır, satır bazlı komutu
okur, "status" için geçerli bir status JSON'u döner, bağlantıyı kapatır.

  fake_daemon.py <socket_yolu> <pid_modu>
    pid_modu: "yok"   → PID dosyası YAZILMAZ (canlı daemon, PID dosyası yok)
               "bozuk" → PID dosyasına ÖLÜ PID (999999) yazılır (bayat PID)
"""
import json
import os
import socket
import sys

sock_path = sys.argv[1]
pid_mode = sys.argv[2] if len(sys.argv) > 2 else "yok"

STATUS = json.dumps({
    "running": True,
    "active_profile": "sahte",
    "devices": [{
        "name": "SAHTE-CIHAZ",
        "path": "/dev/input/event99",
        "device_id": "usb:SAHTE:000000000000",
        "dpi": 1600,
        "poll_rate": 1000,
        "real_polling_rate": 1000,
    }],
})

pid_path = os.path.join(os.path.dirname(sock_path), "rawaccel.pid")
if pid_mode == "yok":
    if os.path.exists(pid_path):
        os.unlink(pid_path)
else:
    with open(pid_path, "w") as f:
        f.write(("999999\n") if pid_mode == "bozuk" else (pid_mode + "\n"))

try:
    os.unlink(sock_path)
except FileNotFoundError:
    pass

srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sock_path)
srv.listen(8)

while True:
    try:
        conn, _ = srv.accept()
    except OSError:
        break
    try:
        conn.recv(4096)
        conn.sendall(STATUS.encode())
    finally:
        conn.close()

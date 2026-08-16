#!/usr/bin/env python3
"""
reginsite - Locker Bridge (headless)  ·  SCREEN-DRIVEN
======================================================
Middleman between the server and the Arduino on the mini PC.

    touchscreen -> Laravel -> (command queue) -> THIS bridge -> Arduino
    Arduino -> THIS bridge -> Laravel (confirm) -> MySQL

Loop:
  * Poll  GET /api/esp32/commands  -> for each, send  OPEN,<locker>,<mode>  to the Mega.
  * Read the Mega:  DONE,<locker>,<uid>  -> POST /api/esp32/confirm {command_id, uid}
                    TIMEOUT,<locker>     -> POST /api/esp32/confirm {command_id, timeout:true}

Deps:  pip install pyserial          (HTTP via the standard library)
Config: bridge/config.ini  (serial_port, base_url, api_key)
"""

import configparser
import json
import os
import sys
import time
import urllib.error
import urllib.request

try:
    import serial
except ImportError:
    sys.exit("pyserial is not installed. Run:  pip install pyserial")

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "config.ini")
POLL_EVERY = 1.0        # seconds between command polls


def load_config():
    cfg = configparser.ConfigParser()
    if not cfg.read(CONFIG_PATH):
        sys.exit(f"Missing config file: {CONFIG_PATH}")
    b = cfg["bridge"]
    return {
        "port": b.get("serial_port", "COM3"),
        "baud": b.getint("baud_rate", 115200),
        "base_url": b.get("base_url", "http://localhost:8000").rstrip("/"),
        "api_key": b.get("api_key", "regin-esp32-2026"),
    }


def api(cfg, method, path, payload=None):
    """Call the device API. Returns (ok, data|error)."""
    url = f"{cfg['base_url']}/api/esp32/{path}"
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("X-API-Key", cfg["api_key"])
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=6) as resp:
            return True, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            return False, json.loads(e.read().decode("utf-8")).get("error", f"HTTP {e.code}")
        except Exception:
            return False, f"HTTP {e.code}"
    except (urllib.error.URLError, ConnectionError, OSError) as e:
        return None, str(e)


def main():
    cfg = load_config()
    print("reginsite bridge (screen-driven)")
    print(f"  serial : {cfg['port']} @ {cfg['baud']}")
    print(f"  server : {cfg['base_url']}")

    outstanding = {}   # locker_number -> command_id awaiting DONE/TIMEOUT

    while True:
        try:
            ser = serial.Serial(cfg["port"], cfg["baud"], timeout=0.2)
        except serial.SerialException as e:
            print(f"  cannot open {cfg['port']}: {e}  — retry in 3s (Arduino plugged in? Serial Monitor closed?)")
            time.sleep(3)
            continue

        print(f"  connected to {cfg['port']}. Waiting... (Ctrl+C to stop)")
        time.sleep(2)
        last_poll = 0

        try:
            while True:
                # --- read anything the Mega said -----------------------------
                line = ser.readline().decode("utf-8", errors="replace").strip()
                if line:
                    print(f"[mega] {line}")
                    if line.startswith("DONE,"):
                        _, locker, uid = (line.split(",", 2) + ["", ""])[:3]
                        cid = outstanding.pop(int(locker), None) if locker.isdigit() else None
                        if cid:
                            ok, data = api(cfg, "POST", "confirm", {"command_id": cid, "uid": uid})
                            if ok:
                                r = data.get("result", {})
                                print(f"  -> {data.get('action','?').upper()} saved: {data.get('tool')} (tx #{r.get('txId')})")
                            else:
                                print(f"  -> confirm failed: {data}")
                    elif line.startswith("TIMEOUT,"):
                        locker = line.split(",", 1)[1]
                        cid = outstanding.pop(int(locker), None) if locker.isdigit() else None
                        if cid:
                            api(cfg, "POST", "confirm", {"command_id": cid, "timeout": True})
                            print(f"  -> locker {locker} timed out; cancelled.")

                # --- poll the server for new OPEN commands -------------------
                if time.time() - last_poll >= POLL_EVERY:
                    last_poll = time.time()
                    ok, data = api(cfg, "GET", "commands")
                    if ok:
                        for c in data.get("commands", []):
                            locker = c.get("lockerId")
                            mode = c.get("mode", "borrow")
                            ser.write(f"OPEN,{locker},{mode}\n".encode("utf-8"))
                            outstanding[int(locker)] = c["id"]
                            print(f"  <- OPEN locker {locker} ({mode}) [cmd {c['id']}]")
        except serial.SerialException:
            print("  serial disconnected — reopening...")
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(2)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nbridge stopped.")

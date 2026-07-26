#!/usr/bin/env python3
"""
reginsite - Serial -> API bridge (runs on the mini PC)
======================================================
Reads borrow/return events from the Arduino Mega over USB serial and writes
them to the database through the Laravel device API. This is what actually
"saves to the database" -- the Mega has no network, so the mini PC does it.

Flow:
    Arduino Mega --(USB serial)--> THIS bridge --(HTTP)--> Laravel --> MySQL

Serial protocol (from the sketch):
    READY
    SCAN,<uid>
    EVENT,OPEN,<uid>    -> POST /api/esp32/borrow
    EVENT,CLOSE,<uid>   -> POST /api/esp32/return
We reply to the Mega with 'ACK' on success or 'NAK,<msg>' on failure.

Offline safety net: if the server is unreachable, the event is appended to
queue.jsonl and replayed via /api/esp32/sync once the server is back.

Dependencies:  pip install pyserial      (HTTP uses the Python standard library)
Config:        bridge/config.ini
"""

import configparser
import json
import os
import sys
import time
import urllib.error
import urllib.request

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial is not installed. Run:  pip install pyserial")

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "config.ini")
QUEUE_PATH = os.path.join(HERE, "queue.jsonl")


# --------------------------------------------------------------------------- #
#  Config                                                                      #
# --------------------------------------------------------------------------- #
def load_config():
    cfg = configparser.ConfigParser()
    if not cfg.read(CONFIG_PATH):
        sys.exit(f"Missing config file: {CONFIG_PATH}")
    b = cfg["bridge"]
    return {
        "port": b.get("serial_port", "COM4"),
        "baud": b.getint("baud_rate", 115200),
        "base_url": b.get("base_url", "http://localhost:8000").rstrip("/"),
        "api_key": b.get("api_key", "regin-esp32-2026"),
        "student_qr": b.get("test_student_qr", "QR-2026-0132"),
        "tool_id": b.getint("tool_id", 10),
    }


# --------------------------------------------------------------------------- #
#  HTTP (stdlib only)                                                          #
# --------------------------------------------------------------------------- #
def api_post(cfg, path, payload):
    """POST JSON to the device API. Returns (ok: bool, data: dict|str)."""
    url = f"{cfg['base_url']}/api/esp32/{path}"
    body = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=body, method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("X-API-Key", cfg["api_key"])
    try:
        with urllib.request.urlopen(req, timeout=6) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            return True, data
    except urllib.error.HTTPError as e:
        # Server answered but rejected it (e.g. tool unavailable, bad key).
        try:
            data = json.loads(e.read().decode("utf-8"))
            return False, data.get("error", f"HTTP {e.code}")
        except Exception:
            return False, f"HTTP {e.code}"
    except (urllib.error.URLError, ConnectionError, OSError) as e:
        # Could not reach the server at all -> treat as offline.
        return None, str(e)  # None == offline (queue it)


# --------------------------------------------------------------------------- #
#  Offline queue                                                               #
# --------------------------------------------------------------------------- #
def queue_event(kind, cfg, uid):
    event = {
        "type": "borrow" if kind == "OPEN" else "return",
        "qr": cfg["student_qr"],
        "tool_id": cfg["tool_id"],
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "uid": uid,
    }
    with open(QUEUE_PATH, "a", encoding="utf-8") as f:
        f.write(json.dumps(event) + "\n")
    print(f"  [queued offline] {event['type']} tool {event['tool_id']}")


def flush_queue(cfg):
    """Replay queued events through /api/esp32/sync. No-op if queue is empty."""
    if not os.path.exists(QUEUE_PATH) or os.path.getsize(QUEUE_PATH) == 0:
        return
    events = []
    with open(QUEUE_PATH, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                events.append(json.loads(line))
    if not events:
        return

    ok, data = api_post(cfg, "sync", {"events": events})
    if ok:
        results = data.get("results", [])
        good = sum(1 for r in results if r.get("ok"))
        print(f"  [sync] flushed {good}/{len(events)} queued event(s)")
        os.remove(QUEUE_PATH)   # clear once accepted
    else:
        print(f"  [sync] server not ready, keeping queue ({data})")


# --------------------------------------------------------------------------- #
#  Event handling                                                             #
# --------------------------------------------------------------------------- #
def handle_event(cfg, ser, kind, uid):
    action = "borrow" if kind == "OPEN" else "return"
    payload = {"qr": cfg["student_qr"], "tool_id": cfg["tool_id"]}
    ok, data = api_post(cfg, action, payload)

    if ok:
        result = data.get("result", {})
        print(f"  -> {action.upper()} saved. tx #{result.get('txId')} "
              f"(tool {cfg['tool_id']}, {cfg['student_qr']})")
        ser.write(b"ACK\n")
    elif ok is None:
        # Offline: buffer and replay later.
        queue_event(kind, cfg, uid)
        ser.write(b"NAK,offline\n")
    else:
        # Server rejected (e.g. tool already borrowed / student blocked).
        print(f"  -> {action.upper()} REJECTED: {data}")
        ser.write(f"NAK,{data}\n".encode("utf-8"))


# --------------------------------------------------------------------------- #
#  Main loop                                                                   #
# --------------------------------------------------------------------------- #
def main():
    cfg = load_config()
    print("reginsite bridge")
    print(f"  serial : {cfg['port']} @ {cfg['baud']}")
    print(f"  server : {cfg['base_url']}  (tool_id={cfg['tool_id']}, student={cfg['student_qr']})")

    while True:
        try:
            ser = serial.Serial(cfg["port"], cfg["baud"], timeout=1)
        except serial.SerialException as e:
            print(f"  cannot open {cfg['port']}: {e}  -- retrying in 3s "
                  f"(is the Mega plugged in / Serial Monitor closed?)")
            time.sleep(3)
            continue

        print(f"  connected to {cfg['port']}. Waiting for scans... (Ctrl+C to stop)")
        time.sleep(2)          # let the board reset after the port opens
        flush_queue(cfg)       # push anything buffered while we were down

        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                print(f"[mega] {line}")

                if line == "READY":
                    flush_queue(cfg)
                elif line.startswith("EVENT,"):
                    parts = line.split(",")
                    if len(parts) >= 3:
                        _, kind, uid = parts[0], parts[1], parts[2]
                        handle_event(cfg, ser, kind, uid)
                # SCAN,<uid> lines are informational only.
        except serial.SerialException:
            print("  serial disconnected -- reopening...")
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

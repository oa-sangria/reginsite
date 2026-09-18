#!/usr/bin/env python3
"""
reginsite - Locker Bridge (headless)  ·  SCREEN-DRIVEN
======================================================
Middleman between the server and the Arduino on the mini PC.

    touchscreen -> Laravel -> (command queue) -> THIS bridge -> Arduino
    Arduino -> THIS bridge -> Laravel (confirm) -> MySQL

Loop:
  * Poll  GET /api/esp32/commands  -> queue them; send  OPEN,<locker>,<mode>  one at a time.
  * While a door is open, poll  POST /api/esp32/command-status  -> if the kiosk cancelled
    it (status went terminal behind our back), send  ABORT  so the Mega relocks now.
  * Read the Mega:  DONE,<locker>,<uid>,<slot> -> POST /api/esp32/confirm {command_id, uid, slot}
                    TIMEOUT,<locker>           -> POST /api/esp32/confirm {command_id, timeout:true, reason}
                    NOWIRE,<locker>            -> same, reason "nowire"

Deps:  pip install pyserial          (HTTP via the standard library)
Config: bridge/config.ini  (serial_port, base_url, api_key)
"""

import configparser
import http.client
import json
import os
import sys
import time
import traceback
import urllib.error
import urllib.request
from collections import deque

try:
    import serial
except ImportError:
    sys.exit("pyserial is not installed. Run:  pip install pyserial")

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "config.ini")
POLL_EVERY = 1.0        # seconds between command polls
OPEN_WINDOW_S = 45      # must match OPEN_TIMEOUT_MS in locker_controller.ino
STALE_AFTER = OPEN_WINDOW_S + 15.0
                        # give up on a command the Mega never answers. Sits above
                        # the Mega's own window, so this only fires when the board
                        # is unplugged, wedged, or answering a different port.
RETRY_FOR = 600.0       # keep retrying an undeliverable confirm for 10 minutes


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
    """Call the device API. Returns (ok, data|error):
         True,  dict   — server answered 2xx
         False, str    — server answered with an error (4xx/5xx): a real verdict
         None,  str    — never got a verdict: unreachable, timed out, garbage body
    Callers treat None as "try again later", never as a rejection."""
    url = f"{cfg['base_url']}/api/esp32/{path}"
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("X-API-Key", cfg["api_key"])
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=6) as resp:
            body = json.loads(resp.read().decode("utf-8"))
            if not isinstance(body, dict):
                return None, f"non-object JSON from {path}"
            return True, body
    except urllib.error.HTTPError as e:
        try:
            return False, json.loads(e.read().decode("utf-8")).get("error", f"HTTP {e.code}")
        except Exception:
            return False, f"HTTP {e.code}"
    except (urllib.error.URLError, ConnectionError, OSError,
            http.client.HTTPException, ValueError) as e:
        # URLError: DNS/refused. OSError: socket timeout, reset. HTTPException:
        # RemoteDisconnected / IncompleteRead / BadStatusLine when Laravel is
        # restarting. ValueError: JSONDecodeError when the body is an HTML error
        # page. All transient; none may take the bridge down.
        return None, str(e)


def main():
    cfg = load_config()
    print("reginsite bridge (screen-driven)")
    print(f"  serial : {cfg['port']} @ {cfg['baud']}")
    print(f"  server : {cfg['base_url']}")

    # locker_number -> [command_id, deadline]. The deadline exists so a board that
    # is unplugged mid-transaction (its DONE never arrives) cannot leave the
    # command 'sent' forever with the kiosk waiting on it.
    outstanding = {}
    # Commands fetched from the server but not yet written to the Mega. Needed
    # because the server marks them 'sent' the moment we poll, so a command we
    # decline to write now would otherwise be lost.
    queue = deque()

    def pending(locker):
        """command_id awaiting DONE/TIMEOUT for this locker, or None."""
        if not str(locker).isdigit():
            return None
        e = outstanding.get(int(locker))
        return e[0] if e else None

    # Lockers whose OPENED line has arrived. A SCAN is only "inside the window"
    # after that: the idle scan can leave a stale SCAN in the serial buffer at
    # the exact moment we dispatch OPEN, and relaying it would tick "tag read"
    # on the kiosk before the Mega has read anything for real.
    opened = set()

    def resolve(locker):
        """Forget this locker's command once the physical side is settled."""
        if str(locker).isdigit():
            outstanding.pop(int(locker), None)
            opened.discard(int(locker))

    # Confirms the server could not be reached for. The Mega has already
    # relocked by the time we post a confirm, so the physical side is settled
    # and the only thing at stake is the DATABASE ROW — a borrow that happened
    # but was never written. Retried once a second until the server answers,
    # for up to RETRY_FOR; the server's terminal-state guard makes a duplicate
    # delivery harmless. Entries: [payload, label, first_attempt_ts].
    unsent = deque()

    def post_confirm(payload, label):
        """POST confirm; on a transport failure queue it for retry.
        Returns (ok, data) exactly like api(); None means 'queued'."""
        ok, data = api(cfg, "POST", "confirm", payload)
        if ok is None:
            unsent.append([payload, label, time.time()])
            print(f"  -> server unreachable ({data}); {label} queued for retry "
                  f"({len(unsent)} waiting)")
        return ok, data

    def report_confirm(ok, data, label):
        if ok:
            if data.get("action"):
                r = data.get("result") or {}
                print(f"  -> {data['action'].upper()} saved: {data.get('tool')} (tx #{r.get('txId')})")
            else:
                # ok:true without an action = the command was already terminal
                # (kiosk cancelled it first). The server recorded nothing.
                print(f"  -> {label} ignored: {data.get('result')}")
        elif ok is False:
            # The server REJECTED the scan (wrong locker's tool, unknown tag,
            # not available). That is final: the door has already re-locked
            # and the server has marked the command failed with the reason,
            # so the kiosk can show it. Nothing to retry.
            print(f"  -> {label} REJECTED: {data}")

    def retry_unsent():
        """One pass over the retry queue, oldest first. Stops at the first
        transport failure (server is still down — no point hammering)."""
        while unsent:
            payload, label, first = unsent[0]
            if time.time() - first > RETRY_FOR:
                unsent.popleft()
                print(f"  !! GAVE UP after {int(RETRY_FOR)}s: {label} {json.dumps(payload)}"
                      f"  <- enter this manually if it was a real borrow/return")
                continue
            ok, data = api(cfg, "POST", "confirm", payload)
            if ok is None:
                return
            unsent.popleft()
            print(f"  -> retry delivered after {int(time.time() - first)}s:")
            report_confirm(ok, data, label)

    def progress(locker, stage):
        """Relay a mid-window event (tag read / tool moved) for the kiosk to
        show. Cosmetic — fire and forget, never retried, never blocks."""
        cid = pending(locker)
        if cid:
            api(cfg, "POST", "command-progress", {"command_id": cid, "stage": stage})

    def handle_line(line):
        """React to one line from the Mega. Anything not listed is just logged."""
        print(f"[mega] {line}")
        if line.startswith("OPENED,"):
            locker = line.split(",", 1)[1]
            if pending(locker):
                opened.add(int(locker))

        elif line.startswith("SCAN,"):
            # A tag read. Only meaningful for the kiosk if a window is actually
            # open on the Mega (see `opened`); the idle scan's SCANs are just
            # logged. DONE may follow at once or only after the slot sensor
            # agrees; either way the student should see "tag read" right now.
            for lk in list(opened):
                progress(lk, "scanned")

        elif line.startswith("MOVED,"):
            # MOVED,<locker>,<slot> — the slot sensor saw the tool leave/return
            # (or the cabinet is tag-only and the Mega says so up front). Only
            # ever printed inside a window. Purely for the kiosk's step rail.
            progress(line.split(",")[1], "moved")

        elif line.startswith("DONE,"):
            # DONE,<locker>,<uid>,<slot>  — the Mega always sends 4 fields.
            # Splitting with maxsplit=2 would glue ",<slot>" onto the uid, and
            # Tool::normTag keeps digits, so the tag would never match.
            parts  = line.split(",", 3)
            locker = parts[1] if len(parts) > 1 else ""
            uid    = parts[2] if len(parts) > 2 else ""
            slot   = parts[3] if len(parts) > 3 else "0"
            cid = pending(locker)
            if not cid:
                return
            payload = {"command_id": cid, "uid": uid}
            if slot.isdigit() and int(slot) > 0:
                payload["slot"] = int(slot)
            # The door is locked whatever the server says, so the locker is
            # free again either way — resolve first, then deal with the row.
            resolve(locker)
            ok, data = post_confirm(payload, f"confirm cmd {cid} uid {uid}")
            report_confirm(ok, data, "confirm")

        elif line.startswith("TIMEOUT,"):
            locker = line.split(",", 1)[1]
            cid = pending(locker)
            if cid:
                resolve(locker)
                # `reason` lets the server tell the kiosk what happened.
                post_confirm({"command_id": cid, "timeout": True, "reason": "timeout"},
                             f"timeout cmd {cid}")
                print(f"  -> locker {locker} timed out; cancelled.")

        elif line.startswith("NOWIRE,"):
            # That cabinet lives on the other controller (or isn't built yet).
            # Cancel it: otherwise the command stays 'sent' forever and the
            # kiosk waits on a door that is never going to open.
            locker = line.split(",", 1)[1]
            cid = pending(locker)
            if cid:
                resolve(locker)
                post_confirm({"command_id": cid, "timeout": True, "reason": "nowire"},
                             f"nowire cmd {cid}")
            print(f"  -> locker {locker} is NOT on this controller; cancelled.")

    def inject_hook(ser):
        # --- TEMPORARY BRING-UP TEST HOOK ------------------------------------
        # Drop a line of text into inject.txt and the bridge forwards it to the
        # Mega. Lets the full chain be exercised without a tag in hand. REMOVE
        # once hardware testing is finished.
        _inj = os.path.join(HERE, "inject.txt")
        if not os.path.exists(_inj):
            return
        try:
            # utf-8-sig strips a BOM if an editor (or PowerShell's -Encoding
            # utf8) wrote one; the Mega parses bytes, so a stray BOM would
            # silently break the command match.
            with open(_inj, "r", encoding="utf-8-sig") as fh:
                _payload = fh.read().strip()
            os.remove(_inj)
            if _payload:
                ser.write((_payload + "\n").encode("utf-8"))
                print(f"  <- INJECT {_payload}")
        except OSError as e:
            print(f"  inject failed: {e}")

    def drain_serial(ser):
        # Drain the whole receive buffer each pass, not one line. Reading a
        # single line per iteration, with an HTTP poll in between, only ever
        # kept up because the Mega used to say almost nothing. With the sensor
        # sample stream it emits ~16 lines/s; one-per-pass drained ~1/s, the OS
        # buffer overflowed, and DONE/TIMEOUT arrived 60 lines late — after the
        # stale watchdog had already cancelled the command. The cap keeps a
        # runaway stream from starving the command poll.
        drained = 0
        while drained < 200:
            if drained and ser.in_waiting == 0:
                break
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if not line:
                break
            drained += 1
            handle_line(line)

    def stale_watchdog():
        # Give up on commands the Mega never answered (unplugged mid-window,
        # wedged, or on the other COM port). Its own window is OPEN_WINDOW_S;
        # STALE_AFTER sits above that so a slow-but-alive board is never cut off.
        now = time.time()
        for lk in [k for k, (_, due) in list(outstanding.items()) if now > due]:
            cid = pending(lk)
            resolve(lk)
            if cid:
                post_confirm({"command_id": cid, "timeout": True, "reason": "stale"},
                             f"stale cmd {cid}")
                print(f"  -> locker {lk} never answered ({int(STALE_AFTER)}s); cancelled.")

    def poll_server(ser):
        """Once per POLL_EVERY: retries, cancel check, then fetch new commands."""
        retry_unsent()

        # --- did the kiosk cancel the door we have open? ---------------------
        # Cancel only flips the command to a terminal status on the server;
        # nothing tells the Mega. Without this the door stays open for the rest
        # of its window, and a tool taken then is never recorded (its DONE hits
        # "already timeout"). So while a command is outstanding, ask the server
        # once a second and send ABORT the moment it has gone terminal behind
        # our back. The Mega answers ABORT with TIMEOUT,<locker>; by then the
        # locker is already resolved here, so that line is ignored.
        for lk, (cid, _) in list(outstanding.items()):
            ok, data = api(cfg, "POST", "command-status", {"command_id": cid})
            if ok and data.get("status") in ("done", "timeout", "failed"):
                ser.write(b"ABORT\n")
                resolve(lk)
                print(f"  <- ABORT locker {lk}: command {cid} is already "
                      f"{data['status']} on the server ({data.get('note') or 'no note'})")

        # --- fetch new OPEN commands ----------------------------------------
        # GET /commands flips pending -> sent, so anything fetched is ours and
        # will never be handed out again. Queue it locally rather than writing
        # it straight to the Mega.
        ok, data = api(cfg, "GET", "commands")
        if not ok:
            return
        for c in data.get("commands") or []:
            if not isinstance(c, dict) or not str(c.get("id", "")).isdigit() \
                    or not str(c.get("lockerId", "")).isdigit():
                print(f"  ?? skipping malformed command from server: {c!r}")
                continue
            queue.append(c)
            print(f"  queued cmd {c['id']} (locker {c.get('lockerId')}, {c.get('mode', 'borrow')})")

    def dispatch(ser):
        # SINGLE-FLIGHT, globally. handleOpen() blocks the Mega for the whole
        # open window, so writing a second OPEN just buries it in the 64-byte
        # serial buffer: the student gets a door opening they did not ask for,
        # and because `outstanding` is keyed by locker the second command
        # silently overwrites the first, so the wrong one gets cancelled. There
        # is also only ONE RC522 in the whole system, so two cabinets open at
        # once makes a scan unattributable.
        if not queue or outstanding:
            return
        c = queue.popleft()
        locker = int(c["lockerId"])
        mode = c.get("mode", "borrow")
        ser.write(f"OPEN,{locker},{mode}\n".encode("utf-8"))
        outstanding[locker] = [int(c["id"]), time.time() + STALE_AFTER]
        print(f"  <- OPEN locker {locker} ({mode}) [cmd {c['id']}]"
              + (f"  ({len(queue)} waiting)" if queue else ""))

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
                # This is a daemon: it has to outlive every rejected tag, server
                # restart and odd serial line without anyone restarting it.
                # Anything unexpected inside one pass is logged with its
                # traceback and the loop carries on; only a serial failure
                # escapes, to the reconnect path below.
                try:
                    inject_hook(ser)
                    drain_serial(ser)
                    stale_watchdog()
                    if time.time() - last_poll >= POLL_EVERY:
                        last_poll = time.time()
                        poll_server(ser)
                    dispatch(ser)
                except (serial.SerialException, KeyboardInterrupt):
                    raise
                except Exception:
                    print("  !! unexpected error in bridge loop (continuing):")
                    traceback.print_exc(file=sys.stdout)   # same stream as the log
                    time.sleep(1)
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

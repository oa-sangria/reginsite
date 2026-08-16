#!/usr/bin/env python3
"""
reginsite - Locker Bridge (GUI)  ·  SCREEN-DRIVEN
=================================================
Desktop version of bridge.py: polls the server for OPEN commands, relays them
to the Arduino, and confirms the result back to the server — with a window
showing COM-port picker, status lights, and a live log.

    touchscreen -> Laravel (command queue) -> THIS app -> Arduino
    Arduino (DONE/TIMEOUT) -> THIS app -> Laravel (confirm) -> MySQL

Deps:  pip install pyserial      (Tkinter ships with Python)
Config: bridge/config.ini        (serial_port, base_url, api_key)
"""

import configparser
import json
import os
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk, scrolledtext
import urllib.error
import urllib.request

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    import sys
    _r = tk.Tk(); _r.withdraw()
    from tkinter import messagebox
    messagebox.showerror("Missing dependency",
                         "pyserial is not installed.\n\nOpen a terminal and run:\n\npip install pyserial")
    sys.exit(1)

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "config.ini")
POLL_EVERY = 1.0


def load_config():
    cfg = configparser.ConfigParser()
    cfg.read(CONFIG_PATH)
    b = cfg["bridge"] if cfg.has_section("bridge") else configparser.SectionProxy(cfg, "DEFAULT")
    def get(k, d):
        try:
            return b.get(k, d)
        except Exception:
            return d
    return {
        "port": get("serial_port", "COM3"),
        "baud": int(get("baud_rate", "115200")),
        "base_url": get("base_url", "http://localhost:8000").rstrip("/"),
        "api_key": get("api_key", "regin-esp32-2026"),
    }


def api(cfg, method, path, payload=None):
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


def server_reachable(cfg):
    try:
        with urllib.request.urlopen(cfg["base_url"] + "/index.html", timeout=3) as r:
            return 200 <= r.status < 400
    except Exception:
        return False


class BridgeApp:
    COL_OK = "#1f9d6b"; COL_BAD = "#d6453d"; COL_IDLE = "#8a8a8a"

    def __init__(self, root):
        self.root = root
        self.cfg = load_config()
        self.ser = None
        self.running = False
        self.outstanding = {}     # locker -> command_id
        self.ui_q = queue.Queue()

        root.title("Reginsite — Locker Bridge")
        root.geometry("640x520"); root.minsize(560, 460)
        self._build_ui()
        self.refresh_ports()
        if self.cfg["port"] in self.port_cb["values"]:
            self.port_var.set(self.cfg["port"])

        self._poll_ui()
        threading.Thread(target=self._monitor_server, daemon=True).start()
        self.log(f"Ready. Server: {self.cfg['base_url']}")
        self.log("Pick the Arduino's COM port and press Start.")

    # ---- UI ---------------------------------------------------------------- #
    def _build_ui(self):
        pad = {"padx": 10, "pady": 6}
        header = tk.Frame(self.root, bg="#1c1927", height=54); header.pack(fill="x")
        tk.Label(header, text="🔒  Locker Bridge", bg="#1c1927", fg="white",
                 font=("Segoe UI", 14, "bold")).pack(side="left", padx=14, pady=12)

        ctl = tk.Frame(self.root); ctl.pack(fill="x", **pad)
        tk.Label(ctl, text="COM port:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_cb = ttk.Combobox(ctl, textvariable=self.port_var, width=12, state="readonly")
        self.port_cb.pack(side="left", padx=6)
        ttk.Button(ctl, text="Refresh", command=self.refresh_ports).pack(side="left")
        self.start_btn = ttk.Button(ctl, text="Start", command=self.toggle)
        self.start_btn.pack(side="right")

        stat = tk.Frame(self.root); stat.pack(fill="x", **pad)
        self.ard_dot = tk.Label(stat, text="●", fg=self.COL_IDLE, font=("Segoe UI", 14)); self.ard_dot.pack(side="left")
        self.ard_lbl = tk.Label(stat, text="Arduino: not connected"); self.ard_lbl.pack(side="left", padx=(2, 24))
        self.srv_dot = tk.Label(stat, text="●", fg=self.COL_IDLE, font=("Segoe UI", 14)); self.srv_dot.pack(side="left")
        self.srv_lbl = tk.Label(stat, text="Server: checking…"); self.srv_lbl.pack(side="left", padx=2)

        lf = tk.Frame(self.root); lf.pack(fill="both", expand=True, padx=10, pady=(0, 6))
        top = tk.Frame(lf); top.pack(fill="x")
        tk.Label(top, text="Activity", font=("Segoe UI", 10, "bold")).pack(side="left")
        ttk.Button(top, text="Clear", command=lambda: self.logbox.delete("1.0", "end")).pack(side="right")
        self.logbox = scrolledtext.ScrolledText(lf, height=16, font=("Consolas", 9),
                                                 bg="#14121c", fg="#d8d3e8", insertbackground="#d8d3e8")
        self.logbox.pack(fill="both", expand=True, pady=(4, 0))
        for name, color in (("ok", "#4ade80"), ("bad", "#f87171"), ("info", "#a49bff"), ("muted", "#8d86a3")):
            self.logbox.tag_config(name, foreground=color)

    def refresh_ports(self):
        ports = [p.device for p in list_ports.comports()]
        self.port_cb["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])
        if not ports:
            self.log("No COM ports found. Plug in the Arduino (install the CH340 driver).", "muted")

    # ---- Start / stop ------------------------------------------------------ #
    def toggle(self):
        self.stop() if self.running else self.start()

    def start(self):
        port = self.port_var.get().strip()
        if not port:
            self.log("Pick a COM port first.", "bad"); return
        try:
            self.ser = serial.Serial(port, self.cfg["baud"], timeout=0.2)
        except serial.SerialException as e:
            self.log(f"Could not open {port}: {e}", "bad")
            self.log("Is the Arduino Serial Monitor still open? Close it first.", "muted")
            return
        self.running = True
        self.start_btn.config(text="Stop"); self.port_cb.config(state="disabled")
        self._set_arduino(True, f"Arduino: connected ({port})")
        self.log(f"Connected to {port}.", "ok")
        threading.Thread(target=self._worker, daemon=True).start()

    def stop(self):
        self.running = False
        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass
        self.ser = None
        self.start_btn.config(text="Start"); self.port_cb.config(state="readonly")
        self._set_arduino(False, "Arduino: not connected")
        self.log("Stopped.", "muted")

    # ---- Worker (background thread) ---------------------------------------- #
    def _worker(self):
        time.sleep(2)
        last_poll = 0
        while self.running and self.ser:
            try:
                line = self.ser.readline().decode("utf-8", errors="replace").strip()
            except serial.SerialException:
                self.ui_q.put(("log", ("Serial disconnected.", "bad")))
                self.ui_q.put(("stopped", None)); return

            if line:
                self.ui_q.put(("log", (f"[mega] {line}", "muted")))
                if line.startswith("DONE,"):
                    parts = (line.split(",", 2) + ["", ""])[:3]
                    locker, uid = parts[1], parts[2]
                    cid = self.outstanding.pop(int(locker), None) if locker.isdigit() else None
                    if cid:
                        ok, data = api(self.cfg, "POST", "confirm", {"command_id": cid, "uid": uid})
                        if ok:
                            r = data.get("result", {})
                            self.ui_q.put(("log", (f"{data.get('action','?').upper()} saved: "
                                                   f"{data.get('tool')} (tx #{r.get('txId')})", "ok")))
                        else:
                            self.ui_q.put(("log", (f"Confirm failed: {data}", "bad")))
                elif line.startswith("TIMEOUT,"):
                    locker = line.split(",", 1)[1]
                    cid = self.outstanding.pop(int(locker), None) if locker.isdigit() else None
                    if cid:
                        api(self.cfg, "POST", "confirm", {"command_id": cid, "timeout": True})
                        self.ui_q.put(("log", (f"Locker {locker} timed out; cancelled.", "bad")))

            if time.time() - last_poll >= POLL_EVERY:
                last_poll = time.time()
                ok, data = api(self.cfg, "GET", "commands")
                if ok:
                    for c in data.get("commands", []):
                        locker = c.get("lockerId"); mode = c.get("mode", "borrow")
                        try:
                            self.ser.write(f"OPEN,{locker},{mode}\n".encode("utf-8"))
                        except Exception:
                            continue
                        self.outstanding[int(locker)] = c["id"]
                        self.ui_q.put(("log", (f"OPEN locker {locker} ({mode})  [cmd {c['id']}]", "info")))

    def _monitor_server(self):
        while True:
            self.ui_q.put(("server", server_reachable(self.cfg)))
            time.sleep(5)

    # ---- UI plumbing ------------------------------------------------------- #
    def _poll_ui(self):
        try:
            while True:
                kind, payload = self.ui_q.get_nowait()
                if kind == "log":
                    text, tag = payload if isinstance(payload, tuple) else (payload, None)
                    self._write_log(text, tag)
                elif kind == "server":
                    self._set_server(payload)
                elif kind == "stopped":
                    self.stop()
        except queue.Empty:
            pass
        self.root.after(120, self._poll_ui)

    def log(self, text, tag=None):
        self._write_log(text, tag)

    def _write_log(self, text, tag=None):
        self.logbox.insert("end", time.strftime("%H:%M:%S") + "  ", "muted")
        self.logbox.insert("end", text + "\n", tag or "")
        self.logbox.see("end")

    def _set_arduino(self, on, text):
        self.ard_dot.config(fg=self.COL_OK if on else self.COL_IDLE); self.ard_lbl.config(text=text)

    def _set_server(self, on):
        self.srv_dot.config(fg=self.COL_OK if on else self.COL_BAD)
        self.srv_lbl.config(text="Server: online" if on else "Server: offline")


def main():
    root = tk.Tk()
    app = BridgeApp(root)
    root.protocol("WM_DELETE_WINDOW", lambda: (app.stop(), root.destroy()))
    root.mainloop()


if __name__ == "__main__":
    main()

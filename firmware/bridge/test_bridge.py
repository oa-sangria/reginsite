"""
Fake-serial + fake-HTTP test for bridge.py — run:  python firmware/bridge/test_bridge.py

No hardware, no server, no pyserial needed: serial.Serial and urllib.request.urlopen
are replaced with scripted stand-ins, main() is driven to completion, and the serial
writes + API calls it made are asserted. Stdlib only.

Drives main() with a scripted Mega and a scripted server, records every serial
write and every API call, and asserts the three scenarios that matter:

  A. kiosk Cancel  -> command goes terminal server-side -> bridge writes ABORT,
     and the Mega's resulting TIMEOUT line does NOT trigger a confirm.
  B. normal DONE   -> confirm posted with uid+slot, no ABORT ever sent.
  C. DONE after the command was already terminal -> server replies ok without
     an action -> bridge logs "ignored" instead of crashing / mis-logging.
"""
import io
import os
import json
import sys
import time
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# --- fake pyserial ------------------------------------------------------------
class SerialException(Exception):
    pass

class FakeSerial:
    """Scripted Mega. `script` is a list of callables(harness) -> line | None.
    Each readline() pops the next step; None means 'nothing on the wire'."""
    def __init__(self, port, baud, timeout=0.2):
        self.writes = []
        self.lines = []          # lines queued for the bridge to read
        self.in_waiting = 0

    def write(self, b):
        self.writes.append(b.decode("utf-8").strip())
        H.on_write(self.writes[-1])

    def readline(self):
        H.tick()
        if self.lines:
            line = self.lines.pop(0)
            self.in_waiting = len(self.lines)
            return (line + "\n").encode("utf-8")
        self.in_waiting = 0
        return b""

    def close(self):
        pass

fake_serial = types.ModuleType("serial")
fake_serial.Serial = FakeSerial
fake_serial.SerialException = SerialException
sys.modules["serial"] = fake_serial

# --- fake urllib ----------------------------------------------------------------
import urllib.request, urllib.error

class FakeResp(io.BytesIO):
    def __enter__(self): return self
    def __exit__(self, *a): pass

def fake_urlopen(req, timeout=6):
    path = req.full_url.split("/api/esp32/")[1]
    payload = json.loads(req.data.decode()) if req.data else None
    H.calls.append((req.get_method(), path, payload))
    body = H.on_api(path, payload)
    if isinstance(body, tuple):          # (http_code, body) -> HTTPError
        code, body = body
        raise urllib.error.HTTPError(req.full_url, code, "err", {}, FakeResp(json.dumps(body).encode()))
    return FakeResp(json.dumps(body).encode())

urllib.request.urlopen = fake_urlopen

# --- harness state ------------------------------------------------------------
class Harness:
    def __init__(self, scenario):
        self.scenario = scenario
        self.calls = []
        self.ser = None
        self.ticks = 0
        self.stage = 0
        self.commands_served = False

    def tick(self):
        self.ticks += 1
        if self.ticks > 400:
            raise KeyboardInterrupt("scenario did not finish")

    def on_write(self, line):
        self.scenario.on_write(self, line)

    def on_api(self, path, payload):
        return self.scenario.on_api(self, path, payload)

# --- scenarios ---------------------------------------------------------------
class ScenarioCancel:
    """A: kiosk cancels while the door is open."""
    def on_api(self, h, path, payload):
        if path == "commands":
            if not h.commands_served:
                h.commands_served = True
                return {"ok": True, "commands": [{"id": 7, "type": "open", "lockerId": "2", "mode": "borrow"}]}
            return {"ok": True, "commands": []}
        if path == "command-status":
            assert payload == {"command_id": 7}, payload
            # First status poll: still sent. Second: kiosk cancelled it.
            h.stage += 1
            if h.stage == 1:
                return {"ok": True, "status": "sent", "note": None}
            return {"ok": True, "status": "timeout", "note": "Cancelled at the kiosk"}
        if path == "confirm":
            raise AssertionError(f"confirm must NOT be posted in the cancel scenario, got {payload}")
        raise AssertionError(path)

    def on_write(self, h, line):
        if line == "ABORT":
            # Mega relocks and answers TIMEOUT,2 — bridge must ignore it.
            _serial.lines.append("TIMEOUT,2")
            _serial.lines.append("__END__")

class ScenarioDone:
    """B: normal path — DONE arrives, confirm posted, no ABORT."""
    def on_api(self, h, path, payload):
        if path == "commands":
            if not h.commands_served:
                h.commands_served = True
                return {"ok": True, "commands": [{"id": 8, "type": "open", "lockerId": "3", "mode": "borrow"}]}
            return {"ok": True, "commands": []}
        if path == "command-status":
            assert payload == {"command_id": 8}
            # still open; the Mega will answer before the next poll
            _serial.lines.append("OPENED,3")
            _serial.lines.append("SCAN,E9 8C 7B 06")
            _serial.lines.append("DONE,3,E9 8C 7B 06,1")
            return {"ok": True, "status": "sent", "note": None}
        if path == "command-progress":
            assert payload == {"command_id": 8, "stage": "scanned"}, payload
            return {"ok": True, "status": "sent", "note": "scanned"}
        if path == "confirm":
            assert payload == {"command_id": 8, "uid": "E9 8C 7B 06", "slot": 1}, payload
            _serial.lines.append("__END__")
            return {"ok": True, "action": "borrow", "tool": "Soldering Iron 2",
                    "result": {"txId": "9"}, "status": "done"}
        raise AssertionError(path)

    def on_write(self, h, line):
        assert line != "ABORT", "ABORT must not be sent on the normal path"

class ScenarioLateDone:
    """C: DONE for a command the server already closed -> ok:true, no action."""
    def on_api(self, h, path, payload):
        if path == "commands":
            if not h.commands_served:
                h.commands_served = True
                return {"ok": True, "commands": [{"id": 9, "type": "open", "lockerId": "1", "mode": "return"}]}
            return {"ok": True, "commands": []}
        if path == "command-status":
            _serial.lines.append("DONE,1,A7 45 64 06,2")
            return {"ok": True, "status": "sent", "note": None}
        if path == "confirm":
            _serial.lines.append("__END__")
            return {"ok": True, "result": "already timeout", "status": "timeout"}
        raise AssertionError(path)

    def on_write(self, h, line):
        pass

# --- run ------------------------------------------------------------------------
import bridge

_serial = None
_orig_init = FakeSerial.__init__
def _capture_init(self, *a, **k):
    global _serial
    _orig_init(self, *a, **k)
    _serial = self
    H.ser = self
FakeSerial.__init__ = _capture_init

_orig_readline = FakeSerial.readline
def _readline_with_end(self):
    b = _orig_readline(self)
    if b.strip() == b"__END__":
        raise KeyboardInterrupt("scenario complete")
    return b
FakeSerial.readline = _readline_with_end

time.sleep = lambda s: None
bridge.POLL_EVERY = 0.0

def run(name, scenario):
    global H
    H = Harness(scenario)
    out = io.StringIO()
    real_stdout = sys.stdout
    sys.stdout = out
    try:
        bridge.main()
    except KeyboardInterrupt as e:
        pass
    finally:
        sys.stdout = real_stdout
    log = out.getvalue()
    print(f"\n=== {name} ===")
    print("serial writes :", H.ser.writes)
    print("api calls     :", [(m, p) for m, p, _ in H.calls])
    print("--- bridge log ---")
    print("\n".join("  " + l for l in log.strip().splitlines()[3:]))
    return H, log

# A — cancel
h, log = run("A: kiosk cancel -> ABORT", ScenarioCancel())
assert h.ser.writes == ["OPEN,2,borrow", "ABORT"], h.ser.writes
assert not any(p == "confirm" for _, p, _ in h.calls), "no confirm on cancel"
assert "ABORT locker 2" in log and "already timeout" in log
assert "[mega] TIMEOUT,2" in log and "timed out; cancelled" not in log, "TIMEOUT after ABORT must be ignored"

# B — normal
h, log = run("B: normal DONE -> confirm", ScenarioDone())
assert h.ser.writes == ["OPEN,3,borrow"], h.ser.writes
assert [(m, p) for m, p, _ in h.calls].count(("POST", "confirm")) == 1
assert "BORROW saved: Soldering Iron 2 (tx #9)" in log, log

# C — late DONE
h, log = run("C: DONE after server closed it", ScenarioLateDone())
assert h.ser.writes == ["OPEN,1,return"], h.ser.writes
assert "confirm ignored: already timeout" in log, log
assert "? saved" not in log and "None" not in log

# ---------------------------------------------------------------------------
# D — server down when DONE arrives: confirm queued, retried, delivered.
class ScenarioRetry:
    def __init__(self): self.down_calls = 0
    def on_api(self, h, path, payload):
        if path == "commands":
            if not h.commands_served:
                h.commands_served = True
                return {"ok": True, "commands": [{"id": 11, "type": "open", "lockerId": "4", "mode": "borrow"}]}
            return {"ok": True, "commands": []}
        if path == "command-status":
            _serial.lines.append("DONE,4,CE B8 7C 06,2")
            return {"ok": True, "status": "sent", "note": None}
        if path == "confirm":
            self.down_calls += 1
            if self.down_calls <= 3:
                raise urllib.error.URLError("connection refused")      # server down
            _serial.lines.append("__END__")
            return {"ok": True, "action": "borrow", "tool": "Clamp Meter 1",
                    "result": {"txId": "12"}, "status": "done"}
        raise AssertionError(path)
    def on_write(self, h, line):
        assert line != "ABORT"

h, log = run("D: server down at DONE -> retried", ScenarioRetry())
confirms = [(p, pl) for _, p, pl in h.calls if p == "confirm"]
assert len(confirms) == 4, confirms
assert all(pl == {"command_id": 11, "uid": "CE B8 7C 06", "slot": 2} for _, pl in confirms)
assert "queued for retry" in log and "retry delivered" in log and "BORROW saved: Clamp Meter 1 (tx #12)" in log, log
assert "GAVE UP" not in log

# ---------------------------------------------------------------------------
# E — an unexpected exception inside one pass must not kill the bridge.
class ScenarioCrash:
    def __init__(self): self.n = 0
    def on_api(self, h, path, payload):
        self.n += 1
        if self.n == 2:
            raise RuntimeError("boom: something nobody anticipated")
        if path == "commands":
            if self.n >= 4:
                _serial.lines.append("__END__")
            return {"ok": True, "commands": []}
        raise AssertionError(path)
    def on_write(self, h, line): pass

h, log = run("E: crash inside the loop -> logged, continues", ScenarioCrash())
assert "unexpected error in bridge loop (continuing)" in log and "RuntimeError: boom" in log, log
assert [p for _, p, _ in h.calls].count("commands") >= 4, "loop must keep polling after the crash"

# ---------------------------------------------------------------------------
# F — progress relay: SCAN/MOVED inside a window -> command-progress; idle SCAN -> nothing.
class ScenarioProgress:
    def on_api(self, h, path, payload):
        if path == "commands":
            if not h.commands_served:
                _serial.lines.append("SCAN,AA BB CC DD")          # idle scan, no window yet
                h.commands_served = True
                return {"ok": True, "commands": [{"id": 12, "type": "open", "lockerId": "2", "mode": "return"}]}
            return {"ok": True, "commands": []}
        if path == "command-status":
            h.stage += 1
            if h.stage == 1:
                _serial.lines += ["OPENED,2", "MOVED,2,3", "SCAN,CD 5B 7B 06", "DONE,2,CD 5B 7B 06,3"]
            return {"ok": True, "status": "sent", "note": None}
        if path == "command-progress":
            return {"ok": True, "status": "sent", "note": payload["stage"]}
        if path == "confirm":
            _serial.lines.append("__END__")
            return {"ok": True, "action": "return", "tool": "Pliers 2", "result": {"txId": "13"}, "status": "done"}
        raise AssertionError(path)
    def on_write(self, h, line): pass

h, log = run("F: progress relay", ScenarioProgress())
prog = [pl for _, p, pl in h.calls if p == "command-progress"]
assert prog == [{"command_id": 12, "stage": "moved"}, {"command_id": 12, "stage": "scanned"}], prog
assert "[mega] SCAN,AA BB CC DD" in log      # idle scan logged but not relayed
assert "RETURN saved: Pliers 2 (tx #13)" in log

print("\nALL SIX SCENARIOS PASS")

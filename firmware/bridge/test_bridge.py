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
        H.writer = self          # which fake port the bridge just wrote to
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
        self.writer = None
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
_serials = {}                 # port -> FakeSerial, for the two-board scenario
_orig_init = FakeSerial.__init__
def _capture_init(self, port, *a, **k):
    global _serial
    _orig_init(self, port, *a, **k)
    self.port = port
    _serial = self
    _serials[port] = self
    H.ser = self
FakeSerial.__init__ = _capture_init

# The protocol scenarios run against ONE fake board; the real config.ini lists
# two ports, which would split the scripted lines across two FakeSerials.
PORTS = ["COM5"]
bridge.load_config = lambda: {"ports": list(PORTS), "baud": 115200,
                              "base_url": "http://localhost:8000", "api_key": "test"}

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
    _serials.clear()
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

# Every board gets a WHO right after its port opens (identity handshake); the
# board answers with its banner + READY. Until it does, it is logged by port.
# A — cancel
h, log = run("A: kiosk cancel -> ABORT", ScenarioCancel())
assert h.ser.writes == ["WHO", "OPEN,2,borrow", "ABORT"], h.ser.writes
assert not any(p == "confirm" for _, p, _ in h.calls), "no confirm on cancel"
assert "ABORT locker 2" in log and "already timeout" in log
assert "[COM5] TIMEOUT,2" in log and "timed out; cancelled" not in log, "TIMEOUT after ABORT must be ignored"

# B — normal
h, log = run("B: normal DONE -> confirm", ScenarioDone())
assert h.ser.writes == ["WHO", "OPEN,3,borrow"], h.ser.writes
assert [(m, p) for m, p, _ in h.calls].count(("POST", "confirm")) == 1
assert "BORROW saved: Soldering Iron 2 (tx #9)" in log, log

# C — late DONE
h, log = run("C: DONE after server closed it", ScenarioLateDone())
assert h.ser.writes == ["WHO", "OPEN,1,return"], h.ser.writes
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
assert "[COM5] SCAN,AA BB CC DD" in log      # idle scan logged but not relayed
assert "RETURN saved: Pliers 2 (tx #13)" in log

# ---------------------------------------------------------------------------
# G — two controllers: OPEN for cabinet 7 goes to the board that announced
# cabinets=6-10 (whichever COM port it is on), and so does the ABORT.
BANNER = "#reginsite locker-controller controller={id} cabinets={lo}-{hi} rfid={rf} sensors=1 bench=0"
class ScenarioTwoBoards:
    def on_api(self, h, path, payload):
        if path == "commands":
            if not h.commands_served:
                h.commands_served = True
                return {"ok": True, "commands": [{"id": 20, "type": "open", "lockerId": "7", "mode": "borrow"}]}
            return {"ok": True, "commands": []}
        if path == "command-status":
            h.stage += 1
            if h.stage == 1:
                return {"ok": True, "status": "sent", "note": None}
            return {"ok": True, "status": "timeout", "note": "Cancelled at the kiosk"}
        if path == "confirm":
            raise AssertionError(f"confirm must NOT be posted, got {payload}")
        raise AssertionError(path)

    def on_write(self, h, line):
        w = h.writer
        if line == "WHO":
            # Ports are deliberately "swapped": controller 2 is on COM5.
            if w.port == "COM5":
                w.lines += [BANNER.format(id=2, lo=6, hi=10, rf=0), "READY,2"]
            else:
                w.lines += [BANNER.format(id=1, lo=1, hi=5, rf=1), "READY,1"]
        elif line == "ABORT":
            w.lines += ["TIMEOUT,7", "__END__"]

PORTS[:] = ["COM8", "COM5"]      # listed in the "wrong" order on purpose
h, log = run("G: two boards -> routed by announced cabinet range", ScenarioTwoBoards())
PORTS[:] = ["COM5"]
assert _serials["COM5"].writes == ["WHO", "OPEN,7,borrow", "ABORT"], _serials["COM5"].writes
# The reader board's idle scan is armed while the reader-less door is open and
# put back (bench=0) once the ABORT closes it.
assert _serials["COM8"].writes == ["WHO", "IDLESCAN,1", "IDLESCAN,0"], _serials["COM8"].writes
assert "OPEN locker 7 (borrow) [cmd 20] -> mega2" in log, log
assert "ABORT locker 7 on mega2" in log, log
assert "[mega2] TIMEOUT,7" in log and "timed out; cancelled" not in log

# H — one board only, cabinet on the other (unplugged) controller: the live
# board answers NOWIRE and the command is cancelled with reason "nowire".
class ScenarioNowire:
    def on_api(self, h, path, payload):
        if path == "commands":
            if not h.commands_served:
                h.commands_served = True
                return {"ok": True, "commands": [{"id": 21, "type": "open", "lockerId": "9", "mode": "borrow"}]}
            return {"ok": True, "commands": []}
        if path == "command-status":
            return {"ok": True, "status": "sent", "note": None}
        if path == "confirm":
            assert payload == {"command_id": 21, "timeout": True, "reason": "nowire"}, payload
            _serial.lines.append("__END__")
            return {"ok": True, "result": "cancelled", "status": "timeout"}
        raise AssertionError(path)

    def on_write(self, h, line):
        if line == "WHO":
            h.writer.lines += [BANNER.format(id=1, lo=1, hi=5, rf=1), "READY,1"]
        elif line.startswith("OPEN,"):
            h.writer.lines.append("NOWIRE,9")

h, log = run("H: cabinet on a missing controller -> NOWIRE -> cancelled", ScenarioNowire())
assert h.ser.writes == ["WHO", "OPEN,9,borrow"], h.ser.writes
assert "locker 9 is NOT on mega1; cancelled" in log, log

# ---------------------------------------------------------------------------
# I/J/K — cabinet on the READER-LESS board (controller 2). Its DONE carries an
# empty tag; the tag comes from controller 1's idle scan and the bridge pairs
# the two. I: lift first, then tap. J: tap first, then lift. K: never tapped.
def two_board_who(h, line):
    w = h.writer
    if line == "WHO":
        if w.port == "COM5":
            w.lines += [BANNER.format(id=1, lo=1, hi=5, rf=1), "READY,1"]
        else:
            w.lines += [BANNER.format(id=2, lo=6, hi=10, rf=0), "READY,2"]
        return True
    return False

class ScenarioPairLiftThenTap:
    def __init__(self): self.confirmed = False
    def on_api(self, h, path, payload):
        if path == "commands":
            if not h.commands_served:
                h.commands_served = True
                return {"ok": True, "commands": [{"id": 30, "type": "open", "lockerId": "7", "mode": "borrow"}]}
            if self.confirmed:
                _serials["COM8"].lines.append("__END__")
            return {"ok": True, "commands": []}
        if path == "command-status":
            h.stage += 1
            if h.stage == 2:                      # DONE is being held -> now the student taps
                _serials["COM5"].lines.append("SCAN,1E 3C 78 06")
            return {"ok": True, "status": "sent", "note": None}
        if path == "command-progress":
            return {"ok": True, "status": "sent", "note": payload["stage"]}
        if path == "confirm":
            assert payload == {"command_id": 30, "uid": "1E 3C 78 06", "slot": 2}, payload
            self.confirmed = True
            return {"ok": True, "action": "borrow", "tool": "Wire Stripper 1",
                    "result": {"txId": "31"}, "status": "done"}
        raise AssertionError(path)
    def on_write(self, h, line):
        if two_board_who(h, line): return
        if line.startswith("OPEN,"):
            h.writer.lines += ["OPENED,7", "MOVED,7,2", "DONE,7,,2"]
        elif line == "IDLESCAN,1":
            h.writer.lines.append("#idlescan=1")
        assert line != "ABORT"

PORTS[:] = ["COM5", "COM8"]
h, log = run("I: reader-less board, lift then tap -> paired", ScenarioPairLiftThenTap())
assert _serials["COM8"].writes == ["WHO", "OPEN,7,borrow"], _serials["COM8"].writes
assert _serials["COM5"].writes == ["WHO", "IDLESCAN,1", "IDLESCAN,0"], _serials["COM5"].writes
assert "waiting up to 45s for the tag" in log and "tag 1E 3C 78 06 paired with cmd 30" in log, log
assert "BORROW saved: Wire Stripper 1 (tx #31)" in log, log
assert "REJECTED" not in log
prog = [pl["stage"] for _, p, pl in h.calls if p == "command-progress"]
assert prog == ["moved"], prog

class ScenarioPairTapThenLift(ScenarioPairLiftThenTap):
    def on_api(self, h, path, payload):
        if path == "command-status":
            h.stage += 1
            if h.stage == 2:                      # tag already seen -> now the slot moves
                _serials["COM8"].lines += ["MOVED,7,3", "DONE,7,,3"]
            return {"ok": True, "status": "sent", "note": None}
        if path == "confirm":
            assert payload == {"command_id": 30, "uid": "74 D6 7B 06", "slot": 3}, payload
            self.confirmed = True
            return {"ok": True, "action": "borrow", "tool": "Wire Stripper 2",
                    "result": {"txId": "32"}, "status": "done"}
        return super().on_api(h, path, payload)
    def on_write(self, h, line):
        if two_board_who(h, line): return
        if line.startswith("OPEN,"):
            h.writer.lines.append("OPENED,7")
        elif line == "IDLESCAN,1":
            h.writer.lines += ["#idlescan=1", "SCAN,74 D6 7B 06"]

h, log = run("J: reader-less board, tap then lift -> paired", ScenarioPairTapThenLift())
assert _serials["COM5"].writes == ["WHO", "IDLESCAN,1", "IDLESCAN,0"], _serials["COM5"].writes
assert "tag 74 D6 7B 06 held for locker 7" in log and "paired with cmd 30" in log, log
assert "BORROW saved: Wire Stripper 2 (tx #32)" in log, log
prog = [pl["stage"] for _, p, pl in h.calls if p == "command-progress"]
assert prog == ["scanned", "moved"], prog

class ScenarioPairNoTag(ScenarioPairLiftThenTap):
    def on_api(self, h, path, payload):
        if path == "command-status":
            return {"ok": True, "status": "sent", "note": None}
        if path == "confirm":
            assert payload == {"command_id": 30, "timeout": True, "reason": "timeout"}, payload
            self.confirmed = True
            return {"ok": True, "result": "cancelled (timeout)", "status": "timeout"}
        return super().on_api(h, path, payload)

bridge.TAG_WAIT_S = 0.0
h, log = run("K: reader-less board, slot moved but no tag -> timeout", ScenarioPairNoTag())
bridge.TAG_WAIT_S = bridge.OPEN_WINDOW_S
PORTS[:] = ["COM5"]
assert "no tag tapped within 0s; cancelled" in log, log
assert [(m, p) for m, p, _ in h.calls].count(("POST", "confirm")) == 1
assert _serials["COM5"].writes == ["WHO", "IDLESCAN,1", "IDLESCAN,0"], _serials["COM5"].writes

# ---------------------------------------------------------------------------
# L — a second tool leaves: the Mega reports ALERT,<cab>,<slot> for the slot
# that is NOT on the DONE line, keeps the borrow going, then CLEAR when it is
# put back during the post-close watch. Each goes to the server as locker-alert.
class ScenarioExtraTool:
    def __init__(self): self.alerts = []
    def on_api(self, h, path, payload):
        if path == "commands":
            if not h.commands_served:
                h.commands_served = True
                return {"ok": True, "commands": [{"id": 40, "type": "open", "lockerId": "1", "mode": "borrow"}]}
            return {"ok": True, "commands": []}
        if path == "command-status":
            return {"ok": True, "status": "sent", "note": None}
        if path == "command-progress":
            return {"ok": True, "status": "sent", "note": payload["stage"]}
        if path == "confirm":
            assert payload == {"command_id": 40, "uid": "E5 77 7B 06", "slot": 1}, payload
            # The tagged tool is recorded even though slot 2 is still out...
            _serial.lines += ["#watch,1,start,extras=1", "CLEAR,1,2", "#watch,1,end,extras=0", "__END__"]
            return {"ok": True, "action": "borrow", "tool": "Pliers 1",
                    "result": {"txId": "41"}, "status": "done"}
        if path == "locker-alert":
            self.alerts.append(payload)
            return {"ok": True}
        raise AssertionError(path)
    def on_write(self, h, line):
        if line == "WHO":
            h.writer.lines += [BANNER.format(id=1, lo=1, hi=5, rf=1), "READY,1"]
        elif line.startswith("OPEN,"):
            # Plier 1 (slot 1) lifted, then Plier 2 (slot 2) as well, then the tag.
            h.writer.lines += ["OPENED,1", "MOVED,1,1", "ALERT,1,2", "SCAN,E5 77 7B 06", "DONE,1,E5 77 7B 06,1"]
        assert line != "ABORT" and not line.startswith("ALARM,"), line   # mega1 has its own buzzer

sc = ScenarioExtraTool()
h, log = run("L: second tool taken -> ALERT relayed, borrow still saved, CLEAR when put back", sc)
assert sc.alerts == [{"locker_id": 1, "slot": 2, "cleared": False},
                     {"locker_id": 1, "slot": 2, "cleared": True}], sc.alerts
assert "locker 1 slot 2: tool LEFT WITHOUT A TAG" in log and "back in place" in log, log
assert "BORROW saved: Pliers 1 (tx #41)" in log, log

# M — the same on controller 2, which has no buzzer: the bridge sounds
# controller 1's (ALARM,30) and silences it on CLEAR (ALARM,0). Also FAULT:
# a reader-less cabinet with no sensor echo refuses to open -> cancelled.
class ScenarioRemoteAlarm:
    def __init__(self): self.alerts = []; self.confirms = []
    def on_api(self, h, path, payload):
        if path == "commands":
            if not h.commands_served:
                h.commands_served = True
                return {"ok": True, "commands": [
                    {"id": 50, "type": "open", "lockerId": "7", "mode": "borrow"},
                    {"id": 51, "type": "open", "lockerId": "9", "mode": "borrow"}]}
            if len(self.confirms) == 2:
                _serials["COM5"].lines.append("__END__")
            return {"ok": True, "commands": []}
        if path == "command-status":
            return {"ok": True, "status": "sent", "note": None}
        if path == "command-progress":
            return {"ok": True, "status": "sent", "note": payload["stage"]}
        if path == "confirm":
            self.confirms.append(payload)
            if payload.get("reason") == "fault":
                return {"ok": True, "result": "cancelled (fault)", "status": "timeout"}
            assert payload == {"command_id": 50, "uid": "1E 3C 78 06", "slot": 2}, payload
            return {"ok": True, "action": "borrow", "tool": "Wire Stripper 1",
                    "result": {"txId": "52"}, "status": "done"}
        if path == "locker-alert":
            self.alerts.append(payload)
            return {"ok": True}
        raise AssertionError(path)
    def on_write(self, h, line):
        if two_board_who(h, line): return
        w = h.writer
        if line == "OPEN,7,borrow":
            w.lines += ["OPENED,7", "MOVED,7,2", "ALERT,7,3", "DONE,7,,2"]
        elif line == "OPEN,9,borrow":
            w.lines += ["FAULT,9,nosensor"]
        elif line == "IDLESCAN,1":
            w.lines += ["#idlescan=1", "SCAN,1E 3C 78 06"]
        elif line == "ALARM,30":
            # controller 1 acknowledges; meanwhile the tool goes back on controller 2
            w.lines.append("#alarm=1")
            _serials["COM8"].lines += ["CLEAR,7,3"]
        elif line == "ALARM,0":
            w.lines.append("#alarm=0")

PORTS[:] = ["COM5", "COM8"]
sc = ScenarioRemoteAlarm()
h, log = run("M: controller-2 ALERT -> ALARM on controller 1; FAULT -> cancelled", sc)
PORTS[:] = ["COM5"]
assert sc.alerts == [{"locker_id": 7, "slot": 3, "cleared": False},
                     {"locker_id": 7, "slot": 3, "cleared": True}], sc.alerts
w1 = _serials["COM5"].writes
assert w1[:2] == ["WHO", "IDLESCAN,1"] and "ALARM,30" in w1 and "ALARM,0" in w1, w1
assert w1.index("ALARM,30") < w1.index("ALARM,0"), w1
assert _serials["COM8"].writes == ["WHO", "OPEN,7,borrow", "OPEN,9,borrow"], _serials["COM8"].writes
assert "BORROW saved: Wire Stripper 1 (tx #52)" in log, log
assert "locker 9 refused to open on mega2 (nosensor); cancelled" in log, log
assert {"command_id": 51, "timeout": True, "reason": "fault"} in sc.confirms, sc.confirms

print("\nALL THIRTEEN SCENARIOS PASS")

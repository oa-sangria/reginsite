# reginsite — Locker Firmware (Arduino Mega + mini-PC bridge)

Smart Tool Lending Cabinet. **Screen-driven:** the touchscreen (mini PC) decides everything; the
Arduino just opens lockers, watches the ultrasonic sensors, reads tool RFID tags, and reports back.

## How the pieces fit
```
touchscreen + QR scanner ─> Laravel ─(command queue)─> bridge.py ─USB─> Arduino Mega
                                 ^                                          │
                                 └──────── confirm (tool UID) ─────────────┘  ─> MySQL
```
The Mega has no network/clock — the **mini-PC bridge** relays between the server and the Arduino.

## Files
```
firmware/
  locker_controller/locker_controller.ino   Arduino Mega firmware (screen-driven)
  rfid_read_test/rfid_read_test.ino         Standalone: prints tag UIDs (for enrolling tools)
  bridge/bridge.py                           mini-PC bridge (console; the only bridge)
  bridge/config.ini                          serial_port, base_url, api_key
  bridge/test_bridge.py                      six-scenario fake-serial test of the bridge (no hardware)
  tests/rfid_usb_capture.html                what a USB keyboard-wedge RFID reader types
```

## Two controllers

The build splits across **two Arduino Megas** so every sensor gets its own TRIG *and* ECHO
(34 sensors = 68 pins, which does not fit one board):

| | Cabinets | Sensors | RC522 | Buzzer |
|---|---|---|---|---|
| `CONTROLLER_ID 1` | 1–5 | 20 (USS 1–20) | yes | yes (D16) |
| `CONTROLLER_ID 2` | 6–10 | 14 (USS 21–34) | no | no |

Same sketch for both — set `CONTROLLER_ID` at the top of `locker_controller.ino` before uploading.
The board announces itself as `READY,<id>` so the bridge knows which is which. **One** bridge
process owns both COM ports and routes each `OPEN` by cabinet number.

Cabinets 9 and 10 hold one Makita Drill each, hence a single slot apiece (4×8 + 1 + 1 = 34).

## Wiring — Mega 1 (cabinets 1–5)
RC522: **SS=53, SCK=52, MOSI=51, MISO=50, RST=5, VCC=3.3V (not 5V), GND=GND**
Buzzer: **D16** (note: that is TX2 — never add a Serial2 device on this board).
`ACTIVE_LOW = true` (flip if your relay is inverted). No status LEDs: the buzzer is the indicator.

| Cabinet | Tool | Relay | Slot TRIG/ECHO |
|---|---|---|---|
| 1 | Pliers | A12 | 22/23 · 24/25 · 26/27 · 28/29 |
| 2 | Side Cutter | A13 | 30/31 · 32/33 · 34/35 · 36/37 |
| 3 | Wire Crimper | D6 | 38/39 · 40/41 · 42/43 · 44/45 |
| 4 | Clamp Meter | D7 | 46/47 · 48/49 · A0/A1 · A2/A3 |
| 5 | Multimeter | D8 | A4/A5 · A6/A7 · A8/A9 · A10/A11 |

`A0`–`A15` are `D54`–`D69` on a Mega and are full digital I/O. (The "A6/A7 are analog-input-only"
rule is Uno/Nano — it does not apply here.)

## Wiring — Mega 2 (cabinets 6–10) — *designed, not built*

| Cabinet | Tool | Relay | Slot TRIG/ECHO |
|---|---|---|---|
| 6 | Screwdriver Set | A0 | 22/23 · 24/25 · 26/27 · 28/29 |
| 7 | Wire Stripper | A1 | 30/31 · 32/33 · 34/35 · 36/37 |
| 8 | Soldering Iron | A2 | 38/39 · 40/41 · 42/43 · 44/45 |
| 9 | Makita Drill A | A3 | 46/47 |
| 10 | Makita Drill B | A4 | 48/49 |

> Relays are on **A0–A4, not D50–D53** as the original pin map had them. D50–D53 are the SPI bus;
> if `SPI.begin()` ever compiles into the Mega 2 build it drives SCK/MOSI as outputs and fires
> cabinets 7 and 8. A0–A15 are otherwise free on that board, so SPI stays clear.

## Boot safety — fit the relay pull-ups

**Fit a 10 kΩ pull-up from each relay `IN` to the relay board's +5 V.** From power-on until
`setup()` runs (bootloader ≈ 0.5–2 s) every Arduino pin is high-impedance and **no firmware can
control it**. With active-low relays and nothing holding the line high, every solenoid energizes
for that whole window. Five (later ten) solenoids inrushing at once browns out the 12 V supply,
which resets the Mega, which repeats — a boot loop that looks like "the Arduino is broken".

This is not rare: **opening the serial port toggles DTR and resets the board**, so every restart
of `bridge.py` re-triggers it.

The firmware closes the two windows it *can* reach (`relaysSafeInit()` runs before anything else
and writes the idle level before `pinMode(OUTPUT)`), but it cannot touch the pre-`setup()` window.

## Power — two 5 V rails, one ground

| Rail | Source | Feeds |
|---|---|---|
| 12 V | SMPS | solenoids, through the relay contacts only |
| 5 V **A** | LM2596 | relay-module VCC |
| 5 V **B** | separate buck converter | every ultrasonic VCC (~15 mA each, ~300 mA peak on Mega 1) |
| 3.3 V | Mega 1's 3.3 V pin | RC522 only — never 5 V |

**Never join the two +5 V outputs.** Two regulators fighting over one rail is how one of them dies.

**Every ground MUST be common** — both buck GND outputs, relay GND, every sensor GND, and the
Mega's GND. This is the classic failure with a separate sensor supply: an HC-SR04's ECHO pin is a
voltage *relative to the sensor's own ground*. If that ground isn't tied to the Mega's, the Mega
has no reference for the signal and reads nothing, or floating-pin noise. Symptom: every sensor
"no echo", maybe one or two showing impossible sub-2 cm readings. Check continuity between the
sensor buck's GND and a Mega GND pin before suspecting anything else.

Verify each buck reads 5.0 V with nothing attached before connecting its load.

## The flow (what the firmware does)
1. Bridge sends `OPEN,<cabinet>,<borrow|return>` (because a student chose a tool on the touchscreen).
2. Firmware unlocks that cabinet, beeps, prints `OPENED,<cabinet>`.
3. It waits (**45 s**, `OPEN_TIMEOUT_MS`) for **both**: the ultrasonic to confirm the tool moved
   (removed for borrow, present for return) **and** the tool's **RFID tag** scanned on the RC522.
   Each half is reported as it happens — `MOVED,<cabinet>,<slot>` and `SCAN,<uid>` (with a short
   **beep**: the student's proof the tap registered) — and the bridge relays them so the kiosk's
   step rail ticks along in real time.
4. On success it relocks, double-beeps, prints `DONE,<cabinet>,<uid>,<slot>`. The bridge then calls
   the server, which records the borrow/return of the exact tool with that UID.
5. If nothing happens in time it relocks and prints `TIMEOUT,<cabinet>` (server cancels). The
   kiosk's **Cancel** button reaches the Mega as `ABORT` (via the bridge) and relocks at once.

Inside the wait loop the RC522 is polled **every pass**. A no-card pass costs one library timeout
(~25 ms — `cardPresent()` sends a single WUPA, which answers both IDLE and HALTed tags), so the loop
runs at ~30 ms/pass and the ultrasonics fire **one sensor per ~60 ms**, round-robin. That split is
deliberate: sensors need a settle gap between pings to avoid crosstalk, but a reader polled slowly
misses a tag tapped and lifted in under 300 ms. With 4 slots and `AGREE_N = 2`, a removal registers
in roughly 0.5 s.

`SENSORS_ENABLED` is **true**. A cabinet whose sensors return no echo at open time automatically
falls back to tag-only (slot reported as `0`, `MOVED` sent immediately), so bring-up cabinets still
work. The RC522 is **re-initialised at the top of every OPEN** and whenever a 10 s idle health check
finds it deaf (`#rc522 reinit (...) v=0x92` in the bridge log) — clone readers lock up and stay
locked up otherwise.

`BENCH_MODE` (top of the sketch) is the one switch for bring-up behaviour: it defaults `ALLOW_SIMTAG`
(serial `SIMTAG,<uid>` stands in for a tag), `idleScanEnabled` (tap a tag any time; beeps) and
`slotDebug` (stream every sensor sample). The board announces `bench=1` on boot. **Set it to 0 for
the field** — the idle beep in particular misleads students returning a tool.

## What you'll see on the mini PC (a good borrow, annotated)

The bridge console is the one place the whole chain is visible. A healthy borrow looks like this:

```
  queued cmd 41 (locker 1, borrow)            <- kiosk tapped BORROW; server queued it
  <- OPEN locker 1 (borrow) [cmd 41]           <- bridge told the Mega
[mega] #rc522 reinit (open) v=0x92             <- reader re-initialised for this window; 0x92 = alive
[mega] #baseline,1,IN,IN,out,IN                <- what the 4 slots looked like before the door opened
[mega] OPENED,1                                <- door unlocked; kiosk step 01 ticks
[mega] #slot,1,3,10.4,A,filled=0               <- sensor samples (slotDebug) — cm, P/A/-, state
[mega] MOVED,1,2                               <- slot B saw the tool leave; kiosk step 02 ticks, step 03 spins
[mega] SCAN,E5 77 7B 06                        <- tag read (Mega beeps once); kiosk step 03 ticks
[mega] DONE,1,E5 77 7B 06,2                    <- both halves met; door relocked, double beep
  -> BORROW saved: Pliers 1 (tx #12)           <- server wrote the row; kiosk shows the receipt
```

Things that are *fine* even though they look odd:
- `[mega] SCAN,...` with no window open — the idle scan (BENCH_MODE). Logged, not acted on.
- `-> confirm ignored: already timeout` — the student cancelled at the kiosk a moment before the
  Mega finished; nothing was recorded, which is correct.
- `<- ABORT locker N: command N is already timeout on the server (Cancelled at the kiosk)` — the
  Cancel path working: the bridge is relocking the door early.
- `-> server unreachable (...); confirm cmd 41 ... queued for retry (1 waiting)` followed later by
  `-> retry delivered after 12s:` — Laravel was restarting; the borrow was still saved.

Things that need attention:
- `!! unexpected error in bridge loop (continuing):` + a traceback — the bridge survived it, but
  copy the traceback into an issue.
- `!! GAVE UP after 600s: confirm cmd ... {"command_id": 41, "uid": "E5 77 7B 06", "slot": 2}` —
  the server was down for 10 minutes while a tool left the cabinet. Enter that borrow by hand on the
  admin site.
- `#rc522 reinit (open) v=0x00` or `v=0xFF` — the reader is not on the bus; see Troubleshooting.
- `NOWIRE,<n>` — the bridge sent a cabinet this board doesn't own.

## Slot distances (empty shelf, cm)

Each slot's sensor reads this with **no tool in it** (measured 2026-09-18, "not yet very accurate").
They live in the `CABS` tables in the sketch as the third number of each `{TRIG,ECHO,emptyCm}`.
Thresholds derive from them: **absent** = within `ABSENT_MARGIN_CM` (1.0) of the shelf, **present**
= more than `PRESENT_MARGIN_CM` (2.0) closer than the shelf, hold-previous in between. A tool body
reads ~4 cm, so a global pair could not fit both a 6.5 cm and a 14 cm shelf.

| Locker | A | B | C | D |
|---|---|---|---|---|
| 1 Pliers | 12 | 12 | 10.5 | 10.5 |
| 2 Side Cutter | 14 | 14 | 10.5 | 10.5 |
| 3 Wire Crimper | 13.5 | 13.5 | 11.5 | 11.5 |
| 4 Clamp Meter | 11 | 11 | 7.5 | 7 |
| 5 Multimeter | 11 | 11.5 | 9 | 9 |
| 6 Screwdriver Set | 8.5 | 8.5 | 6.5 | 6.5 |
| 7 Wire Stripper | 12 | 12 | 10 | 10 |
| 8 Soldering Iron | 12.5 | 12.5 | 10 | 10 |
| 9 Makita Drill A | 7 | | | |
| 10 Makita Drill B | 7 | | | |

To re-measure: `USS` prints every slot as `USS,<cab>,<slot>,<trig>/<echo>,<live cm>,empty=<table cm>`
with the cabinets empty — the live number *is* the new table value. Then watch the `#slot` stream
during a real borrow: a slot that never shows `P` with the tool in it needs a bigger
`PRESENT_MARGIN_CM` gap for that shelf, or the sensor is not seeing the tool's body.

## Serial protocol
| Dir | Message | Meaning |
|-----|---------|---------|
| PC→Mega | `OPEN,<cabinet>,<borrow\|return>` | open a cabinet for a transaction |
| PC→Mega | `WHO` | re-announce identity (no reset) |
| PC→Mega | `ABORT` | relock the open cabinet immediately |
| Mega→PC | `#<banner>` | informational; the bridge logs and ignores it |
| Mega→PC | `READY,<controller_id>` | booted; says which board this is |
| Mega→PC | `OPENED,<cabinet>` | cabinet unlocked |
| Mega→PC | `MOVED,<cabinet>,<slot>` | slot sensor saw the tool go/return (sent at once with slot `0` on a tag-only cabinet) |
| Mega→PC | `SCAN,<uid>` | a tag was read (beeps inside an open window) |
| Mega→PC | `DONE,<cabinet>,<uid>,<slot>` | confirmed → bridge records it (slot 1..N, 0 = unknown) |
| Mega→PC | `TIMEOUT,<cabinet>` | gave up, relocked |
| Mega→PC | `NOWIRE,<cabinet>` | that cabinet belongs to the **other** controller |
| Mega→PC | `ERR,<line>` | command not understood |

**Bring-up / diagnostic commands** (type them in the Serial Monitor, or drop one into
`firmware/bridge/inject.txt` while the bridge is running):

| Command | What it does |
|---|---|
| `SELFTEST` | RC522 version, antenna, gain, REQA/WUPA probes, one full read; relay pin map |
| `USS` | ping every sensor in the table once |
| `SCANECHO` / `SCANALL` | find pins that have a sensor ECHO on them (pull-up probe) |
| `PING,<trig>,<echo>[,us]` | read one arbitrary pair, optional trigger pulse width |
| `TRACE,<trig>,<echo>` | record what ECHO actually does for 60 ms after one trigger |
| `HOLD,<pin>,<0\|1\|x>` / `LINK,<a>,<b>` / `PULSE,<pin>,<ms>` | wiring probes: drive a pin, check two pins for a short, pulse a relay candidate |
| `SLOTDBG,<0\|1>` | sensor sample stream on/off |
| `IDLESCAN,<0\|1>` | idle tag reporting on/off (must be OFF during RF diagnostics) |
| `GAIN,<0-7>` | RC522 receiver gain, live (reset by the per-OPEN reinit) |
| `SIMTAG,<uid>` | stand in for a tag scan inside an open window (`ALLOW_SIMTAG` only) |

> `DONE` has **four** fields. Parsing only three glues `,<slot>` onto the UID, and because
> `Tool::normTag` strips separators but keeps digits, the tag then never matches any tool and
> every confirm returns 422. That bug shipped once — don't reintroduce it.

## Running it
On the mini PC (see also `../SETUP-FRESH-PC.md`):
1. `cd laravel && php artisan migrate:fresh --seed && php artisan serve`  (reseed = fresh demo data)
2. `pip install pyserial` (once)
3. Set `firmware/bridge/config.ini` → `serial_port` (Device Manager → Ports) and make `api_key`
   match `DEVICE_API_KEY` in `laravel/.env`.
4. Run the bridge: `python firmware/bridge/bridge.py`. It is a daemon — it survives rejected tags,
   server restarts and serial glitches on its own (unexpected errors are logged and the loop
   continues; confirms the server could not be reached for are retried for 10 minutes).
   After editing it, `python firmware/bridge/test_bridge.py` must still print `ALL SIX SCENARIOS PASS`.
5. Upload `locker_controller.ino` (install the **MFRC522** library first; close Serial Monitor
   before running the bridge).
6. On the touchscreen open `http://localhost:8000/terminal.html`, scan a student QR, pick a tool.
   The locker opens; take the tool + scan its tag; it saves. (No hardware? the terminal has a
   "simulate tag scan" box, and `rfid_read_test.ino` reads UIDs.)

## Tool RFID tags
Tags are registered in the database (`tools.rfid_tag`, seeded from your scans; editable on the
Inventory page). The firmware doesn't need a tag table — it just forwards the scanned UID and the
**server** matches it. UIDs match in any format (`AA:BB:CC:DD` or `AA BB CC DD`).

All 34 tags are real scans and are seeded. Cabinet assignments changed when the cabinet layout was
finalised — the tags themselves did not, they were only re-homed. See `DatabaseSeeder.php`.

## Troubleshooting
- **No COM port** → install the CH340 driver, reboot; close the Arduino Serial Monitor.
- **`NAK`/confirm fails / nothing saves** → Laravel not running, wrong `base_url`, or `api_key`
  ≠ `.env`.
- **Confirm returns 422 "not a tool from this locker"** → the scanned tag didn't match. If it
  happens for *every* scan, check the bridge is splitting `DONE` into four fields (see the protocol
  note above), not three.
- **`NOWIRE,<n>`** → the bridge sent a cabinet this board doesn't own. Cabinets 1–5 are on
  controller 1, 6–10 on controller 2. Check `CONTROLLER_ID` and which port the board is on.
- **Solenoids click / chatter on reset, or the board reboots in a loop** → the relay pull-ups are
  missing. See "Boot safety" above; firmware cannot fix this one.
- **"not eligible"** → student's program isn't BIT Electrical/Mechatronics/HVAC&R, or they're
  banned/overdue. Reseed if demo data went stale: `php artisan migrate:fresh --seed`.
- **RC522 `0x00`/`0xFF`** → wiring; VCC must be 3.3V. Note **RST is D5** (it was D9 before the
  two-controller rewire).
- **"Reads slow / sometimes doesn't read"** → in the bridge log, measure the gap between `SCAN,`
  and `DONE,`. `SCAN` prompt but `DONE` late or absent = the **slot sensor** gate, not the reader
  (tool not in the beam, or reading inside the hysteresis band — watch the `#slot` stream). `SCAN`
  itself late or absent = the reader: check the `#rc522 reinit (open) v=0x..` line for that window
  (`0x00`/`0xFF` = bus dead), then power (3.3 V rail sag under TX; add 10–100 µF at the module),
  SPI lead length (>15–20 cm at 4 MHz is marginal), and antenna placement vs. the energised solenoid.
- **Door stays open after Cancel** → the bridge is not running the current `bridge.py` (it sends
  `ABORT` when the kiosk cancels), or `command-status` is failing — check its log.

## Going to production
- `#define BENCH_MODE 0` in `locker_controller.ino`, re-upload; the boot banner shows `bench=0`.
- Delete the `inject.txt` hook in `bridge.py` (marked TEMPORARY).
- Launch the kiosk with `?kiosk=1`; the browser fullscreen on the 1024×600 panel.
- Rotate `DEVICE_API_KEY` (`laravel/.env`, `bridge/config.ini`, `terminal.js`) — the old one is public.

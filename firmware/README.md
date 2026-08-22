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
  bridge/bridge.py                           mini-PC bridge (headless)
  bridge/bridge_gui.py                       mini-PC bridge (GUI: port picker, status, log)
  bridge/config.ini                          serial_port, base_url, api_key
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

Also: the 20 sensors need their own 5 V rail (~15 mA each, ~300 mA peak) — more than the Mega's
onboard regulator. Verify the LM2596 output really is 5.0 V *before* connecting anything to it.

## The flow (what the firmware does)
1. Bridge sends `OPEN,<cabinet>,<borrow|return>` (because a student chose a tool on the touchscreen).
2. Firmware unlocks that cabinet, beeps, prints `OPENED,<cabinet>`.
3. It waits (20 s) for **both**: the ultrasonic to confirm the tool moved (removed for borrow,
   present for return) **and** the tool's **RFID tag** scanned on the RC522.
4. On success it relocks, double-beeps, prints `DONE,<cabinet>,<uid>,<slot>`. The bridge then calls
   the server, which records the borrow/return of the exact tool with that UID.
5. If nothing happens in time it relocks and prints `TIMEOUT,<cabinet>` (server cancels).

Inside the wait loop the RC522 is polled **every tick (~5 ms)** while ultrasonics fire **one sensor
per 60 ms**, round-robin. That split is deliberate: sensors need a settle gap between pings to avoid
crosstalk, but a reader polled that slowly misses a tag tapped and lifted in under 300 ms.

While `SENSORS_ENABLED = false` (the bring-up default) step 3 needs only the tag, and the reported
slot is `0`. Flip it to `true` once the ultrasonics are wired.

## Serial protocol
| Dir | Message | Meaning |
|-----|---------|---------|
| PC→Mega | `OPEN,<cabinet>,<borrow\|return>` | open a cabinet for a transaction |
| PC→Mega | `WHO` | re-announce identity (no reset) |
| PC→Mega | `ABORT` | relock the open cabinet immediately |
| Mega→PC | `#<banner>` | informational; the bridge logs and ignores it |
| Mega→PC | `READY,<controller_id>` | booted; says which board this is |
| Mega→PC | `OPENED,<cabinet>` | cabinet unlocked |
| Mega→PC | `SCAN,<uid>` | a tag was read while waiting |
| Mega→PC | `DONE,<cabinet>,<uid>,<slot>` | confirmed → bridge records it (slot 1..N, 0 = unknown) |
| Mega→PC | `TIMEOUT,<cabinet>` | gave up, relocked |
| Mega→PC | `NOWIRE,<cabinet>` | that cabinet belongs to the **other** controller |
| Mega→PC | `ERR,<line>` | command not understood |

> `DONE` has **four** fields. Parsing only three glues `,<slot>` onto the UID, and because
> `Tool::normTag` strips separators but keeps digits, the tag then never matches any tool and
> every confirm returns 422. That bug shipped once — don't reintroduce it.

## Running it
On the mini PC (see also `../SETUP-FRESH-PC.md`):
1. `cd laravel && php artisan migrate:fresh --seed && php artisan serve`  (reseed = fresh demo data)
2. `pip install pyserial` (once)
3. Set `firmware/bridge/config.ini` → `serial_port` (Device Manager → Ports) and make `api_key`
   match `DEVICE_API_KEY` in `laravel/.env`.
4. Run the bridge — **GUI:** `python firmware/bridge/bridge_gui.py` · **headless:** `python bridge.py`.
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

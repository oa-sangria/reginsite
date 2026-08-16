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

## Wiring (Arduino Mega 2560)
RC522: **SS=53, SCK=52, MOSI=51, MISO=50, RST=9, VCC=3.3V (not 5V), GND=GND**
Buzzer: **D40**. Lockers **1–4** are wired; **5–10** are `0` placeholders in the sketch — fill
their pins as you build them out.

| | Locker 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| Relay | 22 | 23 | 24 | 25 |
| LED | 26 | 27 | 28 | 38 |
| Ultrasonic TRIG | 30 | 32 | 34 | 36 |
| Ultrasonic ECHO | 29 | 33 | 35 | 37 |

`ACTIVE_LOW = true` (flip if your relay is inverted). One ultrasonic per locker for now; the full
build has 4 per locker (40 total) — a TODO to expand `trigPins/echoPins` into a 2-D map.

## The flow (what the firmware does)
1. Bridge sends `OPEN,<locker>,<borrow|return>` (because a student chose a tool on the touchscreen).
2. Firmware unlocks that locker, beeps, LED on, prints `OPENED,<locker>`.
3. It waits (20 s) for **both**: the ultrasonic to confirm the tool moved (removed for borrow,
   present for return) **and** the tool's **RFID tag** scanned on the RC522.
4. On success it relocks, double-beeps, prints `DONE,<locker>,<uid>`. The bridge then calls the
   server, which records the borrow/return of the exact tool with that UID.
5. If nothing happens in time it relocks and prints `TIMEOUT,<locker>` (server cancels).

## Serial protocol
| Dir | Message | Meaning |
|-----|---------|---------|
| PC→Mega | `OPEN,<locker>,<borrow\|return>` | open a locker for a transaction |
| Mega→PC | `READY` | booted |
| Mega→PC | `OPENED,<locker>` | locker unlocked |
| Mega→PC | `SCAN,<uid>` | a tag was read while waiting |
| Mega→PC | `DONE,<locker>,<uid>` | confirmed → bridge records it |
| Mega→PC | `TIMEOUT,<locker>` | gave up, relocked |
| Mega→PC | `NOWIRE,<locker>` | that locker isn't wired on this board |

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
> ⚠ Re-scan **Plier 4** — its tag currently holds placeholder `PLIER4-RESCAN` (source UID
> duplicated Plier 3). Read it with `rfid_read_test.ino`, then set it on the Inventory page.

## Troubleshooting
- **No COM port** → install the CH340 driver, reboot; close the Arduino Serial Monitor.
- **`NAK`/confirm fails / nothing saves** → Laravel not running, wrong `base_url`, or `api_key`
  ≠ `.env`.
- **"not eligible"** → student's program isn't BIT Electrical/Mechatronics/HVAC&R, or they're
  banned/overdue. Reseed if demo data went stale: `php artisan migrate:fresh --seed`.
- **RC522 `0x00`/`0xFF`** → wiring; VCC must be 3.3V.

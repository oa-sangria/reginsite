# reginsite — Locker Firmware (Arduino Mega + mini-PC bridge)

First hardware milestone: **scan an RFID card → the solenoid toggles open/closed, and every
toggle saves a borrow/return row to the database.** No QR / student scan yet — that comes later.

## How the pieces fit (why there's a "bridge")

An Arduino Mega has **no network and no clock**, so it *cannot* write to MySQL by itself. It
talks over USB serial to the mini PC, and a small Python **bridge** on the mini PC calls the
Laravel API, which writes the database:

```
Arduino Mega ──USB serial──> bridge.py (mini PC) ──HTTP──> Laravel API ──> MySQL
```

"Does it save locally first?" — The mini PC is the always-on *local* machine. If Laravel + MySQL
run **on the mini PC**, then local and server are the same box and there's no offline gap. The
bridge still keeps a safety-net file (`queue.jsonl`): if the server is ever unreachable, events
are buffered there and replayed through `/api/esp32/sync` when it returns. On-device SD/flash
buffering only becomes necessary if you later replace the Mega with a networked ESP32.

## Contents
```
firmware/
  locker_controller/locker_controller.ino   Arduino Mega sketch
  bridge/bridge.py                           serial -> API bridge (run on the mini PC)
  bridge/config.ini                          COM port, server URL, API key, test student/tool
```

## Wiring (Arduino Mega 2560)

**RC522 RFID reader** (hardware SPI):

| RC522 | Mega | Note |
|-------|------|------|
| SDA / SS | D53 | |
| SCK | D52 | |
| MOSI | D51 | |
| MISO | D50 | |
| RST | D9 | |
| VCC | **3.3V** | not 5V |
| GND | GND | |

**Relay → 12V solenoid (Locker 10):**

| Relay | Mega / power |
|-------|--------------|
| IN | **D31** |
| VCC / GND | 5V / GND |
| COM / NO | switches the 12V brick to the solenoid |

Idle = **locked** (solenoid de-energized). A scan energizes the relay to unlock; the next scan
releases it. The onboard LED (D13) mirrors the lock (on = open).

> If your solenoid opens when it should close, your relay board is the opposite polarity —
> set `#define RELAY_ACTIVE_LOW false` in the sketch (it's `true` by default; most cheap boards
> are active-low).

## 1. Upload the sketch
1. Arduino IDE → **Library Manager** → install **MFRC522** (by GithubCommunity). `SPI` is built in.
2. Open `locker_controller/locker_controller.ino`.
3. **Tools → Board:** Arduino Mega 2560; **Tools → Port:** the Mega's COM port.
4. Upload.

## 2. Test the hardware alone (no PC bridge yet)
Open **Serial Monitor** at **115200 baud**. Tap a card:
```
READY
SCAN,A1B2C3D4
EVENT,OPEN,A1B2C3D4      <- relay clicks, solenoid unlocks, LED on
SCAN,A1B2C3D4
EVENT,CLOSE,A1B2C3D4     <- relay releases, LED off
```
If you see that and hear the relay, the firmware is good. **Close the Serial Monitor** before
running the bridge (only one program can hold the COM port).

## 3. Save to the database (the bridge)
On the mini PC:
1. Make sure the Laravel app is running: `cd laravel && php artisan serve`
   (must be reachable at the `base_url` in `config.ini`).
2. Install the serial library once: `pip install pyserial`
3. Edit `bridge/config.ini` → set `serial_port` to the Mega's port (Device Manager → Ports).
4. Run it:
   ```
   cd firmware/bridge
   python bridge.py
   ```
5. Tap the card. The bridge prints:
   ```
   [mega] EVENT,OPEN,A1B2C3D4
     -> BORROW saved. tx #12 (tool 10, QR-2026-0132)
   ```
   Open the admin site → **Borrow Log** / Dashboard: Locker 10 turns red/removed. Tap again →
   the return is logged and the locker goes green/present. That row is in MySQL. ✅

## Serial protocol
| Direction | Message | Meaning |
|-----------|---------|---------|
| Mega → PC | `READY` | booted |
| Mega → PC | `SCAN,<uid>` | a card was read (informational) |
| Mega → PC | `EVENT,OPEN,<uid>` | lock opened → bridge does **borrow** |
| Mega → PC | `EVENT,CLOSE,<uid>` | lock closed → bridge does **return** |
| PC → Mega | `ACK` | DB write OK (LED double-blink) |
| PC → Mega | `NAK,<msg>` | DB write failed (LED triple-blink; lock not reversed) |

## Offline test (optional)
Stop `php artisan serve`, tap a few times → events go to `bridge/queue.jsonl` and the LED
triple-blinks. Restart Laravel → the bridge auto-flushes the queue via `/api/esp32/sync` and the
rows appear.

## Troubleshooting
- **`cannot open COMx`** — wrong port, or the Arduino Serial Monitor still has it open. Close it.
- **`NAK,offline`** — the bridge can't reach `base_url`. Is `php artisan serve` running? Right IP?
- **`REJECTED: Tool ... is not available`** — tool 10 is already borrowed from an earlier test.
  Reset the demo data: `cd laravel && php artisan migrate:fresh --seed`.
- **Nothing on scan** — recheck RC522 wiring (VCC = 3.3V), and that SS=D53 / RST=D9.

## What's next (not built yet)
- **QR student scan** at the terminal → replaces the fixed `test_student_qr`.
- **Card UID → tool mapping**: register each card's UID (shown in `SCAN,<uid>`) as that tool's
  RFID tag on the Inventory page, then a scan can pick the right tool automatically (would use a
  small `/api/esp32/tag` endpoint).
- Ultrasonic occupancy sensor + status LEDs per the full workflow.

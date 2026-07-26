# reginsite — Locker Firmware (Arduino Mega + mini-PC bridge)

4-locker smart tool cabinet. **Each tool has its own RFID tag.** Scan a tag → that locker
toggles and a borrow/return row is saved to the database. No QR / student scan yet.

## How the pieces fit (why there's a "bridge")
An Arduino Mega has **no network and no clock**, so it can't write to MySQL itself. It talks over
USB serial to the mini PC, and a small Python **bridge** on the mini PC calls the Laravel API,
which writes the database:

```
Arduino Mega ──USB serial──> bridge.py (mini PC) ──HTTP──> Laravel API ──> MySQL
```

If the server is ever unreachable the bridge buffers events to `queue.jsonl` and replays them via
`/api/esp32/sync` once it's back.

## Wiring (your rig)
RC522: **SS=53, SCK=52, MOSI=51, MISO=50, RST=9, VCC=3.3V (not 5V), GND=GND**

| | Locker 1 | Locker 2 | Locker 3 | Locker 4 |
|---|---|---|---|---|
| Relay | 22 | 23 | 24 | 25 |
| LED | 26 | 27 | 28 | 38 |
| Ultrasonic TRIG | 30 | 32 | 34 | 36 |
| Ultrasonic ECHO | 29 | 33 | 35 | 37 |

Idle = locked. `ACTIVE_LOW = true` (flip in the sketch if your relay is inverted). LEDs mirror
lock state. Ultrasonic sensors are read/printed for monitoring but don't gate the logic yet.

## Setup, step by step

### 1. Upload the sketch
Arduino IDE → install the **MFRC522** library → open
`locker_controller/locker_controller.ino` → Board: **Arduino Mega 2560**, pick the Port → Upload.

### 2. Register your 4 tags (one-time)
Open **Serial Monitor @115200** and tap each tool's tag. Unregistered tags print:
```
UNKNOWN,DE AD BE EF
```
Copy each UID into the `tagUID[]` table at the top of the sketch — slot 0 = Locker 1's tag,
slot 1 = Locker 2's, etc. — then re-upload. Now a known tag prints:
```
SCAN,DE AD BE EF
Locker 1 UNLOCKED
EVENT,OPEN,1,DE AD BE EF
```
That's the hardware working. **Close the Serial Monitor** before running the bridge (only one
program can hold the COM port).

### 3. Save to the database (the bridge)
On the mini PC:
1. Start the app fresh: `cd laravel && php artisan migrate:fresh --seed` then `php artisan serve`
   (reseed matters — the demo timestamps go stale over time, which can wrongly block a student).
2. One-time: `pip install pyserial`
3. Edit `bridge/config.ini` → set `serial_port` to your Mega's COM port.
4. Run: `cd firmware/bridge && python bridge.py`
5. Tap a tag → the bridge prints e.g. `Locker 1: BORROW saved. tx #12`. Check the admin
   **Borrow Log** / Dashboard: that locker turns red/removed. Tap the same tag again → return
   logged, locker green/present. ✅ it's in MySQL.

## Serial protocol
| Dir | Message | Meaning |
|-----|---------|---------|
| Mega→PC | `READY` | booted |
| Mega→PC | `SCAN,<uid>` | known tag read |
| Mega→PC | `UNKNOWN,<uid>` | tag not in `tagUID[]` — register it |
| Mega→PC | `EVENT,OPEN,<locker>,<uid>` | opened → bridge **borrows** |
| Mega→PC | `EVENT,CLOSE,<locker>,<uid>` | closed → bridge **returns** |
| PC→Mega | `ACK` / `NAK,<msg>` | DB write ok / failed (LED confirm/error blink) |

## config.ini
- `serial_port`, `base_url`, `api_key` (must match `DEVICE_API_KEY` in `laravel/.env`)
- `test_student_qr` — who every toggle is logged as until the QR scan exists (default Mark, `QR-2026-0132`)
- `locker_tool_map` — which database tool sits in each physical locker (default `1:1,2:2,3:3,4:4`)

## Troubleshooting
- **`cannot open COMx`** → close the Arduino Serial Monitor first.
- **`NAK,offline`** → Laravel isn't running / wrong `base_url`.
- **`REJECTED: Tool ... not available`** or **`... must return first`** → stale demo data;
  run `php artisan migrate:fresh --seed` and test again. Test lockers 1-4 start Available.
- **RC522 reads `0x00`/`0xFF`** → wiring; VCC must be 3.3V, check SS=53 / RST=9.

## Next steps (not built yet)
- QR **student scan** → replaces the fixed `test_student_qr`.
- Wire the **ultrasonic sensors** into the logic (only log a borrow once the tool is actually removed).
- Per-locker status LEDs following the full borrow workflow.

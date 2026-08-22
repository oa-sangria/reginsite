# reginsite — Hardware Verification Checklist

The pin map is now decided (see `README.md`). This is no longer a data-collection form — it is a
**verification** pass: confirm each item behaves as the table says, tick it, and move on.
Test sketches live in `firmware/tests/` (+ `firmware/rfid_read_test`).

Upload one sketch at a time, open **Serial Monitor @ 115200**, line ending **Newline**.

> Work top to bottom. Do **not** connect all loads at once during first bring-up.

---

## 0. Power — before anything else

- ☐ SMPS output ≈ **12 V**
- ☐ LM2596 input ≈ **12 V**
- ☐ LM2596 output adjusted to **5.0 V** — measured with **nothing connected to it**
      (these ship at an arbitrary voltage; a 12 V passthrough kills all sensors at once)
- ☐ Common ground between LM2596 output, relay board, sensors and the Mega
- ☐ Cabinet metalwork is **not** used as an electrical return; no mounting screw shorts a rail

## A. Boot safety — the one that prevents damage

- ☐ **10 kΩ pull-up fitted from each relay `IN` to the relay board +5 V**
- ☐ With the Mega **unpowered**, every relay `IN` measures ~5 V
- ☐ Power-cycle the Mega 20× with 12 V connected → **zero solenoid clicks**
- ☐ Start `bridge.py` (this toggles DTR and resets the board) → **zero clicks**
- ☐ One flyback diode per solenoid fitted (10 total)

Why: from power-on until `setup()` runs (~0.5–2 s) every pin is high-Z and firmware cannot hold
the relays off. Without pull-ups the solenoids energize for that whole window; the inrush browns
out the supply, the Mega resets, and it loops.

---

## B. Relays — confirm pin → cabinet, and polarity
**Sketch:** `tests/relay_led_test` — type a cabinet number `1`–`5`, or `sweep` to pulse each in turn.

> The sketch only ever drives the **relay** pins. It must never be pointed at D22–D49 (ultrasonic
> TRIG *and* ECHO) or D50–D53 (SPI): an HC-SR04 drives its ECHO pin and the RC522 drives MISO, so
> driving those as outputs puts two drivers in opposition.

Mega 1:

| Cabinet | Tool | Relay pin | Opens? |
|---|---|---|---|
| 1 | Pliers | A12 | ☐ |
| 2 | Side Cutter | A13 | ☐ |
| 3 | Wire Crimper | D6 | ☐ |
| 4 | Clamp Meter | D7 | ☐ |
| 5 | Multimeter | D8 | ☐ |

**Polarity:** the solenoid unlocks when the pin is ☐ LOW (→ `ACTIVE_LOW = true`, the default)  ☐ HIGH

---

## C. Buzzer
**Pin D16.** Note D16 is **TX2** — never add a Serial2 device to Mega 1.

- ☐ Audible on boot (2 short beeps)
- ☐ **Drive current checked.** An active buzzer typically draws 25–30 mA, which **exceeds the
      Mega's 20 mA per-pin continuous rating** — drive it through a transistor, or fit a series
      resistor, rather than straight off the pin.

There are **no status LEDs** in this build; the buzzer is the indicator.

---

## D. Ultrasonic — one sensor per tool slot
**Sketch:** `tests/ultrasonic_test`. Type a **TRIG/ECHO pair** (`22/23`, or `a0/a1`), or `cab1`…`cab5`
to walk a whole cabinet one sensor at a time.

Confirm each pair reads plausibly, then record the two distances that set the thresholds:

- **Tool present** reads ≈ ______ cm   ·   **Slot empty** reads ≈ ______ cm

These set `PRESENT_CM` (currently 8.0) and `ABSENT_CM` (currently 12.0) in the firmware. Readings
*between* the two hold the previous state — that hysteresis band is what stops a tool resting near
the boundary from flapping, so leave a real gap between them.

Mega 1 — tick each pair once verified:

| Cabinet | Slot 1 | Slot 2 | Slot 3 | Slot 4 |
|---|---|---|---|---|
| 1 Pliers | ☐ 22/23 | ☐ 24/25 | ☐ 26/27 | ☐ 28/29 |
| 2 Side Cutter | ☐ 30/31 | ☐ 32/33 | ☐ 34/35 | ☐ 36/37 |
| 3 Wire Crimper | ☐ 38/39 | ☐ 40/41 | ☐ 42/43 | ☐ 44/45 |
| 4 Clamp Meter | ☐ 46/47 | ☐ 48/49 | ☐ A0/A1 | ☐ A2/A3 |
| 5 Multimeter | ☐ A4/A5 | ☐ A6/A7 | ☐ A8/A9 | ☐ A10/A11 |

Mega 2 (*not built yet*): cabinets 6–8 use 22/23…44/45 as above; cabinet 9 = 46/47, cabinet 10 = 48/49.

- ☐ Sensors are on the regulated 5 V rail, **not** the Mega's onboard regulator
      (~15 mA each; 20 sensors ≈ 300 mA peak)
- ☐ `SENSORS_ENABLED` flipped to `true` in `locker_controller.ino` once wiring is verified

---

## E. QR scanner — exact text format  *(still open)*
**Tool:** open `tests/qr_capture.html` in a browser, click the box, scan ONE real student ID.
Copy the **"Raw (escaped)"** line and paste it here:

```
(paste here — e.g. "Student No.: 2023101132\nFull Name: ...\nProgram: ...")
```

---

## F. RFID tool tags — done
All 34 tags are scanned and seeded, 4 per cabinet except cabinets 9 and 10 (one Makita Drill each).
Re-scan only if a tag ever fails to match (`rfid_read_test`), then set it on the Inventory page.

---

## G. Pins already assigned (reference)
- **RC522:** SS=53, SCK=52, MOSI=51, MISO=50, **RST=5**, VCC=**3.3 V**
- **Buzzer:** D16
- **Relays (Mega 1):** A12, A13, D6, D7, D8
- **Ultrasonics:** D22–D49 and A0–A11 (see section D)
- **Mega 2 relays:** A0–A4 — deliberately *not* D50–D53, which are the SPI bus

`A0`–`A15` are `D54`–`D69` on a Mega and are full digital I/O.

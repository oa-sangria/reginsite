# reginsite — Hardware Data Checklist

Fill this in using the test sketches, then send it back and I'll drop the values straight into
`locker_controller.ino`. Test sketches live in `firmware/tests/` (+ `firmware/rfid_read_test`).

Upload one sketch at a time, open **Serial Monitor @ 115200**, line ending **Newline**.

---

## A. Relays — which pin opens which locker + polarity
**Sketch:** `tests/relay_led_test` → type `sweep` (pulses pins 22→53 one at a time). Watch which
solenoid clicks/opens for each pin. You can also type a single pin number to toggle it.

| Locker | Tool type       | Relay pin |
|--------|-----------------|-----------|
| 1      | Soldering Iron  |           |
| 2      | Plier           |           |
| 3      | Clamp Ammeter   |           |
| 4      | Multitester     |           |
| 5      | Screwdriver Set |           |
| 6      | Side Cutter     |           |
| 7      | Makita Drill    |           |
| 8      | Wire Stripper   |           |
| 9      | Wire Crimper    |           |
| 10     | (spare)         |           |

**Relay polarity:** the solenoid UNLOCKS when the pin is ☐ HIGH  ☐ LOW
(if LOW → `ACTIVE_LOW = true`, the current default)

---

## B. LEDs — status indicator
**Sketch:** same `tests/relay_led_test` (toggle a pin, see which LED lights).

- Is it ☐ one LED per locker  ☐ a single system LED?
- LED turns ON when pin is ☐ HIGH  ☐ LOW

If **per-locker**, list the pins:

| Locker | LED pin |   | Locker | LED pin |
|--------|---------|---|--------|---------|
| 1      |         |   | 6      |         |
| 2      |         |   | 7      |         |
| 3      |         |   | 8      |         |
| 4      |         |   | 9      |         |
| 5      |         |   | 10     |         |

If **single system LED**: LED pin = ______

**What should the LED show?** (tick / edit)
- ☐ Green solid = system ready / idle
- ☐ LED ON while a locker is open (during a borrow/return)
- ☐ other: ______________________________________________

---

## C. Ultrasonic — one sensor per tool slot
**Sketch:** `tests/ultrasonic_test`. Set the shared **TRIG** pin at the top; type each slot's
**ECHO** pin to read it. Put a tool in the slot, then remove it, and note both distances.

- **Shared TRIG pin:** ______
- **Tool present** reads ≈ ______ cm  ·  **Slot empty** reads ≈ ______ cm
  (I'll set the threshold between them)

**Echo pin per slot** (leave blank if a slot doesn't exist):

| Locker | Slot 1 | Slot 2 | Slot 3 | Slot 4 |
|--------|--------|--------|--------|--------|
| 1 Soldering Iron |   |   |   |   |
| 2 Plier          |   |   |   |   |
| 3 Clamp Ammeter  |   |   |   |   |
| 4 Multitester    |   |   |   |   |
| 5 Screwdriver    |   |   |   |   |
| 6 Side Cutter    |   |   |   |   |
| 7 Makita Drill   |   |   | —  | —  |
| 8 Wire Stripper  |   |   |   |   |
| 9 Wire Crimper   |   |   |   |   |

**Slot counts right?** (4 each, drill = 2)  ☐ yes  ☐ no → correction: ______________

---

## D. QR scanner — exact text format
**Tool:** open `tests/qr_capture.html` in a browser, click the box, scan ONE real student ID.
Copy the **"Raw (escaped)"** line and paste it here:

```
(paste here — e.g. "Student No.: 2023101132\nFull Name: ...\nProgram: ...")
```

---

## E. RFID tool tags — already collected
Done — all tags seeded. Re-scan only if a tag ever fails to match (use `rfid_read_test`).
(Plier 3 = 93:0E:79:06, Plier 4 = 25:F8:7A:06 — fixed.)

---

## F. Pins already assigned (for reference — change if they clash)
RC522: SS=53, SCK=52, MOSI=51, MISO=50, RST=9, VCC=3.3V.  Buzzer (test only): D40.
Lockers 1–4 currently: relay 22/23/24/25, LED 26/27/28/38. Confirm or correct in the tables above.

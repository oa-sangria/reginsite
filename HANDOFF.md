# reginsite — Session Handoff / Project Notes

> Living doc so any agent/session can pick up where we left off. **Update this when you change things.**
> Last updated: 2026-08-17 (session 6: **kiosk UI rebuilt — "Workshop Light"**)

## ⚠ Hardware facts corrected this session (earlier notes were wrong)
- **The touchscreen is 1024×600, not 800×480.** 7″ IPS, 5-point capacitive, **HDMI + USB touch,
  driver-free** (sold as a "Raspberry Pi 7 inch" monitor but it is just an HDMI panel; it runs off
  the Windows mini PC). Design to 1024×600 and launch the browser **fullscreen/kiosk** — with an
  address bar the viewport is only ~1024×540.
- **The SM8070 is a standalone desk unit, not part of the screen.** White/blue cube, ~7 cm, **scan
  window faces UP**, USB keyboard-wedge, DC5V/0.5A over the same USB lead. It has a **speaker
  grille — it beeps on a successful read**, which is the student's real confirmation. Students hold
  the ID **QR-face-down over the window**; they do not aim it at the monitor. Any terminal copy that
  tells them to "hold your ID up to the screen" or points in a fixed direction is wrong.

## Session 6 — terminal kiosk redesign (current)
The touchscreen terminal was a dark violet gradient page sharing the admin stylesheet and was never
laid out for the real panel. **Rebuilt from scratch as its own design system.** Flow, API calls and
payload contract are byte-for-byte unchanged — this was presentation + kiosk behaviour.

- **New file `laravel/public/assets/css/terminal.css`** — "Workshop Light": warm paper `#f2efe8`,
  espresso ink `#1c1a24`, indigo `#5b50e6` reserved for *actions only*. Hairline warm rules, flat
  fills, no decorative gradients. Own `--k-*` token set, all classes prefixed `k-`.
- **Fixed panel layout, never scrolls the page.** `.k-device` is a `54px / 1fr / 30px` grid filling
  `100vw × 100vh`. Only three regions scroll: terms, loan list, tool grid. Above 1180×700 (a dev
  monitor) it clamps to exactly 1024×600 and picks up a bezel so it reads as an appliance instead of
  stretching. Short lists/grids centre via `.k-listwrap`; long ones fill and scroll.
- **The idle screen shows a drawing of the SM8070 desk unit with an ID being lowered onto it** — an
  instruction diagram of the real motion, not a scan target on the screen. Copy is
  "hold your ID with the QR code facing down over the scanner window; it beeps once" — no
  directional claim, since where the unit sits on the counter is up to whoever installs it.
- **The dead `.term-*` block was deleted from `styles.css`** (was ~77 lines, nothing else used it).
  terminal.html no longer loads the admin stylesheet at all.
- **Custom SVG icon sprite** inline in `terminal.html` — one coherent stroke system (1.7 / round),
  with a glyph per real seeded tool type (soldering iron, plier, clamp ammeter, multitester,
  screwdriver, side cutter, drill, stripper, crimper). `toolIcon()` in terminal.js maps by regex.
  *The screwdriver is drawn upright on purpose* — diagonal it twins the soldering iron at 29px.
- **Screens**: idle (animated QR scan target) → terms → home (identity + loans + two slab keys) →
  picker → await (locker-door graphic + 3-step rail) → receipt (perforated ticket + drain timer).
  Borrow picker = 5-col grid so all 10 lockers fit in two rows with no clipped row. Return picker =
  full-width rows (short list, so it carries locker + type + out-time + due state).
- **New kiosk behaviour** (beyond styling):
  - **Inactivity auto-logout** — 75s, countdown shown in the status strip from 20s, urgent at 8s.
    The await screen is exempt (a locker is physically open).
  - **Real link indicator** — the status light goes red on any fetch failure, green on success.
  - **`?kiosk=1`** production mode hides every bench affordance (demo IDs, simulated-tag box,
    BENCH MODE badge, Admin link). **`?station=03`** labels the equipment bar.
  - **Due-time chips** computed from `expectedReturn` ("DUE IN 5H 24M" / "OVERDUE 23H 0M").
  - A rejected simulated tag now shows **inline** instead of `alert()` and keeps the poll alive.
  - **QR wedge bug fixed**: keystrokes aimed at a focused input no longer double-feed the wedge
    buffer (it used to fire a premature `doScan` while someone typed a student no.).
- **Verified — layout**: `node --check` clean; all 11 screen states captured headless at 1024×600 via
  a fetch-stubbing harness that drives the *real* JS by clicking (scratchpad `gen.js`).
- **Verified — logic, against live Laravel + MySQL** (no stubs, same-origin driver pages temporarily
  dropped in `public/`, since deleted):
  - `verify-student` real payload renders correctly — real name/major, real open loan with a due
    chip computed from `expectedReturn`, all 9 lockers with real availability counts, real terms.
  - `Locker 1 — Soldering Iron` parses to plate `LKR 01` + type `Soldering Iron` (em-dash split).
  - **Real borrow**: locker-1 request → command #3 queued → `confirm` with tag `E9:8C:7B:06` →
    `Soldering Iron 2` flipped to *borrowed*, tx6 written against locker 1 with an 8-hour due time,
    command marked *done*, receipt rendered from the real response.
  - **Real return**: same tool → tx6 *returned* with a `return_time`, tool back to *available*,
    locker 1 LED back to *green*, student's open borrows back to 1.
  - **Wrong-tag rejection**: confirming a locker-4 tag against locker 1 returned the real server
    error "Scanned tag is not a tool from this locker", shown inline with the locker still open and
    the poll still alive — no dead end.
  - Receipt auto-dismiss to idle after 10s confirmed (it fired during a long capture).
  - DB reseeded clean afterwards (31 available / 3 borrowed / 0 device_commands); server stopped.
- ⚠ **Known gap**: the "key in your student no." input has no on-screen keypad, so on the Pi it only
  works if a virtual keyboard is installed. `inputmode="numeric"` is set. The QR scanner is the real
  input path; add a numeric keypad overlay if manual entry needs to work unattended.
- The **legacy root `terminal.html` + root `assets/` were deliberately left untouched** (deprecated
  plain-PHP fallback, different API base). Only the Laravel copy was redesigned.

---


## Session 5 — screen-driven system (current)
Full hardware architecture locked in. Real parts: Dell mini PC (runs DB + Laravel + terminal UI +
bridge), touchscreen, **USB QR scanner SM8070** (keyboard-wedge → types the student ID into the
terminal page), Arduino Mega (10 lockers via 3× 4-ch relays, HC-SR04 ultrasonic, RC522 tool tags,
buzzer + green LED).

**Flow (screen-driven, verified server-side via curl):**
1. Terminal: QR scanned → `POST /api/esp32/verify-student {qr}`. Server parses the QR text
   (`Student No. / Full Name / Program`), auto-provisions the student, and **gates by program**:
   only *Bachelor of Industrial Technology* majoring in **Electrical / Mechatronics / HVAC&R**
   (see `app/Services/QrStudent.php`). Then the usual banned/overdue checks.
2. Terminal: student accepts T&C, taps BORROW + a tool type → `POST /api/esp32/borrow-request
   {student_no, locker_id}` → server queues an OPEN command (`device_commands` table).
3. Bridge: `GET /api/esp32/commands` (polls) → sends `OPEN,<locker>,<mode>` to the Arduino.
4. Arduino: unlocks locker, waits for **ultrasonic** to confirm removal + reads the tool's **RFID
   tag**, replies `DONE,<locker>,<uid>` (or `TIMEOUT`).
5. Bridge: `POST /api/esp32/confirm {command_id, uid}` → server finds the tool by that UID and
   records the borrow of that specific instance. Return mirrors this.

**Real inventory is seeded** (`database/seeders/DatabaseSeeder.php`): 9 tool types, one locker
each, RFID tag per tool, from the user's scans. UIDs stored NORMALIZED (no separators, uppercase);
`Tool::findByTag()` / `Tool::normTag()` match any format (`AA:BB` or `AA BB`).
- Plier 3/4 duplicate RESOLVED: Plier 3 = `93:0E:79:06`, Plier 4 = `25:F8:7A:06` (seeded).
- ⚠ **Exposed device key still unrotated** in the public repo (GitGuardian). Rotate
  `DEVICE_API_KEY` (`.env`) + `firmware/bridge/config.ini` `api_key` before the next public push.
- **Buzzer is TEST-ONLY** (`BUZZER_ENABLED=false` in locker_controller.ino); the LED is the real
  status indicator.
- **Test sketches** in `firmware/tests/`: `relay_led_test` (map relay/LED pins + polarity),
  `ultrasonic_test` (map echo pins + find PRESENT_CM), plus `firmware/rfid_read_test` (tag UIDs)
  and `firmware/tests/qr_capture.html` (see exactly what the SM8070 types). Used to collect the
  pin/threshold/QR-format data still needed to finish wiring.

**New device endpoints** (all under `device.key`): `verify-student`, `borrow-request`,
`return-request`, `commands` (GET), `confirm`, plus the originals `state`, `borrow`, `return`,
`locker-status`, `sync`. Screen-driven uses request→commands→confirm; the immediate `borrow`/
`return` (by tool_id) are kept for the simulator/tests.

**Firmware** (`firmware/locker_controller/`): command-driven. Serial protocol —
PC→Mega `OPEN,<locker>,<borrow|return>`; Mega→PC `READY / OPENED,<l> / SCAN,<uid> /
DONE,<l>,<uid>,<slot> / TIMEOUT,<l> / NOWIRE,<l>`. Relay/LED pins for lockers 1–4 filled
(relay 22-25, LED 26-28+38, buzzer D40); **lockers 5–10 relay/LED pins are `0` placeholders.**
**Ultrasonic = PER-SLOT** (one sensor per tool position, 34 total): `slotEcho[locker][slot]` +
one `SHARED_TRIG_PIN` (all echo pins currently `0` — awaiting the user's wiring). Pin budget:
per-sensor trig+echo (68 pins) won't fit a Mega, so the design uses **1 shared trig + 1 echo per
slot (~35 pins)**. If no sensors are wired for a locker, it falls back to tag-only confirm so it's
testable now.

**Physical UI**: **7″ IPS capacitive touchscreen, 1024×600, HDMI + USB touch** on the mini PC runs
`terminal.html` — keep that page touch-friendly and fitting 1024×600 (T&C scrolls). Power: 650VA UPS.
Launch fullscreen/kiosk with `?kiosk=1` in production (see session 6). Styling lives in its own
`assets/css/terminal.css`, NOT the admin `styles.css`.
*(Earlier sessions recorded 800×480 — that was wrong, corrected in session 6.)*

**Bridge**: `firmware/bridge/bridge.py` (headless) and `bridge_gui.py` (Tkinter) both poll the
command queue and confirm results. `pip install pyserial`; config in `config.ini`.

**Terminal** (`laravel/public/terminal.html` + `assets/js/terminal.js`): captures the QR
keyboard-wedge, shows program + eligibility, does the screen-driven borrow/return, and has an
on-screen "simulate tag scan" input so it works without hardware.

**Verified session 5:** PHP8 lint clean; migrate:fresh --seed OK; QR parse + program gate
(eligible passes, non-BIT rejected); borrow-request→commands→confirm(real tag) borrow+return;
wrong-locker tag blocked; already-borrowed tag blocked; bridges `py_compile`; terminal.js
`node --check`; all 8 admin pages headless-render (31 avail / 3 borrowed / 2 overdue / 1 banned).
DB left clean, servers stopped.

---


## What this is
Admin website + device API for a **smart tool locker** system: students borrow/return lab tools
from solenoid-locked lockers driven by an ESP32/Arduino (QR student ID scan, RFID tool tags,
ultrasonic occupancy sensors, red/green LEDs). Hardware is NOT built yet — a web-based terminal
simulator stands in for it.

## Two versions exist (Laravel is primary)
| | Laravel 9 (PRIMARY, work here) | Plain PHP 5.6 (legacy fallback) |
|---|---|---|
| Location | `laravel/` | repo root + `api/` |
| Runs on | `php artisan serve` → **http://localhost:8000** (PHP 8.0.11 CLI from PATH) | XAMPP Apache → http://localhost/reginsite (PHP 5.6) |
| DB | `reginsite_laravel` (migrations+seeders) | `reginsite` (api/install.php) |

The legacy version still works and is untouched. New work goes in `laravel/`. The front-end
files exist in BOTH places — `laravel/public/*.html` + `laravel/public/assets/` is the live copy
for Laravel (API paths differ; see below). If you edit UI, edit the Laravel copy (and optionally
mirror to root for the legacy version).

## How to run (Laravel)
1. XAMPP: start **MySQL** (Apache only needed for the legacy version).
2. `cd c:\xampp\htdocs\reginsite\laravel` then `php artisan serve` (uses PHP 8.0.11 on PATH).
3. Open **http://localhost:8000** → login **admin / admin**.
4. Terminal simulator: **http://localhost:8000/terminal.html** — demo QR: `QR-2026-0132` (Mark, clear),
   `QR-2026-0457` (Regina, banned), `QR-2026-0319` (Carlo, overdue → borrow blocked).
5. Reset/reseed DB: `php artisan migrate:fresh --seed`.

## ⚠ Environment gotchas
- **Two PHPs**: XAMPP Apache = PHP 5.6.40 (legacy only). PATH php = **8.0.11** → this is why
  **Laravel 9** (Laravel 10+ needs PHP 8.1+). Composer 2.7.8 installed globally.
- Old MariaDB: `Schema::defaultStringLength(191)` set in `AppServiceProvider` (767-byte key limit).
- Timezone: `Asia/Manila` in `config/app.php`.
- Device API key: env `DEVICE_API_KEY` in `laravel/.env` (read via `config('services.device_key')`),
  currently `regin-esp32-2026`; duplicated in `laravel/public/assets/js/terminal.js`. Change both.
- Login is username-based (users table customized: username/name/password, no email).

## Laravel structure (all under `laravel/`)
- **Migrations**: `2014_...users` (username), `2026_06_16_000001_create_locker_system_tables`
  (students, lockers, tools, transactions, bans, settings).
- **Models** `app/Models/`: Student (has `eligibility()`, `openBorrows()`), Tool, Locker,
  Transaction, Ban, Setting (`Setting::get/put`), User.
- **`app/Services/LockerSystem.php`** — THE business rules: `runMaintenance()` (overdue flagging,
  auto-ban ≥2d overdue for 2d, auto-unban), `borrow()`, `returnTool()`. Throws HttpException.
- **Controllers**: AuthController (login/logout/me, session), BootstrapController (one-call
  payload matching the front-end's `window.DB` contract: string ids, ISO dates),
  InventoryController (saveTool/saveLocker/saveStudent/saveTerms), Esp32Controller (device API).
- **Middleware**: `VerifyDeviceKey` (alias `device.key`, X-API-Key). `Authenticate::redirectTo`
  returns null (JSON-only). CSRF exempts `api/*`. `Exceptions/Handler::render` forces the
  `{ok:false, error, code}` JSON shape for `api/*`.
- **Routes** (`routes/web.php`, web group so sessions work):
  - `POST /api/auth/login|logout`, `GET /api/auth/me`
  - auth-protected: `GET /api/bootstrap`, `POST /api/tools|lockers|students|terms`
  - device (`X-API-Key`): `POST /api/esp32/verify-student|state|borrow|return|locker-status|sync`
- **Front-end** `public/`: static HTML + vanilla JS (api.js/app.js/pages.js/dashboard.js/terminal.js).
  NOT Blade. `Api.load()` fills `window.DB`; renderers are framework-agnostic.
  Design language: warm paper + indigo ink (user's custom theme — don't genericize).

## Business rules
- `expected_return = borrow_time + borrow_limit_hours (8)` — settings table.
- Overdue → cannot borrow until returned. Overdue ≥ 2 days → auto 2-day ban. Bans auto-lift.
- All enforced in `LockerSystem`, run before every read/device action.

## Device API example
```
curl -X POST "http://localhost:8000/api/esp32/borrow" \
  -H "X-API-Key: regin-esp32-2026" -H "Content-Type: application/json" \
  -d '{"qr":"QR-2026-0132","tool_id":2}'
```
`sync` accepts `{events:[{type: borrow|return, qr, tool_id, timestamp}]}` for offline catch-up.

## Firmware (`firmware/` — started session 4)
Physical locker controller. **Milestone 1 done & verified: RFID scan → solenoid toggle → real
DB row (no QR yet).**
- **Hardware**: Arduino Mega + RC522 RFID + relay → 12V solenoid + charger brick, next to a
  **mini PC**. Wiring: RC522 SS=D53, SCK=D52, MOSI=D51, MISO=D50, RST=D9, VCC=3.3V; relay IN=D31.
- **Why a bridge**: Mega has no network/clock → can't reach MySQL. Mega ↔ USB serial ↔ `bridge.py`
  on the mini PC ↔ HTTP ↔ Laravel ↔ MySQL. Bridge buffers to `queue.jsonl` and replays via
  `/api/esp32/sync` if the server is down. (On-device SD/flash only needed if Mega → ESP32 later.)
- **Files**: `firmware/locker_controller/locker_controller.ino` (Mega sketch, MFRC522 lib,
  `RELAY_ACTIVE_LOW` flag, relay-pin array for growth), `firmware/bridge/bridge.py` (stdlib
  urllib + pyserial; needs `pip install pyserial`), `firmware/bridge/config.ini`
  (COM port, base_url, api_key, `test_student_qr=QR-2026-0132`, `tool_id=10`), `firmware/README.md`.
- **Serial protocol**: Mega→PC `READY` / `SCAN,<uid>` / `EVENT,OPEN,<uid>` (→borrow) /
  `EVENT,CLOSE,<uid>` (→return); PC→Mega `ACK` / `NAK,<msg>`.
- **No-QR test**: every toggle attributed to fixed student QR-2026-0132 + tool 10. No server
  changes (uses existing `/api/esp32/borrow|return|sync`).
- Verified via fake-serial harness against the live server: borrow tx, return tx, repeat toggle,
  offline queue→sync flush all pass; locker 10 returns to available; DB reseeded clean after.

## Next steps (agreed / likely)
1. **ESP32 firmware**: HTTP client for the 6 device endpoints (terminal.js = reference client).
2. Maybe: proper Vite/Blade front-end integration, reports/CSV export, multi-admin, QR image
   generation, real "open locker" command queue (currently `openLocker:true` is advisory).
3. Legacy plain-PHP version can be deleted once Laravel is fully adopted.

## Verification snapshot (2026-06-16, Laravel)
migrate:fresh --seed clean · root→index.html, all static pages 200 · bootstrap 401 without
session, JSON error shape everywhere · login/save tool/save student/dup-QR 422/terms round-trip
pass · esp32: 401 w/o key, banned 403, overdue 403, borrow→locker red/removed→return→green,
heartbeat, sync batch (good+bad) pass · headless render of all 8 admin pages + modals vs live
payload: 0 errors · seed stats: available 5, borrowed 4, returnedToday 4, overdue 2, banned 1.

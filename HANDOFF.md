# reginsite — Session Handoff / Project Notes

> Living doc so any agent/session can pick up where we left off. **Update this when you change things.**
> Last updated: 2026-06-16 (session 4: **started Arduino firmware** in `firmware/` — RFID toggle → DB)

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

# reginsite — Session Handoff / Project Notes

> Living doc so any agent/session can pick up where we left off. **Update this when you change things.**
> Last updated: 2026-09-21 (session 9: **public admin via Tailscale Funnel — `GO-LIVE-ADMIN.md`**; session 8: two-controller bridge review, second-tool ALERT + alarm, relative slot detection)

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

## Session 9 — publishing the admin site, kiosk stays local (current)

User's ask: the admin side (logs, dashboard) reachable from the internet; the kiosk NOT. Chosen
design, after ruling out "upload everything to a host" (kiosk would need internet to open a door)
and a read-only mirror (a sync job + second DB): **the mini PC stays the only server; a tunnel
publishes a second, restricted listener.** Step-by-step for the mini PC is **`GO-LIVE-ADMIN.md`**
(root). What is in the repo for it:
- **`ADMIN_PUBLIC_PORT`** (`.env`, `config('services.admin_public_port')`): Laravel is started
  twice — `:8000` kiosk + bridge as before, `:8001` for the tunnel. Global middleware
  `PublicListenerGuard` answers **404** on the public port for `api/esp32/*`, `terminal.html`,
  `assets/js/terminal.js`, `assets/css/terminal.css` (the device key ships in terminal.js).
  Decided by `SERVER_PORT` (the bound port — a Host header cannot spoof it under `php -S`) or
  `ADMIN_PUBLIC_LISTENER` (Apache `SetEnv`, where SERVER_PORT *is* Host-derived). Unset → no-op.
- **`laravel/server.php`** (new; `artisan serve` prefers it over the framework copy): on the
  public port the three kiosk files are not handed out as static files — they fall through to
  Laravel and the guard. Matches by `realpath` because Windows serves `/Terminal.html`,
  `/./terminal.html`, `/terminal.html.`, `/assets\js\terminal.js` as the same file. It also
  normalises `SCRIPT_*` to `index.php` for every request that reaches Laravel (the built-in
  server otherwise makes Laravel read `/terminal.html/` as `/`).
- `TrustProxies` trusts `127.0.0.1` (Funnel *overwrites* `X-Forwarded-For` with the visitor —
  checked in tailscale's `serve.go`), login route `throttle:10,1`, seeder password =
  `ADMIN_PASSWORD` (default `admin`), new **`php artisan admin:password`** (no reseed needed;
  refuses empty/`admin`). `.env.example` documents the three vars.
- **`start-station.bat`** (root): both listeners + bridge in minimised windows; Edge kiosk line
  commented. `.gitattributes` pins `*.bat` to CRLF — cmd.exe parsed the LF version as `'m' is not
  recognized`.
- Verified here with two `artisan serve`s + curl: 15 kiosk-path spellings and the esp32 routes
  404 on 8001 (JSON `public_only` for the API), admin pages/login/bootstrap 200 on 8001, throttle
  trips at the 10th try, nothing changed on 8000; `admin:password` + reseed-from-`.env` tested;
  the .bat brings up all three windows. **Not done here:** the Tailscale steps themselves (need
  the mini PC + an account) — commands come from the current Funnel CLI reference (v1.52+ syntax).
- Dev-machine `.env` now has `ADMIN_PUBLIC_PORT=8001` (harmless; 8001 is only up when started).
  DB reseeded clean, servers stopped.

## Session 8 — review of `265cec7` (Sep 20) + the "took two pliers" problem

**What `265cec7` (user, Sep 20, away from this doc) did — read this before the firmware:**
- **Two-controller bridge.** `config.ini` `serial_ports = COM5, COM9` (old `serial_port` still
  accepted). ONE `bridge.py` owns both ports; each `Board` learns its identity from the Mega's own
  banner (`controller=N cabinets=lo-hi rfid=0/1 bench=0/1` + `READY,N`), never from config, so a
  swapped USB socket cannot mis-route a door. `WHO` is sent 2 s after the port opens. OPEN/ABORT go
  to the board whose range covers the locker; a missing board → the live one answers `NOWIRE` →
  cancelled. Log lines are `[mega1]`/`[mega2]` once announced, `[COM5]` before.
- **Tag pairing for controller 2** (no RC522 there): its `DONE,<cab>,,<slot>` has an empty uid and
  is held in `pending_tag`; the bridge turns controller 1's idle scan on (`IDLESCAN,1`), waits up
  to `TAG_WAIT_S` (45 s) for a `SCAN,<uid>` from it (tap-then-lift is parked in `scanned`),
  confirms with that uid, then puts the idle scan back to the board's `bench` default. Single-flight
  now also gates on `pending_tag`.
- **Relative slot detection** replaced the absolute `emptyCm ± margin` thresholds (they needed every
  shelf calibrated to the cm and judged Locker 1 empty before the door opened). Baseline = mean of 3
  pings just before the door opens; a slot is "moved" once it differs from that by `CHANGE_CM`
  (2.0) for `AGREE_N` (2) samples, either direction (so `mode` is not consulted); 3 misses after a
  good baseline also count. `emptyCm` in the tables is reference only now. `#baseline,<cab>,<cm|none>…`
  and `#slot,<cab>,<slot>,<cm>,d=±<delta>,changed=0/1` in the log.
- Multimeter → **Meter Tape** (cab 5) everywhere; kiosk "Bench IDs" chips removed (sign-in must go
  through the reader or a keyed-in number). Test suite grew to 11 scenarios.

**The Plier 1 + Plier 2 test (user, Sep 21): a code problem, not the sensor.** Two lines in the
sketch guaranteed the second tool was invisible even with a perfect sensor: step 2 of the window
loop was gated on `changedSlot < 0` — sampling *stopped* the moment one slot moved — and DONE
`return`ed from `handleOpen()`, so nothing sampled after the relock either (the solenoid locks but
the door is still open until the student shuts it). There was also no protocol line for "another
slot moved". Built this session, end to end:
- **Firmware:** `changed` is a live state with hysteresis (trip at `CHANGE_CM`, clear within half of
  it), sampling never stops inside the window, the *primary* slot is the first to move and still
  moved (put back → the next moved slot takes over), and every other moved slot that stays moved for
  `ALERT_HOLD_MS` (1.5 s, a hand reaching past a neighbour) is `ALERT,<cab>,<slot>` + a
  **non-blocking alarm** (`alarmService()`, 150 ms on/off, `ALARM_MS` 30 s, re-armed per new alert,
  silenced early when every extra slot is back → `CLEAR,<cab>,<slot>`). DONE is **not** withheld by
  an alert (record the tagged tool, flag the other — better than a TIMEOUT that records neither).
  After DONE/TIMEOUT/ABORT, `watchAfterClose()` keeps sampling for `WATCH_MS` (30 s, longer while
  the alarm runs; after a TIMEOUT/ABORT *every* moved slot is an alert); it ends at once if the PC
  sends anything — that line is parked in `deferredLine` for `handleSerial()`, so an OPEN is never
  delayed behind it. `ALARM,<s>` PC→Mega command (`ALARM,0` never silences the board's own alarm).
  `FAULT,<cab>,nosensor`: a reader-less cabinet with no sensor echo now refuses to open (it used to
  DONE and relock ~120 ms after OPENED). Header protocol table updated. **Not compiled here** —
  brace/`#if` balance checked by script; compile on the mini PC.
- **Bridge:** `ALERT`/`CLEAR` → `POST locker-alert {locker_id, slot, cleared}` via the same
  retry queue as confirms (now `[path, payload, label, first]`); a controller-2 alert is relayed to
  controller 1 as `ALARM,30` / `ALARM,0` (it has the only buzzer). `FAULT` → confirm reason `fault`.
  `idle_state` resets when the reader board is gone (a DTR reset put its idle scan back to the bench
  default and the bridge never re-sent `IDLESCAN,1`). 13 scenarios pass (L: second tool on
  controller 1; M: controller-2 alert → ALARM relay + FAULT).
- **Server:** migration `2026_09_21_000004_add_alert_to_lockers` (`alert_slots`, `alert`,
  `alert_at`); `POST esp32/locker-alert` keeps the set of slots out and writes the human line
  ("Slot 2 moved without a tag scan — a tool may be out unrecorded · <student> (<no>) was at the
  door", from the locker's latest command); `command-status` now returns `alert`; `bootstrap`
  lockers carry `alert`/`alertSlots`/`alertAt`; `POST lockers {clearAlert:true}` clears it (staff).
  Confirm reason `fault` → "That locker's slot sensors are not responding — please ask staff".
- **Admin:** dashboard locker card goes red "UNTAGGED REMOVAL" with the line + rel-time, alerts panel
  gets "Untagged removals" (counts into "flagged"), inventory row shows the line, the locker edit
  modal has a "Clear this alert" checkbox. **Kiosk:** the await screen and the receipt show a red
  hazard strip "Only one tool per borrow — put the other tool back in its slot" while `alert` is
  set (the receipt keeps polling `command-status` for its 10 s). All four screenshotted via the
  Edge harness; server path exercised with curl against `artisan serve`.
- Also from the review of `265cec7`: `terminal.html` `<symbol id="i-check"viewBox=` missing space
  fixed; README scenario counts were stale.

**Hardware day (adds to the session-7 list):**
7. Borrow, lift Plier 1, tag it, *also* take Plier 2 → within ~2 s: `ALERT,1,2` in the log, buzzer
   alarm, kiosk strip, dashboard card red naming the student. Put Plier 2 back → `CLEAR,1,2`, alarm
   stops, strip gone. Walk away with it → alarm runs 30 s, the alert stays until staff clear it.
8. Open, lift a tool, never tag → after `TIMEOUT` the watch phase reports it as `ALERT` too.
9. Reach past slot A to take the tool in slot B → no ALERT (1.5 s hold-off). If it does alert,
   raise `ALERT_HOLD_MS`; if a real second tool never alerts, watch `d=` in `#slot`.
10. Next OPEN right after a DONE → `#watch,<cab>,end` then `OPENED` at once (watch steps aside).

## Session 7 — review of the Aug 22 + Sep 17 commits, done away from the hardware

Neither of the two commits after session 6 was written up, so first what they did:
- **`518f0e0` (Aug 22)** — bridge got a local queue + single-flight dispatch; kiosk poll 2s → 600ms;
  `Student::eligibility()` blocked on *any* open loan (**reverted this session — see rule change**).
- **`e34a304` "917" (Sep 17)** — `device_commands` grew terminal `timeout`/`failed` statuses + a
  `note`; rejected confirms are terminal; `command-status` endpoint; kiosk polls the *command* and
  shows "Door locked again"/"wrong tool"; bridge drains the whole serial buffer per pass; firmware
  `SENSORS_ENABLED=true`, sensor fallback to tag-only, pin-diagnostic serial commands; two-5V-rail
  power notes. **The USB RFID reader attempt is only the test page
  `firmware/tests/rfid_usb_capture.html`** (VID FFFF / PID 0035) — nothing else references it.
  Most likely reason it "didn't work": those readers are almost always **125 kHz EM4100** wedges and
  the tool tags are **13.56 MHz MIFARE** — it physically cannot see them. The page will show it:
  types nothing for a tool tag, types for a white 125 kHz fob.

**Rule change (user, 2026-09-18):** borrowing is blocked only by an **overdue** item (return it
first) or by holding **3 tools** already (`max_open_borrows` setting, seeded 3, shown on the admin
Terms page). A tool still inside its 8-hour window does not block. `Student::eligibility()` +
seeded T&C term 2 updated. *On the mini PC the T&C text lives in the DB — reseed or edit it on the
Terms page for the new wording to show.*

**Fixed / added this session (all verified except the firmware, which cannot compile here):**
- **Cancel now relocks the door.** Nothing ever sent the Mega `ABORT`. `bridge.py` polls
  `command-status` for its outstanding command once a second and sends `ABORT` when the kiosk has
  made it terminal. Before: door open for the rest of the window, a tool taken then unrecorded.
- **Bridge is a daemon now** — one bad pass is logged with its traceback and the loop continues;
  `api()` treats malformed bodies / half-closed connections as transient; confirms the server could
  not be reached for are **retried for 10 min** (`unsent` queue; `!! GAVE UP` line carries the
  payload to enter by hand). Six scenarios pass in a fake-serial harness (cancel→ABORT, normal DONE,
  late DONE, server down at DONE→retry, crash→continue, progress relay).
- **`bridge_gui.py` deleted** — a stale copy missing every fix since Aug 22; README pointed at it first.
- **Progress rail on the await screen.** Mega prints `MOVED,<cab>,<slot>` when the slot sensor
  flips and **beeps once on `SCAN` inside a window**; bridge relays both as
  `POST command-progress {stage}`; server accumulates them in `note` while the command is open;
  kiosk renders step 02 ✓ on moved, step 03 as a **spinner "hold the tag flat on the reader and
  keep it there until it beeps"** once moved, ✓ "Tag read" on scanned, and "Tag read — now lift the
  tool out" if the tag came first. Tag-only cabinets send `MOVED` immediately. The bridge only
  relays `SCAN` after `OPENED` (an idle-scan line already in the buffer must not tick the rail).
  All four states screenshotted at 1024×600 via the Edge harness.
- **Open window 20 s → 45 s** (`OPEN_TIMEOUT_MS`; bridge `STALE_AFTER` = 60 s follows it).
- **Firmware reader hardening** (compile + verify on the mini PC): `cardPresent()` is WUPA-only
  (REQA was redundant and cost a second 25 ms timeout per poll — loop ~30 ms/pass now, removal
  noticed in ~0.5 s instead of ~1 s); `rfidReinit()` at every OPEN + 10 s idle health check
  (`VersionReg` 0x00/0xFF or antenna off → soft reset), `#rc522 reinit (open) v=0x92` in the log.
  `BENCH_MODE` is the single switch for `ALLOW_SIMTAG`/`idleScanEnabled`/`slotDebug`; `SLOTDBG`
  moved out of `#if HAS_RFID`.
- **Per-slot sensor thresholds** from the user's empty-shelf distances (6.5–14 cm; table in
  `firmware/README.md`), as the third field of each `{TRIG,ECHO,emptyCm}`; thresholds =
  `emptyCm - ABSENT_MARGIN_CM (1.0)` / `- PRESENT_MARGIN_CM (2.0)`. `USS` prints `empty=` for
  re-measuring. The old global 5.9/7.4 pair is the fallback for `emptyCm = 0`.
- Small: `confirm()` 404s instead of 500 if the student was deleted; dead block in `sync()`
  removed; terminal.js sim path no longer has its own inline error (the poll shows the stop plate,
  same as hardware); `1024×600` in the terminal.js header; three unrelated nested repos ignored.

**Hardware day checklist** (also in `firmware/README.md` Troubleshooting):
1. Compile + upload; boot banner shows `bench=1`. `SELFTEST` → `v=0x92`, antenna ON, full read OK.
2. Bridge log: the gap between `SCAN,` and `DONE,`. SCAN prompt, DONE late/absent = **slot sensor**
   gate (watch `#slot`), not the reader. SCAN late/absent = reader → power (3.3 V sag; 10–100 µF at
   the module), SPI lead length (4 MHz, keep < 15–20 cm), antenna vs. solenoid, `#rc522 reinit` line.
3. `USS` with cabinets empty vs. the table; then a borrow per cabinet watching for `P` on the
   emptied slot. Tune `PRESENT_MARGIN_CM` / `ABSENT_MARGIN_CM`, not the table.
4. Kiosk Cancel → door relocks within ~1 s (`<- ABORT` in the log).
5. Tag first, then lift → rail says "Tag read — now lift the tool out"; lift first → spinner.
6. USB reader: `firmware/tests/rfid_usb_capture.html` with a tool tag, then any 125 kHz fob.

Still deferred (unchanged): await-screen department showcase; pending-command TTL on the server;
rotating the exposed `DEVICE_API_KEY`; the `inject.txt` hook and `SIMTAG` stay until bring-up ends.

---


## Session 6 — terminal kiosk redesign
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

**Bridge**: `firmware/bridge/bridge.py` (console; the only bridge since session 7) polls the
command queue and confirms results. `pip install pyserial`; config in `config.ini`.

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

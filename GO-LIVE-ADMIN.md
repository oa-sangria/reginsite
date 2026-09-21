# Reginsite — Publishing the admin site (the kiosk stays local)

Goal: staff open the **admin site** (dashboard, inventory, students, borrow/return logs, terms)
from anywhere on the internet, while the **kiosk, the bridge and the database stay on the mini PC**
and keep working exactly as before — even if the school's internet drops.

Follow the sections **in order**, on the mini PC. First-time setup takes ~20 minutes; everything
after that is one double-click. This assumes the mini PC already runs the system per
`SETUP-FRESH-PC.md` (XAMPP MySQL, Laravel on `php artisan serve`, the bridge).

---

## 0. How it works

```
 internet ──HTTPS──> Tailscale Funnel ──> Tailscale service on the mini PC ──> http://127.0.0.1:8001   ADMIN ONLY
                                                                                     │
 touchscreen kiosk ─────────────────────────────────────────────────────────> http://127.0.0.1:8000   kiosk + admin
 firmware/bridge/bridge.py ─────────────────────────────────────────────────> http://127.0.0.1:8000/api/esp32
                                                                                     │
                                                          both ports = the SAME Laravel folder + the SAME MySQL
```

- Laravel is started **twice** on the mini PC: port **8000** for the touchscreen and the bridge
  (unchanged), port **8001** for the outside world. Both listen on `127.0.0.1` only — the tunnel is
  the only way in from the internet.
- The tunnel is **Tailscale Funnel**: free, gives you a fixed `https://<name>.<tailnet>.ts.net`
  address with a valid certificate, runs as a Windows service, needs no domain, no router
  port-forwarding and no public IP.
- On the public port the code refuses the kiosk: `terminal.html`, `assets/js/terminal.js`,
  `assets/css/terminal.css` and everything under `/api/esp32/` answer **404** there. This is
  enforced by `laravel/app/Http/Middleware/PublicListenerGuard.php` + `laravel/server.php` (already
  in the repo) as soon as `ADMIN_PUBLIC_PORT` is set in `.env` — it is not something you can forget
  to configure on the tunnel. *Why it matters:* the device key ships inside `terminal.js`, so
  without this anyone on the internet could queue a door-open.
- The public address works while the mini PC is on and has internet. When it is off, the address
  is simply unreachable (nothing to see anyway — the data lives on the mini PC).

What is public: the login page and every admin page, with the same `admin` login as on the mini
PC. What is not: the kiosk and the device API. Nothing else on the mini PC is exposed.

---

## 1. Get the code

```
cd C:\xampp\htdocs\reginsite
git pull
cd laravel
php artisan migrate
```

`migrate` adds the locker-alert columns from session 8 if the mini PC has not had them yet. It
keeps existing data.

---

## 2. Settings in `laravel\.env`

Open **`C:\xampp\htdocs\reginsite\laravel\.env`** and set / add these lines:

```
APP_ENV=production
APP_DEBUG=false

ADMIN_PUBLIC_PORT=8001
ADMIN_PASSWORD=<a real password for the admin login>
DEVICE_API_KEY=<a NEW random key, e.g. regin-Kq7v2mZp9-2026>
```

- `APP_DEBUG=false` — with it on, an error page prints file paths and settings to whoever is
  looking. Never leave it on for a public address.
- `ADMIN_PUBLIC_PORT=8001` — turns on the kiosk block for the second listener.
- `ADMIN_PASSWORD` — the seeder gives `admin` this password on every `migrate:fresh --seed`, and
  the command below applies it without reseeding. **Do not keep `admin` / `admin` on a public
  address.**
- `DEVICE_API_KEY` — the old value `regin-esp32-2026` is in the public GitHub repo. Pick a new
  one and put the **same** value in the other two places:
  - `firmware\bridge\config.ini` → `api_key = ...`
  - `laravel\public\assets\js\terminal.js` → `var API_KEY = "...";` (line ~20)

  Both files are tracked by git, so these two edits stay local to the mini PC. If a later
  `git pull` refuses because of them: `git stash`, `git pull`, `git stash pop`.

Then apply the admin password (keeps all data):

```
php artisan admin:password
```

It answers `Password for "admin" updated`. (It refuses an empty password or `admin`.)
If the mini PC is being set up fresh anyway, `php artisan migrate:fresh --seed --force` does the
same thing as part of seeding (`--force` because `APP_ENV=production` otherwise asks first —
and remember it wipes the loan history).

---

## 3. Start both listeners (and the bridge)

Double-click **`C:\xampp\htdocs\reginsite\start-station.bat`**. It opens three minimised windows:

| Window | What it runs |
|--------|--------------|
| `reginsite kiosk :8000` | `php artisan serve --host=127.0.0.1 --port=8000` — the touchscreen + bridge use this |
| `reginsite admin :8001` | `php artisan serve --host=127.0.0.1 --port=8001` — the tunnel points here |
| `reginsite bridge` | `python bridge.py` |

XAMPP MySQL must be running first (see §6 for making it a service). Closing a window stops that
part. If you prefer to start things by hand, the everyday list in `SETUP-FRESH-PC.md` §7 just gains
one more terminal: `php artisan serve --host=127.0.0.1 --port=8001` in the `laravel` folder.
**Always `--host=127.0.0.1`** (the default) — never `0.0.0.0`.

Check on the mini PC itself, in a browser:

| Address | Expect |
|---------|--------|
| `http://localhost:8001/` | the admin login page |
| `http://localhost:8001/terminal.html` | **Not found** |
| `http://localhost:8000/terminal.html` | the kiosk, as always |

If `localhost:8001/terminal.html` shows the kiosk, `ADMIN_PUBLIC_PORT` is missing from `.env`
(or was added while the window was already open — close and re-run `start-station.bat`).

---

## 4. Tailscale on the mini PC (one time)

1. Download the Windows installer from <https://tailscale.com/download/windows>, install it, and
   sign in from the tray icon. Any Google / Microsoft / GitHub account works; the free **Personal**
   plan is enough. This account owns the tailnet — use one the department keeps.
2. Admin console <https://login.tailscale.com/admin/machines> → the mini PC appears → **⋯ → Edit
   machine name** → `regin-admin`. This becomes the first part of the public address.
3. Admin console → **DNS** tab:
   - **MagicDNS** must be enabled (it is by default).
   - **HTTPS Certificates** → **Enable HTTPS**.
   - (Optional, once only) **Tailnet name → Rename** gives you a readable middle part such as
     `pango-lin.ts.net` instead of `tail1a2b3c.ts.net`.
4. Open a terminal on the mini PC and run:

   ```
   tailscale funnel --bg --https=443 http://127.0.0.1:8001
   ```

   The **first** time it stops with a message that Funnel is not enabled for your tailnet and
   prints a `https://login.tailscale.com/f/funnel?node=...` link. Open it, click **Enable**, and run the
   same command again. It then prints:

   ```
   Available on the internet:

   https://regin-admin.<your-tailnet>.ts.net/
   |-- / proxy http://127.0.0.1:8001

   Funnel started and running in the background.
   ```

   `--bg` makes it permanent: Tailscale is a Windows service, and the funnel comes back on its own
   after a reboot. `tailscale funnel status` shows it any time.

   If `tailscale` is "not recognized", use the full path
   `"C:\Program Files\Tailscale\tailscale.exe"`.
5. Write the address down. Optionally put it in `.env` as `APP_URL=https://regin-admin.<tailnet>.ts.net`.

---

## 5. Verify from outside

On a phone with **Wi-Fi off** (mobile data), or from home:

| Address | Expect |
|---------|--------|
| `https://regin-admin.<tailnet>.ts.net/` | login page → log in with `admin` + `ADMIN_PASSWORD` → dashboard with the live locker cards |
| `.../terminal.html` | **Not found** |
| `.../api/esp32/commands` | `{"ok":false,"error":"Not available on the public address","code":"public_only"}` |

Ten wrong passwords in a minute from one address → `Too Many Attempts.` for 60 s. A real
session lasts 2 hours of inactivity (`SESSION_LIFETIME`).

Meanwhile on the mini PC: scan a student at the kiosk, borrow a tool — nothing about the local
flow has changed, and the borrow is on the public dashboard after a page reload (the admin
pages do not auto-refresh).

---

## 6. Make it survive a reboot

1. **MySQL as a service**: XAMPP Control Panel → tick the **Service** checkbox left of MySQL
   (needs "Run as administrator" once). It now starts with Windows.
2. **The station**: press `Win+R`, type `shell:startup`, Enter → right-click → **New → Shortcut**
   → `C:\xampp\htdocs\reginsite\start-station.bat`. It runs at every login.
3. **Auto-login** for the Windows account the kiosk uses, so step 2 fires without a keyboard:
   `Win+R` → `netplwiz` → untick *"Users must enter a user name and password"* (or the Sign-in
   options page on newer builds).
4. **Tailscale** already runs as a service; the funnel was saved with `--bg`.

Reboot once and check §3's table and §5's first row again.

---

## 7. Turn it off / undo

- Stop publishing (address dead immediately, everything local untouched):
  `tailscale funnel reset`
- Publish again later: the §4 step-4 command.
- Remove the public listener entirely: delete the `reginsite admin :8001` line from
  `start-station.bat`. `ADMIN_PUBLIC_PORT` can stay in `.env` — with nothing listening on 8001 it
  does nothing.

---

## 8. Troubleshooting

| Problem | Fix |
|---------|-----|
| The ts.net address shows the **kiosk** | The funnel points at 8000, or `ADMIN_PUBLIC_PORT` is not in `.env`. `tailscale funnel status` must say `proxy http://127.0.0.1:8001`; then re-run `start-station.bat`. |
| Browser: **502 Bad Gateway** / "connection refused" at the ts.net address | The `reginsite admin :8001` window is not running (or MySQL is not). Run `start-station.bat`. |
| Browser: certificate warning | HTTPS Certificates not enabled in the Tailscale DNS tab, or enabled less than a minute ago. |
| `tailscale funnel` says **Funnel is not enabled** every time | Open the printed link and click Enable, or add to the tailnet policy file: `"nodeAttrs": [{"target": ["autogroup:member"], "attr": ["funnel"]}]` |
| `tailscale funnel` says the port must be 443/8443/10000 | You gave the public port; the command wants `--https=443` and the **local** target `http://127.0.0.1:8001`. |
| Login says **Too Many Attempts.** | 10 tries per minute per address. Wait 60 s. |
| Login says wrong password after a reseed | The seeder used `ADMIN_PASSWORD` from `.env` — log in with that, not `admin`. Empty `ADMIN_PASSWORD` = `admin`. |
| `migrate` / `migrate:fresh` asks *"Do you really wish to run this command?"* | Normal with `APP_ENV=production`. Answer yes, or add `--force`. |
| "Could not load data" on the public dashboard | MySQL stopped on the mini PC. |
| Bridge log: `401` / `bad_key` after the key change | `api_key` in `config.ini` ≠ `DEVICE_API_KEY` in `.env`. Kiosk shows a red link light → `API_KEY` in `terminal.js` is the old one. |
| `'m' is not recognized` when the `.bat` runs | The file lost its CRLF line endings (an editor saved it as LF). `git checkout start-station.bat` restores it; `.gitattributes` pins it. |

---

## Appendix — if Laravel ever moves from `artisan serve` to Apache

The port rule still applies: give the public site its own `<VirtualHost>` on its own port, add
`SetEnv ADMIN_PUBLIC_LISTENER 1` inside it (the guard honours that variable as well as the port),
and deny the three kiosk files there yourself — Apache hands out static files before Laravel sees
them:

```
<VirtualHost 127.0.0.1:8001>
    DocumentRoot "C:/xampp/htdocs/reginsite/laravel/public"
    SetEnv ADMIN_PUBLIC_LISTENER 1
    <FilesMatch "^(terminal\.html|terminal\.js|terminal\.css)$">
        Require all denied
    </FilesMatch>
</VirtualHost>
```

(Untested here — the dev machine's Apache is PHP 5.6. Check `localhost:8001/terminal.html` is
403/404 before pointing the funnel at it.)

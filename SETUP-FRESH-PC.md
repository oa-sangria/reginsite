# Reginsite — Fresh Windows PC Setup Guide

Everything you need to take a **brand-new Windows PC** (nothing installed) and get the whole
tool-locker system running: the website, the database, and the Arduino/RFID hardware saving
scans to it.

Follow the sections **in order**. Copy-paste the commands exactly. Total time: ~30–45 minutes,
most of it downloads.

---

## 0. What you're building

```
Arduino Mega ──USB cable──> THIS PC ───────────────────────────> browser
 (RFID + relays)        │                                        (dashboard)
                        ├─ Python "bridge"  ─┐
                        │                    ├─> Laravel web app ─> MySQL database
                        └─ (talks serial)  ──┘   (php artisan serve)   (stores everything)
```

You will install **6 things**:

| # | Software | Why you need it |
|---|----------|-----------------|
| 1 | **XAMPP** (PHP 8.2 + MariaDB) | Runs the website (PHP) **and** the database. One installer covers both. |
| 2 | **Composer** | Downloads the website's PHP libraries (Laravel). |
| 3 | **Git** | Downloads the project code from GitHub. |
| 4 | **Python 3** | Runs the "bridge" that sends RFID scans to the database. |
| 5 | **Arduino IDE** + **CH340 driver** | Uploads the firmware to the Arduino Mega and makes it show up as a COM port. |
| 6 | **VS Code** *(optional)* | A nice editor for viewing/changing files. |

> You do **not** need Node.js/npm — the website's front-end is plain files, nothing to compile.

---

## 1. Install the software

Install these one by one. Where it matters, the important checkbox is called out.

### 1.1 XAMPP (gives you PHP + MySQL/MariaDB)
- Download: <https://www.apachefriends.org/download.html> — pick the **PHP 8.2.x** version.
- Run the installer, keep the default install path **`C:\xampp`**.
- You can untick "Mercury" and "Tomcat" (not needed). Keep **Apache** and **MySQL**.
- After install, open **XAMPP Control Panel** and click **Start** next to **MySQL**.
  (You don't strictly need Apache — the website runs itself — but leave it available.)

### 1.2 Composer
- Download: <https://getcomposer.org/download/> → **Composer-Setup.exe**.
- During setup it asks for the PHP to use — point it at **`C:\xampp\php\php.exe`**.
- Tick "Add to PATH" if asked.

### 1.3 Git
- Download: <https://git-scm.com/download/win> → run the installer.
- All defaults are fine. (This also gives you "Git Bash" if you like.)

### 1.4 Python 3
- Download: <https://www.python.org/downloads/> → latest **Python 3.x**.
- **IMPORTANT:** on the first installer screen, tick **“Add python.exe to PATH”** before clicking
  Install. If you forget this, `python` won't work in the terminal.

### 1.5 Arduino IDE + CH340 driver
- Arduino IDE: <https://www.arduino.cc/en/software> → install.
- **CH340 USB driver** (most Arduino Mega clones need this, or Windows won't show a COM port):
  search "CH340 driver Windows", install it, then reboot. (Genuine Arduinos may not need it —
  but installing it does no harm.)

### 1.6 VS Code *(optional)*
- <https://code.visualstudio.com/> — handy for editing `.env` and config files.

---

## 2. Check your PATH (do this once)

The website uses PHP from XAMPP. Make sure Windows can find it.

1. Press **Start**, type **"environment variables"**, open **"Edit the system environment
   variables"** → **Environment Variables**.
2. Under **System variables**, select **Path** → **Edit** → **New** → add:
   ```
   C:\xampp\php
   ```
3. OK out of all windows. **Close and reopen** any terminal.

Now verify everything is installed (open a **new** PowerShell or Command Prompt):

```
php -v          (should say PHP 8.2.x)
composer -V     (should say Composer version 2.x)
git --version
python --version
```

If any command is "not recognized", that tool isn't on PATH — re-check its install step.

---

## 3. Get the project code

Open a terminal and run:

```
cd C:\xampp\htdocs
git clone https://github.com/oa-sangria/reginsite.git
```

You now have the project at **`C:\xampp\htdocs\reginsite`**.

> **No GitHub / offline?** Instead of cloning, just copy the whole `reginsite` folder onto the
> PC (e.g. via USB) into `C:\xampp\htdocs\reginsite`. Everything below is the same.

---

## 4. Set up the website (Laravel + database)

Do these from the `laravel` folder:

```
cd C:\xampp\htdocs\reginsite\laravel
composer install
```

`composer install` downloads the PHP libraries into a `vendor` folder (they aren't in the
download). This takes a minute or two the first time.

### 4.1 Create the settings file (`.env`)

```
copy .env.example .env
php artisan key:generate
```

Now open **`C:\xampp\htdocs\reginsite\laravel\.env`** in a text editor and set these lines:

```
APP_NAME=Reginsite
APP_URL=http://localhost:8000

DB_CONNECTION=mysql
DB_HOST=127.0.0.1
DB_PORT=3306
DB_DATABASE=reginsite_laravel
DB_USERNAME=root
DB_PASSWORD=

DEVICE_API_KEY=change-this-to-your-own-key
```

Notes:
- `DB_PASSWORD` is **blank** — XAMPP's MySQL has no password by default.
- `DEVICE_API_KEY` is a password **you invent** for the Arduino side. Pick your own random text
  (e.g. `reginsite-locker-7fK2p`). **Write it down** — you'll enter the same value in the bridge
  config later. (Don't reuse the old public one from GitHub.)

### 4.2 Create the database

Easiest way — phpMyAdmin:
1. Make sure **MySQL is Started** in the XAMPP Control Panel.
2. Go to <http://localhost/phpmyadmin> in your browser.
3. Left side → **New** → database name **`reginsite_laravel`** → **Create**.

*(Command-line alternative: `C:\xampp\mysql\bin\mysql.exe -u root -e "CREATE DATABASE reginsite_laravel"`)*

### 4.3 Build the tables + demo data, then run it

```
php artisan migrate:fresh --seed
php artisan serve
```

- `migrate:fresh --seed` creates all tables and loads the demo students/tools/lockers.
- `php artisan serve` starts the website. **Leave this terminal open** (Ctrl+C stops it).

Open **<http://localhost:8000>** in your browser → log in with **`admin`** / **`admin`**. 🎉

---

## 5. Set up the bridge (sends RFID scans to the database)

Open a **second** terminal (leave the website running in the first).

```
pip install pyserial
```

Then edit **`C:\xampp\htdocs\reginsite\firmware\bridge\config.ini`**:
- `serial_port` → the COM port your Arduino uses (you'll confirm this in Step 6; e.g. `COM3`).
- `api_key` → **must match** the `DEVICE_API_KEY` you set in `.env`.
- Leave `base_url = http://localhost:8000` (the website is on this same PC).

Start the bridge:

```
cd C:\xampp\htdocs\reginsite\firmware\bridge
python bridge.py
```

Leave this terminal open too. It waits for scans and saves them.

---

## 6. Set up the Arduino (RFID + lockers)

### 6.1 Add the RFID library
- Arduino IDE → **Tools → Manage Libraries** → search **“MFRC522”** → install the one by
  *GithubCommunity*.

### 6.2 Find your COM port
- Plug the Arduino Mega into USB.
- Arduino IDE → **Tools → Port** → note which **COMx** appears (e.g. COM3).
  - **No port shows up?** Install/reinstall the **CH340 driver** (Step 1.5) and reboot.
- Put that COM number into `firmware/bridge/config.ini` → `serial_port` (Step 5).

### 6.3 Read your 4 tag UIDs
- Open **`firmware/rfid_read_test/rfid_read_test.ino`** → **Tools → Board: Arduino Mega 2560** →
  select the Port → **Upload**.
- Open **Tools → Serial Monitor**, set baud to **115200**. Tap each of your 4 tags — each prints
  a line like `UID: DE AD BE EF`. Write down which tag is for which locker.

### 6.4 Load the real firmware
- Open **`firmware/locker_controller/locker_controller.ino`**.
- At the top, fill in the `tagUID[]` table with the 4 UIDs (slot 0 = Locker 1, etc.).
- **Upload**. **Close the Serial Monitor** (the bridge needs the COM port).

### 6.5 Test end-to-end
- Make sure all three are running: **MySQL** (XAMPP) → **website** (`php artisan serve`) →
  **bridge** (`python bridge.py`).
- Tap a tag. The bridge window prints something like `Locker 1: BORROW saved. tx #12`.
- Refresh the dashboard at <http://localhost:8000> → that locker turns red. Tap again → it
  returns and turns green. **The scan is now in the database.** ✅

---

## 7. Everyday startup order (after the first-time setup)

Each time you turn the PC on and want to use the system:

1. **XAMPP Control Panel** → Start **MySQL**.
2. Terminal 1: `cd C:\xampp\htdocs\reginsite\laravel` → `php artisan serve`
3. Terminal 2: `cd C:\xampp\htdocs\reginsite\firmware\bridge` → `python bridge.py`
4. Plug in the Arduino → start tapping tags.

To reset the demo data at any time: in the `laravel` folder run `php artisan migrate:fresh --seed`.

---

## 8. Troubleshooting

| Problem | Fix |
|---------|-----|
| `php` / `composer` / `python` "not recognized" | The tool isn't on PATH. Re-check Step 2 and reopen the terminal. |
| `composer install` fails on PHP version | You installed an older XAMPP. Use the **PHP 8.2** XAMPP. |
| Website error "could not connect to database" | MySQL isn't started in XAMPP, or `DB_DATABASE`/password in `.env` is wrong. |
| Login says wrong password / no data | You skipped `php artisan migrate:fresh --seed`, or MySQL was off when you ran it. |
| "must return first" / "not available" on a scan | Demo data went stale. Run `php artisan migrate:fresh --seed` again. Test lockers 1–4 start free. |
| Arduino has **no COM port** | Install the **CH340 driver** and reboot. Try a different USB cable/port (some cables are charge-only). |
| Bridge says `cannot open COMx` | The Arduino Serial Monitor is still open — close it. Or wrong COM in `config.ini`. |
| Bridge says `NAK,offline` | The website (`php artisan serve`) isn't running, or `base_url` is wrong. |
| Bridge saves nothing / 401 | `api_key` in `config.ini` doesn't match `DEVICE_API_KEY` in `.env`. Make them identical. |
| `RC522 Version: 0x00` or `0xFF` | RFID wiring problem. VCC must be **3.3V** (not 5V); check SS=53, RST=9. |
| "Address already in use" on `artisan serve` | Port 8000 is busy. Run `php artisan serve --port=8001` and use that URL. |

---

## Quick reference

- **Website:** <http://localhost:8000> — login `admin` / `admin`
- **Database name:** `reginsite_laravel` (user `root`, no password)
- **Project folder:** `C:\xampp\htdocs\reginsite`
- **Reset demo data:** `php artisan migrate:fresh --seed` (run in `laravel/`)
- **Device key:** the same value in `laravel/.env` (`DEVICE_API_KEY`) **and**
  `firmware/bridge/config.ini` (`api_key`)

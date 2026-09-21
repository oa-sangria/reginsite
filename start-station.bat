@echo off
rem ============================================================================
rem  reginsite - start the whole station on the mini PC (see GO-LIVE-ADMIN.md)
rem
rem    window "kiosk :8000"   Laravel for the touchscreen + bridge  http://localhost:8000
rem    window "admin :8001"   Laravel for the PUBLIC admin          http://localhost:8001
rem                           (Tailscale Funnel points here; kiosk + /api/esp32 answer 404)
rem    window "bridge"        firmware\bridge\bridge.py  (both Megas)
rem
rem  XAMPP MySQL must already be running - tick "Service" next to MySQL in the XAMPP
rem  control panel once and it starts with Windows. Put a shortcut to this file in
rem  shell:startup so everything comes back after a reboot.
rem ============================================================================
set "ROOT=%~dp0"

start "reginsite kiosk :8000" /min cmd /k "cd /d "%ROOT%laravel" && php artisan serve --host=127.0.0.1 --port=8000"
start "reginsite admin :8001" /min cmd /k "cd /d "%ROOT%laravel" && php artisan serve --host=127.0.0.1 --port=8001"

rem give Laravel ~3 s to come up before the bridge starts polling it
ping -n 4 127.0.0.1 >nul

start "reginsite bridge" /min cmd /k "cd /d "%ROOT%firmware\bridge" && python bridge.py"

rem Kiosk browser, fullscreen on the 1024x600 panel. Remove "rem " once the panel is wired:
rem start "" "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --kiosk "http://localhost:8000/terminal.html?kiosk=1" --edge-kiosk-type=fullscreen --no-first-run

echo Started: kiosk :8000, admin :8001, bridge. Close their windows to stop them.

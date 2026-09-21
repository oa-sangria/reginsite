<?php

/*
 * Router for `php artisan serve` — Laravel uses this file instead of the framework's copy
 * when it exists at the project root. It is the framework's router plus ONE rule: on the
 * public admin listener (ADMIN_PUBLIC_PORT in .env — see GO-LIVE-ADMIN.md) the kiosk's
 * static files are not handed out by the built-in server; they fall through to Laravel,
 * where App\Http\Middleware\PublicListenerGuard answers 404. Keep the list below in sync
 * with PublicListenerGuard::KIOSK_PATHS.
 */

$publicPath = getcwd();

$uri = urldecode(
    parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH) ?? ''
);

$kioskFiles = ['terminal.html', 'assets/js/terminal.js', 'assets/css/terminal.css'];

/*
 * True when the request would hand out one of the kiosk files on the public listener.
 * Compares the RESOLVED file (realpath), because the built-in server serves whatever the OS
 * resolves: on Windows "/Terminal.html", "/./terminal.html", "/assets/../terminal.html",
 * "/terminal.html." and "/assets\js\terminal.js" are all the same file.
 */
$hidesKioskFile = static function (string $file) use ($publicPath, $kioskFiles): bool {
    // .env is not loaded yet at this point, so read the one value we need directly.
    $env = @file_get_contents(__DIR__.'/.env');
    if ($env === false || !preg_match('/^ADMIN_PUBLIC_PORT\s*=\s*"?(\d+)/m', $env, $m)) {
        return false;
    }
    if ((int) $m[1] <= 0 || (int) ($_SERVER['SERVER_PORT'] ?? 0) !== (int) $m[1]) {
        return false;
    }
    $real = realpath($file);
    if ($real === false) {
        return false;
    }
    foreach ($kioskFiles as $kiosk) {
        $target = realpath($publicPath.'/'.$kiosk);
        if ($target !== false && strcasecmp($real, $target) === 0) {
            return true;
        }
    }

    return false;
};

// This file allows us to emulate Apache's "mod_rewrite" functionality from the
// built-in PHP web server. This provides a convenient way to test a Laravel
// application without having installed a "real" web server software here.
if ($uri !== '/' && file_exists($publicPath.$uri) && !$hidesKioskFile($publicPath.$uri)) {
    return false;
}

// The built-in server points SCRIPT_* at the request path (or at the existing file), which
// can make Laravel read "/terminal.html/" or a hidden kiosk file as "/". Present every
// request that reaches Laravel the way Apache would.
$_SERVER['SCRIPT_FILENAME'] = $publicPath.'/index.php';
$_SERVER['SCRIPT_NAME'] = '/index.php';
$_SERVER['PHP_SELF'] = '/index.php';

require_once $publicPath.'/index.php';

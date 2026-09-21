<?php

namespace App\Http\Middleware;

use Closure;
use Illuminate\Http\Request;

/*
 * The admin site can be published (GO-LIVE-ADMIN.md) by pointing a tunnel at a SECOND
 * listener — `php artisan serve --port=<ADMIN_PUBLIC_PORT>` — while the kiosk and the
 * bridge keep using the local one. Every request that arrives on the public listener is
 * refused if it is a kiosk/device path: the device key ships inside terminal.js, so
 * without this anyone on the internet could queue a door-open.
 *
 * "Public listener" = SERVER_PORT equals ADMIN_PUBLIC_PORT (the bound port of the PHP
 * built-in server; a Host header cannot change it) OR the web server set the
 * ADMIN_PUBLIC_LISTENER variable (Apache vhost: `SetEnv ADMIN_PUBLIC_LISTENER 1`).
 * With ADMIN_PUBLIC_PORT unset this middleware does nothing.
 */
class PublicListenerGuard
{
    /* Paths that must never be reachable from the public address. Prefix match. */
    public const KIOSK_PATHS = [
        'api/esp32',
        'terminal.html',
        'assets/js/terminal.js',
        'assets/css/terminal.css',
    ];

    public function handle(Request $request, Closure $next)
    {
        if (self::isPublicListener($request) && self::isKioskPath($request->path())) {
            if ($request->is('api/*')) {
                return response()->json([
                    'ok' => false, 'error' => 'Not available on the public address', 'code' => 'public_only',
                ], 404);
            }

            return response('Not found', 404)->header('Content-Type', 'text/plain');
        }

        return $next($request);
    }

    public static function isPublicListener(Request $request): bool
    {
        if ($request->server('ADMIN_PUBLIC_LISTENER')) {
            return true;
        }
        $port = (int) config('services.admin_public_port');

        return $port > 0 && (int) $request->server('SERVER_PORT') === $port;
    }

    /* Prefix match on a normalised path: lower-case, "\" as "/", "." and ".." segments
       folded, trailing "/", "." and spaces dropped (Windows resolves those to the same file). */
    public static function isKioskPath(string $path): bool
    {
        $out = [];
        foreach (explode('/', strtolower(str_replace('\\', '/', $path))) as $seg) {
            $seg = rtrim($seg, '. ');
            if ($seg === '' || $seg === '.') {
                continue;
            }
            if ($seg === '..') {
                array_pop($out);
                continue;
            }
            $out[] = $seg;
        }
        $path = implode('/', $out);

        foreach (self::KIOSK_PATHS as $prefix) {
            if ($path === $prefix || str_starts_with($path, $prefix.'/')) {
                return true;
            }
        }

        return false;
    }
}

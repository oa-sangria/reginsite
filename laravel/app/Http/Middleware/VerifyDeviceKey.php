<?php

namespace App\Http\Middleware;

use Closure;
use Illuminate\Http\Request;

/* ESP32 device auth: X-API-Key header must match DEVICE_API_KEY in .env. */
class VerifyDeviceKey
{
    public function handle(Request $request, Closure $next)
    {
        $expected = (string) config('services.device_key');
        $given = (string) $request->header('X-API-Key', '');

        if ($expected === '' || !hash_equals($expected, $given)) {
            return response()->json([
                'ok' => false, 'error' => 'Invalid or missing X-API-Key', 'code' => 'bad_key',
            ], 401);
        }

        return $next($request);
    }
}

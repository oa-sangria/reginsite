<?php

namespace App\Http\Middleware;

use Illuminate\Http\Middleware\TrustProxies as Middleware;
use Illuminate\Http\Request;

class TrustProxies extends Middleware
{
    /**
     * The trusted proxies for this application.
     *
     * The public tunnel agent (GO-LIVE-ADMIN.md) runs on this same machine and forwards
     * to a local listener, so its X-Forwarded-* headers are honoured: the visitor's real
     * IP reaches the login throttle and the log instead of 127.0.0.1.
     *
     * @var array<int, string>|string|null
     */
    protected $proxies = '127.0.0.1';

    /**
     * The headers that should be used to detect proxies.
     *
     * @var int
     */
    protected $headers =
        Request::HEADER_X_FORWARDED_FOR |
        Request::HEADER_X_FORWARDED_HOST |
        Request::HEADER_X_FORWARDED_PORT |
        Request::HEADER_X_FORWARDED_PROTO |
        Request::HEADER_X_FORWARDED_AWS_ELB;
}

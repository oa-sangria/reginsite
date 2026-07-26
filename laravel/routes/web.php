<?php

use App\Http\Controllers\AuthController;
use App\Http\Controllers\BootstrapController;
use App\Http\Controllers\Esp32Controller;
use App\Http\Controllers\InventoryController;
use Illuminate\Support\Facades\Route;

/*
|--------------------------------------------------------------------------
| reginsite routes
|--------------------------------------------------------------------------
| The admin front-end is static HTML/JS served straight from public/
| (index.html, dashboard.html, …) — no Blade views. These routes are the
| JSON API it talks to, in the web group so PHP sessions work.
| CSRF is exempted for api/* (see VerifyCsrfToken).
*/

Route::prefix('api')->group(function () {
    // Admin auth
    Route::post('auth/login', [AuthController::class, 'login']);
    Route::post('auth/logout', [AuthController::class, 'logout']);
    Route::get('auth/me', [AuthController::class, 'me']);

    // Admin data + saves (session required)
    Route::middleware('auth')->group(function () {
        Route::get('bootstrap', [BootstrapController::class, 'index']);
        Route::post('tools', [InventoryController::class, 'saveTool']);
        Route::post('lockers', [InventoryController::class, 'saveLocker']);
        Route::post('students', [InventoryController::class, 'saveStudent']);
        Route::post('terms', [InventoryController::class, 'saveTerms']);
    });

    // Device API for the ESP32 terminal (X-API-Key)
    Route::middleware('device.key')->prefix('esp32')->group(function () {
        Route::post('verify-student', [Esp32Controller::class, 'verifyStudent']);
        Route::post('state', [Esp32Controller::class, 'state']);
        Route::post('borrow', [Esp32Controller::class, 'borrow']);
        Route::post('return', [Esp32Controller::class, 'returnTool']);
        Route::post('locker-status', [Esp32Controller::class, 'lockerStatus']);
        Route::post('sync', [Esp32Controller::class, 'sync']);
    });
});

// Root -> login page of the static front-end
Route::get('/', fn () => redirect('/index.html'));

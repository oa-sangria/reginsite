<?php

namespace App\Exceptions;

use Illuminate\Auth\AuthenticationException;
use Illuminate\Foundation\Exceptions\Handler as ExceptionHandler;
use Illuminate\Validation\ValidationException;
use Symfony\Component\HttpKernel\Exception\HttpExceptionInterface;
use Throwable;

class Handler extends ExceptionHandler
{
    /**
     * A list of exception types with their corresponding custom log levels.
     *
     * @var array<class-string<\Throwable>, \Psr\Log\LogLevel::*>
     */
    protected $levels = [
        //
    ];

    /**
     * A list of the exception types that are not reported.
     *
     * @var array<int, class-string<\Throwable>>
     */
    protected $dontReport = [
        //
    ];

    /**
     * A list of the inputs that are never flashed to the session on validation exceptions.
     *
     * @var array<int, string>
     */
    protected $dontFlash = [
        'current_password',
        'password',
        'password_confirmation',
    ];

    /**
     * Register the exception handling callbacks for the application.
     *
     * @return void
     */
    public function register()
    {
        $this->reportable(function (Throwable $e) {
            //
        });
    }

    /**
     * API routes always answer JSON in the {ok:false, error, code} shape the
     * front-end and ESP32 firmware expect — never an HTML error page.
     */
    public function render($request, Throwable $e)
    {
        if ($request->is('api/*')) {
            if ($e instanceof AuthenticationException) {
                return response()->json([
                    'ok' => false, 'error' => 'Not authenticated', 'code' => 'unauthorized',
                ], 401);
            }
            if ($e instanceof ValidationException) {
                return response()->json([
                    'ok' => false,
                    'error' => collect($e->errors())->flatten()->first() ?? 'Invalid input',
                    'code' => 'validation',
                ], 422);
            }
            if ($e instanceof HttpExceptionInterface) {
                return response()->json([
                    'ok' => false,
                    'error' => $e->getMessage() !== '' ? $e->getMessage() : 'Request failed',
                    'code' => (string) $e->getStatusCode(),
                ], $e->getStatusCode());
            }

            return response()->json([
                'ok' => false,
                'error' => config('app.debug') ? $e->getMessage() : 'Server error',
                'code' => 'server_error',
            ], 500);
        }

        return parent::render($request, $e);
    }
}

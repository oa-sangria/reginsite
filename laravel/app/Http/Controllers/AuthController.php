<?php

namespace App\Http\Controllers;

use Illuminate\Http\Request;
use Illuminate\Support\Facades\Auth;

class AuthController extends Controller
{
    public function login(Request $request)
    {
        $data = $request->validate([
            'username' => 'required|string',
            'password' => 'required|string',
        ]);

        if (!Auth::attempt($data)) {
            return response()->json([
                'ok' => false, 'error' => 'Invalid username or password', 'code' => 'bad_login',
            ], 401);
        }
        $request->session()->regenerate();
        $u = Auth::user();

        return response()->json(['ok' => true, 'user' => [
            'id' => $u->id, 'username' => $u->username, 'name' => $u->name,
        ]]);
    }

    public function logout(Request $request)
    {
        Auth::logout();
        $request->session()->invalidate();
        $request->session()->regenerateToken();

        return response()->json(['ok' => true]);
    }

    public function me()
    {
        $u = Auth::user();

        return response()->json(['ok' => true, 'user' => $u ? [
            'id' => $u->id, 'username' => $u->username, 'name' => $u->name,
        ] : null]);
    }
}

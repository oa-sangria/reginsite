<?php

namespace App\Console\Commands;

use App\Models\User;
use Illuminate\Console\Command;
use Illuminate\Support\Facades\Hash;

/*
 * `php artisan admin:password` — set the `admin` login's password WITHOUT reseeding
 * (migrate:fresh --seed wipes the loan history). Uses ADMIN_PASSWORD from .env, or
 * the value given on the command line. GO-LIVE-ADMIN.md step 2.
 */
class AdminPassword extends Command
{
    protected $signature = 'admin:password {password? : New password (default: ADMIN_PASSWORD in .env)}';

    protected $description = 'Set the admin user password (from ADMIN_PASSWORD in .env unless given)';

    public function handle(): int
    {
        $password = (string) ($this->argument('password') ?? config('services.admin_password'));

        if ($password === '' || $password === 'admin') {
            $this->error('Refusing to set an empty password or the default "admin". Put a real one in ADMIN_PASSWORD in .env, or pass it as an argument.');

            return self::FAILURE;
        }

        $user = User::where('username', 'admin')->first();
        if (!$user) {
            $this->error('No user "admin" in the database — run php artisan migrate:fresh --seed first.');

            return self::FAILURE;
        }

        $user->password = Hash::make($password);
        $user->save();
        $this->info('Password for "admin" updated ('.strlen($password).' characters).');

        return self::SUCCESS;
    }
}

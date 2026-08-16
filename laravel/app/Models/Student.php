<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Student extends Model
{
    protected $fillable = ['student_no', 'name', 'strand', 'program', 'major', 'qr_code', 'status', 'banned_until'];
    protected $casts = ['banned_until' => 'datetime'];

    public function transactions()
    {
        return $this->hasMany(Transaction::class);
    }

    public function bans()
    {
        return $this->hasMany(Ban::class);
    }

    public function openBorrows()
    {
        return $this->transactions()->whereNull('return_time');
    }

    /* Eligibility: allowed program/major, not banned, no overdue item. */
    public function eligibility(): array
    {
        // Program gate only applies once a program is on file (scanned IDs);
        // legacy/demo students with no program set are treated as allowed.
        if ($this->program && !\App\Services\QrStudent::programAllowed($this->program, $this->major)) {
            return ['can_borrow' => false,
                'reason' => 'Program not eligible — Industrial Technology (Electrical / Mechatronics / HVAC&R) only'];
        }
        if ($this->status === 'banned') {
            $until = $this->banned_until ? $this->banned_until->format('Y-m-d H:i:s') : '—';
            return ['can_borrow' => false, 'reason' => 'Student is banned until ' . $until];
        }
        $overdue = $this->openBorrows()->where('status', 'overdue')->count();
        if ($overdue > 0) {
            return ['can_borrow' => false, 'reason' => "Has {$overdue} overdue item(s) — must return first"];
        }
        return ['can_borrow' => true, 'reason' => 'Clear'];
    }
}

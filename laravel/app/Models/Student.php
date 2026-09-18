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

    /* Eligibility: allowed program/major, not banned, no OVERDUE item, and fewer
       than max_open_borrows (default 3) tools out. Holding a tool that is still
       within its 8-hour window does NOT block — only an overdue one does.
       (Aug 22 briefly enforced "one tool at a time"; that was the wrong reading
       of the rule and was reverted Sep 18.) */
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

        // One query for both checks; overdue is reported first because it is the
        // more serious state and it is what drives the ban.
        $open = $this->openBorrows()->with('tool')->get();
        $overdue = $open->where('status', 'overdue');
        if ($overdue->count() > 0) {
            $names = $overdue->map(fn ($t) => optional($t->tool)->name)->filter()->implode(', ');
            return ['can_borrow' => false,
                'reason' => 'Overdue: ' . ($names !== '' ? $names : $overdue->count() . ' item(s)')
                    . ' — return it before borrowing again'];
        }
        $max = max(1, (int) Setting::get('max_open_borrows', '3'));
        if ($open->count() >= $max) {
            return ['can_borrow' => false,
                'reason' => 'Already holding ' . $open->count() . ' tools (the maximum is ' . $max
                    . ') — return one before borrowing again'];
        }
        return ['can_borrow' => true, 'reason' => 'Clear'];
    }
}

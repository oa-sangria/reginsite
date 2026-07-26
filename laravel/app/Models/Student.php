<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Student extends Model
{
    protected $fillable = ['student_no', 'name', 'strand', 'qr_code', 'status', 'banned_until'];
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

    /* Eligibility per the workflow: banned OR has an overdue item -> cannot borrow. */
    public function eligibility(): array
    {
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

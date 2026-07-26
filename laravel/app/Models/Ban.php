<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Ban extends Model
{
    protected $fillable = ['student_id', 'transaction_id', 'reason', 'ban_from', 'ban_until'];
    protected $casts = ['ban_from' => 'datetime', 'ban_until' => 'datetime'];

    public function student()
    {
        return $this->belongsTo(Student::class);
    }

    public function transaction()
    {
        return $this->belongsTo(Transaction::class);
    }
}

<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Transaction extends Model
{
    protected $fillable = [
        'student_id', 'tool_id', 'locker_id', 'qty',
        'borrow_time', 'expected_return', 'return_time', 'status',
    ];
    protected $casts = [
        'borrow_time' => 'datetime',
        'expected_return' => 'datetime',
        'return_time' => 'datetime',
    ];

    public function student()
    {
        return $this->belongsTo(Student::class);
    }

    public function tool()
    {
        return $this->belongsTo(Tool::class);
    }

    public function locker()
    {
        return $this->belongsTo(Locker::class);
    }
}

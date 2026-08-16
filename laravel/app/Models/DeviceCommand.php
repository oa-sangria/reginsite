<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

/* A queued instruction for the Arduino (via the mini-PC bridge). */
class DeviceCommand extends Model
{
    protected $fillable = [
        'type', 'locker_id', 'mode', 'tool_id', 'student_id', 'transaction_id', 'status',
    ];
}

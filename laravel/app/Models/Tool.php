<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Tool extends Model
{
    protected $fillable = ['name', 'rfid_tag', 'locker_id', 'status'];

    public function locker()
    {
        return $this->belongsTo(Locker::class);
    }

    public function transactions()
    {
        return $this->hasMany(Transaction::class);
    }
}

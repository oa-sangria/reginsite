<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Tool extends Model
{
    protected $fillable = ['name', 'rfid_tag', 'locker_id', 'status'];

    /** Normalize an RFID UID: strip separators, uppercase ("A7:45" -> "A745"). */
    public static function normTag($uid): string
    {
        return strtoupper(preg_replace('/[^0-9A-Fa-f]/', '', (string) $uid));
    }

    /** Find a tool by any UID formatting. */
    public static function findByTag($uid): ?self
    {
        $n = self::normTag($uid);
        return $n === '' ? null : self::where('rfid_tag', $n)->first();
    }

    public function locker()
    {
        return $this->belongsTo(Locker::class);
    }

    public function transactions()
    {
        return $this->hasMany(Transaction::class);
    }
}

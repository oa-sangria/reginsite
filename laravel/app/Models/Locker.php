<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Locker extends Model
{
    protected $fillable = ['name', 'tool_id', 'sensor', 'occupancy', 'led', 'last_seen'];
    protected $casts = ['last_seen' => 'datetime'];

    public function tool()
    {
        return $this->belongsTo(Tool::class);
    }
}

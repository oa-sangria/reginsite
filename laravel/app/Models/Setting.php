<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Setting extends Model
{
    protected $primaryKey = 'name';
    public $incrementing = false;
    protected $keyType = 'string';
    public $timestamps = false;
    protected $fillable = ['name', 'value'];

    public static function get(string $name, $default = null)
    {
        $row = static::find($name);
        return $row ? $row->value : $default;
    }

    public static function put(string $name, string $value): void
    {
        static::updateOrCreate(['name' => $name], ['value' => $value]);
    }
}

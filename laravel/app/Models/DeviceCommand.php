<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

/* A queued instruction for the Arduino (via the mini-PC bridge).
 *
 * status lifecycle:
 *   pending -> sent -> done      the physical step completed and was recorded
 *                   -> timeout   door re-locked without a usable scan
 *                   -> failed    a tag was scanned but the server rejected it
 * done/timeout/failed are terminal; a later confirm must not overwrite them.
 */
class DeviceCommand extends Model
{
    protected $fillable = [
        'type', 'locker_id', 'mode', 'tool_id', 'student_id', 'transaction_id', 'status', 'note',
    ];

    public const TERMINAL = ['done', 'timeout', 'failed'];

    public function isTerminal(): bool
    {
        return in_array($this->status, self::TERMINAL, true);
    }
}

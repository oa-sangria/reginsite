<?php

namespace App\Http\Controllers;

use App\Models\Ban;
use App\Models\Locker;
use App\Models\Setting;
use App\Models\Student;
use App\Models\Tool;
use App\Models\Transaction;
use App\Services\LockerSystem;

/**
 * One-call payload for the admin front-end. Shape matches the original
 * static data.js `window.DB` contract: string ids, ISO date strings.
 */
class BootstrapController extends Controller
{
    public function index(LockerSystem $system)
    {
        $system->runMaintenance();

        $iso = fn ($dt) => $dt ? $dt->toIso8601String() : null;

        return response()->json(['ok' => true, 'data' => [
            'config' => [
                'borrowLimitHours' => (int) Setting::get('borrow_limit_hours', '8'),
                'banTriggerDays' => (int) Setting::get('ban_trigger_days', '2'),
                'banLengthDays' => (int) Setting::get('ban_length_days', '2'),
            ],
            'students' => Student::orderBy('name')->get()->map(fn ($s) => [
                'id' => (string) $s->id,
                'studentId' => $s->student_no,
                'name' => $s->name,
                // Admin UI shows this column as "strand" — surface the major there.
                'strand' => $s->major ?: $s->strand,
                'program' => $s->program,
                'major' => $s->major,
                'qr' => $s->qr_code,
                'status' => $s->status,
                'bannedUntil' => $iso($s->banned_until),
            ]),
            'tools' => Tool::orderBy('id')->get()->map(fn ($t) => [
                'id' => (string) $t->id,
                'name' => $t->name,
                'rfidTag' => $t->rfid_tag,
                'lockerId' => $t->locker_id ? (string) $t->locker_id : '',
                'status' => $t->status,
            ]),
            'lockers' => Locker::orderBy('id')->get()->map(fn ($l) => [
                'id' => (string) $l->id,
                'number' => $l->name,
                'toolId' => $l->tool_id ? (string) $l->tool_id : '',
                'sensor' => $l->sensor,
                'occupancy' => $l->occupancy,
                'led' => $l->led,
                'lastSeen' => $iso($l->last_seen),
            ]),
            'transactions' => Transaction::orderByDesc('borrow_time')->get()->map(fn ($t) => [
                'id' => 'TXN-' . $t->id,
                'txId' => (string) $t->id,
                'studentId' => (string) $t->student_id,
                'toolId' => (string) $t->tool_id,
                'lockerId' => $t->locker_id ? (string) $t->locker_id : '',
                'qty' => $t->qty,
                'borrowTime' => $iso($t->borrow_time),
                'expectedReturn' => $iso($t->expected_return),
                'returnTime' => $iso($t->return_time),
                'status' => $t->status,
            ]),
            'bans' => Ban::orderByDesc('ban_from')->get()->map(fn ($b) => [
                'id' => (string) $b->id,
                'studentId' => (string) $b->student_id,
                'transactionId' => $b->transaction_id ? 'TXN-' . $b->transaction_id : '',
                'reason' => $b->reason,
                'from' => $iso($b->ban_from),
                'until' => $iso($b->ban_until),
            ]),
            'terms' => (string) Setting::get('terms', ''),
        ]]);
    }
}

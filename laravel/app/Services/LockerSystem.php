<?php

namespace App\Services;

use App\Models\Ban;
use App\Models\Locker;
use App\Models\Setting;
use App\Models\Student;
use App\Models\Tool;
use App\Models\Transaction;
use Carbon\Carbon;
use Symfony\Component\HttpKernel\Exception\HttpException;

/**
 * Business rules for the tool-locker workflow. Mirrors the plain-PHP
 * prototype's run_maintenance()/do_borrow()/do_return().
 */
class LockerSystem
{
    /**
     * Bring state current. Called before reads and device actions:
     *  1. Active borrows past expected_return -> 'overdue'
     *  2. Overdue >= ban_trigger_days -> create ban + mark student banned
     *  3. Bans past ban_until -> lift
     */
    public function runMaintenance(): void
    {
        $trigger = (int) Setting::get('ban_trigger_days', '2');
        $length  = (int) Setting::get('ban_length_days', '2');
        $now = Carbon::now();

        Transaction::whereNull('return_time')
            ->where('expected_return', '<', $now)
            ->where('status', '!=', 'overdue')
            ->update(['status' => 'overdue']);

        $toBan = Transaction::whereNull('return_time')
            ->where('expected_return', '<', $now->copy()->subDays($trigger))
            ->whereDoesntHave('student.bans', function ($q) {
                $q->whereColumn('bans.transaction_id', 'transactions.id');
            })
            ->get();
        foreach ($toBan as $tx) {
            Ban::create([
                'student_id' => $tx->student_id,
                'transaction_id' => $tx->id,
                'reason' => "Overdue tool exceeded {$trigger} days",
                'ban_from' => $now,
                'ban_until' => $now->copy()->addDays($length),
            ]);
            Student::where('id', $tx->student_id)->update([
                'status' => 'banned',
                'banned_until' => $now->copy()->addDays($length),
            ]);
        }

        Student::where('status', 'banned')
            ->whereNotNull('banned_until')
            ->where('banned_until', '<', $now)
            ->update(['status' => 'active', 'banned_until' => null]);
    }

    /** Create a borrow transaction and flip tool/locker state. */
    public function borrow(Student $student, int $toolId, int $qty = 1, ?Carbon $when = null): array
    {
        $elig = $student->eligibility();
        if (!$elig['can_borrow']) {
            throw new HttpException(403, $elig['reason']);
        }

        $tool = Tool::find($toolId);
        if (!$tool) {
            throw new HttpException(404, 'Tool not found');
        }
        if ($tool->status !== 'available') {
            throw new HttpException(409, "Tool '{$tool->name}' is not available ({$tool->status})");
        }

        $limit = (int) Setting::get('borrow_limit_hours', '8');
        $borrowAt = $when ?? Carbon::now();
        $expected = $borrowAt->copy()->addHours($limit);

        $tx = Transaction::create([
            'student_id' => $student->id, 'tool_id' => $tool->id, 'locker_id' => $tool->locker_id,
            'qty' => $qty, 'borrow_time' => $borrowAt, 'expected_return' => $expected,
            'status' => 'borrowed',
        ]);

        $tool->update(['status' => 'borrowed']);
        if ($tool->locker_id) {
            Locker::where('id', $tool->locker_id)
                ->update(['occupancy' => 'removed', 'led' => 'red', 'last_seen' => Carbon::now()]);
        }

        return [
            'txId' => (string) $tx->id,
            'tool' => $tool->name,
            'lockerId' => $tool->locker_id ? (string) $tool->locker_id : '',
            'borrowTime' => $borrowAt->toIso8601String(),
            'expectedReturn' => $expected->toIso8601String(),
            'openLocker' => true,   // firmware: OPEN_LOCKER command target
        ];
    }

    /** Close the open borrow for this student+tool and flip state back. */
    public function returnTool(Student $student, int $toolId, ?Carbon $when = null): array
    {
        $tx = Transaction::where('student_id', $student->id)
            ->where('tool_id', $toolId)
            ->whereNull('return_time')
            ->orderBy('borrow_time')
            ->first();
        if (!$tx) {
            throw new HttpException(404, 'No open borrow found for this student and tool');
        }

        $returnAt = $when ?? Carbon::now();
        $tx->update(['return_time' => $returnAt, 'status' => 'returned']);
        Tool::where('id', $toolId)->update(['status' => 'available']);
        if ($tx->locker_id) {
            Locker::where('id', $tx->locker_id)
                ->update(['occupancy' => 'present', 'led' => 'green', 'last_seen' => Carbon::now()]);
        }

        return [
            'txId' => (string) $tx->id,
            'lockerId' => $tx->locker_id ? (string) $tx->locker_id : '',
            'returnTime' => $returnAt->toIso8601String(),
            'late' => $returnAt->gt($tx->expected_return),
            'openLocker' => true,
        ];
    }
}

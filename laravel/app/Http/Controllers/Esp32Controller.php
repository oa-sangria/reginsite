<?php

namespace App\Http\Controllers;

use App\Models\DeviceCommand;
use App\Models\Locker;
use App\Models\Setting;
use App\Models\Student;
use App\Models\Tool;
use App\Models\Transaction;
use App\Services\LockerSystem;
use App\Services\QrStudent;
use Carbon\Carbon;
use Illuminate\Http\Request;
use Symfony\Component\HttpKernel\Exception\HttpException;

/**
 * Device API for the locker terminal + Arduino (X-API-Key via device.key).
 *
 * Screen-driven flow:
 *   1. terminal: verify-student  {qr}            -> student + eligibility + tools
 *   2. terminal: borrow-request  {student_no, locker_id}  -> queues an OPEN command
 *   3. bridge:   GET commands                    -> Arduino opens that locker
 *   4. bridge:   confirm {command_id, uid}       -> records the borrow of the
 *                                                   specific tool that was scanned
 * Return mirrors this with return-request + confirm.
 * The old immediate borrow/return (by tool_id) are kept for the simulator/tests.
 */
class Esp32Controller extends Controller
{
    private LockerSystem $system;

    public function __construct(LockerSystem $system)
    {
        $this->system = $system;
    }

    /* Resolve a student from student_no (preferred) or a raw QR string. */
    private function resolveStudent(Request $request): Student
    {
        $no = trim((string) $request->input('student_no', ''));
        if ($no !== '') {
            $s = Student::where('student_no', $no)->orWhere('qr_code', $no)->first();
            if ($s) {
                return $s;
            }
        }
        [$s, $err] = QrStudent::resolve((string) $request->input('qr', ''));
        if (!$s) {
            throw new HttpException(404, $err ?: 'Unknown student');
        }
        return $s;
    }

    private function studentPayload(Student $s): array
    {
        return [
            'id' => (string) $s->id, 'studentId' => $s->student_no, 'name' => $s->name,
            'program' => $s->program, 'major' => $s->major,
            'status' => $s->status,
            'bannedUntil' => $s->banned_until ? $s->banned_until->toIso8601String() : null,
        ];
    }

    // 1) Identify the student from a QR scan. -------------------------------- //
    public function verifyStudent(Request $request)
    {
        $this->system->runMaintenance();
        [$s, $err] = QrStudent::resolve((string) $request->input('qr', ''));
        if (!$s) {
            return response()->json(['ok' => false, 'error' => $err, 'code' => 'not_eligible'], 200);
        }

        $borrowed = $s->openBorrows()->with(['tool', 'locker'])->orderBy('borrow_time')->get()
            ->map(fn ($t) => [
                'txId' => (string) $t->id, 'toolId' => (string) $t->tool_id,
                'tool' => $t->tool->name ?? '—', 'rfidTag' => $t->tool->rfid_tag ?? '',
                'lockerId' => $t->locker_id ? (string) $t->locker_id : '',
                'locker' => $t->locker->name ?? '—',
                'borrowTime' => $t->borrow_time->toIso8601String(),
                'expectedReturn' => $t->expected_return->toIso8601String(),
                'status' => $t->status,
            ]);

        // Available tool TYPES grouped by locker (each locker holds one type).
        $available = Locker::with(['tool' => function ($q) {
            $q->where('status', 'available');
        }])->orderBy('id')->get()->map(function ($l) {
            $count = Tool::where('locker_id', $l->id)->where('status', 'available')->count();
            $sample = Tool::where('locker_id', $l->id)->first();
            return [
                'lockerId' => (string) $l->id, 'locker' => $l->name,
                'type' => $sample ? preg_replace('/\s+\d+$/', '', $sample->name) : $l->name,
                'available' => $count,
            ];
        })->filter(fn ($x) => $x['available'] > 0)->values();

        return response()->json(['ok' => true,
            'student' => $this->studentPayload($s),
            'eligibility' => $s->eligibility(),
            'borrowed' => $borrowed,
            'availableByLocker' => $available,
            'terms' => (string) Setting::get('terms', ''),
            'borrowLimitHours' => (int) Setting::get('borrow_limit_hours', '8'),
        ]);
    }

    // 2a) Borrow request from the touchscreen -> queue an OPEN command. ------- //
    public function borrowRequest(Request $request)
    {
        $this->system->runMaintenance();
        $s = $this->resolveStudent($request);
        $elig = $s->eligibility();
        if (!$elig['can_borrow']) {
            throw new HttpException(403, $elig['reason']);
        }
        $lockerId = (int) $request->input('locker_id', 0);
        $locker = Locker::find($lockerId);
        if (!$locker) {
            throw new HttpException(404, 'Locker not found');
        }
        $hasTool = Tool::where('locker_id', $lockerId)->where('status', 'available')->exists();
        if (!$hasTool) {
            throw new HttpException(409, 'No available tool in ' . $locker->name);
        }
        $cmd = DeviceCommand::create([
            'type' => 'open', 'locker_id' => $lockerId, 'mode' => 'borrow',
            'student_id' => $s->id, 'status' => 'pending',
        ]);
        return response()->json(['ok' => true, 'commandId' => $cmd->id,
            'lockerId' => (string) $lockerId, 'locker' => $locker->name,
            'message' => 'Opening ' . $locker->name . ' — take one tool and scan its tag.']);
    }

    // 2b) Return request -> queue an OPEN command for that tool's locker. ----- //
    public function returnRequest(Request $request)
    {
        $this->system->runMaintenance();
        $s = $this->resolveStudent($request);
        $toolId = (int) $request->input('tool_id', 0);
        $tx = Transaction::where('student_id', $s->id)->where('tool_id', $toolId)
            ->whereNull('return_time')->first();
        if (!$tx) {
            throw new HttpException(404, 'That tool is not on loan to this student');
        }
        $cmd = DeviceCommand::create([
            'type' => 'open', 'locker_id' => $tx->locker_id, 'mode' => 'return',
            'student_id' => $s->id, 'tool_id' => $toolId, 'transaction_id' => $tx->id,
            'status' => 'pending',
        ]);
        $locker = Locker::find($tx->locker_id);
        return response()->json(['ok' => true, 'commandId' => $cmd->id,
            'lockerId' => (string) $tx->locker_id, 'locker' => $locker->name ?? '—',
            'message' => 'Opening ' . ($locker->name ?? 'locker') . ' — place the tool back and scan its tag.']);
    }

    // 3) Bridge pulls pending commands (and they're marked sent). ------------ //
    public function commands()
    {
        $cmds = DeviceCommand::where('status', 'pending')->orderBy('id')->get();
        if ($cmds->count()) {
            DeviceCommand::whereIn('id', $cmds->pluck('id'))->update(['status' => 'sent']);
        }
        return response()->json(['ok' => true, 'commands' => $cmds->map(fn ($c) => [
            'id' => $c->id, 'type' => $c->type, 'lockerId' => (string) $c->locker_id,
            'mode' => $c->mode,
        ])]);
    }

    // 4) Bridge confirms the physical result (RFID tag scanned at pickup). ---- //
    public function confirm(Request $request)
    {
        $cmd = DeviceCommand::find((int) $request->input('command_id', 0));
        if (!$cmd) {
            throw new HttpException(404, 'Unknown command');
        }
        if ($request->boolean('timeout')) {
            $cmd->update(['status' => 'done']);
            return response()->json(['ok' => true, 'result' => 'cancelled (timeout)']);
        }

        $uid = (string) $request->input('uid', '');
        $tool = Tool::findByTag($uid);
        $student = Student::find($cmd->student_id);

        if ($cmd->mode === 'borrow') {
            if (!$tool || (int) $tool->locker_id !== (int) $cmd->locker_id) {
                throw new HttpException(422, 'Scanned tag is not a tool from this locker');
            }
            if ($tool->status !== 'available') {
                throw new HttpException(409, $tool->name . ' is not available (' . $tool->status . ')');
            }
            $result = $this->system->borrow($student, $tool->id);
            $cmd->update(['status' => 'done', 'tool_id' => $tool->id, 'transaction_id' => $result['txId']]);
            return response()->json(['ok' => true, 'action' => 'borrow', 'tool' => $tool->name, 'result' => $result]);
        }

        // return
        if (!$tool) {
            throw new HttpException(422, 'Scanned tag not recognized');
        }
        $result = $this->system->returnTool($student, $tool->id);
        $cmd->update(['status' => 'done', 'tool_id' => $tool->id]);
        return response()->json(['ok' => true, 'action' => 'return', 'tool' => $tool->name, 'result' => $result]);
    }

    // --- Immediate borrow/return (simulator / direct tests) ----------------- //
    public function borrow(Request $request)
    {
        $this->system->runMaintenance();
        $s = $this->resolveStudent($request);
        $result = $this->system->borrow($s, (int) $request->input('tool_id', 0), max(1, (int) $request->input('qty', 1)));
        return response()->json(['ok' => true, 'result' => $result]);
    }

    public function returnTool(Request $request)
    {
        $this->system->runMaintenance();
        $s = $this->resolveStudent($request);
        $result = $this->system->returnTool($s, (int) $request->input('tool_id', 0));
        return response()->json(['ok' => true, 'result' => $result]);
    }

    // --- Telemetry + offline sync ------------------------------------------- //
    public function state()
    {
        $this->system->runMaintenance();
        return response()->json(['ok' => true,
            'lockers' => Locker::with('tool')->orderBy('id')->get()->map(fn ($l) => [
                'id' => (string) $l->id, 'name' => $l->name, 'sensor' => $l->sensor,
                'occupancy' => $l->occupancy, 'led' => $l->led,
                'available' => Tool::where('locker_id', $l->id)->where('status', 'available')->count(),
            ]),
        ]);
    }

    public function lockerStatus(Request $request)
    {
        $locker = Locker::find((int) $request->input('locker_id', 0));
        if (!$locker) {
            throw new HttpException(404, 'Locker not found');
        }
        $data = $request->validate([
            'sensor' => 'nullable|in:online,offline',
            'occupancy' => 'nullable|in:present,removed',
            'led' => 'nullable|in:green,red,off',
        ]);
        $locker->update(array_filter([
            'sensor' => $data['sensor'] ?? null,
            'occupancy' => $data['occupancy'] ?? null,
            'led' => $data['led'] ?? null,
        ]) + ['last_seen' => Carbon::now()]);
        return response()->json(['ok' => true]);
    }

    public function sync(Request $request)
    {
        $this->system->runMaintenance();
        $events = $request->input('events', []);
        $results = [];
        foreach (is_array($events) ? $events : [] as $i => $ev) {
            try {
                $s = Student::where('student_no', $ev['student_no'] ?? '')
                    ->orWhere('qr_code', $ev['qr'] ?? '')->first();
                if (!$s && !empty($ev['uid'])) {
                    // borrow/return by tag when no student key present
                }
                if (!$s) {
                    throw new HttpException(404, 'Unknown student in event');
                }
                $tool = !empty($ev['uid']) ? Tool::findByTag($ev['uid']) : Tool::find($ev['tool_id'] ?? 0);
                if (!$tool) {
                    throw new HttpException(404, 'Unknown tool in event');
                }
                $when = !empty($ev['timestamp']) ? Carbon::parse($ev['timestamp']) : null;
                $result = ($ev['type'] ?? '') === 'return'
                    ? $this->system->returnTool($s, $tool->id, $when)
                    : $this->system->borrow($s, $tool->id, 1, $when);
                $results[] = ['index' => $i, 'ok' => true, 'result' => $result];
            } catch (\Throwable $e) {
                $results[] = ['index' => $i, 'ok' => false, 'error' => $e->getMessage()];
            }
        }
        return response()->json(['ok' => true, 'results' => $results]);
    }
}

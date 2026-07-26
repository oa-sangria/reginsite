<?php

namespace App\Http\Controllers;

use App\Models\Locker;
use App\Models\Setting;
use App\Models\Student;
use App\Services\LockerSystem;
use Carbon\Carbon;
use Illuminate\Http\Request;
use Symfony\Component\HttpKernel\Exception\HttpException;

/**
 * Device API for the ESP32/Arduino locker terminal (X-API-Key auth via
 * device.key middleware). The terminal simulator uses these same endpoints,
 * so the firmware swaps in with zero server changes.
 */
class Esp32Controller extends Controller
{
    private LockerSystem $system;

    public function __construct(LockerSystem $system)
    {
        $this->system = $system;
    }

    private function findStudent(string $qr): Student
    {
        $student = Student::where('qr_code', trim($qr))->first();
        if (!$student) {
            throw new HttpException(404, 'Unknown QR code — student not registered');
        }
        return $student;
    }

    public function verifyStudent(Request $request)
    {
        $this->system->runMaintenance();
        $s = $this->findStudent((string) $request->input('qr', ''));

        $borrowed = $s->openBorrows()->with(['tool', 'locker'])->orderBy('borrow_time')->get()
            ->map(fn ($t) => [
                'txId' => (string) $t->id,
                'toolId' => (string) $t->tool_id,
                'tool' => $t->tool->name ?? '—',
                'rfidTag' => $t->tool->rfid_tag ?? '',
                'lockerId' => $t->locker_id ? (string) $t->locker_id : '',
                'locker' => $t->locker->name ?? '—',
                'borrowTime' => $t->borrow_time->toIso8601String(),
                'expectedReturn' => $t->expected_return->toIso8601String(),
                'status' => $t->status,
            ]);

        return response()->json(['ok' => true,
            'student' => [
                'id' => (string) $s->id, 'studentId' => $s->student_no, 'name' => $s->name,
                'strand' => $s->strand, 'status' => $s->status,
                'bannedUntil' => $s->banned_until ? $s->banned_until->toIso8601String() : null,
            ],
            'eligibility' => $s->eligibility(),
            'borrowed' => $borrowed,
            'terms' => (string) Setting::get('terms', ''),
            'borrowLimitHours' => (int) Setting::get('borrow_limit_hours', '8'),
        ]);
    }

    public function state()
    {
        $this->system->runMaintenance();

        return response()->json(['ok' => true,
            'lockers' => Locker::with('tool')->orderBy('id')->get()->map(fn ($l) => [
                'id' => (string) $l->id, 'name' => $l->name, 'sensor' => $l->sensor,
                'occupancy' => $l->occupancy, 'led' => $l->led,
                'toolId' => $l->tool_id ? (string) $l->tool_id : '',
                'tool' => $l->tool->name ?? null,
                'rfidTag' => $l->tool->rfid_tag ?? null,
                'toolStatus' => $l->tool->status ?? null,
            ]),
        ]);
    }

    public function borrow(Request $request)
    {
        $this->system->runMaintenance();
        $s = $this->findStudent((string) $request->input('qr', ''));
        $result = $this->system->borrow(
            $s,
            (int) $request->input('tool_id', 0),
            max(1, (int) $request->input('qty', 1))
        );

        return response()->json(['ok' => true, 'result' => $result]);
    }

    public function returnTool(Request $request)
    {
        $this->system->runMaintenance();
        $s = $this->findStudent((string) $request->input('qr', ''));
        $result = $this->system->returnTool($s, (int) $request->input('tool_id', 0));

        return response()->json(['ok' => true, 'result' => $result]);
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

    /** Batch upload of transactions recorded while the terminal was offline. */
    public function sync(Request $request)
    {
        $this->system->runMaintenance();
        $events = $request->input('events', []);
        $results = [];

        foreach (is_array($events) ? $events : [] as $i => $ev) {
            try {
                $s = $this->findStudent((string) ($ev['qr'] ?? ''));
                $when = !empty($ev['timestamp']) ? Carbon::parse($ev['timestamp']) : null;
                $result = ($ev['type'] ?? '') === 'return'
                    ? $this->system->returnTool($s, (int) ($ev['tool_id'] ?? 0), $when)
                    : $this->system->borrow($s, (int) ($ev['tool_id'] ?? 0), 1, $when);
                $results[] = ['index' => $i, 'ok' => true, 'result' => $result];
            } catch (HttpException $e) {
                $results[] = ['index' => $i, 'ok' => false, 'error' => $e->getMessage()];
            } catch (\Exception $e) {
                $results[] = ['index' => $i, 'ok' => false, 'error' => $e->getMessage()];
            }
        }

        return response()->json(['ok' => true, 'results' => $results]);
    }
}

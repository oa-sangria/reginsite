<?php

namespace App\Http\Controllers;

use App\Models\Locker;
use App\Models\Setting;
use App\Models\Student;
use App\Models\Tool;
use Carbon\Carbon;
use Illuminate\Http\Request;

/* Admin save endpoints: tools, lockers, students, terms (add or edit by id). */
class InventoryController extends Controller
{
    public function saveTool(Request $request)
    {
        $data = $request->validate([
            'id' => 'nullable',
            'name' => 'required|string|max:120',
            'rfidTag' => 'nullable|string|max:60',
            'lockerId' => 'nullable',
            'status' => 'in:available,borrowed,maintenance',
        ]);

        $attrs = [
            'name' => $data['name'],
            'rfid_tag' => $data['rfidTag'] ?? '',
            'locker_id' => !empty($data['lockerId']) ? (int) $data['lockerId'] : null,
            'status' => $data['status'] ?? 'available',
        ];
        $tool = !empty($data['id'])
            ? tap(Tool::findOrFail((int) $data['id']))->update($attrs)
            : Tool::create($attrs);

        // Keep the locker's tool link in sync when a tool is (re)assigned.
        if ($tool->locker_id) {
            Locker::where('id', $tool->locker_id)->update(['tool_id' => $tool->id]);
        }

        return response()->json(['ok' => true, 'id' => (string) $tool->id]);
    }

    public function saveLocker(Request $request)
    {
        $data = $request->validate([
            'id' => 'nullable',
            'number' => 'required|string|max:40',
            'toolId' => 'nullable',
            'sensor' => 'in:online,offline',
            'occupancy' => 'in:present,removed',
            'led' => 'in:green,red,off',
        ]);

        $attrs = [
            'name' => $data['number'],
            'tool_id' => !empty($data['toolId']) ? (int) $data['toolId'] : null,
            'sensor' => $data['sensor'] ?? 'online',
            'occupancy' => $data['occupancy'] ?? 'present',
            'led' => $data['led'] ?? 'off',
        ];
        if (!empty($data['id'])) {
            $locker = tap(Locker::findOrFail((int) $data['id']))->update($attrs);
        } else {
            $attrs['last_seen'] = Carbon::now();
            $locker = Locker::create($attrs);
        }

        if ($locker->tool_id) {
            Tool::where('id', $locker->tool_id)->update(['locker_id' => $locker->id]);
        }

        return response()->json(['ok' => true, 'id' => (string) $locker->id]);
    }

    public function saveStudent(Request $request)
    {
        $data = $request->validate([
            'id' => 'nullable',
            'name' => 'required|string|max:120',
            'studentId' => 'required|string|max:30',
            'strand' => 'nullable|string|max:40',
            'qr' => 'nullable|string|max:60',
            'status' => 'in:active,banned',
        ]);

        $qr = trim($data['qr'] ?? '') ?: 'QR-' . $data['studentId'];
        $status = $data['status'] ?? 'active';

        $dupe = Student::where('qr_code', $qr)
            ->when(!empty($data['id']), fn ($q) => $q->where('id', '!=', (int) $data['id']))
            ->exists();
        if ($dupe) {
            return response()->json([
                'ok' => false, 'error' => 'QR code already registered to another student',
            ], 422);
        }

        $attrs = [
            'student_no' => $data['studentId'],
            'name' => $data['name'],
            'strand' => $data['strand'] ?? '',
            'qr_code' => $qr,
            'status' => $status,
        ];

        if (!empty($data['id'])) {
            $student = Student::findOrFail((int) $data['id']);
            // Manual ban/unban from admin: banned -> 2-day window unless already set
            $attrs['banned_until'] = $status === 'banned'
                ? ($student->banned_until ?? Carbon::now()->addDays(2))
                : null;
            $student->update($attrs);
        } else {
            $student = Student::create($attrs);
        }

        return response()->json(['ok' => true, 'id' => (string) $student->id]);
    }

    public function saveTerms(Request $request)
    {
        Setting::put('terms', (string) $request->input('terms', ''));

        return response()->json(['ok' => true]);
    }
}

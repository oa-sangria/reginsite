<?php

namespace Database\Seeders;

use App\Models\Ban;
use App\Models\Locker;
use App\Models\Setting;
use App\Models\Student;
use App\Models\Tool;
use App\Models\Transaction;
use App\Models\User;
use Carbon\Carbon;
use Illuminate\Database\Seeder;
use Illuminate\Support\Facades\Hash;

/*
 * Real tool inventory: 9 tool types, one locker per type, RFID tag per tool.
 * UIDs are stored NORMALIZED (no separators, uppercase) so scans match
 * regardless of "AA:BB" vs "AA BB" formatting.
 */
class DatabaseSeeder extends Seeder
{
    /** Strip separators + uppercase, e.g. "A7:45:64:06" -> "A74564 06" -> "A7456406". */
    private function norm(string $uid): string
    {
        return strtoupper(preg_replace('/[^0-9A-Fa-f]/', '', $uid));
    }

    public function run()
    {
        User::create([
            'username' => 'admin', 'name' => 'Administrator',
            'password' => Hash::make('admin'),
        ]);

        Setting::put('terms',
            "TERMS & CONDITIONS — Tool Borrowing\n\n"
            . "1. Tools must be returned within 8 HOURS of borrowing. A reminder is issued at the 8-hour mark.\n"
            . "2. You may not borrow another tool while you have an OVERDUE or unreturned item — return it first.\n"
            . "3. Tools left overdue for 2 days or more will result in a 2-DAY BORROWING BAN.\n"
            . "4. Inspect the tool before removing it. Report any damage to the laboratory custodian.\n"
            . "5. Return the tool to its ASSIGNED locker and scan its RFID tag to complete the return.\n"
            . "6. You are responsible for any loss or damage to the borrowed tool.\n\n"
            . "By selecting BORROW you agree to these Terms & Conditions.");
        Setting::put('borrow_limit_hours', '8');
        Setting::put('ban_trigger_days', '2');
        Setting::put('ban_length_days', '2');

        // --- Students (demo). Programs set so the eligibility gate passes. -----
        $EE = 'Electrical Technology';
        $MX = 'Mechatronics Technology';
        $HV = 'Heating, Ventilating, Air Conditioning and Refrigeration Technology';
        $BIT = 'Bachelor of Industrial Technology';
        $students = [
            ['2023106548', 'Kurt Allen L Deramas',  $EE, 'banned', Carbon::now()->addDay()],
            ['2023101132', 'Nhoel Martin H Castro', $MX, 'active', null],
            ['2023102906', 'Benedict D Agravante',  $HV, 'active', null],
            ['2023104417', 'Joshua Dela Cruz',      $EE, 'active', null],
            ['2023100582', 'Bea Mendoza',           $MX, 'active', null],
            ['2023103390', 'Carlo Aquino',          $EE, 'active', null],
            ['2023106711', 'Nina Villanueva',       $HV, 'active', null],
            ['2023105028', 'Paolo Ramirez',         $MX, 'active', null],
        ];
        foreach ($students as $s) {
            Student::create([
                'student_no' => $s[0], 'name' => $s[1], 'strand' => '',
                'program' => $BIT, 'major' => $s[2],
                'qr_code' => $s[0], 'status' => $s[3], 'banned_until' => $s[4],
            ]);
        }

        /* --- Cabinets + tools -------------------------------------------------
         * Matches the two-Mega production pin map: cabinets 1-8 have 4 ultrasonic
         * slots, cabinets 9 and 10 have 1 each (34 sensors, 34 tools). The two
         * Makita Drills get a cabinet each, which is why 9 and 10 are single-slot.
         * Cabinets 1-5 live on Mega 1, cabinets 6-10 on Mega 2.
         *
         * INVARIANT: lockers.id (auto-increment) == the physical cabinet number the
         * bridge sends in "OPEN,<locker>,<mode>". Seeding order is therefore load
         * bearing — if a locker is ever deleted and re-created the ids shift and
         * OPEN will unlock the wrong door.
         *
         * Each entry: cabinet label => [tool-name prefix, first tool number, UIDs].
         * The name prefix and start index are separate from the label so cabinets 9
         * and 10 can be "Makita Drill A"/"B" while holding "Makita Drill 1"/"2".
         */
        $inventory = [
            1  => ['Pliers',          'Pliers',         1, ['E5:77:7B:06', 'CD:5B:7B:06', '93:0E:79:06', '25:F8:7A:06']],
            2  => ['Side Cutter',     'Side Cutter',    1, ['1A:D9:78:06', '5C:D8:7A:06', 'BA:F0:7B:06', '63:75:7B:06']],
            3  => ['Wire Crimper',    'Wire Crimper',   1, ['CB:56:CF:83', '63:98:66:06', 'E1:C9:66:06', '87:3A:7C:06']],
            4  => ['Clamp Meter',     'Clamp Meter',    1, ['CE:B8:7C:06', '4E:B0:78:06', 'D6:42:7B:06', '72:8B:79:06']],
            5  => ['Multimeter',      'Multimeter',     1, ['C5:CB:7A:06', '0D:6B:7A:06', 'F4:38:7C:06', 'DE:73:7B:06']],
            6  => ['Screwdriver Set', 'Screwdriver Set',1, ['F2:01:7A:06', '0C:D7:7B:06', '81:1C:7B:06', '8C:B3:7C:06']],
            7  => ['Wire Stripper',   'Wire Stripper',  1, ['1E:3C:78:06', '74:D6:7B:06', '56:CD:7C:06', '3B:86:7C:06']],
            8  => ['Soldering Iron',  'Soldering Iron', 1, ['A7:45:64:06', 'E9:8C:7B:06', 'B7:5D:7A:06', 'DB:F0:7B:06']],
            9  => ['Makita Drill A',  'Makita Drill',   1, ['92:E0:7C:06']],
            10 => ['Makita Drill B',  'Makita Drill',   2, ['41:3A:78:06']],
        ];

        foreach ($inventory as $n => [$label, , , ]) {
            Locker::create([
                'name' => "Locker {$n} — {$label}",
                'sensor' => 'online', 'occupancy' => 'present', 'led' => 'green',
                'last_seen' => Carbon::now(),
            ]);
        }

        $byTag = [];        // normalized tag => tool id
        $lockerOf = [];     // normalized tag => cabinet number
        foreach ($inventory as $lockerId => [, $prefix, $firstNo, $uids]) {
            foreach ($uids as $i => $uid) {
                $tag = $this->norm($uid);
                $t = Tool::create([
                    'name' => $prefix . ' ' . ($firstNo + $i),
                    'rfid_tag' => $tag,
                    'locker_id' => $lockerId,
                    'status' => 'available',
                ]);
                $byTag[$tag] = $t->id;
                $lockerOf[$tag] = $lockerId;
            }
        }

        // --- A little demo history so the dashboard isn't empty ---------------
        // Takes a UID, not a tool id + locker id: the cabinet is derived from the
        // inventory above so the two can never disagree when tools are re-homed.
        $mk = function (int $studentId, string $uid, int $borrowAgoH, ?int $returnAgoH)
                use ($byTag, $lockerOf) {
            $tag = $this->norm($uid);
            $toolId = $byTag[$tag];
            $lockerId = $lockerOf[$tag];
            $borrow = Carbon::now()->subHours($borrowAgoH);
            $expected = $borrow->copy()->addHours(8);
            $return = $returnAgoH === null ? null : Carbon::now()->subHours($returnAgoH);
            $status = $return !== null ? 'returned' : (Carbon::now()->gt($expected) ? 'overdue' : 'borrowed');
            Transaction::create([
                'student_id' => $studentId, 'tool_id' => $toolId, 'locker_id' => $lockerId, 'qty' => 1,
                'borrow_time' => $borrow, 'expected_return' => $expected,
                'return_time' => $return, 'status' => $status,
            ]);
            if ($return === null) {
                Tool::where('id', $toolId)->update(['status' => 'borrowed']);
            }
        };
        // Active (tx #1 = the overdue one that drives the ban)
        $mk(1, 'A7:45:64:06', 72, null);   // Kurt  — Soldering Iron 1 (cab 8), overdue 3 days -> banned
        $mk(2, 'C5:CB:7A:06', 2,  null);   // Nhoel — Multimeter 1 (cab 5), on time
        $mk(6, '1A:D9:78:06', 9,  null);   // Carlo — Side Cutter 1 (cab 2), overdue (>8h)
        // Returned today
        $mk(3, 'E5:77:7B:06', 28, 5);      // Bene  — Pliers 1 (cab 1)
        $mk(5, 'F2:01:7A:06', 30, 4);      // Bea   — Screwdriver Set 1 (cab 6)

        Ban::create([
            'student_id' => 1, 'transaction_id' => 1,
            'reason' => 'Overdue tool exceeded 2 days',
            'ban_from' => Carbon::now()->subDay(), 'ban_until' => Carbon::now()->addDay(),
        ]);
    }
}

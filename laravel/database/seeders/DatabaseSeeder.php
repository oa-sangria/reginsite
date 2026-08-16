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

        // --- Lockers (one tool TYPE each; #10 spare) --------------------------
        $lockerNames = [
            1 => 'Soldering Iron', 2 => 'Plier', 3 => 'Clamp Ammeter', 4 => 'Multitester (Metro)',
            5 => 'Screwdriver Set', 6 => 'Side Cutter', 7 => 'Makita Drill', 8 => 'Wire Stripper',
            9 => 'Wire Crimper', 10 => 'Spare',
        ];
        foreach ($lockerNames as $n => $label) {
            Locker::create([
                'name' => "Locker {$n} — {$label}",
                'sensor' => $n === 10 ? 'offline' : 'online',
                'occupancy' => 'present', 'led' => $n === 10 ? 'off' : 'green',
                'last_seen' => Carbon::now(),
            ]);
        }

        // --- Tools: real RFID tags, grouped into lockers ----------------------
        $inventory = [
            1 => ['Soldering Iron', ['A7:45:64:06', 'E9:8C:7B:06', 'B7:5D:7A:06', 'DB:F0:7B:06']],
            2 => ['Plier',          ['E5:77:7B:06', 'CD:5B:7B:06', '93:0E:79:06', '25:F8:7A:06']],
            3 => ['Clamp Ammeter',  ['CE:B8:7C:06', '4E:B0:78:06', 'D6:42:7B:06', '72:8B:79:06']],
            4 => ['Multitester',    ['C5:CB:7A:06', '0D:6B:7A:06', 'F4:38:7C:06', 'DE:73:7B:06']],
            5 => ['Screwdriver Set',['F2:01:7A:06', '0C:D7:7B:06', '81:1C:7B:06', '8C:B3:7C:06']],
            6 => ['Side Cutter',    ['1A:D9:78:06', '5C:D8:7A:06', 'BA:F0:7B:06', '63:75:7B:06']],
            7 => ['Makita Drill',   ['92:E0:7C:06', '41:3A:78:06']],
            8 => ['Wire Stripper',  ['1E:3C:78:06', '74:D6:7B:06', '56:CD:7C:06', '3B:86:7C:06']],
            9 => ['Wire Crimper',   ['CB:56:CF:83', '63:98:66:06', 'E1:C9:66:06', '87:3A:7C:06']],
        ];
        $byTag = [];
        foreach ($inventory as $lockerId => [$type, $uids]) {
            foreach ($uids as $i => $uid) {
                $tag = $this->norm($uid);
                $t = Tool::create([
                    'name' => $type . ' ' . ($i + 1),
                    'rfid_tag' => $tag,
                    'locker_id' => $lockerId,
                    'status' => 'available',
                ]);
                $byTag[$tag] = $t->id;
            }
        }

        // --- A little demo history so the dashboard isn't empty ---------------
        $mk = function (int $studentId, int $toolId, int $lockerId, int $borrowAgoH, ?int $returnAgoH) {
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
        $solder1 = $byTag[$this->norm('A7:45:64:06')];   // Soldering Iron 1, locker 1
        $multi1  = $byTag[$this->norm('C5:CB:7A:06')];   // Multitester 1, locker 4
        $side1   = $byTag[$this->norm('1A:D9:78:06')];   // Side Cutter 1, locker 6
        $mk(1, $solder1, 1, 72, null);   // Kurt — overdue 3 days -> banned
        $mk(2, $multi1, 4, 2, null);     // Nhoel — on time
        $mk(6, $side1, 6, 9, null);      // Carlo — overdue (>8h)
        // Returned today
        $mk(3, $byTag[$this->norm('E5:77:7B:06')], 2, 28, 5);   // Plier 1
        $mk(5, $byTag[$this->norm('F2:01:7A:06')], 5, 30, 4);   // Screwdriver Set 1

        Ban::create([
            'student_id' => 1, 'transaction_id' => 1,
            'reason' => 'Overdue tool exceeded 2 days',
            'ban_from' => Carbon::now()->subDay(), 'ban_until' => Carbon::now()->addDay(),
        ]);
    }
}

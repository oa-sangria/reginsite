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

/* Demo dataset — mirrors the plain-PHP prototype's install.php seed. */
class DatabaseSeeder extends Seeder
{
    public function run()
    {
        User::create([
            'username' => 'admin',
            'name' => 'Administrator',
            'password' => Hash::make('admin'),
        ]);

        Setting::put('terms',
            "TERMS & CONDITIONS — Tool Borrowing\n\n"
            . "1. Tools must be returned within 8 HOURS of borrowing. A reminder is issued at the 8-hour mark.\n"
            . "2. You may not borrow another tool while you have an OVERDUE or unreturned item — return it first.\n"
            . "3. Tools left overdue for 2 days or more will result in a 2-DAY BORROWING BAN.\n"
            . "4. Inspect the tool before removing it. Report any damage immediately to the laboratory custodian.\n"
            . "5. Return the tool to its ASSIGNED locker and scan its RFID tag to complete the return.\n"
            . "6. You are responsible for any loss or damage to the borrowed tool.\n"
            . "7. Lost RFID tags or keychains must be reported within 24 hours.\n\n"
            . "By selecting BORROW you agree to these Terms & Conditions.");
        Setting::put('borrow_limit_hours', '8');
        Setting::put('ban_trigger_days', '2');
        Setting::put('ban_length_days', '2');

        $students = [
            ['2026-0457', 'Regina Reyes',     'STEM',    'banned', Carbon::now()->addDay()],
            ['2026-0132', 'Mark Santos',      'TVL-ICT', 'active', null],
            ['2026-0890', 'Andrea Cruz',      'ABM',     'active', null],
            ['2026-0223', 'Joshua Dela Cruz', 'STEM',    'active', null],
            ['2026-0775', 'Bea Mendoza',      'HUMSS',   'active', null],
            ['2026-0319', 'Carlo Aquino',     'TVL-ICT', 'active', null],
            ['2026-0641', 'Nina Villanueva',  'GAS',     'active', null],
            ['2026-0508', 'Paolo Ramirez',    'TVL-HE',  'active', null],
        ];
        foreach ($students as $s) {
            Student::create([
                'student_no' => $s[0], 'name' => $s[1], 'strand' => $s[2],
                'qr_code' => 'QR-' . $s[0], 'status' => $s[3], 'banned_until' => $s[4],
            ]);
        }

        $lockers = [
            ['Locker 1',  'online',  'removed', 'red'],
            ['Locker 2',  'online',  'present', 'green'],
            ['Locker 3',  'offline', 'present', 'off'],
            ['Locker 4',  'online',  'present', 'green'],
            ['Locker 5',  'online',  'removed', 'red'],
            ['Locker 6',  'online',  'removed', 'red'],
            ['Locker 7',  'online',  'present', 'green'],
            ['Locker 8',  'online',  'removed', 'red'],
            ['Locker 9',  'online',  'present', 'off'],
            ['Locker 10', 'online',  'present', 'green'],
        ];
        foreach ($lockers as $l) {
            Locker::create([
                'name' => $l[0], 'sensor' => $l[1], 'occupancy' => $l[2],
                'led' => $l[3], 'last_seen' => Carbon::now(),
            ]);
        }

        $tools = [
            ['Long-nose Plier',    'RFID-A1',  1,  'borrowed'],
            ['Combination Plier',  'RFID-A2',  2,  'available'],
            ['Vernier Caliper',    'RFID-A3',  3,  'available'],
            ['Screwdriver Set',    'RFID-A4',  4,  'available'],
            ['Digital Multimeter', 'RFID-A5',  5,  'borrowed'],
            ['Soldering Iron',     'RFID-A6',  6,  'borrowed'],
            ['Wire Stripper',      'RFID-A7',  7,  'available'],
            ['Claw Hammer',        'RFID-A8',  8,  'borrowed'],
            ['Adjustable Wrench',  'RFID-A9',  9,  'maintenance'],
            ['Hex Key Set',        'RFID-A10', 10, 'available'],
        ];
        foreach ($tools as $t) {
            Tool::create(['name' => $t[0], 'rfid_tag' => $t[1], 'locker_id' => $t[2], 'status' => $t[3]]);
        }
        // 1:1 seed mapping — locker N holds tool N
        foreach (Locker::all() as $locker) {
            $locker->update(['tool_id' => $locker->id]);
        }

        // Transactions — hours relative to now; expected = borrow + 8h
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
        };
        // Active
        $mk(1, 1, 1, 72, null);   // Regina — overdue 3 days (banned)
        $mk(2, 5, 5, 2, null);    // Mark — on time
        $mk(6, 6, 6, 9, null);    // Carlo — overdue (>8h)
        $mk(4, 8, 8, 1, null);    // Joshua — on time
        // Returned today
        $mk(3, 3, 3, 28, 5);
        $mk(5, 4, 4, 48, 4);
        $mk(7, 7, 7, 5, 1);
        $mk(4, 4, 4, 12, 3);
        // Older history
        $mk(8, 2, 2, 96, 72);
        $mk(2, 9, 9, 144, 120);
        $mk(3, 10, 10, 240, 216);

        Ban::create([
            'student_id' => 1, 'transaction_id' => 1,
            'reason' => 'Overdue tool exceeded 2 days',
            'ban_from' => Carbon::now()->subDay(), 'ban_until' => Carbon::now()->addDay(),
        ]);
    }
}

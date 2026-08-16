<?php

namespace App\Services;

use App\Models\Student;

/**
 * Parses the student-ID QR text and resolves it to a Student record.
 *
 * The QR (from the SM8070 scanner) encodes lines like:
 *     Student No.: 2023106548
 *     Full Name: KURT ALLEN L DERAMAS
 *     Program: Bachelor of Industrial Technology major in Electrical Technology
 *
 * Only "Bachelor of Industrial Technology" students in these majors may use
 * the system:
 *     - Electrical Technology
 *     - Mechatronics Technology
 *     - Heating, Ventilating, Air Conditioning and Refrigeration Technology
 */
class QrStudent
{
    /** Major keywords we accept (matched case-insensitively as substrings). */
    public const ALLOWED_MAJORS = [
        'electrical technology',
        'mechatronics technology',
        'heating, ventilating, air conditioning and refrigeration technology',
    ];

    /** Parse raw QR text into ['student_no','name','program','major'] (any may be null). */
    public static function parse(string $raw): array
    {
        $raw = trim($raw);
        $out = ['student_no' => null, 'name' => null, 'program' => null, 'major' => null, 'raw' => $raw];

        if (preg_match('/Student\s*No\.?\s*:?\s*([0-9A-Za-z\-]+)/i', $raw, $m)) {
            $out['student_no'] = trim($m[1]);
        }
        if (preg_match('/Full\s*Name\s*:?\s*(.+)/i', $raw, $m)) {
            $out['name'] = trim(preg_split('/\r|\n/', $m[1])[0]);
        }
        if (preg_match('/Program\s*:?\s*(.+)/i', $raw, $m)) {
            $program = trim(preg_split('/\r|\n/', $m[1])[0]);
            $out['program'] = $program;
            if (preg_match('/major\s+in\s+(.+)/i', $program, $mm)) {
                $out['major'] = trim($mm[1]);
                // strip the "major in ..." tail from the program name
                $out['program'] = trim(preg_replace('/\s*major\s+in\s+.+$/i', '', $program));
            }
        }
        return $out;
    }

    /** Is this program+major allowed to use the system? */
    public static function programAllowed(?string $program, ?string $major): bool
    {
        $program = strtolower((string) $program);
        $major = strtolower((string) $major);
        if (strpos($program, 'industrial technology') === false) {
            return false;
        }
        foreach (self::ALLOWED_MAJORS as $allowed) {
            if ($major !== '' && strpos($major, $allowed) !== false) {
                return true;
            }
            // also accept short forms of the HVAC&R major
            if ($allowed === self::ALLOWED_MAJORS[2] &&
                (strpos($major, 'refrigeration') !== false || strpos($major, 'hvac') !== false)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Turn a scan into a Student. Auto-provisions a new BIT student the first
     * time their ID is seen; falls back to an existing record for plain codes
     * (e.g. admin/demo cards that don't carry program text).
     * Returns [Student|null, ?string errorReason].
     */
    public static function resolve(string $raw): array
    {
        $p = self::parse($raw);

        // New-style ID QR (has a Program line) -> validate + upsert.
        if ($p['student_no'] && $p['program']) {
            if (!self::programAllowed($p['program'], $p['major'])) {
                return [null, 'Not eligible: system is for Bachelor of Industrial Technology '
                    . '(Electrical, Mechatronics, or HVAC&R) students only.'];
            }
            $student = Student::firstOrNew(['student_no' => $p['student_no']]);
            $student->name = $p['name'] ?: ($student->name ?: $p['student_no']);
            $student->program = $p['program'];
            $student->major = $p['major'];
            if (!$student->qr_code) {
                $student->qr_code = $p['student_no'];
            }
            if (!$student->status) {
                $student->status = 'active';
            }
            $student->save();
            return [$student, null];
        }

        // Plain code: match an already-registered student by qr_code or number.
        $code = trim($raw);
        $student = Student::where('qr_code', $code)->orWhere('student_no', $code)->first();
        if (!$student) {
            return [null, 'Unknown ID — student not registered.'];
        }
        return [$student, null];
    }
}

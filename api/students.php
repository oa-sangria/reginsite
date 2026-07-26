<?php
/* reginsite — Student add/edit (admin, PHP 5.6 compatible) */
require_once __DIR__ . "/config.php";
require_admin();

$b = body();
if (req($b, "action") !== "save") fail("Unknown action", 400);

$name = trim((string)req($b, "name"));
$no   = trim((string)req($b, "studentId"));
$qr   = trim((string)req($b, "qr"));
if ($name === "" || $no === "") fail("Student name and student number are required");
if ($qr === "") $qr = "QR-" . $no;
$strand = trim((string)req($b, "strand"));
$status = in_array(req($b, "status"), array("active", "banned"), true) ? req($b, "status") : "active";
$id = req($b, "id") === "" ? null : (int)req($b, "id");

$pdo = db();
try {
    if ($id) {
        // Manual ban/unban from admin: banned -> 2-day window unless one already set
        if ($status === "banned") {
            $pdo->prepare("UPDATE students SET student_no=?, name=?, strand=?, qr_code=?, status='banned',
                           banned_until=COALESCE(banned_until, DATE_ADD(NOW(), INTERVAL 2 DAY)) WHERE id=?")
                ->execute(array($no, $name, $strand, $qr, $id));
        } else {
            $pdo->prepare("UPDATE students SET student_no=?, name=?, strand=?, qr_code=?, status='active',
                           banned_until=NULL WHERE id=?")
                ->execute(array($no, $name, $strand, $qr, $id));
        }
    } else {
        $pdo->prepare("INSERT INTO students (student_no, name, strand, qr_code, status) VALUES (?,?,?,?,?)")
            ->execute(array($no, $name, $strand, $qr, $status));
        $id = (int)$pdo->lastInsertId();
    }
} catch (PDOException $e) {
    if (isset($e->errorInfo[1]) && (int)$e->errorInfo[1] === 1062) {
        fail("QR code already registered to another student");
    }
    throw $e;
}
ok(array("id" => (string)$id));

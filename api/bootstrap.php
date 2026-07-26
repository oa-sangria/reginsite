<?php
/* ============================================================================
   reginsite — Bootstrap: everything the admin front-end needs in one call.
   Payload shape matches the old static data.js (window.DB) so the existing
   renderers keep working. All ids are strings. (PHP 5.6 compatible)
   ========================================================================== */
require_once __DIR__ . "/config.php";
require_admin();
run_maintenance();

$pdo = db();

$students = array();
foreach ($pdo->query("SELECT * FROM students ORDER BY name")->fetchAll() as $r) {
    $students[] = array(
        "id" => (string)$r["id"],
        "studentId" => $r["student_no"],
        "name" => $r["name"],
        "strand" => $r["strand"],
        "qr" => $r["qr_code"],
        "status" => $r["status"],
        "bannedUntil" => iso($r["banned_until"]),
    );
}

$tools = array();
foreach ($pdo->query("SELECT * FROM tools ORDER BY id")->fetchAll() as $r) {
    $tools[] = array(
        "id" => (string)$r["id"],
        "name" => $r["name"],
        "rfidTag" => $r["rfid_tag"],
        "lockerId" => $r["locker_id"] === null ? "" : (string)$r["locker_id"],
        "status" => $r["status"],
    );
}

$lockers = array();
foreach ($pdo->query("SELECT * FROM lockers ORDER BY id")->fetchAll() as $r) {
    $lockers[] = array(
        "id" => (string)$r["id"],
        "number" => $r["name"],
        "toolId" => $r["tool_id"] === null ? "" : (string)$r["tool_id"],
        "sensor" => $r["sensor"],
        "occupancy" => $r["occupancy"],
        "led" => $r["led"],
        "lastSeen" => iso($r["last_seen"]),
    );
}

$transactions = array();
foreach ($pdo->query("SELECT * FROM transactions ORDER BY borrow_time DESC")->fetchAll() as $r) {
    $transactions[] = array(
        "id" => "TXN-" . $r["id"],
        "txId" => (string)$r["id"],
        "studentId" => (string)$r["student_id"],
        "toolId" => (string)$r["tool_id"],
        "lockerId" => $r["locker_id"] === null ? "" : (string)$r["locker_id"],
        "qty" => (int)$r["qty"],
        "borrowTime" => iso($r["borrow_time"]),
        "expectedReturn" => iso($r["expected_return"]),
        "returnTime" => iso($r["return_time"]),
        "status" => $r["status"],
    );
}

$bans = array();
foreach ($pdo->query("SELECT * FROM bans ORDER BY ban_from DESC")->fetchAll() as $r) {
    $bans[] = array(
        "id" => (string)$r["id"],
        "studentId" => (string)$r["student_id"],
        "transactionId" => $r["transaction_id"] === null ? "" : ("TXN-" . $r["transaction_id"]),
        "reason" => $r["reason"],
        "from" => iso($r["ban_from"]),
        "until" => iso($r["ban_until"]),
    );
}

ok(array("data" => array(
    "config" => array(
        "borrowLimitHours" => (int)setting("borrow_limit_hours", "8"),
        "banTriggerDays" => (int)setting("ban_trigger_days", "2"),
        "banLengthDays" => (int)setting("ban_length_days", "2"),
    ),
    "students" => $students,
    "tools" => $tools,
    "lockers" => $lockers,
    "transactions" => $transactions,
    "bans" => $bans,
    "terms" => (string)setting("terms", ""),
)));

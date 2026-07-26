<?php
/* reginsite — Tool add/edit (admin, PHP 5.6 compatible) */
require_once __DIR__ . "/config.php";
require_admin();

$b = body();
if (req($b, "action") !== "save") fail("Unknown action", 400);

$name = trim((string)req($b, "name"));
if ($name === "") fail("Tool name is required");
$rfid = trim((string)req($b, "rfidTag"));
$lockerId = req($b, "lockerId") === "" ? null : (int)req($b, "lockerId");
$status = in_array(req($b, "status"), array("available", "borrowed", "maintenance"), true) ? req($b, "status") : "available";
$id = req($b, "id") === "" ? null : (int)req($b, "id");

$pdo = db();
if ($id) {
    $pdo->prepare("UPDATE tools SET name=?, rfid_tag=?, locker_id=?, status=? WHERE id=?")
        ->execute(array($name, $rfid, $lockerId, $status, $id));
} else {
    $pdo->prepare("INSERT INTO tools (name, rfid_tag, locker_id, status) VALUES (?,?,?,?)")
        ->execute(array($name, $rfid, $lockerId, $status));
    $id = (int)$pdo->lastInsertId();
}
// Keep the locker's tool link in sync when a tool is (re)assigned to a locker.
if ($lockerId !== null) {
    $pdo->prepare("UPDATE lockers SET tool_id=? WHERE id=?")->execute(array($id, $lockerId));
}
ok(array("id" => (string)$id));

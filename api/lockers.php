<?php
/* reginsite — Locker add/edit (admin, PHP 5.6 compatible) */
require_once __DIR__ . "/config.php";
require_admin();

$b = body();
if (req($b, "action") !== "save") fail("Unknown action", 400);

$name = trim((string)req($b, "number"));
if ($name === "") fail("Locker name is required");
$toolId = req($b, "toolId") === "" ? null : (int)req($b, "toolId");
$sensor = in_array(req($b, "sensor"), array("online", "offline"), true) ? req($b, "sensor") : "online";
$occupancy = in_array(req($b, "occupancy"), array("present", "removed"), true) ? req($b, "occupancy") : "present";
$led = in_array(req($b, "led"), array("green", "red", "off"), true) ? req($b, "led") : "off";
$id = req($b, "id") === "" ? null : (int)req($b, "id");

$pdo = db();
if ($id) {
    $pdo->prepare("UPDATE lockers SET name=?, tool_id=?, sensor=?, occupancy=?, led=? WHERE id=?")
        ->execute(array($name, $toolId, $sensor, $occupancy, $led, $id));
} else {
    $pdo->prepare("INSERT INTO lockers (name, tool_id, sensor, occupancy, led, last_seen) VALUES (?,?,?,?,?,NOW())")
        ->execute(array($name, $toolId, $sensor, $occupancy, $led));
    $id = (int)$pdo->lastInsertId();
}
if ($toolId !== null) {
    $pdo->prepare("UPDATE tools SET locker_id=? WHERE id=?")->execute(array($id, $toolId));
}
ok(array("id" => (string)$id));

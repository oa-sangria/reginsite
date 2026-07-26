<?php
/* ============================================================================
   reginsite — Device API for the ESP32/Arduino locker terminal.
   Auth: X-API-Key header (see DEVICE_API_KEY in config.php).
   The terminal simulator (terminal.html) uses these same endpoints, so the
   firmware later swaps in with zero server changes. (PHP 5.6 compatible)

   Actions (?action=... or {"action": ...}):
     verify_student {qr}                       -> student + eligibility + terms
     state                                     -> lockers + tools map for the terminal
     borrow  {qr, tool_id, [qty]}              -> create transaction, open locker
     return  {qr, tool_id}                     -> close transaction
     locker_status {locker_id, sensor?, occupancy?, led?}  -> sensor heartbeat
     sync    {events: [{type, qr, tool_id, timestamp}...]} -> batch offline events
   ========================================================================== */
require_once __DIR__ . "/config.php";
require_device();
run_maintenance();

$b = body();
$action = isset($_GET["action"]) ? $_GET["action"] : req($b, "action");
$pdo = db();

/* ---- helpers (throw ApiError so batch sync can catch per-event) ---------- */
function find_student_by_qr($qr) {
    $st = db()->prepare("SELECT * FROM students WHERE qr_code = ?");
    $st->execute(array($qr));
    $s = $st->fetch();
    if (!$s) throw new ApiError("Unknown QR code — student not registered", 404, "unknown_student");
    return $s;
}
function student_payload($s) {
    return array(
        "id" => (string)$s["id"], "studentId" => $s["student_no"], "name" => $s["name"],
        "strand" => $s["strand"], "status" => $s["status"], "bannedUntil" => iso($s["banned_until"]),
    );
}
function open_borrows($studentId) {
    $st = db()->prepare(
        "SELECT t.*, tl.name AS tool_name, tl.rfid_tag, lk.name AS locker_name
         FROM transactions t
         JOIN tools tl ON tl.id = t.tool_id
         LEFT JOIN lockers lk ON lk.id = t.locker_id
         WHERE t.student_id = ? AND t.return_time IS NULL ORDER BY t.borrow_time");
    $st->execute(array($studentId));
    $out = array();
    foreach ($st->fetchAll() as $r) {
        $out[] = array(
            "txId" => (string)$r["id"], "toolId" => (string)$r["tool_id"], "tool" => $r["tool_name"],
            "rfidTag" => $r["rfid_tag"],
            "lockerId" => $r["locker_id"] === null ? "" : (string)$r["locker_id"],
            "locker" => $r["locker_name"] !== null ? $r["locker_name"] : "—",
            "borrowTime" => iso($r["borrow_time"]), "expectedReturn" => iso($r["expected_return"]),
            "status" => $r["status"],
        );
    }
    return $out;
}

/* Perform a borrow at a given time (shared by live borrow + offline sync). */
function do_borrow($s, $toolId, $qty, $when = null) {
    $pdo = db();
    $elig = student_eligibility($s);
    if (!$elig["can_borrow"]) throw new ApiError($elig["reason"], 403, "not_eligible");

    $st = $pdo->prepare("SELECT * FROM tools WHERE id = ?");
    $st->execute(array($toolId));
    $tool = $st->fetch();
    if (!$tool) throw new ApiError("Tool not found", 404, "unknown_tool");
    if ($tool["status"] !== "available") {
        throw new ApiError("Tool '" . $tool["name"] . "' is not available (" . $tool["status"] . ")", 409, "unavailable");
    }

    $limit = (int)setting("borrow_limit_hours", "8");
    $borrowAt = $when !== null ? $when : date("Y-m-d H:i:s");
    $expected = date("Y-m-d H:i:s", strtotime($borrowAt) + $limit * 3600);

    $pdo->prepare("INSERT INTO transactions (student_id, tool_id, locker_id, qty, borrow_time, expected_return, status)
                   VALUES (?,?,?,?,?,?, 'borrowed')")
        ->execute(array($s["id"], $toolId, $tool["locker_id"], $qty, $borrowAt, $expected));
    $txId = (int)$pdo->lastInsertId();

    $pdo->prepare("UPDATE tools SET status='borrowed' WHERE id=?")->execute(array($toolId));
    if ($tool["locker_id"] !== null) {
        $pdo->prepare("UPDATE lockers SET occupancy='removed', led='red', last_seen=NOW() WHERE id=?")
            ->execute(array($tool["locker_id"]));
    }
    return array(
        "txId" => (string)$txId, "tool" => $tool["name"],
        "lockerId" => $tool["locker_id"] === null ? "" : (string)$tool["locker_id"],
        "borrowTime" => iso($borrowAt), "expectedReturn" => iso($expected),
        "openLocker" => true,   // firmware: OPEN_LOCKER command target
    );
}

/* Perform a return at a given time (shared by live return + offline sync). */
function do_return($s, $toolId, $when = null) {
    $pdo = db();
    $st = $pdo->prepare("SELECT * FROM transactions
                         WHERE student_id=? AND tool_id=? AND return_time IS NULL
                         ORDER BY borrow_time LIMIT 1");
    $st->execute(array($s["id"], $toolId));
    $tx = $st->fetch();
    if (!$tx) throw new ApiError("No open borrow found for this student and tool", 404, "no_open_tx");

    $returnAt = $when !== null ? $when : date("Y-m-d H:i:s");
    $pdo->prepare("UPDATE transactions SET return_time=?, status='returned' WHERE id=?")
        ->execute(array($returnAt, $tx["id"]));
    $pdo->prepare("UPDATE tools SET status='available' WHERE id=?")->execute(array($toolId));
    if ($tx["locker_id"] !== null) {
        $pdo->prepare("UPDATE lockers SET occupancy='present', led='green', last_seen=NOW() WHERE id=?")
            ->execute(array($tx["locker_id"]));
    }
    $late = strtotime($returnAt) > strtotime($tx["expected_return"]);
    return array(
        "txId" => (string)$tx["id"],
        "lockerId" => $tx["locker_id"] === null ? "" : (string)$tx["locker_id"],
        "returnTime" => iso($returnAt), "late" => $late,
        "openLocker" => true,
    );
}

/* ---- actions -------------------------------------------------------------- */
try {
    switch ($action) {

        case "verify_student":
            $s = find_student_by_qr(trim((string)req($b, "qr")));
            ok(array(
                "student" => student_payload($s),
                "eligibility" => student_eligibility($s),
                "borrowed" => open_borrows((int)$s["id"]),
                "terms" => (string)setting("terms", ""),
                "borrowLimitHours" => (int)setting("borrow_limit_hours", "8"),
            ));
            break;

        case "state":
            $rows = $pdo->query(
                "SELECT lk.id, lk.name, lk.sensor, lk.occupancy, lk.led,
                        tl.id AS tool_id, tl.name AS tool_name, tl.rfid_tag, tl.status AS tool_status
                 FROM lockers lk LEFT JOIN tools tl ON tl.id = lk.tool_id ORDER BY lk.id")->fetchAll();
            $lockers = array();
            foreach ($rows as $r) {
                $lockers[] = array(
                    "id" => (string)$r["id"], "name" => $r["name"], "sensor" => $r["sensor"],
                    "occupancy" => $r["occupancy"], "led" => $r["led"],
                    "toolId" => $r["tool_id"] === null ? "" : (string)$r["tool_id"],
                    "tool" => $r["tool_name"], "rfidTag" => $r["rfid_tag"],
                    "toolStatus" => $r["tool_status"],
                );
            }
            ok(array("lockers" => $lockers));
            break;

        case "borrow":
            $s = find_student_by_qr(trim((string)req($b, "qr")));
            $qty = max(1, (int)req($b, "qty", 1));
            $result = do_borrow($s, (int)req($b, "tool_id", 0), $qty);
            ok(array("result" => $result));
            break;

        case "return":
            $s = find_student_by_qr(trim((string)req($b, "qr")));
            $result = do_return($s, (int)req($b, "tool_id", 0));
            ok(array("result" => $result));
            break;

        case "locker_status":
            $id = (int)req($b, "locker_id", 0);
            $st = $pdo->prepare("SELECT * FROM lockers WHERE id=?");
            $st->execute(array($id));
            if (!$st->fetch()) fail("Locker not found", 404, "unknown_locker");
            $fields = array("last_seen = NOW()");
            $vals = array();
            $allowed = array(
                "sensor" => array("online", "offline"),
                "occupancy" => array("present", "removed"),
                "led" => array("green", "red", "off"),
            );
            foreach ($allowed as $f => $opts) {
                if (isset($b[$f]) && in_array($b[$f], $opts, true)) { $fields[] = "$f = ?"; $vals[] = $b[$f]; }
            }
            $vals[] = $id;
            $pdo->prepare("UPDATE lockers SET " . implode(", ", $fields) . " WHERE id=?")->execute($vals);
            ok();
            break;

        case "sync":
            // Batch upload of transactions recorded while the terminal was offline.
            // events: [{type: borrow|return, qr, tool_id, timestamp}]
            $events = is_array(req($b, "events", null)) ? $b["events"] : array();
            $results = array();
            foreach ($events as $i => $ev) {
                try {
                    $s = find_student_by_qr(trim((string)req($ev, "qr")));
                    $when = isset($ev["timestamp"]) ? date("Y-m-d H:i:s", strtotime((string)$ev["timestamp"])) : null;
                    $r = req($ev, "type") === "return"
                        ? do_return($s, (int)req($ev, "tool_id", 0), $when)
                        : do_borrow($s, (int)req($ev, "tool_id", 0), 1, $when);
                    $results[] = array("index" => $i, "ok" => true, "result" => $r);
                } catch (Exception $e) {
                    $results[] = array("index" => $i, "ok" => false, "error" => $e->getMessage());
                }
            }
            ok(array("results" => $results));
            break;

        default:
            fail("Unknown action", 400);
    }
} catch (ApiError $e) {
    fail($e->getMessage(), $e->http, $e->errCode);
}

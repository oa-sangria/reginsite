<?php
/* ============================================================================
   reginsite — API core: DB connection, session, helpers, business rules.
   NOTE: written for PHP 5.6 (this XAMPP ships php5_module) — no ??, no
   scalar type hints. Keep it that way until XAMPP is upgraded.
   ========================================================================== */

define("DB_HOST", "127.0.0.1");
define("DB_NAME", "reginsite");
define("DB_USER", "root");
define("DB_PASS", "");
define("DEVICE_API_KEY", "regin-esp32-2026");   // ESP32 sends this as X-API-Key

date_default_timezone_set("Asia/Manila");

/* ---- Responses ---------------------------------------------------------- */
function json_out($data, $code = 200) {
    http_response_code($code);
    header("Content-Type: application/json; charset=utf-8");
    echo json_encode($data);
    exit;
}
function ok($data = array()) {
    json_out(array_merge(array("ok" => true), is_array($data) ? $data : array("data" => $data)));
}
function fail($msg, $code = 400, $errCode = "") {
    json_out(array("ok" => false, "error" => $msg, "code" => $errCode !== "" ? $errCode : (string)$code), $code);
}

/* Domain errors thrown by shared helpers (catchable, unlike fail()). */
class ApiError extends RuntimeException {
    public $http = 400;
    public $errCode = "";
    public function __construct($msg, $http = 400, $errCode = "") {
        parent::__construct($msg);
        $this->http = $http;
        $this->errCode = $errCode;
    }
}

/* ---- DB ------------------------------------------------------------------ */
function db() {
    static $pdo = null;
    if ($pdo === null) {
        try {
            $pdo = new PDO(
                "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4",
                DB_USER, DB_PASS,
                array(PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
                      PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC)
            );
        } catch (PDOException $e) {
            // 1049 = unknown database -> installer has not been run yet
            if ((int)$e->getCode() === 1049 || strpos($e->getMessage(), "1049") !== false) {
                fail("Database not installed. Run api/install.php first.", 500, "no_db");
            }
            fail("Database connection failed: " . $e->getMessage(), 500, "db_error");
        }
    }
    return $pdo;
}

/* ---- Request helpers ----------------------------------------------------- */
function body() {
    $raw = file_get_contents("php://input");
    $j = json_decode($raw ? $raw : "null", true);
    return is_array($j) ? $j : $_POST;
}
function req($arr, $key, $default = "") {
    return isset($arr[$key]) ? $arr[$key] : $default;
}
function setting($name, $default = null) {
    $st = db()->prepare("SELECT value FROM settings WHERE name = ?");
    $st->execute(array($name));
    $v = $st->fetchColumn();
    return $v === false ? $default : $v;
}
function set_setting($name, $value) {
    db()->prepare("INSERT INTO settings (name, value) VALUES (?, ?)
                   ON DUPLICATE KEY UPDATE value = VALUES(value)")->execute(array($name, $value));
}

/* ---- Auth ---------------------------------------------------------------- */
function boot_session() {
    if (session_status() === PHP_SESSION_NONE) {
        session_name("REGINSITE");
        session_start();
    }
}
function require_admin() {
    boot_session();
    if (empty($_SESSION["admin"])) fail("Not authenticated", 401, "unauthorized");
    return $_SESSION["admin"];
}
function require_device() {
    $key = isset($_SERVER["HTTP_X_API_KEY"]) ? $_SERVER["HTTP_X_API_KEY"] : "";
    if (!hash_equals(DEVICE_API_KEY, $key)) fail("Invalid or missing X-API-Key", 401, "bad_key");
}

/* ---- Business rules maintenance ------------------------------------------
   Called before reads/device actions so state is always current:
   1. Active borrows past expected_return  -> status 'overdue'
   2. Overdue >= ban_trigger_days          -> create ban + mark student banned
   3. Bans past ban_until                  -> lift (student back to active)
--------------------------------------------------------------------------- */
function run_maintenance() {
    $pdo = db();
    $trigger = (int)setting("ban_trigger_days", "2");
    $length  = (int)setting("ban_length_days", "2");

    // 1) Flag overdue
    $pdo->exec("UPDATE transactions SET status='overdue'
                WHERE return_time IS NULL AND expected_return < NOW() AND status <> 'overdue'");

    // 2) Auto-ban: overdue >= trigger days, no ban yet for that transaction
    $rows = $pdo->query(
        "SELECT t.id, t.student_id FROM transactions t
         LEFT JOIN bans b ON b.transaction_id = t.id
         WHERE t.return_time IS NULL AND b.id IS NULL
           AND t.expected_return < DATE_SUB(NOW(), INTERVAL {$trigger} DAY)"
    )->fetchAll();
    foreach ($rows as $r) {
        $pdo->prepare("INSERT INTO bans (student_id, transaction_id, reason, ban_from, ban_until)
                       VALUES (?, ?, ?, NOW(), DATE_ADD(NOW(), INTERVAL {$length} DAY))")
            ->execute(array($r["student_id"], $r["id"], "Overdue tool exceeded {$trigger} days"));
        $pdo->prepare("UPDATE students SET status='banned',
                       banned_until=DATE_ADD(NOW(), INTERVAL {$length} DAY) WHERE id=?")
            ->execute(array($r["student_id"]));
    }

    // 3) Lift expired bans
    $pdo->exec("UPDATE students SET status='active', banned_until=NULL
                WHERE status='banned' AND banned_until IS NOT NULL AND banned_until < NOW()");
}

/* ---- Shared domain helpers ------------------------------------------------ */
function iso($sqlDt) {
    if ($sqlDt === null || $sqlDt === "") return null;
    return str_replace(" ", "T", $sqlDt);
}

/* Eligibility per the workflow: banned OR has an overdue item -> cannot borrow. */
function student_eligibility($student) {
    $st = db()->prepare("SELECT COUNT(*) FROM transactions
                         WHERE student_id=? AND return_time IS NULL AND status='overdue'");
    $st->execute(array($student["id"]));
    $overdue = (int)$st->fetchColumn();

    if ($student["status"] === "banned") {
        $until = $student["banned_until"] !== null ? $student["banned_until"] : "—";
        return array("can_borrow" => false, "reason" => "Student is banned until " . $until);
    }
    if ($overdue > 0) {
        return array("can_borrow" => false, "reason" => "Has {$overdue} overdue item(s) — must return first");
    }
    return array("can_borrow" => true, "reason" => "Clear");
}

<?php
/* ============================================================================
   reginsite — Installer. Creates the database, tables, and demo seed data.
   Open in browser:  http://localhost/reginsite/api/install.php
   Re-run fresh:     http://localhost/reginsite/api/install.php?fresh=1
   (PHP 5.6 compatible)
   ========================================================================== */
require_once __DIR__ . "/config.php";

header("Content-Type: text/html; charset=utf-8");
$fresh = isset($_GET["fresh"]);

try {
    // Connect WITHOUT selecting the db so we can create it.
    $pdo = new PDO("mysql:host=" . DB_HOST . ";charset=utf8mb4", DB_USER, DB_PASS,
        array(PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION));

    if ($fresh) $pdo->exec("DROP DATABASE IF EXISTS `" . DB_NAME . "`");
    $pdo->exec("CREATE DATABASE IF NOT EXISTS `" . DB_NAME . "` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");
    $pdo->exec("USE `" . DB_NAME . "`");

    /* ---- Schema ---------------------------------------------------------- */
    $pdo->exec("
    CREATE TABLE IF NOT EXISTS users (
      id INT AUTO_INCREMENT PRIMARY KEY,
      username VARCHAR(50) NOT NULL UNIQUE,
      password_hash VARCHAR(255) NOT NULL,
      name VARCHAR(100) NOT NULL DEFAULT 'Administrator'
    ) ENGINE=InnoDB;

    CREATE TABLE IF NOT EXISTS students (
      id INT AUTO_INCREMENT PRIMARY KEY,
      student_no VARCHAR(30) NOT NULL,
      name VARCHAR(120) NOT NULL,
      strand VARCHAR(40) NOT NULL DEFAULT '',
      qr_code VARCHAR(60) NOT NULL UNIQUE,
      status ENUM('active','banned') NOT NULL DEFAULT 'active',
      banned_until DATETIME NULL
    ) ENGINE=InnoDB;

    CREATE TABLE IF NOT EXISTS lockers (
      id INT AUTO_INCREMENT PRIMARY KEY,
      name VARCHAR(40) NOT NULL,
      tool_id INT NULL,
      sensor ENUM('online','offline') NOT NULL DEFAULT 'online',
      occupancy ENUM('present','removed') NOT NULL DEFAULT 'present',
      led ENUM('green','red','off') NOT NULL DEFAULT 'green',
      last_seen DATETIME NULL
    ) ENGINE=InnoDB;

    CREATE TABLE IF NOT EXISTS tools (
      id INT AUTO_INCREMENT PRIMARY KEY,
      name VARCHAR(120) NOT NULL,
      rfid_tag VARCHAR(60) NOT NULL DEFAULT '',
      locker_id INT NULL,
      status ENUM('available','borrowed','maintenance') NOT NULL DEFAULT 'available',
      FOREIGN KEY (locker_id) REFERENCES lockers(id) ON DELETE SET NULL
    ) ENGINE=InnoDB;

    CREATE TABLE IF NOT EXISTS transactions (
      id INT AUTO_INCREMENT PRIMARY KEY,
      student_id INT NOT NULL,
      tool_id INT NOT NULL,
      locker_id INT NULL,
      qty INT NOT NULL DEFAULT 1,
      borrow_time DATETIME NOT NULL,
      expected_return DATETIME NOT NULL,
      return_time DATETIME NULL,
      status ENUM('borrowed','overdue','returned') NOT NULL DEFAULT 'borrowed',
      FOREIGN KEY (student_id) REFERENCES students(id) ON DELETE CASCADE,
      FOREIGN KEY (tool_id) REFERENCES tools(id) ON DELETE CASCADE
    ) ENGINE=InnoDB;

    CREATE TABLE IF NOT EXISTS bans (
      id INT AUTO_INCREMENT PRIMARY KEY,
      student_id INT NOT NULL,
      transaction_id INT NULL,
      reason VARCHAR(200) NOT NULL DEFAULT '',
      ban_from DATETIME NOT NULL,
      ban_until DATETIME NOT NULL,
      FOREIGN KEY (student_id) REFERENCES students(id) ON DELETE CASCADE
    ) ENGINE=InnoDB;

    CREATE TABLE IF NOT EXISTS settings (
      name VARCHAR(60) PRIMARY KEY,
      value TEXT NOT NULL
    ) ENGINE=InnoDB;
    ");

    /* ---- Seed (only when empty) ------------------------------------------ */
    $already = (int)$pdo->query("SELECT COUNT(*) FROM users")->fetchColumn() > 0;
    if (!$already) {
        // Admin login
        $pdo->prepare("INSERT INTO users (username, password_hash, name) VALUES (?,?,?)")
            ->execute(array("admin", password_hash("admin", PASSWORD_DEFAULT), "Administrator"));

        // Settings
        $terms = "TERMS & CONDITIONS — Tool Borrowing\n\n"
          . "1. Tools must be returned within 8 HOURS of borrowing. A reminder is issued at the 8-hour mark.\n"
          . "2. You may not borrow another tool while you have an OVERDUE or unreturned item — return it first.\n"
          . "3. Tools left overdue for 2 days or more will result in a 2-DAY BORROWING BAN.\n"
          . "4. Inspect the tool before removing it. Report any damage immediately to the laboratory custodian.\n"
          . "5. Return the tool to its ASSIGNED locker and scan its RFID tag to complete the return.\n"
          . "6. You are responsible for any loss or damage to the borrowed tool.\n"
          . "7. Lost RFID tags or keychains must be reported within 24 hours.\n\n"
          . "By selecting BORROW you agree to these Terms & Conditions.";
        $set = $pdo->prepare("INSERT INTO settings (name, value) VALUES (?,?)");
        $settings = array(
            array("terms", $terms),
            array("borrow_limit_hours", "8"),
            array("ban_trigger_days", "2"),
            array("ban_length_days", "2"),
        );
        foreach ($settings as $s) $set->execute($s);

        // Students
        $stu = $pdo->prepare("INSERT INTO students (student_no, name, strand, qr_code, status, banned_until) VALUES (?,?,?,?,?,?)");
        $students = array(
            array("2026-0457", "Regina Reyes",     "STEM",    "QR-2026-0457", "banned", date("Y-m-d H:i:s", strtotime("+1 day"))),
            array("2026-0132", "Mark Santos",      "TVL-ICT", "QR-2026-0132", "active", null),
            array("2026-0890", "Andrea Cruz",      "ABM",     "QR-2026-0890", "active", null),
            array("2026-0223", "Joshua Dela Cruz", "STEM",    "QR-2026-0223", "active", null),
            array("2026-0775", "Bea Mendoza",      "HUMSS",   "QR-2026-0775", "active", null),
            array("2026-0319", "Carlo Aquino",     "TVL-ICT", "QR-2026-0319", "active", null),
            array("2026-0641", "Nina Villanueva",  "GAS",     "QR-2026-0641", "active", null),
            array("2026-0508", "Paolo Ramirez",    "TVL-HE",  "QR-2026-0508", "active", null),
        );
        foreach ($students as $s) $stu->execute($s);

        // Lockers (tool_id linked after tools exist)
        $lk = $pdo->prepare("INSERT INTO lockers (name, sensor, occupancy, led, last_seen) VALUES (?,?,?,?,NOW())");
        $lockers = array(
            array("Locker 1",  "online",  "removed", "red"),
            array("Locker 2",  "online",  "present", "green"),
            array("Locker 3",  "offline", "present", "off"),
            array("Locker 4",  "online",  "present", "green"),
            array("Locker 5",  "online",  "removed", "red"),
            array("Locker 6",  "online",  "removed", "red"),
            array("Locker 7",  "online",  "present", "green"),
            array("Locker 8",  "online",  "removed", "red"),
            array("Locker 9",  "online",  "present", "off"),
            array("Locker 10", "online",  "present", "green"),
        );
        foreach ($lockers as $l) $lk->execute($l);

        // Tools (locker i+1)
        $tl = $pdo->prepare("INSERT INTO tools (name, rfid_tag, locker_id, status) VALUES (?,?,?,?)");
        $tools = array(
            array("Long-nose Plier",    "RFID-A1",  1,  "borrowed"),
            array("Combination Plier",  "RFID-A2",  2,  "available"),
            array("Vernier Caliper",    "RFID-A3",  3,  "available"),
            array("Screwdriver Set",    "RFID-A4",  4,  "available"),
            array("Digital Multimeter", "RFID-A5",  5,  "borrowed"),
            array("Soldering Iron",     "RFID-A6",  6,  "borrowed"),
            array("Wire Stripper",      "RFID-A7",  7,  "available"),
            array("Claw Hammer",        "RFID-A8",  8,  "borrowed"),
            array("Adjustable Wrench",  "RFID-A9",  9,  "maintenance"),
            array("Hex Key Set",        "RFID-A10", 10, "available"),
        );
        foreach ($tools as $t) $tl->execute($t);
        $pdo->exec("UPDATE lockers SET tool_id = id");   // 1:1 mapping in seed

        // Transactions — times relative to now, expected = borrow + 8h
        $txStmt = $pdo->prepare(
          "INSERT INTO transactions (student_id, tool_id, locker_id, qty, borrow_time, expected_return, return_time, status)
           VALUES (?,?,?,?,?,?,?,?)");
        $H = 3600;
        $mk = function ($sid, $tid, $lid, $borrowAgoSec, $returnAgoSec) use ($txStmt, $H) {
            $borrow = time() - $borrowAgoSec;
            $expected = $borrow + 8 * $H;
            $return = $returnAgoSec === null ? null : time() - $returnAgoSec;
            $status = $return !== null ? "returned" : (time() > $expected ? "overdue" : "borrowed");
            $txStmt->execute(array($sid, $tid, $lid, 1,
                date("Y-m-d H:i:s", $borrow), date("Y-m-d H:i:s", $expected),
                $return === null ? null : date("Y-m-d H:i:s", $return), $status));
        };
        // Active
        $mk(1, 1, 1, 3 * 24 * $H, null);      // Regina — overdue 3 days (banned)
        $mk(2, 5, 5, 2 * $H, null);           // Mark — on time
        $mk(6, 6, 6, 9 * $H, null);           // Carlo — overdue (>8h)
        $mk(4, 8, 8, 1 * $H, null);           // Joshua — on time
        // Returned today
        $mk(3, 3, 3, 28 * $H, 5 * $H);
        $mk(5, 4, 4, 2 * 24 * $H, 4 * $H);
        $mk(7, 7, 7, 5 * $H, 1 * $H);
        $mk(4, 4, 4, 12 * $H, 3 * $H);
        // Older history
        $mk(8, 2, 2, 4 * 24 * $H, 3 * 24 * $H);
        $mk(2, 9, 9, 6 * 24 * $H, 5 * 24 * $H);
        $mk(3, 10, 10, 10 * 24 * $H, 9 * 24 * $H);

        // Regina's ban (transaction #1)
        $pdo->prepare("INSERT INTO bans (student_id, transaction_id, reason, ban_from, ban_until) VALUES (?,?,?,?,?)")
            ->execute(array(1, 1, "Overdue tool exceeded 2 days",
                date("Y-m-d H:i:s", strtotime("-1 day")), date("Y-m-d H:i:s", strtotime("+1 day"))));
    }

    $msg = $already
        ? "Database already installed — nothing changed. Add <code>?fresh=1</code> to wipe and reseed."
        : "Database <b>" . DB_NAME . "</b> created and seeded with demo data.";
    echo "<!DOCTYPE html><html><head><title>reginsite install</title></head>
      <body style='font-family:system-ui,Segoe UI,Arial;padding:40px;max-width:640px;margin:auto'>
      <h2>&#9989; Install " . ($already ? "check" : "complete") . "</h2>
      <p>$msg</p>
      <p>Login: <code>admin</code> / <code>admin</code> &nbsp;&middot;&nbsp; Device API key: <code>" . DEVICE_API_KEY . "</code></p>
      <p><a href='../index.html'>&rarr; Open the admin site</a> &nbsp;|&nbsp; <a href='../terminal.html'>&rarr; Open the locker terminal simulator</a></p>
      </body></html>";
} catch (Exception $e) {
    http_response_code(500);
    echo "<h2>Install failed</h2><pre>" . htmlspecialchars($e->getMessage()) . "</pre>";
}

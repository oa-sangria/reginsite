<?php
/* reginsite — Admin auth: login / logout / me  (PHP 5.6 compatible) */
require_once __DIR__ . "/config.php";
boot_session();

$b = body();
$action = isset($_GET["action"]) ? $_GET["action"] : req($b, "action");

switch ($action) {
    case "login":
        $user = trim((string)req($b, "username"));
        $pass = (string)req($b, "password");
        $st = db()->prepare("SELECT * FROM users WHERE username = ?");
        $st->execute(array($user));
        $row = $st->fetch();
        if (!$row || !password_verify($pass, $row["password_hash"])) {
            fail("Invalid username or password", 401, "bad_login");
        }
        $_SESSION["admin"] = array("id" => (int)$row["id"], "username" => $row["username"], "name" => $row["name"]);
        ok(array("user" => $_SESSION["admin"]));
        break;

    case "logout":
        $_SESSION = array();
        session_destroy();
        ok();
        break;

    case "me":
        ok(array("user" => isset($_SESSION["admin"]) ? $_SESSION["admin"] : null));
        break;

    default:
        fail("Unknown action", 400);
}

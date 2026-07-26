<?php
/* reginsite — Terms & Conditions save (admin, PHP 5.6 compatible) */
require_once __DIR__ . "/config.php";
require_admin();

$b = body();
if (req($b, "action") !== "save") fail("Unknown action", 400);

set_setting("terms", (string)req($b, "terms"));
ok();

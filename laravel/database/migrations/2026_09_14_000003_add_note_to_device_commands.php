<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

/*
 * device_commands.status grows two terminal states beyond 'done':
 *   timeout — the door re-locked with no tag scanned (or the cabinet isn't on
 *             this controller, or the bridge gave up waiting)
 *   failed  — a tag WAS scanned but the server rejected it (wrong locker's
 *             tool, tool not available, tag unknown)
 * `note` carries the human-readable reason so the kiosk can say why instead
 * of sitting on "Take your tool" forever.
 */
return new class extends Migration
{
    public function up()
    {
        Schema::table('device_commands', function (Blueprint $table) {
            $table->string('note', 160)->nullable()->after('status');
        });
    }

    public function down()
    {
        Schema::table('device_commands', function (Blueprint $table) {
            $table->dropColumn('note');
        });
    }
};

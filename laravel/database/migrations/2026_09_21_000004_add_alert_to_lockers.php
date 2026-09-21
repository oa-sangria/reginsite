<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

/*
 * Untagged removals. The Mega keeps sampling every slot of an open cabinet
 * (and for a while after it relocks); a slot other than the one on the DONE
 * line moving means a tool left with no tag scan — the borrow the sensors
 * exist to catch. The bridge relays that as ALERT/CLEAR and the server keeps
 * it here so the dashboard and the kiosk can show it:
 *   alert_slots — comma list of slots currently out, e.g. "2" or "2,3"
 *   alert       — the human line ("Slot 2 moved without a tag scan · Bene…")
 *   alert_at    — when the first of them was raised
 * All null once every slot is back (or staff clear it from the locker form).
 */
return new class extends Migration
{
    public function up()
    {
        Schema::table('lockers', function (Blueprint $table) {
            $table->string('alert_slots', 40)->nullable()->after('led');
            $table->string('alert', 160)->nullable()->after('alert_slots');
            $table->dateTime('alert_at')->nullable()->after('alert');
        });
    }

    public function down()
    {
        Schema::table('lockers', function (Blueprint $table) {
            $table->dropColumn(['alert_slots', 'alert', 'alert_at']);
        });
    }
};

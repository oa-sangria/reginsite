<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

/*
 * Screen-driven upgrade:
 *  - students gain program + major (parsed from the QR ID), for the
 *    "Bachelor of Industrial Technology" eligibility gate.
 *  - device_commands: a tiny queue the mini-PC bridge polls so the server can
 *    tell the Arduino to OPEN a specific locker (borrow/return).
 */
return new class extends Migration
{
    public function up()
    {
        Schema::table('students', function (Blueprint $table) {
            $table->string('program', 160)->nullable()->after('strand');
            $table->string('major', 120)->nullable()->after('program');
        });

        Schema::create('device_commands', function (Blueprint $table) {
            $table->id();
            $table->string('type', 20)->default('open');          // open
            $table->unsignedBigInteger('locker_id')->nullable();
            $table->string('mode', 10)->nullable();               // borrow | return
            $table->unsignedBigInteger('tool_id')->nullable();
            $table->unsignedBigInteger('student_id')->nullable();
            $table->unsignedBigInteger('transaction_id')->nullable();
            $table->string('status', 10)->default('pending');     // pending | sent | done
            $table->timestamps();
        });
    }

    public function down()
    {
        Schema::dropIfExists('device_commands');
        Schema::table('students', function (Blueprint $table) {
            $table->dropColumn(['program', 'major']);
        });
    }
};

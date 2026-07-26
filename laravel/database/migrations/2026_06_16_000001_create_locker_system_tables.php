<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

/* Smart tool-locker domain tables. Mirrors the plain-PHP prototype schema. */
return new class extends Migration
{
    public function up()
    {
        Schema::create('students', function (Blueprint $table) {
            $table->id();
            $table->string('student_no', 30);
            $table->string('name', 120);
            $table->string('strand', 40)->default('');
            $table->string('qr_code', 60)->unique();
            $table->enum('status', ['active', 'banned'])->default('active');
            $table->dateTime('banned_until')->nullable();
            $table->timestamps();
        });

        Schema::create('lockers', function (Blueprint $table) {
            $table->id();
            $table->string('name', 40);
            $table->unsignedBigInteger('tool_id')->nullable();
            $table->enum('sensor', ['online', 'offline'])->default('online');
            $table->enum('occupancy', ['present', 'removed'])->default('present');
            $table->enum('led', ['green', 'red', 'off'])->default('green');
            $table->dateTime('last_seen')->nullable();
            $table->timestamps();
        });

        Schema::create('tools', function (Blueprint $table) {
            $table->id();
            $table->string('name', 120);
            $table->string('rfid_tag', 60)->default('');
            $table->foreignId('locker_id')->nullable()->constrained('lockers')->nullOnDelete();
            $table->enum('status', ['available', 'borrowed', 'maintenance'])->default('available');
            $table->timestamps();
        });

        Schema::create('transactions', function (Blueprint $table) {
            $table->id();
            $table->foreignId('student_id')->constrained('students')->cascadeOnDelete();
            $table->foreignId('tool_id')->constrained('tools')->cascadeOnDelete();
            $table->unsignedBigInteger('locker_id')->nullable();
            $table->unsignedInteger('qty')->default(1);
            $table->dateTime('borrow_time');
            $table->dateTime('expected_return');
            $table->dateTime('return_time')->nullable();
            $table->enum('status', ['borrowed', 'overdue', 'returned'])->default('borrowed');
            $table->timestamps();
        });

        Schema::create('bans', function (Blueprint $table) {
            $table->id();
            $table->foreignId('student_id')->constrained('students')->cascadeOnDelete();
            $table->foreignId('transaction_id')->nullable()->constrained('transactions')->nullOnDelete();
            $table->string('reason', 200)->default('');
            $table->dateTime('ban_from');
            $table->dateTime('ban_until');
            $table->timestamps();
        });

        Schema::create('settings', function (Blueprint $table) {
            $table->string('name', 60)->primary();
            $table->text('value');
        });
    }

    public function down()
    {
        Schema::dropIfExists('settings');
        Schema::dropIfExists('bans');
        Schema::dropIfExists('transactions');
        Schema::dropIfExists('tools');
        Schema::dropIfExists('lockers');
        Schema::dropIfExists('students');
    }
};

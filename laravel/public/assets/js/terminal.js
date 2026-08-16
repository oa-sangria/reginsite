/* ============================================================================
   reginsite — Locker Terminal (touchscreen kiosk)  ·  SCREEN-DRIVEN
   Runs on the mini PC's touchscreen. The USB QR scanner (SM8070) is a
   keyboard-wedge: it "types" the student ID text, which we capture below.
   Talks to /api/esp32/* — the same API the Arduino bridge uses.
   ========================================================================== */
(function () {
  "use strict";

  var API = "/api/esp32";
  var API_KEY = "regin-esp32-2026";   // matches DEVICE_API_KEY in laravel/.env
  var S = {};                          // current session
  var pollTimer = null;

  function call(path, payload, method) {
    return fetch(API + "/" + path, {
      method: method || "POST",
      headers: { "Content-Type": "application/json", "X-API-Key": API_KEY },
      body: method === "GET" ? undefined : JSON.stringify(payload || {})
    }).then(function (r) {
      return r.json().then(function (j) {
        if (j.ok === false && path !== "verify-student") {
          var e = new Error(j.error || "Request failed"); e.data = j; throw e;
        }
        return j;
      });
    });
  }

  function el(id) { return document.getElementById(id); }
  function esc(s) {
    return String(s == null ? "" : s).replace(/&/g, "&amp;").replace(/</g, "&lt;")
      .replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  }
  function stage(html) { el("stage").innerHTML = html; }
  function stopPoll() { if (pollTimer) { clearInterval(pollTimer); pollTimer = null; } }

  setInterval(function () { var c = el("termClock"); if (c) c.textContent = new Date().toLocaleTimeString(); }, 1000);

  /* ---- QR keyboard-wedge capture ---------------------------------------- */
  // Scanners type fast and finish with Enter; multiline QR arrives as several
  // lines. We buffer keystrokes and flush after a short idle gap.
  var qrBuf = "", qrTimer = null;
  document.addEventListener("keydown", function (e) {
    if (!S.idle) return;                 // only capture on the idle screen
    if (e.key === "Enter") { qrBuf += "\n"; }
    else if (e.key.length === 1) { qrBuf += e.key; }
    else { return; }
    clearTimeout(qrTimer);
    qrTimer = setTimeout(function () {
      var scan = qrBuf.trim(); qrBuf = "";
      if (scan) doScan(scan);
    }, 150);
  });

  /* ---- Idle -------------------------------------------------------------- */
  function screenIdle() {
    S = { idle: true };
    stopPoll();
    stage(
      '<div class="term-center">' +
        '<div class="term-big-ic">▣</div>' +
        "<h1>Scan your Student ID</h1>" +
        '<p class="term-sub">Hold your ID\'s QR code up to the scanner</p>' +
        '<div class="term-scanrow">' +
          '<input id="qrInput" class="term-input" placeholder="…or type a Student No. and press Enter" />' +
          '<button class="term-btn primary" id="scanBtn">Go</button>' +
        "</div>" +
        '<div class="term-demo">Demo IDs: ' +
          ["2023101132", "2023102906", "2023106548"].map(function (q) {
            return '<button class="term-chip" data-qr="' + q + '">' + q + "</button>";
          }).join("") +
        "</div>" +
      "</div>");
    el("scanBtn").addEventListener("click", function () { var v = el("qrInput").value.trim(); if (v) doScan(v); });
    el("qrInput").addEventListener("keydown", function (e) {
      if (e.key === "Enter") { e.stopPropagation(); var v = el("qrInput").value.trim(); if (v) doScan(v); }
    });
    Array.prototype.forEach.call(document.querySelectorAll(".term-chip"), function (b) {
      b.addEventListener("click", function () { doScan(b.getAttribute("data-qr")); });
    });
  }

  function doScan(qr) {
    S.idle = false;
    stage('<div class="term-center"><div class="term-spin"></div><p>Reading ID…</p></div>');
    call("verify-student", { qr: qr }).then(function (j) {
      if (!j.ok) { screenError("Not eligible", j.error, screenIdle); return; }
      S.student = j.student; S.eligibility = j.eligibility;
      S.borrowed = j.borrowed; S.available = j.availableByLocker; S.terms = j.terms;
      screenTerms();
    })["catch"](function (e) { screenError("Scan failed", e.message, screenIdle); });
  }

  /* ---- Terms ------------------------------------------------------------- */
  function screenTerms() {
    stage(
      '<div class="term-panel">' +
        "<h2>Terms &amp; Conditions</h2>" +
        '<pre class="term-terms">' + esc(S.terms) + "</pre>" +
        '<div class="term-actions">' +
          '<button class="term-btn ghost" id="declineBtn">Decline</button>' +
          '<button class="term-btn primary" id="agreeBtn">I Agree</button>' +
        "</div></div>");
    el("declineBtn").addEventListener("click", screenIdle);
    el("agreeBtn").addEventListener("click", screenHome);
  }

  /* ---- Home -------------------------------------------------------------- */
  function screenHome() {
    var s = S.student, elig = S.eligibility;
    var block = elig.can_borrow ? "" : '<div class="term-note danger">⛔ ' + esc(elig.reason) + "</div>";
    var borrowed = S.borrowed.length
      ? S.borrowed.map(function (b) {
          return '<div class="term-item"><span>' + esc(b.tool) + "</span><span class='" +
            (b.status === "overdue" ? "term-late" : "term-ok") + "'>" +
            (b.status === "overdue" ? "OVERDUE" : "on loan") + "</span></div>"; }).join("")
      : '<div class="term-item muted">No tools currently borrowed</div>';
    stage(
      '<div class="term-panel">' +
        '<div class="term-student"><div class="term-avatar">' + esc((s.name || "?").charAt(0)) + "</div>" +
          "<div><h2>" + esc(s.name) + "</h2><p class='term-sub'>" + esc(s.studentId) +
          " · " + esc(s.major || s.program || "") + "</p></div></div>" +
        block +
        "<h3 class='term-h3'>Your borrowed tools</h3>" + borrowed +
        '<div class="term-actions big">' +
          '<button class="term-btn primary xl" id="borrowBtn"' + (elig.can_borrow ? "" : " disabled") + ">BORROW</button>" +
          '<button class="term-btn ok xl" id="returnBtn"' + (S.borrowed.length ? "" : " disabled") + ">RETURN</button>" +
        "</div>" +
        '<div class="term-actions"><button class="term-btn ghost" id="cancelBtn">Log out</button></div>' +
      "</div>");
    el("cancelBtn").addEventListener("click", screenIdle);
    if (elig.can_borrow) el("borrowBtn").addEventListener("click", screenPickLocker);
    if (S.borrowed.length) el("returnBtn").addEventListener("click", screenPickReturn);
  }

  /* ---- Borrow: pick a tool type (locker) -------------------------------- */
  function screenPickLocker() {
    if (!S.available.length) { screenError("Nothing available", "All tools are out right now.", screenHome); return; }
    stage(
      '<div class="term-panel"><h2>Select a tool to borrow</h2><div class="term-grid">' +
        S.available.map(function (a) {
          return '<button class="term-tool" data-locker="' + esc(a.lockerId) + '">' +
            "<strong>" + esc(a.type) + "</strong><span>" + esc(a.locker) +
            " · " + a.available + " available</span></button>"; }).join("") +
      "</div><div class='term-actions'><button class='term-btn ghost' id='backBtn'>← Back</button></div></div>");
    el("backBtn").addEventListener("click", screenHome);
    Array.prototype.forEach.call(document.querySelectorAll(".term-tool"), function (b) {
      b.addEventListener("click", function () { requestOpen("borrow", b.getAttribute("data-locker")); });
    });
  }

  /* ---- Return: pick a borrowed tool ------------------------------------- */
  function screenPickReturn() {
    stage(
      '<div class="term-panel"><h2>Select a tool to return</h2><div class="term-grid">' +
        S.borrowed.map(function (b) {
          return '<button class="term-tool" data-tool="' + esc(b.toolId) + '">' +
            "<strong>" + esc(b.tool) + "</strong><span>" + esc(b.locker) + "</span></button>"; }).join("") +
      "</div><div class='term-actions'><button class='term-btn ghost' id='backBtn'>← Back</button></div></div>");
    el("backBtn").addEventListener("click", screenHome);
    Array.prototype.forEach.call(document.querySelectorAll(".term-tool"), function (b) {
      b.addEventListener("click", function () { requestOpen("return", null, b.getAttribute("data-tool")); });
    });
  }

  /* ---- Request the locker to open --------------------------------------- */
  function requestOpen(mode, lockerId, toolId) {
    var path = mode === "borrow" ? "borrow-request" : "return-request";
    var body = mode === "borrow"
      ? { student_no: S.student.studentId, locker_id: lockerId }
      : { student_no: S.student.studentId, tool_id: toolId };
    stage('<div class="term-center"><div class="term-spin"></div><p>Opening locker…</p></div>');
    call(path, body).then(function (j) {
      screenAwait(mode, j);
    })["catch"](function (e) { screenError("Could not open", e.message, screenHome); });
  }

  /* ---- Wait for the physical scan (Arduino) or simulate it -------------- */
  function screenAwait(mode, cmd) {
    var pre = S.borrowed.length;
    stage(
      '<div class="term-panel"><h2>' + (mode === "borrow" ? "Take your tool" : "Return your tool") + "</h2>" +
        '<div class="term-note">' + esc(cmd.message) + "</div>" +
        "<ol class='term-steps'><li class='active'>" + esc(cmd.locker) + " unlocking</li>" +
        "<li>" + (mode === "borrow" ? "Remove the tool" : "Place the tool back") + "</li>" +
        "<li>Scan the tool's RFID tag</li></ol>" +
        '<p class="term-sub muted">The reader confirms automatically. No hardware? simulate it:</p>' +
        '<div class="term-scanrow">' +
          '<input id="uidInput" class="term-input" placeholder="tool RFID UID e.g. E9:8C:7B:06" />' +
          '<button class="term-btn primary" id="simBtn">Confirm</button></div>' +
        '<div class="term-actions"><button class="term-btn ghost" id="cancelBtn">Cancel</button></div></div>');
    el("cancelBtn").addEventListener("click", function () {
      stopPoll(); call("confirm", { command_id: cmd.commandId, timeout: true })["catch"](function () {}); screenHome();
    });
    el("simBtn").addEventListener("click", function () {
      var uid = el("uidInput").value.trim(); if (!uid) return;
      call("confirm", { command_id: cmd.commandId, uid: uid }).then(function (j) {
        screenDone(mode, j);
      })["catch"](function (e) { alert(e.message); });
    });
    // Real hardware path: poll until the borrowed set changes, then refresh.
    pollTimer = setInterval(function () {
      call("verify-student", { qr: S.student.studentId }).then(function (j) {
        if (!j.ok) return;
        if (j.borrowed.length !== pre) {
          S.borrowed = j.borrowed; S.available = j.availableByLocker;
          screenDone(mode, { tool: null });
        }
      })["catch"](function () {});
    }, 2000);
  }

  /* ---- Done / error ------------------------------------------------------ */
  function screenDone(mode, j) {
    stopPoll();
    stage(
      '<div class="term-center"><div class="term-big-ic ok">✔</div>' +
        "<h1>" + (mode === "borrow" ? "Borrow" : "Return") + " complete</h1>" +
        '<p class="term-sub">' + (j && j.tool ? esc(j.tool) + " · " : "") + "Thank you!</p>" +
        '<div class="term-actions"><button class="term-btn primary" id="doneBtn">Done</button></div></div>');
    el("doneBtn").addEventListener("click", screenIdle);
    setTimeout(function () { if (el("doneBtn")) screenIdle(); }, 10000);
  }

  function screenError(title, msg, back) {
    stopPoll();
    stage(
      '<div class="term-center"><div class="term-big-ic err">✖</div><h1>' + esc(title) + "</h1>" +
        '<p class="term-sub">' + esc(msg) + "</p>" +
        '<div class="term-actions"><button class="term-btn primary" id="backBtn">OK</button></div></div>');
    el("backBtn").addEventListener("click", back || screenIdle);
  }

  screenIdle();
})();

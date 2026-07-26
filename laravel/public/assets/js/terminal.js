/* ============================================================================
   reginsite — Locker terminal SIMULATOR.
   Pretends to be the ESP32 kiosk: talks only to api/esp32.php with the
   device API key, exactly like the firmware will. The hardware steps
   (relay, solenoid, ultrasonic, RFID) are animated stand-ins.
   ========================================================================== */
(function () {
  "use strict";

  var API = "/api/esp32";
  var API_KEY = "regin-esp32-2026";   // matches DEVICE_API_KEY in api/config.php

  var S = {           // session state for the current student interaction
    student: null, eligibility: null, borrowed: [], terms: "", limit: 8
  };

  /* ---- API ---------------------------------------------------------------- */
  function call(action, data) {
    return fetch(API + "/" + action.replace(/_/g, "-"), {
      method: "POST",
      headers: { "Content-Type": "application/json", "X-API-Key": API_KEY },
      body: JSON.stringify(data || {})
    }).then(function (res) {
      return res.json().then(function (j) {
        if (!res.ok || j.ok === false) {
          var e = new Error(j.error || "Request failed");
          e.code = j.code || "";
          throw e;
        }
        return j;
      });
    });
  }

  /* ---- Helpers -------------------------------------------------------------- */
  function el(id) { return document.getElementById(id); }
  function esc(s) {
    return String(s == null ? "" : s).replace(/&/g, "&amp;").replace(/</g, "&lt;")
      .replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  }
  function fmt(dt) {
    if (!dt) return "—";
    var d = new Date(dt);
    var h = d.getHours(), ap = h >= 12 ? "PM" : "AM"; h = h % 12; if (!h) h = 12;
    var mm = d.getMinutes() < 10 ? "0" + d.getMinutes() : d.getMinutes();
    return (d.getMonth() + 1) + "/" + d.getDate() + " " + h + ":" + mm + " " + ap;
  }
  function stage(html) { el("stage").innerHTML = html; }

  setInterval(function () {
    var c = el("termClock");
    if (c) c.textContent = new Date().toLocaleTimeString();
  }, 1000);

  /* ---- Screen: idle --------------------------------------------------------- */
  function screenIdle() {
    S.student = null;
    stage(
      '<div class="term-center">' +
        '<div class="term-big-ic">▣</div>' +
        "<h1>Scan your Student ID</h1>" +
        '<p class="term-sub">Hold the QR code on your school ID up to the scanner</p>' +
        '<div class="term-scanrow">' +
          '<input id="qrInput" class="term-input" placeholder="QR code (e.g. QR-2026-0132)" autofocus />' +
          '<button class="term-btn primary" id="scanBtn">Scan</button>' +
        "</div>" +
        '<div class="term-demo">Demo IDs: ' +
          ["QR-2026-0132", "QR-2026-0890", "QR-2026-0457", "QR-2026-0319"].map(function (q) {
            return '<button class="term-chip" data-qr="' + q + '">' + q + "</button>";
          }).join("") +
        "</div>" +
      "</div>");

    el("scanBtn").addEventListener("click", doScan);
    el("qrInput").addEventListener("keydown", function (e) { if (e.key === "Enter") doScan(); });
    Array.prototype.forEach.call(document.querySelectorAll(".term-chip"), function (b) {
      b.addEventListener("click", function () { el("qrInput").value = b.getAttribute("data-qr"); doScan(); });
    });
  }

  function doScan() {
    var qr = el("qrInput").value.trim();
    if (!qr) return;
    stage('<div class="term-center"><div class="term-spin"></div><p>Reading student ID…</p></div>');
    call("verify_student", { qr: qr }).then(function (j) {
      S.student = j.student; S.eligibility = j.eligibility;
      S.borrowed = j.borrowed; S.terms = j.terms; S.limit = j.borrowLimitHours;
      S.qr = qr;
      screenTerms();
    })["catch"](function (e) {
      screenError("Scan failed", e.message, screenIdle);
    });
  }

  /* ---- Screen: terms & conditions ------------------------------------------ */
  function screenTerms() {
    stage(
      '<div class="term-panel">' +
        "<h2>Terms &amp; Conditions</h2>" +
        '<pre class="term-terms">' + esc(S.terms) + "</pre>" +
        '<div class="term-actions">' +
          '<button class="term-btn ghost" id="declineBtn">Decline</button>' +
          '<button class="term-btn primary" id="agreeBtn">I Agree</button>' +
        "</div>" +
      "</div>");
    el("declineBtn").addEventListener("click", screenIdle);
    el("agreeBtn").addEventListener("click", screenStudent);
  }

  /* ---- Screen: student home ------------------------------------------------- */
  function screenStudent() {
    var s = S.student, elig = S.eligibility;
    var blockNote = elig.can_borrow ? "" :
      '<div class="term-note danger">⛔ ' + esc(elig.reason) + "</div>";
    var borrowedHtml = S.borrowed.length
      ? S.borrowed.map(function (b) {
          return '<div class="term-item"><span>' + esc(b.tool) + " · " + esc(b.locker) + "</span>" +
            '<span class="' + (b.status === "overdue" ? "term-late" : "term-ok") + '">' +
            (b.status === "overdue" ? "OVERDUE" : "due " + fmt(b.expectedReturn)) + "</span></div>";
        }).join("")
      : '<div class="term-item muted">No tools currently borrowed</div>';

    stage(
      '<div class="term-panel">' +
        '<div class="term-student"><div class="term-avatar">' + esc(s.name.charAt(0)) + "</div>" +
          "<div><h2>" + esc(s.name) + "</h2>" +
          '<p class="term-sub">' + esc(s.studentId) + " · " + esc(s.strand) + "</p></div></div>" +
        blockNote +
        '<h3 class="term-h3">Your borrowed tools</h3>' + borrowedHtml +
        '<div class="term-actions big">' +
          '<button class="term-btn primary xl" id="borrowBtn"' + (elig.can_borrow ? "" : " disabled") + ">BORROW</button>" +
          '<button class="term-btn ok xl" id="returnBtn"' + (S.borrowed.length ? "" : " disabled") + ">RETURN</button>" +
        "</div>" +
        '<div class="term-actions"><button class="term-btn ghost" id="cancelBtn">Cancel / Log out</button></div>' +
      "</div>");

    el("cancelBtn").addEventListener("click", screenIdle);
    if (S.eligibility.can_borrow) el("borrowBtn").addEventListener("click", screenPickTool);
    if (S.borrowed.length) el("returnBtn").addEventListener("click", screenPickReturn);
  }

  /* ---- Screen: pick a tool to borrow ---------------------------------------- */
  function screenPickTool() {
    stage('<div class="term-center"><div class="term-spin"></div><p>Checking tool availability…</p></div>');
    call("state").then(function (j) {
      var avail = j.lockers.filter(function (l) {
        return l.toolId && l.toolStatus === "available" && l.sensor === "online";
      });
      if (!avail.length) { screenError("No tools available", "All tools are currently borrowed or offline.", screenStudent); return; }
      stage(
        '<div class="term-panel">' +
          "<h2>Select a tool to borrow</h2>" +
          '<div class="term-grid">' +
            avail.map(function (l) {
              return '<button class="term-tool" data-tool="' + l.toolId + '" data-locker="' + esc(l.name) + '">' +
                "<strong>" + esc(l.tool) + "</strong><span>" + esc(l.name) + "</span></button>";
            }).join("") +
          "</div>" +
          '<div class="term-actions"><button class="term-btn ghost" id="backBtn">← Back</button></div>' +
        "</div>");
      el("backBtn").addEventListener("click", screenStudent);
      Array.prototype.forEach.call(document.querySelectorAll(".term-tool"), function (b) {
        b.addEventListener("click", function () {
          runHardware("borrow", parseInt(b.getAttribute("data-tool"), 10), b.getAttribute("data-locker"));
        });
      });
    })["catch"](function (e) { screenError("Error", e.message, screenStudent); });
  }

  /* ---- Screen: pick a tool to return ----------------------------------------- */
  function screenPickReturn() {
    stage(
      '<div class="term-panel">' +
        "<h2>Select a tool to return</h2>" +
        '<div class="term-grid">' +
          S.borrowed.map(function (b) {
            return '<button class="term-tool" data-tool="' + b.toolId + '" data-locker="' + esc(b.locker) + '">' +
              "<strong>" + esc(b.tool) + "</strong><span>" + esc(b.locker) +
              (b.status === "overdue" ? ' · <em class="term-late">OVERDUE</em>' : "") + "</span></button>";
          }).join("") +
        "</div>" +
        '<div class="term-actions"><button class="term-btn ghost" id="backBtn">← Back</button></div>' +
      "</div>");
    el("backBtn").addEventListener("click", screenStudent);
    Array.prototype.forEach.call(document.querySelectorAll(".term-tool"), function (b) {
      b.addEventListener("click", function () {
        runHardware("return", parseInt(b.getAttribute("data-tool"), 10), b.getAttribute("data-locker"));
      });
    });
  }

  /* ---- Screen: simulated hardware sequence ----------------------------------- */
  function runHardware(mode, toolId, lockerName) {
    var steps = mode === "borrow"
      ? ["Command sent to Arduino", "Relay module activated", "12V solenoid unlocked — " + lockerName + " OPEN",
         "Take the tool from the locker…", "Ultrasonic: tool REMOVED", "Red LED ON"]
      : ["Command sent to Arduino", "Relay module activated", "12V solenoid unlocked — " + lockerName + " OPEN",
         "Place the tool inside the locker…", "Ultrasonic: tool PRESENT", "Green LED ON"];

    stage(
      '<div class="term-panel">' +
        "<h2>" + (mode === "borrow" ? "Borrowing" : "Returning") + "…</h2>" +
        '<ol class="term-steps" id="steps">' +
          steps.map(function (s) { return "<li>" + esc(s) + "</li>"; }).join("") +
        "</ol>" +
        '<div class="term-actions" id="rfidRow" style="visibility:hidden">' +
          '<button class="term-btn primary xl" id="rfidBtn">📶 Scan RFID tool tag</button>' +
        "</div>" +
      "</div>");

    var items = document.querySelectorAll("#steps li");
    var i = 0;
    var timer = setInterval(function () {
      if (i > 0) items[i - 1].className = "done";
      if (i >= items.length) {
        clearInterval(timer);
        el("rfidRow").style.visibility = "visible";
        return;
      }
      items[i].className = "active";
      i++;
    }, 650);

    el("rfidBtn").addEventListener("click", function () {
      el("rfidBtn").disabled = true;
      el("rfidBtn").textContent = "Validating tag…";
      call(mode, { qr: S.qr, tool_id: toolId }).then(function (j) {
        screenDone(mode, j.result);
      })["catch"](function (e) {
        screenError((mode === "borrow" ? "Borrow" : "Return") + " failed", e.message, screenStudent);
      });
    });
  }

  /* ---- Screen: success / error ----------------------------------------------- */
  function screenDone(mode, r) {
    var detail = mode === "borrow"
      ? "Return it within " + S.limit + " hours — due <b>" + fmt(r.expectedReturn) + "</b>."
      : (r.late ? '<span class="term-late">Returned LATE.</span> Thank you for bringing it back.'
                : "Returned on time — thank you!");
    stage(
      '<div class="term-center">' +
        '<div class="term-big-ic ok">✔</div>' +
        "<h1>" + (mode === "borrow" ? "Borrow" : "Return") + " complete</h1>" +
        '<p class="term-sub">' + detail + "</p>" +
        '<p class="term-sub muted">Transaction #' + esc(r.txId) + " recorded. Locker closing…</p>" +
        '<div class="term-actions"><button class="term-btn primary" id="doneBtn">Done</button></div>' +
      "</div>");
    el("doneBtn").addEventListener("click", screenIdle);
    setTimeout(function () { if (el("doneBtn")) screenIdle(); }, 12000);
  }

  function screenError(title, msg, back) {
    stage(
      '<div class="term-center">' +
        '<div class="term-big-ic err">✖</div>' +
        "<h1>" + esc(title) + "</h1>" +
        '<p class="term-sub">' + esc(msg) + "</p>" +
        '<div class="term-actions"><button class="term-btn primary" id="backBtn">OK</button></div>' +
      "</div>");
    el("backBtn").addEventListener("click", back || screenIdle);
  }

  screenIdle();
})();

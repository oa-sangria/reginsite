/* ============================================================================
   reginsite — Locker Terminal (touchscreen kiosk)  ·  SCREEN-DRIVEN
   ---------------------------------------------------------------------------
   Runs full-screen on the mini PC's 7" 1024x600 touchscreen. The USB QR scanner
   (SM8070) is a keyboard-wedge desk unit: it "types" the student ID text, which
   we capture below. Talks to /api/esp32/* — the same API the Arduino bridge uses.

   Screens: idle → terms → home → pick → await (locker open) → receipt.
   Presentation lives in assets/css/terminal.css ("Workshop Light").

   URL flags:
     ?kiosk=1     production mode — hides the bench-test affordances
                  (simulated tag entry, BENCH MODE badge, Admin link)
     ?station=03  label shown in the equipment bar
   ========================================================================== */
(function () {
  "use strict";

  var API = "/api/esp32";
  var API_KEY = "regin-esp32-2026";   // matches DEVICE_API_KEY in laravel/.env

  var QS = location.search;
  var KIOSK = /[?&]kiosk=1/.test(QS);                     // hide bench tooling
  var STATION = (QS.match(/[?&]station=([\w-]+)/) || [])[1] || "01";

  var IDLE_LOGOUT_MS = 75000;         // abandon a signed-in session after 75s
  var IDLE_WARN_MS   = 20000;         // start showing the countdown at 20s
  var RECEIPT_MS     = 10000;         // receipt auto-dismiss

  var S = {};                          // current session
  var pollTimer = null, idleTimer = null, idleTick = null, idleDeadline = 0;

  /* ---- Plumbing ---------------------------------------------------------- */
  // Every request doubles as a link check — the bar's status light is real.
  function link(up) {
    var dot = document.querySelector(".k-dot"), lbl = el("liveLabel");
    if (!dot || !lbl) return;
    dot.classList.toggle("is-down", !up);
    lbl.textContent = up ? "LINK" : "NO LINK";
  }
  function call(path, payload, method) {
    return fetch(API + "/" + path, {
      method: method || "POST",
      headers: { "Content-Type": "application/json", "X-API-Key": API_KEY },
      body: method === "GET" ? undefined : JSON.stringify(payload || {})
    }).then(function (r) {
      link(true);
      return r.json().then(function (j) {
        if (j.ok === false && path !== "verify-student") {
          var e = new Error(j.error || "Request failed"); e.data = j; throw e;
        }
        return j;
      });
    }, function (e) { link(false); throw e; });
  }

  function el(id) { return document.getElementById(id); }
  function esc(s) {
    return String(s == null ? "" : s).replace(/&/g, "&amp;").replace(/</g, "&lt;")
      .replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  }
  function ico(id, cls) { return '<svg class="icon ' + (cls || "") + '"><use href="#' + id + '"/></svg>'; }
  function stage(html) { el("stage").innerHTML = '<section class="k-screen">' + html + "</section>"; }
  function hint(t) { el("hint").textContent = t; }
  function stopPoll() { if (pollTimer) { clearInterval(pollTimer); pollTimer = null; } }
  function on(id, fn) { var n = el(id); if (n) n.addEventListener("click", fn); }

  /* ---- Equipment bar ----------------------------------------------------- */
  el("station").textContent = "STATION " + STATION;
  if (KIOSK) {
    el("simBadge").style.display = "none";
    el("adminLink").style.display = "none";
  }
  (function tickClock() {
    var d = new Date();
    var h = d.getHours(), m = d.getMinutes();
    el("clock").textContent = (h % 12 || 12) + ":" + (m < 10 ? "0" + m : m) + " " + (h < 12 ? "AM" : "PM");
    setTimeout(tickClock, 1000);
  })();

  /* ---- Formatting helpers ------------------------------------------------ */
  // Locker names are seeded as "Locker 3 — Plier": split the number from the type.
  function lockerNo(name) {
    var m = String(name || "").match(/(\d+)/);
    var n = m ? parseInt(m[1], 10) : 0;
    return n ? (n < 10 ? "0" + n : String(n)) : "--";
  }
  function lockerType(name) {
    var parts = String(name || "").split(/\s+[—–-]\s+/);
    return parts.length > 1 ? parts.slice(1).join(" - ") : String(name || "");
  }
  function plate(name) { return '<span class="k-plate">LKR ' + lockerNo(name) + "</span>"; }

  // One glyph per real tool type in the seeded inventory.
  var ICONS = [
    [/solder/i, "t-soldering"], [/plier/i, "t-plier"], [/clamp/i, "t-clamp"],
    [/tape/i, "t-tape"], [/multi(tester|meter)|metro/i, "t-multitester"], [/screwdriver/i, "t-screwdriver"],
    [/cutter/i, "t-cutter"], [/drill|makita/i, "t-drill"],
    [/strip/i, "t-stripper"], [/crimp/i, "t-crimper"]
  ];
  function toolIcon(name) {
    for (var i = 0; i < ICONS.length; i++) if (ICONS[i][0].test(name || "")) return ICONS[i][1];
    return "t-generic";
  }

  function spanMs(ms) {
    var mins = Math.round(Math.abs(ms) / 60000);
    var d = Math.floor(mins / 1440), h = Math.floor((mins % 1440) / 60), m = mins % 60;
    if (d) return d + "D " + h + "H";
    if (h) return h + "H " + m + "M";
    return m + "M";
  }
  // Turns expected_return into the chip students actually read.
  function dueChip(b) {
    var late = b.status === "overdue";
    var t = b.expectedReturn ? new Date(b.expectedReturn).getTime() : 0;
    if (!t) return '<span class="k-due' + (late ? " k-due--late" : "") + '">' + (late ? "OVERDUE" : "ON LOAN") + "</span>";
    var diff = t - Date.now();
    if (late || diff <= 0) return '<span class="k-due k-due--late">OVERDUE ' + spanMs(diff) + "</span>";
    return '<span class="k-due">DUE IN ' + spanMs(diff) + "</span>";
  }
  function clockStr(iso) {
    var d = iso ? new Date(iso) : new Date();
    var h = d.getHours(), m = d.getMinutes();
    return (h % 12 || 12) + ":" + (m < 10 ? "0" + m : m) + " " + (h < 12 ? "AM" : "PM");
  }
  // Availability read as pips — how a machine shows a count, not a progress bar.
  function pips(n) {
    var out = "", cap = Math.min(n, 6), i;
    for (i = 0; i < 6; i++) out += '<i class="' + (i < cap ? "" : "off") + '"></i>';
    return '<span class="k-pips">' + out + "<u>" + n + " READY</u></span>";
  }

  /* ---- Session inactivity ----------------------------------------------- */
  // A kiosk must never sit on somebody else's session. The await screen is
  // exempt: a locker is physically open and the student is standing there.
  function armIdle() {
    disarmIdle();
    idleDeadline = Date.now() + IDLE_LOGOUT_MS;
    idleTick = setInterval(function () {
      var left = idleDeadline - Date.now();
      var node = el("timeout");
      if (left <= 0) { disarmIdle(); screenIdle(); return; }
      if (left <= IDLE_WARN_MS) {
        node.textContent = "SESSION ENDS IN " + Math.ceil(left / 1000) + "S";
        node.className = "k-timeout" + (left <= 8000 ? " is-urgent" : "");
      } else if (node.textContent) {
        node.textContent = ""; node.className = "k-timeout";
      }
    }, 500);
  }
  function disarmIdle() {
    if (idleTick) { clearInterval(idleTick); idleTick = null; }
    if (idleTimer) { clearTimeout(idleTimer); idleTimer = null; }
    var node = el("timeout"); if (node) { node.textContent = ""; node.className = "k-timeout"; }
  }
  function bumpIdle() { if (idleTick) idleDeadline = Date.now() + IDLE_LOGOUT_MS; }
  document.addEventListener("pointerdown", bumpIdle, true);
  document.addEventListener("keydown", bumpIdle, true);

  /* ---- QR keyboard-wedge capture ---------------------------------------- */
  // Scanners type fast and finish with Enter; a multiline QR arrives as several
  // lines. Buffer the keystrokes and flush after a short idle gap. Keystrokes
  // aimed at a focused field are left alone — that field does its own submit,
  // so a wedge scan still works if the input happens to hold focus.
  var qrBuf = "", qrTimer = null;
  document.addEventListener("keydown", function (e) {
    if (!S.idle) return;
    var tag = e.target && e.target.tagName;
    if (tag === "INPUT" || tag === "TEXTAREA") return;
    if (e.key === "Enter") { qrBuf += "\n"; }
    else if (e.key.length === 1) { qrBuf += e.key; }
    else { return; }
    clearTimeout(qrTimer);
    qrTimer = setTimeout(function () {
      var scan = qrBuf.trim(); qrBuf = "";
      if (scan) doScan(scan);
    }, 150);
  });

  /* ---- 01 · Idle -------------------------------------------------------- */
  // Instruction diagram, not a scan target: the SM8070 is a desk unit sitting
  // beside the screen with its window facing UP, so the drawing shows an ID
  // being lowered onto that window — the motion the student has to make.
  // Drawn top-down to match how they actually see the unit on the counter.
  function readerArt() {
    // Small QR block on the illustrated ID card.
    var dots = [[36,27],[41,31],[35,36],[45,39],[49,34],[36,45],[41,49],[47,46],[50,51],[32,50]]
      .map(function (d) { return '<rect x="' + d[0] + '" y="' + d[1] + '" width="2.6" height="2.6"/>'; }).join("");
    var finders = [[23,25],[44,25],[23,46]]
      .map(function (f) {
        return '<rect x="' + f[0] + '" y="' + f[1] + '" width="8" height="8" rx="1.2" ' +
               'fill="none" stroke="var(--k-ink)" stroke-width="2"/>';
      }).join("");

    return '<svg viewBox="0 0 200 200" fill="none" aria-hidden="true">' +
      // contact shadow
      '<ellipse cx="120" cy="192" rx="56" ry="6" fill="rgba(28,26,36,.08)"/>' +
      // USB lead off the back
      '<path d="M176 116c13 3 16 13 12 23s-13 15-21 13" stroke="var(--k-ink-2)" ' +
        'stroke-width="2.4" stroke-linecap="round"/>' +
      // body
      '<rect x="64" y="88" width="112" height="100" rx="15" fill="var(--k-card)" ' +
        'stroke="var(--k-ink)" stroke-width="2.2"/>' +
      // moulded bezel rings
      '<rect x="76" y="100" width="88" height="76" rx="12" fill="none" ' +
        'stroke="var(--k-indigo)" stroke-width="2"/>' +
      '<rect x="83" y="106" width="74" height="64" rx="9" fill="none" ' +
        'stroke="var(--k-indigo)" stroke-width="1.2" opacity=".45"/>' +
      // the window, its aim dot, and the read pulse
      '<rect x="90" y="112" width="60" height="52" rx="7" fill="var(--k-indigo-w)" ' +
        'stroke="var(--k-indigo)" stroke-width="1.6"/>' +
      '<circle class="k-ping" cx="120" cy="138" r="21" fill="none" ' +
        'stroke="var(--k-indigo)" stroke-width="2"/>' +
      '<circle cx="120" cy="138" r="2.4" fill="var(--k-indigo)"/>' +
      // travel path: card down onto the window
      '<path d="M92 84l14 20" stroke="var(--k-indigo)" stroke-width="2" ' +
        'stroke-dasharray="3 5" stroke-linecap="round" opacity=".65"/>' +
      '<path d="M100 98l7 8 2-10" stroke="var(--k-indigo)" stroke-width="2" ' +
        'stroke-linecap="round" stroke-linejoin="round" opacity=".65"/>' +
      // the ID card, QR face down
      '<g class="k-cardfloat">' +
        '<g transform="rotate(-7 62 44)">' +
          '<rect x="10" y="12" width="104" height="64" rx="9" fill="var(--k-card)" ' +
            'stroke="var(--k-ink)" stroke-width="2.2"/>' +
          '<g fill="var(--k-ink)">' + dots + "</g>" + finders +
          '<g fill="var(--k-line-2)">' +
            '<rect x="62" y="27" width="44" height="5" rx="2.5"/>' +
            '<rect x="62" y="39" width="32" height="5" rx="2.5"/>' +
            '<rect x="62" y="51" width="40" height="5" rx="2.5"/>' +
          "</g>" +
        "</g>" +
      "</g>" +
    "</svg>";
  }

  function screenIdle() {
    S = { idle: true };
    stopPoll(); disarmIdle();
    hint("Scanner ready · present your ID to the reader");
    // No one-tap demo IDs here, even on the bench: signing in has to go through
    // the reader or a keyed-in number, so a test never skips the step a student
    // will actually face.
    stage(
      '<div class="k-split k-split--scan">' +
        '<div class="k-reader">' +
          readerArt() +
          '<span class="k-readercap">SM8070 · Reader live</span>' +
        "</div>" +
        "<div>" +
          '<p class="k-eyebrow">Step 01</p>' +
          '<h1 class="k-h1">Scan your student ID</h1>' +
          '<p class="k-lead">Hold your ID with the QR code facing down over the scanner window. ' +
            "It beeps once when your ID is read.</p>" +
          '<p class="k-note" style="margin-top:7px">Borrowing is open to Industrial Technology — ' +
            "Electrical, Mechatronics and HVAC&amp;R.</p>" +
          '<div class="k-inputrow" style="margin-top:15px;max-width:400px">' +
            '<input id="qrInput" class="k-input" placeholder="or key in your student no." ' +
              'inputmode="numeric" autocomplete="off" spellcheck="false" />' +
            '<button class="k-btn k-btn--primary" id="scanBtn">' + ico("i-qr") + "Scan</button>" +
          "</div>" +
        "</div>" +
      "</div>");
    on("scanBtn", function () { var v = el("qrInput").value.trim(); if (v) doScan(v); });
    el("qrInput").addEventListener("keydown", function (e) {
      if (e.key === "Enter") { e.stopPropagation(); var v = el("qrInput").value.trim(); if (v) doScan(v); }
    });
  }

  function doScan(qr) {
    S.idle = false;
    hint("Verifying with the server");
    stage(
      '<div class="k-center">' +
        '<div class="k-spin"></div>' +
        '<h2 class="k-h2">Reading your ID</h2>' +
        '<p class="k-note" style="margin-top:6px;font-family:var(--k-mono);letter-spacing:.08em">' +
          esc(String(qr).split("\n")[0].slice(0, 42)) + "</p>" +
      "</div>");
    call("verify-student", { qr: qr }).then(function (j) {
      if (!j.ok) { screenStop("Not eligible", j.error, screenIdle); return; }
      S.student = j.student; S.eligibility = j.eligibility;
      S.borrowed = j.borrowed; S.available = j.availableByLocker;
      S.terms = j.terms; S.limit = j.borrowLimitHours;
      screenTerms();
    })["catch"](function (e) { screenStop("Scan failed", e.message, screenIdle); });
  }

  /* ---- 02 · Terms ------------------------------------------------------- */
  function screenTerms() {
    armIdle();
    hint("Read and accept to continue");
    stage(
      '<div class="k-fill">' +
        '<div class="k-head">' +
          "<div>" +
            '<p class="k-eyebrow">Step 02</p>' +
            '<h2 class="k-h2">Terms of use</h2>' +
          "</div>" +
          '<span class="k-plate">' + esc(S.student.studentId) + "</span>" +
        "</div>" +
        '<pre class="k-terms" id="termsBox">' + esc(S.terms || "No terms have been set.") + "</pre>" +
        '<div class="k-actions k-actions--end" style="margin-top:12px">' +
          '<p class="k-note" style="margin-right:auto">Tools are due back within ' +
            esc(S.limit || 8) + " hours.</p>" +
          '<button class="k-btn k-btn--ghost" id="declineBtn">Decline</button>' +
          '<button class="k-btn k-btn--primary" id="agreeBtn">' + ico("i-check") + "I agree</button>" +
        "</div>" +
      "</div>");
    on("declineBtn", screenIdle);
    on("agreeBtn", screenHome);
  }

  /* ---- 03 · Home -------------------------------------------------------- */
  function screenHome() {
    armIdle(); stopPoll();
    var s = S.student, elig = S.eligibility;
    hint("Choose an action");

    var stop = elig.can_borrow ? "" :
      '<div class="k-hazard k-hazard--stop">' + ico("i-alert") +
        "<span>" + esc(elig.reason) + "</span></div>";

    var loans = S.borrowed.length
      ? S.borrowed.map(function (b) {
          return '<div class="k-row">' + ico(toolIcon(b.tool)) +
            "<div><b>" + esc(b.tool) + "</b><small>LKR " + lockerNo(b.locker) +
            " · OUT " + clockStr(b.borrowTime) + "</small></div>" + dueChip(b) + "</div>";
        }).join("")
      : '<div class="k-empty">Nothing signed out to you right now.</div>';

    stage(
      '<div class="k-split k-split--home">' +
        '<div class="k-col">' +
          '<div class="k-who">' +
            '<span class="k-face">' + esc((s.name || "?").charAt(0).toUpperCase()) + "</span>" +
            "<div>" +
              "<h2>" + esc(s.name) + "</h2>" +
              "<p>" + esc(s.studentId) + "<span>|</span>" + esc(s.major || s.program || "—") + "</p>" +
            "</div>" +
          "</div>" +
          stop +
          // Centred in whatever the identity block leaves, so the column
          // balances against the full-height action slabs on the right.
          '<div class="k-listwrap">' +
            '<p class="k-sect">On loan · ' + S.borrowed.length + "</p>" +
            '<div class="k-list">' + loans + "</div>" +
          "</div>" +
        "</div>" +
        '<div class="k-col">' +
          '<button class="k-slab k-slab--borrow" id="borrowBtn"' + (elig.can_borrow ? "" : " disabled") + ">" +
            ico("i-out") + "<b>Borrow</b><span>Take a tool out</span></button>" +
          '<button class="k-slab k-slab--return" id="returnBtn"' + (S.borrowed.length ? "" : " disabled") + ">" +
            ico("i-in") + "<b>Return</b><span>Bring a tool back</span></button>" +
          '<button class="k-btn k-btn--quiet" id="outBtn" style="align-self:center">' +
            ico("i-logout") + "End session</button>" +
        "</div>" +
      "</div>");
    on("outBtn", screenIdle);
    if (elig.can_borrow) on("borrowBtn", screenPickBorrow);
    if (S.borrowed.length) on("returnBtn", screenPickReturn);
  }

  /* ---- 04a · Borrow: pick a tool type (one type per locker) ------------- */
  function screenPickBorrow() {
    armIdle();
    if (!S.available.length) {
      screenStop("Nothing available", "Every tool is signed out right now. Try again later.", screenHome);
      return;
    }
    hint("Tap the tool you need");
    stage(
      '<div class="k-fill">' +
        '<div class="k-head" style="margin-bottom:12px">' +
          "<div>" +
            '<p class="k-eyebrow">Step 03</p>' +
            '<h2 class="k-h2">What do you need?</h2>' +
          "</div>" +
          '<button class="k-btn k-btn--ghost k-btn--sm" id="backBtn">' + ico("i-back") + "Back</button>" +
        "</div>" +
        '<div class="k-listwrap"><div class="k-grid">' +
          S.available.map(function (a) {
            return '<button class="k-tool" data-locker="' + esc(a.lockerId) + '">' +
              ico(toolIcon(a.type)) +
              "<b>" + esc(a.type) + "</b>" +
              '<span class="k-plate">LKR ' + lockerNo(a.locker) + "</span>" +
              pips(a.available) + "</button>";
          }).join("") +
        "</div></div>" +
      "</div>");
    on("backBtn", screenHome);
    Array.prototype.forEach.call(document.querySelectorAll(".k-tool"), function (b) {
      b.addEventListener("click", function () { requestOpen("borrow", b.getAttribute("data-locker")); });
    });
  }

  /* ---- 04b · Return: pick one of your borrowed tools -------------------- */
  function screenPickReturn() {
    armIdle();
    hint("Tap the tool you are returning");
    stage(
      '<div class="k-fill">' +
        '<div class="k-head" style="margin-bottom:12px">' +
          "<div>" +
            '<p class="k-eyebrow">Step 03</p>' +
            '<h2 class="k-h2">What are you returning?</h2>' +
          "</div>" +
          '<button class="k-btn k-btn--ghost k-btn--sm" id="backBtn">' + ico("i-back") + "Back</button>" +
        "</div>" +
        '<div class="k-listwrap"><div class="k-list">' +
          S.borrowed.map(function (b) {
            return '<button class="k-pick" data-tool="' + esc(b.toolId) + '" data-tag="' + esc(b.rfidTag || "") + '">' +
              ico(toolIcon(b.tool), "k-tool-ic") +
              "<div><b>" + esc(b.tool) + "</b><small>LKR " + lockerNo(b.locker) +
                " · " + esc(lockerType(b.locker)) + " · OUT " + clockStr(b.borrowTime) + "</small></div>" +
              dueChip(b) +
              ico("i-next", "k-chev") +
              "</button>";
          }).join("") +
        "</div></div>" +
      "</div>");
    on("backBtn", screenHome);
    Array.prototype.forEach.call(document.querySelectorAll(".k-pick"), function (b) {
      b.addEventListener("click", function () {
        requestOpen("return", null, b.getAttribute("data-tool"), b.getAttribute("data-tag"));
      });
    });
  }

  /* ---- Queue the OPEN command ------------------------------------------- */
  function requestOpen(mode, lockerId, toolId, tag) {
    var path = mode === "borrow" ? "borrow-request" : "return-request";
    var body = mode === "borrow"
      ? { student_no: S.student.studentId, locker_id: lockerId }
      : { student_no: S.student.studentId, tool_id: toolId };
    hint("Sending the unlock command");
    stage('<div class="k-center"><div class="k-spin"></div>' +
          '<h2 class="k-h2">Unlocking the locker</h2>' +
          '<p class="k-lead" style="max-width:34ch">Stand by — the controller is releasing the door.</p></div>');
    call(path, body).then(function (j) {
      screenAwait(mode, j, tag);
    })["catch"](function (e) { screenStop("Could not open", e.message, screenHome); });
  }

  /* ---- 05 · Await the physical confirm (RFID via the bridge) ------------ */
  /* The slot sensors keep watching every slot while the door is open and for
     a while after it relocks. A second slot moving means a tool is leaving
     with no tag — the Mega's buzzer alarms and the server flags the locker;
     command-status carries that flag as `alert`, so the student is told to put
     it back while the alarm is still sounding. Shown on the await screen and
     the receipt (the second tool usually goes after the first is recorded). */
  function extraWarn(borrowing) {
    return '<div class="k-hazard k-hazard--stop" id="extraWarn" style="display:none;margin-top:12px">' +
      ico("i-alert") + "<span>" +
      (borrowing ? "Only one tool per borrow — <b>put the other tool back in its slot</b>."
                 : "Another slot moved — <b>put that tool back in its slot</b>.") +
      " The alarm stops once its slot reads full again.</span></div>";
  }
  function showExtraWarn(on) {
    var w = el("extraWarn");
    if (w) w.style.display = on ? "flex" : "none";
  }

  function screenAwait(mode, cmd, tag) {
    disarmIdle();                        // a door is open — never time out here
    var borrowing = mode === "borrow";
    hint(borrowing ? "Waiting for the tool to be removed" : "Waiting for the tool to be replaced");

    var bench = KIOSK ? "" :
      '<div class="k-dev" style="margin-top:14px">' +
        '<span class="k-dev-tag">Bench<br>test</span>' +
        '<input id="uidInput" class="k-input k-input--sm" placeholder="' +
          (tag ? "tag " + esc(tag) : "tool RFID UID, e.g. E9:8C:7B:06") + '" ' +
          'value="' + esc(tag || "") + '" autocomplete="off" spellcheck="false" />' +
        '<button class="k-btn k-btn--ghost k-btn--sm" id="simBtn">Confirm tag</button>' +
      "</div>";

    stage(
      '<div class="k-split k-split--door">' +
        '<div class="k-door">' +
          "<u>Locker</u>" +
          "<b>" + lockerNo(cmd.locker) + "</b>" +
          '<span class="k-door-type">' + esc(lockerType(cmd.locker) || "") + "</span>" +
          "<em>Unlocked</em>" +
        "</div>" +
        '<div style="display:flex;flex-direction:column;min-height:0">' +
          '<p class="k-eyebrow">Step 04</p>' +
          '<h2 class="k-h2" style="margin-bottom:13px">' +
            (borrowing ? "Take your tool" : "Put the tool back") + "</h2>" +
          '<ol class="k-rail">' +
            '<li class="is-done" data-n="01"><b>Door released</b>' +
              "<small>" + esc(cmd.message || "Locker unlocked") + "</small></li>" +
            '<li class="is-active" data-n="02" id="railLift"><b>' +
              (borrowing ? "Lift the tool out" : "Seat the tool in its slot") + "</b>" +
              "<small>" + (borrowing ? "The slot sensor confirms it left" : "The slot sensor confirms it is back") +
              "</small></li>" +
            '<li data-n="03" id="railTag"><b>Tap its RFID tag on the reader</b>' +
              "<small>Records the exact tool — then the door locks</small></li>" +
          "</ol>" +
          extraWarn(borrowing) +
          bench +
          '<div class="k-actions" style="margin-top:auto;padding-top:10px">' +
            '<button class="k-btn k-btn--ghost k-btn--sm" id="cancelBtn">' + ico("i-x") + "Cancel</button>" +
          "</div>" +
        "</div>" +
      "</div>");

    on("cancelBtn", function () {
      stopPoll();
      call("confirm", { command_id: cmd.commandId, timeout: true, reason: "cancelled" })["catch"](function () {});
      screenHome();
    });
    on("simBtn", function () {
      var uid = el("uidInput").value.trim(); if (!uid) return;
      // Bench stand-in for the bridge's confirm. Either way the command goes
      // terminal server-side (done, or failed with a note), and the poll below
      // renders the outcome exactly as it would for a real scan — so a rejected
      // tag shows the same stop plate the student would see in the field.
      call("confirm", { command_id: cmd.commandId, uid: uid })["catch"](function () {});
    });

    // Real hardware path: the bridge confirms server-side. Poll the COMMAND, not
    // the loan list — a loan only appears on success, so watching it alone
    // leaves the screen stuck on "Take your tool" after a timeout or a rejected
    // tag, with the door already locked and the student none the wiser.
    //
    // 600ms: the outcome is already recorded server-side by the time we poll,
    // so this interval IS the delay the student sees. command-status is one
    // indexed lookup on localhost.
    // Mid-window progress. While the command is open, `note` carries the
    // stages the Mega has reported so far ("moved", "scanned"). The rail is
    // the student's only window into a reader that can take a moment: the
    // spinner on step 03 says "keep the tag there", and the tick says "got it".
    var shown = "";
    function renderRail(moved, scanned) {
      var key = (moved ? "m" : "") + (scanned ? "s" : "");
      if (key === shown) return;
      shown = key;
      var lift = el("railLift"), tag = el("railTag");
      if (!lift || !tag) return;

      lift.className = moved ? "is-done" : (scanned ? "is-busy" : "is-active");
      lift.querySelector("small").textContent = moved
        ? (borrowing ? "The slot sensor saw it leave" : "The slot sensor saw it go back")
        : (scanned
            ? (borrowing ? "Tag read — now lift the tool out of its slot" : "Tag read — now seat the tool in its slot")
            : (borrowing ? "The slot sensor confirms it left" : "The slot sensor confirms it is back"));

      tag.className = scanned ? "is-done" : (moved ? "is-busy" : "");
      tag.querySelector("b").textContent = scanned ? "Tag read" : "Tap its RFID tag on the reader";
      tag.querySelector("small").textContent = scanned
        ? (moved ? "Saving…" : "Recorded — waiting on the slot sensor")
        : (moved ? "Hold the tag flat on the reader and keep it there until it beeps"
                 : "Records the exact tool — then the door locks");

      hint(scanned && moved ? "Saving the transaction"
         : scanned ? (borrowing ? "Tag read — lift the tool out" : "Tag read — seat the tool")
         : moved   ? "Hold the tag on the reader until it beeps"
                   : (borrowing ? "Waiting for the tool to be removed" : "Waiting for the tool to be replaced"));
    }

    var settling = false;
    pollTimer = setInterval(function () {
      if (settling) return;
      call("command-status", { command_id: cmd.commandId }).then(function (c) {
        if (!c.ok || settling) return;
        showExtraWarn(!!c.alert);

        if (c.status === "pending" || c.status === "sent") {
          var stages = "," + (c.note || "") + ",";
          renderRail(stages.indexOf(",moved,") >= 0, stages.indexOf(",scanned,") >= 0);
          return;                                  // keep waiting
        }

        if (c.status === "done") {
          settling = true;
          // Refresh the session so the receipt and the menu reflect the new loan.
          call("verify-student", { qr: S.student.studentId }).then(function (j) {
            if (j.ok) { S.borrowed = j.borrowed; S.available = j.availableByLocker; S.eligibility = j.eligibility; }
            screenReceipt(mode, { tool: c.tool }, cmd);
          })["catch"](function () { screenReceipt(mode, { tool: c.tool }, cmd); });

        } else if (c.status === "timeout") {
          settling = true;
          screenStop(
            "Door locked again",
            (c.note || "No tag was scanned in time.") + " Start again when you're ready.",
            screenHome);

        } else if (c.status === "failed") {
          settling = true;
          screenStop(
            borrowing ? "That isn't the right tool" : "Tag not recognised",
            (c.note || "The tag was rejected.") + " The door has re-locked — please try again.",
            screenHome);
        }
        // pending / sent: keep waiting.
      })["catch"](function () {});
    }, 600);
  }

  /* ---- 06 · Receipt ----------------------------------------------------- */
  function screenReceipt(mode, j, cmd) {
    stopPoll(); disarmIdle();
    var borrowing = mode === "borrow";
    var due = borrowing
      ? new Date(Date.now() + (S.limit || 8) * 3600000)
      : null;
    hint(borrowing ? "Borrow recorded" : "Return recorded");

    stage(
      '<div class="k-center">' +
        '<div class="k-ticket">' +
          '<div class="k-ticket-top">' +
            "<u>" + (borrowing ? "Borrow recorded" : "Return recorded") + "</u>" +
            '<span class="k-seal">' + ico("i-check") + "</span>" +
          "</div>" +
          "<h2>" + esc((j && j.tool) || lockerType(cmd && cmd.locker) || "Tool") + "</h2>" +
          '<div class="k-perf"></div>' +
          '<dl style="margin:0">' +
            '<div class="k-kv"><dt>Student</dt><dd>' + esc(S.student.studentId) + "</dd></div>" +
            '<div class="k-kv"><dt>Locker</dt><dd>LKR ' + lockerNo(cmd && cmd.locker) + "</dd></div>" +
            '<div class="k-kv"><dt>' + (borrowing ? "Signed out" : "Returned") + "</dt><dd>" + clockStr() + "</dd></div>" +
            (due ? '<div class="k-kv"><dt>Due back</dt><dd>' + clockStr(due.toISOString()) + "</dd></div>" : "") +
          "</dl>" +
          '<div class="k-timer"><i style="animation-duration:' + (RECEIPT_MS / 1000) + 's"></i></div>' +
        "</div>" +
        '<div class="k-actions" style="margin-top:15px">' +
          '<button class="k-btn k-btn--ghost" id="moreBtn">Back to menu</button>' +
          '<button class="k-btn k-btn--primary" id="doneBtn">' + ico("i-check") + "Done</button>" +
        "</div>" +
        extraWarn(borrowing) +
      "</div>");
    // The door is relocked but not shut: the Mega is still watching the slots.
    // Keep asking about the locker so a second tool leaving now shows here.
    if (cmd && cmd.commandId) {
      pollTimer = setInterval(function () {
        call("command-status", { command_id: cmd.commandId }).then(function (c) {
          if (c.ok) showExtraWarn(!!c.alert);
        })["catch"](function () {});
      }, 600);
    }
    on("doneBtn", screenIdle);
    on("moreBtn", function () {
      // Refresh eligibility/loans before showing the menu again.
      call("verify-student", { qr: S.student.studentId }).then(function (r) {
        if (!r.ok) { screenIdle(); return; }
        S.eligibility = r.eligibility; S.borrowed = r.borrowed; S.available = r.availableByLocker;
        screenHome();
      })["catch"](screenIdle);
    });
    setTimeout(function () { if (el("doneBtn")) screenIdle(); }, RECEIPT_MS);
  }

  /* ---- Stop plate ------------------------------------------------------- */
  function screenStop(title, msg, back) {
    stopPoll(); disarmIdle();
    hint("Blocked");
    stage(
      '<div class="k-center">' +
        '<div class="k-plate-lg k-plate-lg--stop">' + ico("i-alert") + "</div>" +
        '<h2 class="k-h2" style="margin-top:10px">' + esc(title) + "</h2>" +
        '<p class="k-lead" style="max-width:44ch;text-align:center">' + esc(msg || "") + "</p>" +
        '<div class="k-actions" style="margin-top:16px">' +
          '<button class="k-btn k-btn--primary" id="backBtn">Got it</button>' +
        "</div>" +
      "</div>");
    on("backBtn", function () { (back || screenIdle)(); });
  }

  screenIdle();
})();

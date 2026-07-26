/* ============================================================================
   reginsite — Page renderers for the admin modules.
   Each public render* function builds its page into #page.
   Add/Edit mutate the in-memory DB arrays (prototype — not persisted).
   ========================================================================== */

/* ---- Small DOM / markup helpers --------------------------------------- */
function _el(id) { return document.getElementById(id); }
function _val(id) { var e = _el(id); return e ? e.value.trim() : ""; }
function _on(id, evt, fn) { var e = _el(id); if (e) e.addEventListener(evt, fn); }
function _cap(s) { return s.charAt(0).toUpperCase() + s.slice(1); }
function _esc(s) { return App.escapeHtml(s); }

function _pageHead(sub, actionsHtml) {
  return '<div class="page-head"><div class="sub">' + sub + "</div>" +
    '<div class="toolbar">' + (actionsHtml || "") + "</div></div>";
}
function _searchIcon() {
  return '<svg class="icon" viewBox="0 0 24 24"><path d="M15.5 14h-.79l-.28-.27a6.5 6.5 0 1 0-.7.7l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0A4.5 4.5 0 1 1 14 9.5 4.5 4.5 0 0 1 9.5 14z"/></svg>';
}
function _search(id, ph) {
  return '<div class="search grow">' + _searchIcon() +
    '<input class="input" id="' + id + '" type="text" placeholder="' + ph + '" /></div>';
}
function _select(id, opts, allLabel) {
  return '<select class="select" id="' + id + '" style="max-width:190px">' +
    opts.map(function (o) {
      var v = o.value !== undefined ? o.value : o;
      var l = o.label !== undefined ? o.label : (v === "all" ? (allLabel || "All") : _cap(v));
      return '<option value="' + _esc(v) + '">' + _esc(l) + "</option>";
    }).join("") + "</select>";
}
function _who(studentId) {
  var s = App.studentById(studentId);
  if (!s) return "—";
  return '<div class="who"><span class="avatar">' + App.initials(s.name) + "</span>" +
    '<span><div class="cell-strong">' + _esc(s.name) + "</div>" +
    '<div class="cell-sub">' + _esc(s.studentId) + " · " + _esc(s.strand) + "</div></span></div>";
}
function _emptyRow(cols, msg) {
  return '<tr><td colspan="' + cols + '" class="empty">' + (msg || "No records found.") + "</td></tr>";
}
function _match(t, q) {
  if (!q) return true; q = q.toLowerCase();
  var s = App.studentById(t.studentId) || {};
  return [s.name, s.studentId, s.strand, App.toolName(t.toolId), App.lockerNo(t.lockerId), t.id, t.status]
    .join(" ").toLowerCase().indexOf(q) >= 0;
}

/* ======================================================================== */
/*  BORROW LOG                                                              */
/* ======================================================================== */
function renderBorrowLog() {
  _el("page").innerHTML =
    _pageHead("Every tool checkout recorded from the locker terminals (borrow workflow).") +
    '<div class="card"><div class="card-head"><div class="toolbar" style="flex:1">' +
      _search("blSearch", "Search student, tool, locker…") +
      _select("blStatus", ["all", "borrowed", "overdue", "returned"], "All statuses") +
    '</div><span class="count-pill" id="blCount"></span></div>' +
    '<div class="table-wrap"><table class="data"><thead><tr>' +
      "<th>Student</th><th>Tool</th><th>Locker</th><th>Borrowed</th><th>Expected Return</th><th>Qty</th><th>Status</th>" +
    '</tr></thead><tbody id="blBody"></tbody></table></div></div>';

  function draw() {
    var q = _val("blSearch"), s = _val("blStatus");
    var list = DB.transactions
      .filter(function (t) { return s === "all" || t.status === s; })
      .filter(function (t) { return _match(t, q); })
      .sort(function (a, b) { return new Date(b.borrowTime) - new Date(a.borrowTime); });
    _el("blBody").innerHTML = list.map(function (t) {
      return "<tr><td>" + _who(t.studentId) + "</td>" +
        '<td class="cell-strong">' + _esc(App.toolName(t.toolId)) + "</td>" +
        "<td>" + _esc(App.lockerNo(t.lockerId)) + "</td>" +
        "<td>" + App.fmtDateTime(t.borrowTime) + "</td>" +
        "<td>" + App.fmtDateTime(t.expectedReturn) + "</td>" +
        "<td>" + t.qty + "</td>" +
        "<td>" + App.statusBadge(t.status) + "</td></tr>";
    }).join("") || _emptyRow(7);
    _el("blCount").textContent = list.length + " record" + (list.length === 1 ? "" : "s");
  }
  _on("blSearch", "input", draw); _on("blStatus", "change", draw); draw();
}

/* ======================================================================== */
/*  RETURN LOG                                                              */
/* ======================================================================== */
function renderReturnLog() {
  _el("page").innerHTML =
    _pageHead("Tools checked back into their lockers (return workflow).") +
    '<div class="card"><div class="card-head"><div class="toolbar" style="flex:1">' +
      _search("rlSearch", "Search student, tool, locker…") +
    '</div><span class="count-pill" id="rlCount"></span></div>' +
    '<div class="table-wrap"><table class="data"><thead><tr>' +
      "<th>Student</th><th>Tool</th><th>Locker</th><th>Borrowed</th><th>Returned</th><th>On Time</th><th>Status</th>" +
    '</tr></thead><tbody id="rlBody"></tbody></table></div></div>';

  function draw() {
    var q = _val("rlSearch");
    var list = DB.transactions
      .filter(function (t) { return t.returnTime; })
      .filter(function (t) { return _match(t, q); })
      .sort(function (a, b) { return new Date(b.returnTime) - new Date(a.returnTime); });
    _el("rlBody").innerHTML = list.map(function (t) {
      var onTime = new Date(t.returnTime) <= new Date(t.expectedReturn);
      return "<tr><td>" + _who(t.studentId) + "</td>" +
        '<td class="cell-strong">' + _esc(App.toolName(t.toolId)) + "</td>" +
        "<td>" + _esc(App.lockerNo(t.lockerId)) + "</td>" +
        "<td>" + App.fmtDateTime(t.borrowTime) + "</td>" +
        "<td>" + App.fmtDateTime(t.returnTime) + "</td>" +
        "<td>" + (onTime ? App.badge("On time", "ok") : App.badge("Late", "danger")) + "</td>" +
        "<td>" + App.statusBadge(t.status) + "</td></tr>";
    }).join("") || _emptyRow(7);
    _el("rlCount").textContent = list.length + " return" + (list.length === 1 ? "" : "s");
  }
  _on("rlSearch", "input", draw); draw();
}

/* ======================================================================== */
/*  TRANSACTION LOG (unified borrow + return events)                        */
/* ======================================================================== */
function renderTransactions() {
  var studentOpts = [{ value: "all", label: "All students" }].concat(
    DB.students.map(function (s) { return { value: s.id, label: s.name }; }));

  _el("page").innerHTML =
    _pageHead("Combined student logs and full borrow/return history.") +
    '<div class="card"><div class="card-head"><div class="toolbar" style="flex:1">' +
      _search("txSearch", "Search…") +
      _select("txType", ["all", "borrow", "return"], "All types") +
      _select("txStudent", studentOpts) +
    '</div><span class="count-pill" id="txCount"></span></div>' +
    '<div class="table-wrap"><table class="data"><thead><tr>' +
      "<th>Type</th><th>Student</th><th>Tool</th><th>Locker</th><th>When</th><th>Status</th>" +
    '</tr></thead><tbody id="txBody"></tbody></table></div></div>';

  function draw() {
    var q = _val("txSearch"), type = _val("txType"), stu = _val("txStudent");
    var list = App.events()
      .filter(function (e) { return type === "all" || e.type === type; })
      .filter(function (e) { return stu === "all" || e.tx.studentId === stu; })
      .filter(function (e) { return _match(e.tx, q); });
    _el("txBody").innerHTML = list.map(function (e) {
      var typeBadge = e.type === "borrow" ? App.badge("Borrow", "info") : App.badge("Return", "ok");
      return "<tr><td>" + typeBadge + "</td>" +
        "<td>" + _who(e.tx.studentId) + "</td>" +
        '<td class="cell-strong">' + _esc(App.toolName(e.tx.toolId)) + "</td>" +
        "<td>" + _esc(App.lockerNo(e.tx.lockerId)) + "</td>" +
        "<td>" + App.fmtDateTime(e.time) + '<div class="cell-sub">' + App.relTime(e.time) + "</div></td>" +
        "<td>" + App.statusBadge(e.tx.status) + "</td></tr>";
    }).join("") || _emptyRow(6);
    _el("txCount").textContent = list.length + " event" + (list.length === 1 ? "" : "s");
  }
  _on("txSearch", "input", draw); _on("txType", "change", draw); _on("txStudent", "change", draw); draw();
}

/* ======================================================================== */
/*  STUDENT LOG (+ drawer)                                                  */
/* ======================================================================== */
function renderStudents() {
  _el("page").innerHTML =
    _pageHead("All registered students, their status, and borrowing eligibility. Click a row for history.",
      '<button class="btn btn-primary btn-sm" id="stuAdd">Add Student</button>') +
    '<div class="card"><div class="card-head"><div class="toolbar" style="flex:1">' +
      _search("stSearch", "Search name, ID, strand…") +
      _select("stStatus", ["all", "active", "banned"], "All statuses") +
    '</div><span class="count-pill" id="stCount"></span></div>' +
    '<div class="table-wrap"><table class="data"><thead><tr>' +
      "<th>Student</th><th>Strand</th><th>QR ID</th><th>Active Borrows</th><th>Status</th><th>Can Borrow?</th>" +
    '</tr></thead><tbody id="stBody"></tbody></table></div></div>';

  function draw() {
    var q = _val("stSearch").toLowerCase(), s = _val("stStatus");
    var list = DB.students
      .filter(function (st) { return s === "all" || st.status === s; })
      .filter(function (st) {
        return !q || [st.name, st.studentId, st.strand, st.qr].join(" ").toLowerCase().indexOf(q) >= 0;
      });
    _el("stBody").innerHTML = list.map(function (st) {
      var active = App.activeBorrowsFor(st.id).length;
      var block = App.borrowBlock(st.id);
      var canBorrow = block.blocked
        ? App.badge(st.status === "banned" ? "Banned" : "Return first", "danger")
        : App.badge("Yes", "ok");
      return '<tr class="clickable" data-id="' + st.id + '">' +
        "<td>" + _who(st.id) + "</td>" +
        "<td>" + _esc(st.strand) + "</td>" +
        '<td class="cell-sub">' + _esc(st.qr) + "</td>" +
        "<td>" + (active ? App.badge(active + " out", "info") : '<span class="muted">—</span>') + "</td>" +
        "<td>" + App.statusBadge(st.status) + "</td>" +
        "<td>" + canBorrow + "</td></tr>";
    }).join("") || _emptyRow(6);
    _el("stCount").textContent = list.length + " student" + (list.length === 1 ? "" : "s");
    Array.prototype.forEach.call(document.querySelectorAll("#stBody tr.clickable"), function (tr) {
      tr.addEventListener("click", function () { _openStudentDrawer(tr.getAttribute("data-id")); });
    });
  }
  _on("stSearch", "input", draw); _on("stStatus", "change", draw);
  _on("stuAdd", "click", function () { _studentModal(); });
  draw();
}

function _studentModal(id) {
  var s = id ? App.studentById(id) : null;
  var statusOpts = ["active", "banned"].map(function (st) {
    return '<option value="' + st + '"' + (s && s.status === st ? " selected" : "") + ">" + _cap(st) + "</option>";
  }).join("");

  openModal(
    '<div class="modal-head"><h2>' + (s ? "Edit Student" : "Add Student") + '</h2><button class="icon-btn" onclick="closeModal()">×</button></div>' +
    '<div class="modal-body">' +
      _field("Full name", _txt("fName", s ? s.name : "")) +
      '<div class="row-2">' +
        _field("Student number", _txt("fNo", s ? s.studentId : "")) +
        _field("Strand", _txt("fStrand", s ? s.strand : "")) +
      "</div>" +
      _field("QR code (blank = auto from student number)", _txt("fQr", s ? s.qr : "")) +
      _field("Status", '<select class="select" id="fStatus">' + statusOpts + "</select>") +
    "</div>" +
    '<div class="modal-foot"><button class="btn btn-ghost" onclick="closeModal()">Cancel</button>' +
      '<button class="btn btn-primary" id="fSave">Save</button></div>');

  _on("fSave", "click", function () {
    var name = _val("fName"); if (!name) { _el("fName").focus(); return; }
    var no = _val("fNo"); if (!no) { _el("fNo").focus(); return; }
    _saveVia("students.php", {
      action: "save", id: s ? s.id : "",
      name: name, studentId: no, strand: _val("fStrand"), qr: _val("fQr"), status: _val("fStatus")
    }, renderStudents);
  });
}

function _openStudentDrawer(studentId) {
  var s = App.studentById(studentId);
  if (!s) return;
  var block = App.borrowBlock(studentId);
  var ban = DB.bans.find(function (b) { return b.studentId === studentId; });

  var banNote = "";
  if (s.status === "banned" && ban) {
    banNote = '<div class="note danger">⛔ Banned until ' + App.fmtDate(s.bannedUntil) +
      " — " + _esc(ban.reason) + "</div>";
  } else if (block.blocked) {
    banNote = '<div class="note">⚠ ' + _esc(block.reason) + "</div>";
  }

  var hist = DB.transactions
    .filter(function (t) { return t.studentId === studentId; })
    .sort(function (a, b) { return new Date(b.borrowTime) - new Date(a.borrowTime); });

  var histHtml = hist.length ? hist.map(function (t) {
    return '<div class="card" style="box-shadow:none;margin-bottom:10px">' +
      '<div class="card-body" style="padding:12px 14px">' +
        '<div style="display:flex;justify-content:space-between;gap:10px;align-items:center">' +
          '<strong>' + _esc(App.toolName(t.toolId)) + "</strong>" + App.statusBadge(t.status) + "</div>" +
        '<div class="kv"><span class="k">Locker</span><span>' + _esc(App.lockerNo(t.lockerId)) + "</span></div>" +
        '<div class="kv"><span class="k">Borrowed</span><span>' + App.fmtDateTime(t.borrowTime) + "</span></div>" +
        '<div class="kv"><span class="k">Expected</span><span>' + App.fmtDateTime(t.expectedReturn) + "</span></div>" +
        '<div class="kv"><span class="k">Returned</span><span>' + (t.returnTime ? App.fmtDateTime(t.returnTime) : "—") + "</span></div>" +
      "</div></div>";
  }).join("") : '<div class="empty">No borrow history.</div>';

  _openDrawer(
    '<div class="drawer-head"><h2>Student Details</h2>' +
      '<span style="display:flex;gap:8px;align-items:center">' +
        '<button class="btn btn-ghost btn-sm" onclick="closeDrawer();_studentModal(\'' + s.id + '\')">Edit</button>' +
        '<button class="icon-btn" onclick="closeDrawer()">×</button></span></div>' +
    '<div class="drawer-body">' +
      '<div class="who" style="margin-bottom:14px"><span class="avatar" style="width:46px;height:46px;font-size:16px">' +
        App.initials(s.name) + '</span><span><div class="cell-strong" style="font-size:16px">' + _esc(s.name) +
        '</div><div class="cell-sub">' + _esc(s.studentId) + " · " + _esc(s.strand) + "</div></span></div>" +
      banNote +
      '<div class="card" style="box-shadow:none;margin:14px 0">' +
        '<div class="card-body" style="padding:6px 14px">' +
          '<div class="kv"><span class="k">QR Code</span><span>' + _esc(s.qr) + "</span></div>" +
          '<div class="kv"><span class="k">Status</span><span>' + App.statusBadge(s.status) + "</span></div>" +
          '<div class="kv"><span class="k">Active borrows</span><span>' + App.activeBorrowsFor(studentId).length + "</span></div>" +
          '<div class="kv"><span class="k">Eligibility</span><span>' + (block.blocked ? App.badge("Blocked", "danger") : App.badge("Can borrow", "ok")) + "</span></div>" +
        "</div></div>" +
      '<div class="section-title" style="margin:18px 0 10px">Borrow / Return History</div>' +
      histHtml +
    "</div>");
}

/* ======================================================================== */
/*  INVENTORY (Tools + Lockers, with Add/Edit modals)                       */
/* ======================================================================== */
var _invTab = "tools";
function renderInventory() {
  _el("page").innerHTML =
    _pageHead("Manage tools and lockers. Add or edit items below.",
      '<button class="btn btn-primary btn-sm" id="invAdd"></button>') +
    '<div style="margin-bottom:16px"><div class="tabs">' +
      '<button class="tab" data-tab="tools">Tools</button>' +
      '<button class="tab" data-tab="lockers">Lockers</button></div></div>' +
    '<div class="card"><div class="table-wrap" id="invTable"></div></div>';

  Array.prototype.forEach.call(document.querySelectorAll(".tab"), function (t) {
    t.addEventListener("click", function () { _invTab = t.getAttribute("data-tab"); _drawInv(); });
  });
  _on("invAdd", "click", function () { _invTab === "tools" ? _toolModal() : _lockerModal(); });
  _drawInv();
}

function _drawInv() {
  Array.prototype.forEach.call(document.querySelectorAll(".tab"), function (t) {
    t.classList.toggle("active", t.getAttribute("data-tab") === _invTab);
  });
  _el("invAdd").innerHTML = App.icon("inventory") + (_invTab === "tools" ? "Add Tool" : "Add Locker");

  if (_invTab === "tools") {
    _el("invTable").innerHTML =
      '<table class="data"><thead><tr><th>Tool</th><th>RFID Tag</th><th>Assigned Locker</th><th>Status</th><th></th></tr></thead><tbody>' +
      DB.tools.map(function (t) {
        return "<tr><td class='cell-strong'>" + _esc(t.name) + "</td>" +
          "<td><code>" + _esc(t.rfidTag) + "</code></td>" +
          "<td>" + _esc(App.lockerNo(t.lockerId)) + "</td>" +
          "<td>" + App.statusBadge(t.status) + "</td>" +
          '<td style="text-align:right"><button class="btn btn-ghost btn-sm" onclick="_toolModal(\'' + t.id + '\')">Edit</button></td></tr>';
      }).join("") + "</tbody></table>";
  } else {
    _el("invTable").innerHTML =
      '<table class="data"><thead><tr><th>Locker</th><th>Tool</th><th>Ultrasonic Sensor</th><th>Occupancy</th><th>LED</th><th></th></tr></thead><tbody>' +
      DB.lockers.map(function (l) {
        return "<tr><td class='cell-strong'>" + _esc(l.number) + "</td>" +
          "<td>" + _esc(App.toolName(l.toolId)) + "</td>" +
          "<td>" + App.statusBadge(l.sensor) + "</td>" +
          "<td>" + App.statusBadge(l.occupancy) + "</td>" +
          '<td><span class="led-dot ' + l.led + '"></span> ' + _cap(l.led) + "</td>" +
          '<td style="text-align:right"><button class="btn btn-ghost btn-sm" onclick="_lockerModal(\'' + l.id + '\')">Edit</button></td></tr>';
      }).join("") + "</tbody></table>";
  }
}

function _field(label, inner) { return '<div class="field"><label>' + label + "</label>" + inner + "</div>"; }
function _txt(id, value) { return '<input class="input" id="' + id + '" value="' + _esc(value || "") + '" />'; }

/* POST to the API, refresh window.DB, close the modal, re-render. */
function _saveVia(endpoint, payload, after) {
  var btn = _el("fSave");
  if (btn) { btn.disabled = true; btn.textContent = "Saving…"; }
  Api.post(endpoint, payload)
    .then(function () { return Api.load(); })
    .then(function () { closeModal(); if (after) after(); })
    ["catch"](function (e) {
      if (btn) { btn.disabled = false; btn.textContent = "Save"; }
      alert(e.message);
    });
}

function _toolModal(id) {
  var t = id ? App.toolById(id) : null;
  var lockerOpts = DB.lockers.map(function (l) { return '<option value="' + l.id + '"' +
    (t && t.lockerId === l.id ? " selected" : "") + ">" + _esc(l.number) + "</option>"; }).join("");
  var statusOpts = ["available", "borrowed", "maintenance"].map(function (s) {
    return '<option value="' + s + '"' + (t && t.status === s ? " selected" : "") + ">" + _cap(s) + "</option>"; }).join("");

  openModal(
    '<div class="modal-head"><h2>' + (t ? "Edit Tool" : "Add Tool") + '</h2><button class="icon-btn" onclick="closeModal()">×</button></div>' +
    '<div class="modal-body">' +
      _field("Tool name", _txt("fName", t ? t.name : "")) +
      _field("RFID tag", _txt("fRfid", t ? t.rfidTag : "RFID-")) +
      _field("Assigned locker", '<select class="select" id="fLocker">' + lockerOpts + "</select>") +
      _field("Status", '<select class="select" id="fStatus">' + statusOpts + "</select>") +
    "</div>" +
    '<div class="modal-foot"><button class="btn btn-ghost" onclick="closeModal()">Cancel</button>' +
      '<button class="btn btn-primary" id="fSave">Save</button></div>');

  _on("fSave", "click", function () {
    var name = _val("fName"); if (!name) { _el("fName").focus(); return; }
    _saveVia("tools.php", {
      action: "save", id: t ? t.id : "",
      name: name, rfidTag: _val("fRfid"), lockerId: _val("fLocker"), status: _val("fStatus")
    }, _drawInv);
  });
}

function _lockerModal(id) {
  var l = id ? App.lockerById(id) : null;
  function opts(arr, sel) { return arr.map(function (o) {
    return '<option value="' + o + '"' + (sel === o ? " selected" : "") + ">" + _cap(o) + "</option>"; }).join(""); }
  var toolOpts = '<option value="">— none —</option>' + DB.tools.map(function (t) {
    return '<option value="' + t.id + '"' + (l && l.toolId === t.id ? " selected" : "") + ">" + _esc(t.name) + "</option>"; }).join("");

  openModal(
    '<div class="modal-head"><h2>' + (l ? "Edit Locker" : "Add Locker") + '</h2><button class="icon-btn" onclick="closeModal()">×</button></div>' +
    '<div class="modal-body">' +
      _field("Locker name / number", _txt("fNum", l ? l.number : "Locker ")) +
      _field("Tool", '<select class="select" id="fTool">' + toolOpts + "</select>") +
      '<div class="row-2">' +
        _field("Ultrasonic sensor", '<select class="select" id="fSensor">' + opts(["online", "offline"], l ? l.sensor : "online") + "</select>") +
        _field("Occupancy", '<select class="select" id="fOcc">' + opts(["present", "removed"], l ? l.occupancy : "present") + "</select>") +
      "</div>" +
      _field("LED indicator", '<select class="select" id="fLed">' + opts(["green", "red", "off"], l ? l.led : "green") + "</select>") +
    "</div>" +
    '<div class="modal-foot"><button class="btn btn-ghost" onclick="closeModal()">Cancel</button>' +
      '<button class="btn btn-primary" id="fSave">Save</button></div>');

  _on("fSave", "click", function () {
    var num = _val("fNum"); if (!num) { _el("fNum").focus(); return; }
    _saveVia("lockers.php", {
      action: "save", id: l ? l.id : "",
      number: num, toolId: _val("fTool"), sensor: _val("fSensor"), occupancy: _val("fOcc"), led: _val("fLed")
    }, _drawInv);
  });
}

/* ======================================================================== */
/*  BANNED STUDENTS                                                         */
/* ======================================================================== */
function renderBanned() {
  // Overdue active borrows that are NOT yet banned (at-risk)
  var atRisk = App.activeBorrows().filter(function (t) {
    var s = App.studentById(t.studentId);
    return t.status === "overdue" && (!s || s.status !== "banned");
  });

  var banRows = DB.bans.map(function (b) {
    var s = App.studentById(b.studentId);
    var tx = DB.transactions.find(function (t) { return t.id === b.transactionId; });
    var remaining = App.daysOverdue(b.until) === 0 ? Math.max(0, Math.ceil((new Date(b.until) - Date.now()) / 86400000)) : 0;
    return "<tr><td>" + _who(b.studentId) + "</td>" +
      '<td class="cell-strong">' + _esc(tx ? App.toolName(tx.toolId) : "—") + "</td>" +
      "<td>" + _esc(b.reason) + "</td>" +
      "<td>" + App.fmtDate(b.from) + " → " + App.fmtDate(b.until) + "</td>" +
      "<td>" + App.badge(remaining + " day(s) left", "danger") + "</td></tr>";
  }).join("") || _emptyRow(5, "No students are currently banned.");

  var riskRows = atRisk.map(function (t) {
    return "<tr><td>" + _who(t.studentId) + "</td>" +
      '<td class="cell-strong">' + _esc(App.toolName(t.toolId)) + "</td>" +
      "<td>" + _esc(App.lockerNo(t.lockerId)) + "</td>" +
      "<td>" + App.fmtDateTime(t.expectedReturn) + "</td>" +
      "<td>" + App.badge(App.daysOverdue(t.expectedReturn) + " day(s) overdue", "warn") + "</td></tr>";
  }).join("") || _emptyRow(5, "No overdue items.");

  _el("page").innerHTML =
    _pageHead("Students banned for overdue tools, plus students at risk of a ban.") +
    '<div class="note danger" style="margin-bottom:16px">Rule: a tool overdue for ' + DB.config.banTriggerDays +
      "+ days triggers a " + DB.config.banLengthDays + "-day borrowing ban. Banned students must return overdue items first.</div>" +

    '<div class="section-title">Currently Banned</div>' +
    '<div class="card"><div class="table-wrap"><table class="data"><thead><tr>' +
      "<th>Student</th><th>Overdue Tool</th><th>Reason</th><th>Ban Window</th><th>Remaining</th>" +
    '</tr></thead><tbody>' + banRows + "</tbody></table></div></div>" +

    '<div class="section-title">At Risk — Currently Overdue</div>' +
    '<div class="card"><div class="table-wrap"><table class="data"><thead><tr>' +
      "<th>Student</th><th>Tool</th><th>Locker</th><th>Was Due</th><th>Overdue</th>" +
    '</tr></thead><tbody>' + riskRows + "</tbody></table></div></div>";
}

/* ======================================================================== */
/*  TERMS & CONDITIONS                                                      */
/* ======================================================================== */
function renderTerms() {
  _el("page").innerHTML =
    _pageHead("The Terms &amp; Conditions shown on the locker terminal screen when a student borrows.") +
    '<div class="dash-grid">' +
      '<div class="card"><div class="card-head"><h2>Edit Terms</h2>' +
        '<span class="hint">Saved to the database</span></div>' +
        '<div class="card-body">' +
          '<textarea class="input" id="termsText" style="min-height:340px;resize:vertical;font-family:inherit;line-height:1.6"></textarea>' +
          '<div style="display:flex;gap:10px;margin-top:14px;align-items:center">' +
            '<button class="btn btn-primary" id="termsSave">Save Terms</button>' +
            '<button class="btn btn-ghost" id="termsReset">Reset</button>' +
            '<span id="termsMsg" class="badge badge-ok" style="display:none">Saved</span>' +
          "</div></div></div>" +
      '<div class="card"><div class="card-head"><h2>Terminal Preview</h2></div>' +
        '<div class="card-body">' +
          '<div class="note">⏱ Borrow limit: ' + DB.config.borrowLimitHours + " hours · Ban: overdue ≥ " +
            DB.config.banTriggerDays + " days → " + DB.config.banLengthDays + "-day ban</div>" +
          '<pre id="termsPreview" style="white-space:pre-wrap;background:var(--surface-2);border:1px solid var(--border);' +
            'border-radius:10px;padding:16px;margin-top:14px;font-family:inherit;font-size:13px;line-height:1.6"></pre>' +
        "</div></div>" +
    "</div>";

  _el("termsText").value = DB.terms;
  _el("termsPreview").textContent = DB.terms;
  _on("termsText", "input", function () { _el("termsPreview").textContent = _el("termsText").value; });
  _on("termsSave", "click", function () {
    var text = _el("termsText").value;
    var btn = _el("termsSave");
    btn.disabled = true; btn.textContent = "Saving…";
    Api.post("terms.php", { action: "save", terms: text }).then(function () {
      DB.terms = text;
      btn.disabled = false; btn.textContent = "Save Terms";
      var msg = _el("termsMsg"); msg.style.display = "inline-flex";
      setTimeout(function () { msg.style.display = "none"; }, 1800);
    })["catch"](function (e) {
      btn.disabled = false; btn.textContent = "Save Terms";
      alert(e.message);
    });
  });
  _on("termsReset", "click", function () {
    _el("termsText").value = DB.terms; _el("termsPreview").textContent = DB.terms;
  });
}

/* ---- Modal / Drawer plumbing (shared) --------------------------------- */
function _ensure(cls, id, onClose) {
  if (_el(id)) return;
  var ov = document.createElement("div"); ov.className = "overlay"; ov.id = id + "Ov";
  ov.addEventListener("click", onClose); document.body.appendChild(ov);
  var box = document.createElement("div"); box.className = cls; box.id = id;
  document.body.appendChild(box);
}
function openModal(html) {
  _ensure("modal", "modal", closeModal);
  _el("modal").innerHTML = html;
  _el("modalOv").classList.add("open"); _el("modal").classList.add("open");
}
function closeModal() { if (_el("modal")) { _el("modal").classList.remove("open"); _el("modalOv").classList.remove("open"); } }
function _openDrawer(html) {
  _ensure("drawer", "drawer", closeDrawer);
  _el("drawer").innerHTML = html;
  _el("drawerOv").classList.add("open"); _el("drawer").classList.add("open");
}
function closeDrawer() { if (_el("drawer")) { _el("drawer").classList.remove("open"); _el("drawerOv").classList.remove("open"); } }

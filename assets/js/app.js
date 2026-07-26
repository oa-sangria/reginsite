/* ============================================================================
   reginsite — Shared front-end layer.
   - Mock auth guard (sessionStorage only — prototype, NOT real security)
   - Renders the sidebar + topbar (built in JS, no fetch -> works on file:// too)
   - Date/format helpers, lookups, status badges
   - Derived stats + flattened borrow/return events used across pages
   ========================================================================== */
window.App = (function () {
  "use strict";

  var AUTH_KEY = "reginsite_auth";

  /* ---- Auth (server session via api/auth.php; sessionStorage = UI hint) -- */
  function login(user, pass) {
    return Api.post("auth.php", { action: "login", username: user, password: pass })
      .then(function (j) {
        sessionStorage.setItem(AUTH_KEY, JSON.stringify({ user: j.user.username, at: Date.now() }));
        return j.user;
      });
  }
  function logout() {
    Api.post("auth.php", { action: "logout" })["catch"](function () {})
      .then(function () {
        sessionStorage.removeItem(AUTH_KEY);
        location.href = "index.html";
      });
  }
  function currentUser() {
    try { return JSON.parse(sessionStorage.getItem(AUTH_KEY)); } catch (e) { return null; }
  }
  function requireAuth() {
    if (!currentUser()) { location.href = "index.html"; return false; }
    return true;
  }

  /* ---- Lookups ----------------------------------------------------------- */
  function studentById(id) { return DB.students.find(function (s) { return s.id === id; }); }
  function toolById(id)    { return DB.tools.find(function (t) { return t.id === id; }); }
  function lockerById(id)  { return DB.lockers.find(function (l) { return l.id === id; }); }
  function studentName(id) { var s = studentById(id); return s ? s.name : "—"; }
  function toolName(id)    { var t = toolById(id); return t ? t.name : "—"; }
  function lockerNo(id)    { var l = lockerById(id); return l ? l.number : (id || "—"); }

  /* ---- Date / format helpers -------------------------------------------- */
  function pad(n) { return n < 10 ? "0" + n : "" + n; }
  function asDate(d) { return d instanceof Date ? d : new Date(d); }
  function fmtDate(d) {
    if (!d) return "—"; d = asDate(d);
    var m = ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"];
    return m[d.getMonth()] + " " + d.getDate() + ", " + d.getFullYear();
  }
  function fmtTime(d) {
    if (!d) return "—"; d = asDate(d);
    var h = d.getHours(), ap = h >= 12 ? "PM" : "AM";
    h = h % 12; if (h === 0) h = 12;
    return h + ":" + pad(d.getMinutes()) + " " + ap;
  }
  function fmtDateTime(d) { return d ? fmtDate(d) + " · " + fmtTime(d) : "—"; }
  function relTime(d) {
    if (!d) return "—"; d = asDate(d);
    var s = Math.round((Date.now() - d.getTime()) / 1000);
    var future = s < 0; s = Math.abs(s);
    var out;
    if (s < 60) out = "just now";
    else if (s < 3600) out = Math.floor(s / 60) + "m";
    else if (s < 86400) out = Math.floor(s / 3600) + "h";
    else out = Math.floor(s / 86400) + "d";
    if (out === "just now") return out;
    return future ? "in " + out : out + " ago";
  }
  // Whole days a date is in the past (0 if future / today)
  function daysOverdue(d) {
    if (!d) return 0; d = asDate(d);
    var diff = Date.now() - d.getTime();
    return diff <= 0 ? 0 : Math.floor(diff / 86400000);
  }

  /* ---- Derived data ------------------------------------------------------ */
  function isReturnedToday(tx) {
    if (!tx.returnTime) return false;
    var r = asDate(tx.returnTime), n = new Date();
    return r.getFullYear() === n.getFullYear() && r.getMonth() === n.getMonth() && r.getDate() === n.getDate();
  }
  function activeBorrows() {
    return DB.transactions.filter(function (t) { return !t.returnTime; });
  }
  function activeBorrowsFor(studentId) {
    return activeBorrows().filter(function (t) { return t.studentId === studentId; });
  }
  // A student is borrow-blocked if they have any overdue / still-out item, or are banned.
  function borrowBlock(studentId) {
    var s = studentById(studentId);
    if (s && s.status === "banned") return { blocked: true, reason: "Banned until " + fmtDate(s.bannedUntil) };
    var out = activeBorrowsFor(studentId);
    var overdue = out.filter(function (t) { return t.status === "overdue"; });
    if (overdue.length) return { blocked: true, reason: "Has overdue item — must return first" };
    if (out.length) return { blocked: false, reason: out.length + " active borrow(s)" };
    return { blocked: false, reason: "Clear" };
  }
  function stats() {
    return {
      available: DB.tools.filter(function (t) { return t.status === "available"; }).length,
      borrowed: activeBorrows().length,
      returnedToday: DB.transactions.filter(isReturnedToday).length,
      overdue: DB.transactions.filter(function (t) { return t.status === "overdue"; }).length,
      banned: DB.students.filter(function (s) { return s.status === "banned"; }).length,
      maintenance: DB.tools.filter(function (t) { return t.status === "maintenance"; }).length,
      lockersOffline: DB.lockers.filter(function (l) { return l.sensor === "offline"; }).length
    };
  }
  // Flatten transactions into individual borrow/return events (newest first).
  function events() {
    var list = [];
    DB.transactions.forEach(function (t) {
      list.push({ type: "borrow", time: t.time = asDate(t.borrowTime), tx: t });
      if (t.returnTime) list.push({ type: "return", time: asDate(t.returnTime), tx: t });
    });
    list.sort(function (a, b) { return b.time - a.time; });
    return list;
  }

  /* ---- UI helpers -------------------------------------------------------- */
  function badge(text, kind) {
    return '<span class="badge badge-' + kind + '">' + text + "</span>";
  }
  function statusBadge(status) {
    var map = {
      borrowed: "info", overdue: "danger", returned: "ok",
      available: "ok", maintenance: "warn", banned: "danger", active: "ok",
      online: "ok", offline: "muted", present: "ok", removed: "danger"
    };
    return badge(status.charAt(0).toUpperCase() + status.slice(1), map[status] || "muted");
  }
  function escapeHtml(str) {
    return String(str == null ? "" : str)
      .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;").replace(/'/g, "&#39;");
  }
  function initials(name) {
    return name.split(" ").map(function (w) { return w[0]; }).join("").slice(0, 2).toUpperCase();
  }

  /* ---- Icons (inline SVG) ------------------------------------------------ */
  var ICONS = {
    dashboard: '<path d="M3 13h8V3H3v10zm0 8h8v-6H3v6zm10 0h8V11h-8v10zm0-18v6h8V3h-8z"/>',
    borrow:    '<path d="M20 8h-3V4H3v13h2a3 3 0 0 0 6 0h2a3 3 0 0 0 6 0h1v-5l-3-4zM8 18.5A1.5 1.5 0 1 1 8 15.5a1.5 1.5 0 0 1 0 3zm9.5 0a1.5 1.5 0 1 1 0-3 1.5 1.5 0 0 1 0 3zM17 12V9.5h2.14L21 12h-4z"/>',
    ret:       '<path d="M12 5V1L7 6l5 5V7a6 6 0 1 1-6 6H4a8 8 0 1 0 8-8z"/>',
    students:  '<path d="M16 11c1.66 0 3-1.34 3-3s-1.34-3-3-3-3 1.34-3 3 1.34 3 3 3zm-8 0c1.66 0 3-1.34 3-3S9.66 5 8 5 5 6.34 5 8s1.34 3 3 3zm0 2c-2.33 0-7 1.17-7 3.5V19h14v-2.5c0-2.33-4.67-3.5-7-3.5zm8 0c-.29 0-.62.02-.97.05 1.16.84 1.97 1.97 1.97 3.45V19h6v-2.5c0-2.33-4.67-3.5-7-3.5z"/>',
    inventory: '<path d="M20 2H4c-1 0-2 .9-2 2v3.01c0 .72.43 1.34 1 1.69V20c0 1.1 1.1 2 2 2h14c.9 0 2-.9 2-2V8.7c.57-.35 1-.97 1-1.69V4c0-1.1-1-2-2-2zm-5 12H9v-2h6v2zm5-7H4V4h16v3z"/>',
    tx:        '<path d="M9.4 16.6 4.8 12l4.6-4.6L8 6l-6 6 6 6 1.4-1.4zm5.2 0 4.6-4.6-4.6-4.6L16 6l6 6-6 6-1.4-1.4z"/>',
    banned:    '<path d="M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zM4 12a8 8 0 0 1 12.9-6.32L5.68 16.9A7.96 7.96 0 0 1 4 12zm8 8a7.96 7.96 0 0 1-4.9-1.68L18.32 7.1A8 8 0 0 1 12 20z"/>',
    terms:     '<path d="M14 2H6c-1.1 0-2 .9-2 2v16c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z"/>',
    logout:    '<path d="M17 7l-1.41 1.41L18.17 11H8v2h10.17l-2.58 2.58L17 17l5-5zM4 5h8V3H4c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h8v-2H4V5z"/>',
    lock:      '<path d="M18 8h-1V6A5 5 0 0 0 7 6v2H6a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V10a2 2 0 0 0-2-2zM9 6a3 3 0 0 1 6 0v2H9V6zm3 11a2 2 0 1 1 0-4 2 2 0 0 1 0 4z"/>'
  };
  function icon(name, cls) {
    return '<svg class="icon ' + (cls || "") + '" viewBox="0 0 24 24" aria-hidden="true">' + (ICONS[name] || "") + "</svg>";
  }

  /* ---- Layout (sidebar + topbar) ----------------------------------------- */
  var NAV = [
    { group: "Overview" },
    { page: "dashboard",    href: "dashboard.html",    label: "Dashboard",    icon: "dashboard" },
    { group: "Activity Logs" },
    { page: "borrow-log",   href: "borrow-log.html",   label: "Borrow Log",   icon: "borrow" },
    { page: "return-log",   href: "return-log.html",   label: "Return Log",   icon: "ret" },
    { page: "transactions", href: "transactions.html", label: "Transaction Log", icon: "tx" },
    { group: "Management" },
    { page: "inventory",    href: "inventory.html",    label: "Inventory",    icon: "inventory" },
    { page: "students",     href: "students.html",     label: "Student Log",  icon: "students" },
    { page: "banned",       href: "banned.html",       label: "Banned Students", icon: "banned" },
    { page: "terms",        href: "terms.html",        label: "Terms & Conditions", icon: "terms" }
  ];
  var TITLES = {
    dashboard: "Dashboard", "borrow-log": "Borrow Module Log", "return-log": "Return Module Log",
    students: "Student Log", inventory: "Inventory Management", transactions: "Tool Transaction Log",
    banned: "Banned Students", terms: "Terms & Conditions"
  };

  function renderLayout() {
    var active = document.body.getAttribute("data-page");
    var st = stats();
    var navHtml = NAV.map(function (n) {
      if (n.group) return '<div class="nav-group">' + escapeHtml(n.group) + "</div>";
      var badgeStr = "";
      if (n.page === "banned" && st.banned)  badgeStr = '<span class="nav-count">' + st.banned + "</span>";
      if (n.page === "borrow-log" && st.overdue) badgeStr = '<span class="nav-count warn">' + st.overdue + "</span>";
      if (n.page === "transactions" && st.overdue) badgeStr = '<span class="nav-count warn">' + st.overdue + "</span>";
      return '<a class="nav-item' + (n.page === active ? " active" : "") + '" href="' + n.href + '">' +
        '<span class="nav-ic">' + icon(n.icon) + "</span><span>" + n.label + "</span>" + badgeStr + "</a>";
    }).join("");

    var sidebar = document.getElementById("sidebar");
    if (sidebar) {
      var sensorsOk = DB.lockers.filter(function (l) { return l.sensor === "online"; }).length;
      sidebar.innerHTML =
        '<div class="brand">' + icon("lock", "brand-icon") +
          '<div><div class="brand-name">Reginsite</div><div class="brand-sub">Smart Tool Locker</div></div></div>' +
        '<nav class="nav">' + navHtml + "</nav>" +
        '<div class="sidebar-foot">' +
          '<div class="sys-status"><span class="pulse-dot"></span><div><strong>ESP32 Sync · Live</strong>' +
            '<small>' + sensorsOk + " / " + DB.lockers.length + " sensors online</small></div></div>" +
        "</div>";
    }

    var topbar = document.getElementById("topbar");
    if (topbar) {
      var u = currentUser();
      var now = new Date();
      var dateStr = fmtDate(now);
      topbar.innerHTML =
        '<div class="topbar-title"><button class="menu-toggle" id="menuToggle" aria-label="Menu">☰</button>' +
          '<div><h1>' + (TITLES[active] || "") + "</h1>" +
          '<div class="topbar-date">' + dateStr + "</div></div></div>" +
        '<div class="topbar-right">' +
          '<span class="live-pill"><span class="pulse-dot"></span>Live</span>' +
          '<div class="admin-chip"><span class="avatar">AD</span><span class="admin-meta">' +
            "<strong>Administrator</strong><small>" + escapeHtml(u ? u.user : "admin") + "</small></span></div>" +
          '<button class="btn btn-ghost btn-sm" id="logoutBtn">' + icon("logout") + "Logout</button>" +
        "</div>";
      var lb = document.getElementById("logoutBtn");
      if (lb) lb.addEventListener("click", logout);
      var mt = document.getElementById("menuToggle");
      if (mt) mt.addEventListener("click", function () {
        document.body.classList.toggle("sidebar-open");
      });
    }
  }

  /* Boot a page: client auth hint -> fetch data -> layout -> page renderer.
     Redirects to the installer if the DB is missing, to login on 401. */
  function init(renderFn) {
    if (!requireAuth()) return;
    Api.load().then(function () {
      renderLayout();
      if (renderFn) renderFn();
    })["catch"](function (err) {
      if (err.code === "no_db") { location.href = "api/install.php"; return; }
      if (err.status === 401) { sessionStorage.removeItem(AUTH_KEY); location.href = "index.html"; return; }
      document.body.innerHTML =
        '<div style="font-family:sans-serif;padding:48px;max-width:560px;margin:auto">' +
        "<h2>Could not load data</h2><p>" + escapeHtml(err.message) + "</p>" +
        '<p>Check that Apache & MySQL are running in XAMPP, then <a href="javascript:location.reload()">retry</a>.</p></div>';
    });
  }

  return {
    // auth
    login: login, logout: logout, currentUser: currentUser, requireAuth: requireAuth, init: init,
    // lookups
    studentById: studentById, toolById: toolById, lockerById: lockerById,
    studentName: studentName, toolName: toolName, lockerNo: lockerNo,
    // dates
    fmtDate: fmtDate, fmtTime: fmtTime, fmtDateTime: fmtDateTime, relTime: relTime, daysOverdue: daysOverdue,
    // derived
    stats: stats, events: events, activeBorrows: activeBorrows, activeBorrowsFor: activeBorrowsFor,
    borrowBlock: borrowBlock, isReturnedToday: isReturnedToday,
    // ui
    badge: badge, statusBadge: statusBadge, escapeHtml: escapeHtml, initials: initials, icon: icon
  };
})();

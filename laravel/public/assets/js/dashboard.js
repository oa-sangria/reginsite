/* ============================================================================
   reginsite — Dashboard renderer (KPIs, locker map, alerts, activity feed)
   ========================================================================== */
function renderDashboard() {
  var st = App.stats();
  var totalTools = DB.tools.length;

  /* ---- KPI cards --------------------------------------------------------- */
  var kpis = [
    { val: st.available,     label: "Available Tools",  kind: "ok",     icon: "inventory", sub: "of " + totalTools + " total" },
    { val: st.borrowed,      label: "Borrowed Tools",   kind: "info",   icon: "borrow",    sub: "currently out" },
    { val: st.returnedToday, label: "Returned Today",   kind: "ok",     icon: "ret",       sub: "logged today" },
    { val: st.overdue,       label: "Overdue",          kind: "danger", icon: "tx",        sub: st.overdue ? "needs attention" : "all on time" },
    { val: st.banned,        label: "Banned Students",  kind: "warn",   icon: "banned",    sub: st.banned ? "active bans" : "none active" }
  ];
  document.getElementById("kpis").innerHTML = kpis.map(function (k) {
    return '<div class="kpi ' + k.kind + '">' +
      '<div class="kpi-top"><span class="kpi-ic">' + App.icon(k.icon) + "</span></div>" +
      '<div class="kpi-val">' + k.val + "</div>" +
      '<div class="kpi-label">' + k.label + "</div>" +
      '<div class="kpi-sub">' + k.sub + "</div></div>";
  }).join("");

  /* ---- Tool inventory breakdown ------------------------------------------ */
  renderInventoryBreakdown(st, totalTools);

  /* ---- Locker map -------------------------------------------------------- */
  var present = DB.lockers.filter(function (l) { return l.sensor !== "offline" && l.occupancy === "present"; }).length;
  var removed = DB.lockers.filter(function (l) { return l.sensor !== "offline" && l.occupancy === "removed"; }).length;
  document.getElementById("lockerLegend").innerHTML =
    '<span class="lg"><span class="led-dot green"></span>' + present + " present</span>" +
    '<span class="lg"><span class="led-dot red"></span>' + removed + " out</span>" +
    '<span class="lg"><span class="led-dot off"></span>' + st.lockersOffline + " offline</span>";

  document.getElementById("lockerMap").innerHTML = DB.lockers.map(function (l) {
    var tool = App.toolById(l.toolId);
    var cls, state;
    if (l.sensor === "offline")            { cls = "offline";     state = "Sensor Offline"; }
    else if (tool && tool.status === "maintenance") { cls = "maintenance"; state = "Maintenance"; }
    else if (l.occupancy === "removed")    { cls = "removed";     state = "Tool Removed"; }
    else                                   { cls = "present";     state = "Tool Present"; }

    // Who currently holds the tool from this locker (active borrow)?
    var active = App.activeBorrows().filter(function (t) { return t.lockerId === l.id; })[0];
    var borrower = active
      ? '<div class="locker-borrower">Borrowed by <b>' + App.escapeHtml(App.studentName(active.studentId)) +
          '</b> · ' + App.relTime(active.borrowTime) + "</div>"
      : "";

    return '<div class="locker ' + cls + '">' +
        '<div class="locker-top"><span class="locker-no">' + App.escapeHtml(l.number) + "</span>" +
          '<span class="led-dot ' + l.led + '" title="LED ' + l.led + '"></span></div>' +
        '<div class="locker-tool">' + App.escapeHtml(tool ? tool.name : "—") + "</div>" +
        '<div class="locker-state">' + state + "</div>" +
        '<div class="locker-meta"><span class="sensor-dot ' + l.sensor + '"></span> Ultrasonic ' + l.sensor +
          ' · LED ' + l.led + "</div>" +
        borrower +
      "</div>";
  }).join("");

  /* ---- Alerts ------------------------------------------------------------ */
  var alerts = [];
  alerts.push(alertRow("danger", st.overdue, "Overdue tools", "Past the 8-hour borrow limit", "transactions.html"));
  alerts.push(alertRow("warn", st.banned, "Banned students", "Overdue ≥ 2 days · 2-day ban", "banned.html"));
  alerts.push(alertRow("muted", st.lockersOffline, "Lockers offline", "Ultrasonic sensor not reporting", "inventory.html"));
  alerts.push(alertRow("muted", st.maintenance, "Tools in maintenance", "Temporarily unavailable", "inventory.html"));
  document.getElementById("alerts").innerHTML = alerts.join("");
  var attention = st.overdue + st.banned + st.lockersOffline + st.maintenance;
  document.getElementById("alertCount").textContent = attention + " flagged";

  /* ---- Recent activity --------------------------------------------------- */
  var evs = App.events().slice(0, 8);
  document.getElementById("feed").innerHTML = evs.map(function (e) {
    var verb = e.type === "borrow" ? "borrowed" : "returned";
    var icoCls = e.type === "borrow" ? "borrow" : "ret";
    var icoName = e.type === "borrow" ? "borrow" : "ret";
    return '<div class="feed-item">' +
        '<span class="feed-ic ' + icoCls + '">' + App.icon(icoName) + "</span>" +
        '<div class="feed-main"><div class="t"><b>' + App.escapeHtml(App.studentName(e.tx.studentId)) + "</b> " +
          verb + " <b>" + App.escapeHtml(App.toolName(e.tx.toolId)) + "</b></div>" +
          '<div class="cell-sub">' + App.escapeHtml(App.lockerNo(e.tx.lockerId)) + "</div></div>" +
        '<span class="feed-time">' + App.relTime(e.time) + "</span>" +
      "</div>";
  }).join("") || '<div class="empty">No activity yet.</div>';
}

function alertRow(kind, count, title, sub, href) {
  return '<a class="alert-row ' + kind + (count ? "" : " is-clear") + '" href="' + href + '">' +
    '<span class="big">' + count + "</span>" +
    '<span class="txt"><strong>' + title + "</strong><small>" + sub + "</small></span>" +
    '<span class="alert-arrow">→</span></a>';
}

/* Horizontal stacked bar of tool statuses + a legend. */
function renderInventoryBreakdown(st, total) {
  document.getElementById("invTotal").textContent = total + " tools";
  var segs = [
    { n: st.available,   cls: "ok",     label: "Available" },
    { n: st.borrowed,    cls: "info",   label: "Borrowed" },
    { n: st.maintenance, cls: "warn",   label: "Maintenance" }
  ];
  var bar = segs.filter(function (s) { return s.n > 0; }).map(function (s) {
    return '<span class="seg seg-' + s.cls + '" style="flex:' + s.n + '" title="' +
      s.label + ": " + s.n + '"></span>';
  }).join("");

  var legend = segs.map(function (s) {
    var pct = total ? Math.round((s.n / total) * 100) : 0;
    return '<div class="leg-row"><span class="leg-dot leg-' + s.cls + '"></span>' +
      '<span class="leg-label">' + s.label + "</span>" +
      '<span class="leg-val">' + s.n + '<small>' + pct + "%</small></span></div>";
  }).join("");

  document.getElementById("inventoryBreakdown").innerHTML =
    '<div class="util-bar">' + bar + "</div>" +
    '<div class="util-legend">' + legend + "</div>";
}

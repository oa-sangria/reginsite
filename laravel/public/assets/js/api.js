/* ============================================================================
   reginsite — API client. Replaces the old static data.js.
   Api.load() fills window.DB with the same shape data.js used, so all
   existing renderers (app.js / dashboard.js / pages.js) work unchanged.
   ========================================================================== */
window.Api = (function () {
  "use strict";

  var BASE = "/api/";

  function request(path, opts) {
    return fetch(BASE + path, opts).then(function (res) {
      return res.json().catch(function () {
        throw new Error("Server returned an invalid response (" + res.status + ")");
      }).then(function (j) {
        if (!res.ok || j.ok === false) {
          var err = new Error(j.error || ("Request failed (" + res.status + ")"));
          err.status = res.status;
          err.code = j.code || "";
          throw err;
        }
        return j;
      });
    });
  }

  function get(path) { return request(path); }
  function post(path, data) {
    return request(path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(data || {})
    });
  }

  /* Fetch the full dataset into window.DB (shape mirrors old data.js). */
  function load() {
    return get("bootstrap").then(function (j) {
      window.DB = j.data;
      return j.data;
    });
  }

  return { load: load, get: get, post: post };
})();

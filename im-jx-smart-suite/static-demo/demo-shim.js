/* IM JX Smart Suite — version de démonstration statique (lecture seule).
   Remplace le backend FastAPI : les réponses GET viennent d'un instantané enregistré
   (demo-data.json), les actions d'écriture sont désactivées, le temps réel est simulé. */
(function () {
  const realFetch = window.fetch.bind(window);
  const DEMO_MSG = "Action désactivée dans la version de démonstration (lecture seule).";
  let dataPromise = null;
  function loadData() {
    if (!dataPromise) {
      dataPromise = realFetch("/demo-data.json.gz")
        .then((r) => new Response(r.body.pipeThrough(new DecompressionStream("gzip"))).json())
        .then((d) => {
          // Instantané dédupliqué : refs[utilisateur][url] -> pool[hash] = {status, body}
          d.responses = {};
          Object.keys(d.refs).forEach((u) => {
            const t = (d.responses[u] = {});
            Object.keys(d.refs[u]).forEach((k) => { t[k] = d.pool[d.refs[u][k]]; });
          });
          return d;
        });
    }
    return dataPromise;
  }
  loadData();

  function json(body, status) {
    return new Response(JSON.stringify(body), { status: status || 200, headers: { "Content-Type": "application/json" } });
  }

  function currentUser(data, headers) {
    let auth = "";
    if (headers) {
      if (headers instanceof Headers) auth = headers.get("Authorization") || "";
      else auth = headers.Authorization || headers.authorization || "";
    }
    const m = auth.match(/demo-([A-Za-z0-9_]+)/);
    if (m && data.sessions[m[1]]) return m[1];
    try {
      const s = JSON.parse(localStorage.getItem("imjx_session") || "null");
      if (s && data.sessions[s.username]) return s.username;
    } catch (e) {}
    return "admin";
  }

  function stripParam(key, name) {
    const [path, qs] = key.split("?");
    if (!qs) return key;
    const p = new URLSearchParams(qs);
    p.delete(name);
    const rest = p.toString();
    return rest ? path + "?" + rest : path + "?";
  }

  function lookup(table, key, seedDay) {
    if (!table) return null;
    if (table[key]) return table[key];
    // Les données de démo sont datées du jour de l'enregistrement : on ignore la date demandée.
    if (/[?&]day=/.test(key)) {
      const swapped = key.replace(/([?&]day=)[^&]*/, "$1" + seedDay);
      if (table[swapped]) return table[swapped];
    }
    const noSince = stripParam(key, "since_id");
    if (table[noSince]) return table[noSince];
    const path = key.split("?")[0];
    if (table[path]) return table[path];
    if (table[path + "?"]) return table[path + "?"];
    const sameKey = Object.keys(table).find((k) => k.split("?")[0] === path);
    return sameKey ? table[sameKey] : null;
  }

  function listLike(path) {
    return /\/api\/(alarms|maintenance|reclamations|sync|commands|chat\/messages|chat\/search|machines|users|analytics\/traceability)/.test(path);
  }

  async function handle(url, init) {
    const method = ((init && init.method) || "GET").toUpperCase();
    const data = await loadData();
    const key = url.pathname + url.search;

    if (url.pathname === "/api/demo-info") return json({ demo: true, static: true });
    if (url.pathname === "/api/health") return json({ status: "ok", demo: true });

    if (url.pathname === "/api/auth/login" && method === "POST") {
      let body = {};
      try { body = JSON.parse((init && init.body) || "{}"); } catch (e) {}
      const u = String(body.username || "").trim();
      if (data.passwords[u] && data.passwords[u] === body.password) return json(data.sessions[u]);
      return json({ detail: "Identifiants incorrects" }, 401);
    }

    if (method !== "GET") return json({ detail: DEMO_MSG }, 403);

    if (url.pathname.startsWith("/api/export")) return json({ detail: DEMO_MSG }, 403);

    if (url.pathname === "/api/chat/search") {
      const q = (url.searchParams.get("q") || "").toLowerCase();
      const ch = url.searchParams.get("channel") || "GENERAL";
      const user = currentUser(data, init && init.headers);
      const hit = lookup(data.responses[user], "/api/chat/messages?channel=" + encodeURIComponent(ch) + "&limit=50", data.seedDay);
      const msgs = hit && Array.isArray(hit.body) ? hit.body : (hit && hit.body && hit.body.messages) || [];
      return json(msgs.filter((m) => JSON.stringify(m).toLowerCase().includes(q)));
    }

    const user = currentUser(data, init && init.headers);
    const hit = lookup(data.responses[user], key, data.seedDay) || lookup(data.responses.admin, key, data.seedDay);
    if (hit) return json(hit.body, hit.status);
    return json(listLike(url.pathname) ? [] : {}, listLike(url.pathname) ? 200 : 404);
  }

  window.fetch = function (input, init) {
    try {
      const raw = typeof input === "string" ? input : input.url;
      const url = new URL(raw, location.href);
      if (url.origin === location.origin && url.pathname.startsWith("/api/")) {
        if (!init && typeof input !== "string") init = { method: input.method, headers: input.headers };
        return handle(url, init || {});
      }
    } catch (e) {}
    return realFetch(input, init);
  };

  // Temps réel simulé : connexion « ouverte », aucun message.
  function FakeWebSocket() {
    this.readyState = 0;
    const self = this;
    setTimeout(function () { self.readyState = 1; if (self.onopen) self.onopen({}); }, 80);
  }
  FakeWebSocket.prototype.send = function () {};
  FakeWebSocket.prototype.close = function () { this.readyState = 3; };
  FakeWebSocket.prototype.addEventListener = function () {};
  FakeWebSocket.CONNECTING = 0; FakeWebSocket.OPEN = 1; FakeWebSocket.CLOSING = 2; FakeWebSocket.CLOSED = 3;
  window.WebSocket = FakeWebSocket;

  // Bandeau discret « démonstration »
  document.addEventListener("DOMContentLoaded", function () {
    const b = document.createElement("div");
    b.textContent = "Version de démonstration · données fictives · lecture seule";
    b.style.cssText = "position:fixed;top:0;right:0;z-index:99999;font:10px monospace;background:rgba(180,83,9,.92);color:#fff;padding:3px 8px;border-bottom-left-radius:6px;pointer-events:none;";
    document.body.appendChild(b);
  });
})();

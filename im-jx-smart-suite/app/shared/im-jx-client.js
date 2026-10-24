/* IM JX Smart Suite - client partagé (auth, sync backend, notifications live) */
(function () {
  const API_BASE = ""; // même origine (servi par le backend FastAPI)
  const WS_BASE = (location.protocol === "https:" ? "wss://" : "ws://") + location.host;
  const ALERT_KEYWORDS = ["RECLAM", "ALARM", "DISPATCH", "TASK", "ADMIN", "ESCAL", "VALIDATION"];

  function getSession() {
    try {
      return JSON.parse(localStorage.getItem("imjx_session") || "null");
    } catch (e) {
      return null;
    }
  }

  function logout() {
    localStorage.removeItem("imjx_session");
    window.location.href = "/login.html";
  }

  function requireAuth(cfg) {
    const session = getSession();
    if (!session || !session.token) {
      window.location.href = "/login.html";
      return null;
    }
    if (cfg && cfg.department && session.department !== cfg.department && session.role !== "admin") {
      window.location.href = "/login.html";
      return null;
    }
    if (cfg && cfg.roles && cfg.roles.length && !cfg.roles.includes(session.role) && session.role !== "admin") {
      window.location.href = "/login.html";
      return null;
    }
    return session;
  }

  function authHeaders(session) {
    return {
      "Content-Type": "application/json",
      "Authorization": "Bearer " + session.token,
    };
  }

  function isAlertPayload(raw) {
    return ALERT_KEYWORDS.some((k) => String(raw).toUpperCase().includes(k));
  }

  // Remplace les anciens appels fetch(googleScriptUrl, ...) : route intelligemment
  // vers /api/sync (log de routine) ou /api/reclamations (alerte instantanée).
  function sync(rawPayload) {
    const session = getSession();
    if (!session) return Promise.reject(new Error("no session"));
    const cfg = window.IMJX_PAGE || {};
    const endpoint = isAlertPayload(rawPayload) ? "/api/reclamations" : "/api/sync";
    const body = isAlertPayload(rawPayload)
      ? { department: cfg.department, operator: session.full_name, rawData: rawPayload }
      : { department: cfg.department, page: cfg.page || "", operator: session.full_name, rawData: rawPayload };

    return fetch(API_BASE + endpoint, {
      method: "POST",
      headers: authHeaders(session),
      body: JSON.stringify(body),
    });
  }

  // ---------- Notifications live (bandeau) ----------
  function ensureToastRoot() {
    let root = document.getElementById("imjx-toast-root");
    if (!root) {
      root = document.createElement("div");
      root.id = "imjx-toast-root";
      root.style.cssText = "position:fixed;top:8px;left:50%;transform:translateX(-50%);z-index:99999;display:flex;flex-direction:column;gap:6px;max-width:92vw;width:420px;";
      document.body.appendChild(root);
    }
    return root;
  }

  function showToast(data) {
    const root = ensureToastRoot();
    const el = document.createElement("div");
    const urgent = data.priority === "urgent" || data.type === "alarm";
    el.style.cssText = `font-family:monospace;font-size:12px;padding:10px 14px;border-radius:8px;color:#fff;box-shadow:0 4px 14px rgba(0,0,0,.4);border:1px solid ${urgent ? "#f87171" : "#38bdf8"};background:${urgent ? "rgba(127,29,29,.95)" : "rgba(15,23,42,.95)"};animation:imjx-fade .2s ease-in;`;
    const label = data.type === "command" ? "📢 COMMANDE ADMIN" : data.type === "alarm" ? "🚨 ALERTE" : "ℹ️";
    el.innerHTML = `<div style="font-weight:bold;margin-bottom:2px;">${label} — ${data.sender || data.operator || ""}</div><div>${(data.message || data.raw || "").toString().slice(0, 220)}</div>`;
    root.appendChild(el);
    setTimeout(() => { el.style.opacity = "0"; el.style.transition = "opacity .5s"; setTimeout(() => el.remove(), 500); }, 9000);
  }

  // ---------- Connexion WebSocket temps réel ----------
  let ws;
  let reconnectAttempts = 0;
  let reconnectTimer = null;
  const RECONNECT_DELAYS = [3000, 5000, 10000, 20000, 30000]; // backoff progressif, plafonné à 30s

  function scheduleReconnect(onMessage) {
    if (reconnectTimer) return;
    const delay = RECONNECT_DELAYS[Math.min(reconnectAttempts, RECONNECT_DELAYS.length - 1)];
    reconnectAttempts++;
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      connectWS(onMessage);
    }, delay);
  }

  function connectWS(onMessage) {
    const session = getSession();
    if (!session) return;
    ws = new WebSocket(WS_BASE + "/ws?token=" + encodeURIComponent(session.token));
    ws.onopen = () => { reconnectAttempts = 0; };
    ws.onmessage = (evt) => {
      try {
        const data = JSON.parse(evt.data);
        if (data.type === "ping") return; // heartbeat serveur, rien à afficher
        if (typeof onMessage === "function") onMessage(data);
        if (data.type === "command" || data.type === "alarm" || data.type === "alarm_escalated") {
          showToast(data); addNotification(data, session);
        }
        if ((data.type === "chat_message" || data.type === "chat_delete" || data.type === "presence") && window.IMJX.onChatEvent) {
          window.IMJX.onChatEvent(data);
        }
      } catch (e) { /* ignore */ }
    };
    ws.onclose = (evt) => {
      // Code 4401 = session expirée / token invalide (voir backend get_ws_user) :
      // inutile de boucler indéfiniment, on prévient l'utilisateur et on le renvoie au login.
      if (evt.code === 4401) {
        showToast({ type: "alarm", sender: "Système", message: "Session expirée — merci de vous reconnecter." });
        setTimeout(() => logout(), 2500);
        return;
      }
      scheduleReconnect(onMessage);
    };
  }

  // ---------- Barre flottante utilisateur ----------
  function injectUserBar(session) {
    const bar = document.createElement("div");
    bar.style.cssText = "position:fixed;bottom:20px;right:8px;z-index:99998;font-family:monospace;font-size:10px;background:rgba(15,23,42,.9);color:#94a3b8;padding:6px 10px;border-radius:8px;border:1px solid #334155;display:flex;gap:8px;align-items:center;";
    bar.innerHTML = `<span>${session.full_name} · ${session.department}/${session.role}</span><button id="imjx-logout" style="background:#334155;color:#fff;border:none;padding:3px 8px;border-radius:4px;cursor:pointer;">Logout</button>`;
    document.body.appendChild(bar);
    document.getElementById("imjx-logout").onclick = logout;
  }

  // ---------- Pied de page de marque unifié (identique sur toutes les pages) ----------
  // Point UNIQUE à modifier pour changer la signature/le style affiché partout dans l'app.
  function injectBrandFooter() {
    if (document.getElementById("imjx-brand-footer")) return;
    const footer = document.createElement("div");
    footer.id = "imjx-brand-footer";
    footer.style.cssText = "position:fixed;bottom:0;left:0;right:0;z-index:99997;text-align:center;font-family:monospace;font-size:9px;letter-spacing:.06em;color:#64748b;background:rgba(2,6,23,.9);border-top:1px solid #1e293b;padding:3px 0;pointer-events:none;";
    footer.textContent = "IM JX Smart Suite · by NAJAR JX";
    document.body.appendChild(footer);
  }

  // ---------- Centre de notifications : LA place unique où arrivent toutes les
  // commandes admin + alertes/alarmes reçues (historique + live via WebSocket). ----------
  const NOTIF_READ_KEY_PREFIX = "imjx_notif_lastread_";
  let notifList = [];

  function notifReadKey(session) {
    return NOTIF_READ_KEY_PREFIX + (session.username || session.full_name || "user");
  }

  function getLastReadId(session) {
    return parseInt(localStorage.getItem(notifReadKey(session)) || "0", 10);
  }

  function setLastReadId(session, id) {
    localStorage.setItem(notifReadKey(session), String(id));
  }

  function renderNotifPanel(session) {
    const list = document.getElementById("imjx-notif-list");
    const badge = document.getElementById("imjx-notif-badge");
    if (!list) return;
    const lastRead = getLastReadId(session);
    const unread = notifList.filter((n) => n.id > lastRead).length;
    if (badge) {
      badge.style.display = unread > 0 ? "flex" : "none";
      badge.textContent = unread > 9 ? "9+" : String(unread);
    }
    if (!notifList.length) {
      list.innerHTML = '<div style="padding:14px;text-align:center;color:#64748b;">Aucune commande reçue</div>';
      return;
    }
    list.innerHTML = notifList.slice(0, 30).map((n) => {
      const urgent = n.priority === "urgent" || n.type === "alarm";
      const color = urgent ? "#f87171" : "#38bdf8";
      const label = n.type === "alarm" ? "🚨 ALARME" : "📢 COMMANDE";
      return `<div style="padding:8px 10px;border-bottom:1px solid #1e293b;border-left:3px solid ${color};">
        <div style="font-weight:bold;font-size:10px;color:${color};">${label} — ${n.sender || "Admin"}</div>
        <div style="font-size:11px;margin:3px 0;color:#e2e8f0;">${(n.message || "").toString().slice(0, 300)}</div>
        <div style="font-size:9px;color:#64748b;">${n.timestamp ? new Date(n.timestamp).toLocaleString() : ""}</div>
      </div>`;
    }).join("");
  }

  function addNotification(data, session) {
    if (!session || !data || (data.type !== "command" && data.type !== "alarm")) return;
    notifList.unshift({
      id: data.id || Date.now(),
      sender: data.sender,
      message: data.message || data.raw,
      priority: data.priority,
      type: data.type,
      timestamp: data.timestamp || new Date().toISOString(),
    });
    notifList = notifList.slice(0, 50);
    renderNotifPanel(session);
  }

  async function loadNotifHistory(session) {
    try {
      const res = await fetch(API_BASE + "/api/commands/mine?since_id=0", { headers: authHeaders(session) });
      if (!res.ok) return;
      const rows = await res.json();
      notifList = rows.map((r) => ({ ...r, type: "command" }));
      renderNotifPanel(session);
    } catch (e) { /* ignore */ }
  }

  function injectNotificationCenter(session) {
    const wrap = document.createElement("div");
    wrap.style.cssText = "position:fixed;bottom:58px;right:8px;z-index:99998;font-family:monospace;";
    wrap.innerHTML = `
      <button id="imjx-notif-btn" style="position:relative;background:rgba(15,23,42,.9);color:#94a3b8;border:1px solid #334155;padding:6px 10px;border-radius:8px;cursor:pointer;font-size:13px;">
        🔔<span id="imjx-notif-badge" style="display:none;position:absolute;top:-6px;right:-6px;background:#ef4444;color:#fff;font-size:9px;min-width:16px;height:16px;border-radius:8px;align-items:center;justify-content:center;padding:0 3px;"></span>
      </button>
      <div id="imjx-notif-panel" style="display:none;position:absolute;bottom:40px;right:0;width:320px;max-width:88vw;max-height:360px;overflow-y:auto;background:rgba(15,23,42,.98);border:1px solid #334155;border-radius:8px;box-shadow:0 8px 24px rgba(0,0,0,.5);">
        <div style="padding:8px 10px;border-bottom:1px solid #334155;font-size:11px;font-weight:bold;color:#38bdf8;">📥 Commandes & alertes reçues</div>
        <div id="imjx-notif-list"></div>
      </div>`;
    document.body.appendChild(wrap);
    const btn = document.getElementById("imjx-notif-btn");
    const panel = document.getElementById("imjx-notif-panel");
    btn.onclick = () => {
      const willOpen = panel.style.display !== "block";
      panel.style.display = willOpen ? "block" : "none";
      if (willOpen && notifList.length) {
        setLastReadId(session, Math.max(...notifList.map((n) => n.id)));
        renderNotifPanel(session);
      }
    };
    loadNotifHistory(session);
  }

  // ---------- Chargement du widget de chat (injecté dynamiquement, aucune page à modifier) ----------
  function loadChatWidget() {
    if (document.getElementById("imjx-chat-script")) return;
    const s = document.createElement("script");
    s.id = "imjx-chat-script";
    s.src = "/shared/im-jx-chat.js";
    document.body.appendChild(s);
  }

  window.IMJX = {
    getSession,
    requireAuth,
    logout,
    sync,
    connectWS,
    authHeaders,
  };

  // Vérification immédiate (avant même le rendu du reste de la page) pour éviter
  // un flash de contenu non autorisé, puis initialisation complète au DOMContentLoaded.
  (function immediateGuard() {
    const cfg = window.IMJX_PAGE || {};
    if (!cfg.department && !cfg.roles) return; // page sans config (ex: login)
    const session = getSession();
    const deptOk = !cfg.department || session?.department === cfg.department || session?.role === "admin";
    const roleOk = !cfg.roles || !cfg.roles.length || cfg.roles.includes(session?.role) || session?.role === "admin";
    if (!session || !deptOk || !roleOk) {
      window.location.replace("/login.html");
    }
  })();

  document.addEventListener("DOMContentLoaded", function () {
    injectBrandFooter(); // affiché partout, y compris sur les pages sans session active
    const cfg = window.IMJX_PAGE || {};
    const session = requireAuth(cfg);
    if (!session) return;
    injectUserBar(session);
    injectNotificationCenter(session);
    connectWS();
    loadChatWidget();
  });
})();

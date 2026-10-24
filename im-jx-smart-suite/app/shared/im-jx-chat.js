/* IM JX Smart Suite - Chat interne (texte, images, vidéos courtes, fichiers, réponses, recherche) */
(function () {
  const session = window.IMJX && window.IMJX.getSession && window.IMJX.getSession();
  if (!session) return;

  const EMOJIS = ["😀","😂","👍","🙏","🔥","✅","❌","⚠️","🚨","🛠️","📦","⏳","💪","🤝","😢","😡","👏","🎉","❓","💡"];
  const LASTSEEN_PREFIX = "imjx_chat_lastseen_";

  let channels = [];
  let activeChannel = null;
  let messagesById = {};
  let unread = {};       // { channel: count }
  let onlineSet = new Set();
  let panelOpen = false;
  let replyTo = null;    // { id, sender_name, preview }

  function getLastSeen(ch) { return parseInt(localStorage.getItem(LASTSEEN_PREFIX + ch) || "0", 10); }
  function setLastSeen(ch, id) { localStorage.setItem(LASTSEEN_PREFIX + ch, String(id)); }

  function colorForName(name) {
    const colors = ["#38bdf8", "#a78bfa", "#fb923c", "#34d399", "#f472b6", "#facc15", "#22d3ee"];
    let hash = 0;
    for (let i = 0; i < (name || "").length; i++) hash = name.charCodeAt(i) + ((hash << 5) - hash);
    return colors[Math.abs(hash) % colors.length];
  }

  function fmtTime(iso) {
    try { return new Date(iso).toLocaleTimeString("fr-FR", { hour: "2-digit", minute: "2-digit" }); }
    catch (e) { return ""; }
  }

  // ---------- Construction du DOM ----------
  function buildDOM() {
    const wrap = document.createElement("div");
    wrap.id = "imjx-chat-root";
    wrap.innerHTML = `
      <button id="imjx-chat-toggle" style="position:fixed;bottom:8px;left:8px;z-index:99997;width:52px;height:52px;border-radius:9999px;background:#059669;color:#fff;border:none;box-shadow:0 4px 14px rgba(0,0,0,.4);font-size:22px;cursor:pointer;display:flex;align-items:center;justify-content:center;">
        💬<span id="imjx-chat-badge" style="display:none;position:absolute;top:-4px;right:-4px;background:#ef4444;color:#fff;border-radius:9999px;font-size:10px;font-family:monospace;padding:1px 5px;font-weight:bold;"></span>
      </button>

      <div id="imjx-chat-panel" style="display:none;position:fixed;bottom:70px;left:8px;z-index:99997;width:340px;max-width:92vw;height:480px;max-height:75vh;background:#0f172a;border:1px solid #334155;border-radius:12px;box-shadow:0 10px 30px rgba(0,0,0,.5);font-family:monospace;overflow:hidden;flex-direction:column;">
        <div style="background:#020617;padding:8px 10px;border-bottom:1px solid #334155;display:flex;align-items:center;gap:6px;">
          <select id="imjx-chat-channel" style="flex:1;background:#1e293b;color:#fff;font-size:11px;padding:4px 6px;border-radius:6px;border:1px solid #334155;outline:none;"></select>
          <button id="imjx-chat-search-toggle" title="Rechercher" style="background:#1e293b;border:1px solid #334155;color:#94a3b8;border-radius:6px;width:26px;height:26px;cursor:pointer;">🔎</button>
          <span id="imjx-chat-online" style="font-size:9px;color:#34d399;white-space:nowrap;"></span>
          <button id="imjx-chat-close" style="background:transparent;border:none;color:#94a3b8;cursor:pointer;font-size:14px;">✕</button>
        </div>

        <div id="imjx-chat-search-bar" style="display:none;padding:6px 8px;border-bottom:1px solid #334155;">
          <input id="imjx-chat-search-input" placeholder="Rechercher..." style="width:100%;background:#1e293b;color:#fff;font-size:11px;padding:5px 8px;border-radius:6px;border:1px solid #334155;outline:none;">
        </div>

        <div id="imjx-chat-messages" style="flex:1;overflow-y:auto;padding:8px;font-size:11px;color:#e2e8f0;display:flex;flex-direction:column;gap:6px;"></div>

        <div id="imjx-chat-reply-preview" style="display:none;padding:5px 8px;background:#1e293b;border-top:1px solid #334155;font-size:10px;color:#94a3b8;justify-content:space-between;align-items:center;">
          <span id="imjx-chat-reply-text"></span>
          <button id="imjx-chat-reply-cancel" style="background:none;border:none;color:#f87171;cursor:pointer;">✕</button>
        </div>

        <div style="padding:6px 8px;border-top:1px solid #334155;background:#020617;display:flex;gap:4px;align-items:center;position:relative;">
          <button id="imjx-chat-emoji-btn" style="background:#1e293b;border:1px solid #334155;color:#facc15;border-radius:6px;width:28px;height:28px;cursor:pointer;">😀</button>
          <button id="imjx-chat-attach-btn" style="background:#1e293b;border:1px solid #334155;color:#38bdf8;border-radius:6px;width:28px;height:28px;cursor:pointer;">📎</button>
          <input type="file" id="imjx-chat-file-input" style="display:none;" accept="image/*,video/*,.pdf,.doc,.docx,.xls,.xlsx,.txt">
          <input id="imjx-chat-text-input" placeholder="Message..." style="flex:1;background:#1e293b;color:#fff;font-size:11px;padding:6px 8px;border-radius:6px;border:1px solid #334155;outline:none;">
          <button id="imjx-chat-send-btn" style="background:#059669;border:none;color:#fff;border-radius:6px;width:30px;height:28px;cursor:pointer;">➤</button>
          <div id="imjx-chat-emoji-picker" style="display:none;position:absolute;bottom:38px;left:6px;background:#1e293b;border:1px solid #334155;border-radius:8px;padding:6px;display:grid;grid-template-columns:repeat(5,1fr);gap:4px;width:160px;"></div>
        </div>
      </div>
    `;
    document.body.appendChild(wrap);

    document.getElementById("imjx-chat-toggle").onclick = togglePanel;
    document.getElementById("imjx-chat-close").onclick = togglePanel;
    document.getElementById("imjx-chat-channel").onchange = (e) => switchChannel(e.target.value);
    document.getElementById("imjx-chat-send-btn").onclick = sendTextMessage;
    document.getElementById("imjx-chat-text-input").addEventListener("keydown", (e) => {
      if (e.key === "Enter") sendTextMessage();
    });
    document.getElementById("imjx-chat-attach-btn").onclick = () => document.getElementById("imjx-chat-file-input").click();
    document.getElementById("imjx-chat-file-input").addEventListener("change", handleFileSelected);
    document.getElementById("imjx-chat-reply-cancel").onclick = () => setReplyTo(null);
    document.getElementById("imjx-chat-search-toggle").onclick = toggleSearchBar;
    document.getElementById("imjx-chat-search-input").addEventListener("input", debounce(runSearch, 350));

    const emojiBtn = document.getElementById("imjx-chat-emoji-btn");
    const emojiPicker = document.getElementById("imjx-chat-emoji-picker");
    emojiPicker.innerHTML = EMOJIS.map(e => `<span style="cursor:pointer;font-size:16px;text-align:center;" data-emoji="${e}">${e}</span>`).join("");
    emojiBtn.onclick = () => { emojiPicker.style.display = emojiPicker.style.display === "grid" ? "none" : "grid"; };
    emojiPicker.addEventListener("click", (e) => {
      const em = e.target.getAttribute("data-emoji");
      if (em) {
        const input = document.getElementById("imjx-chat-text-input");
        input.value += em;
        input.focus();
        emojiPicker.style.display = "none";
      }
    });
  }

  function debounce(fn, ms) {
    let t;
    return (...args) => { clearTimeout(t); t = setTimeout(() => fn(...args), ms); };
  }

  function togglePanel() {
    panelOpen = !panelOpen;
    document.getElementById("imjx-chat-panel").style.display = panelOpen ? "flex" : "none";
    if (panelOpen && activeChannel) markChannelRead(activeChannel);
  }

  function toggleSearchBar() {
    const bar = document.getElementById("imjx-chat-search-bar");
    const isHidden = bar.style.display === "none";
    bar.style.display = isHidden ? "block" : "none";
    if (!isHidden) { document.getElementById("imjx-chat-search-input").value = ""; renderMessages(currentChannelMessages()); }
  }

  // ---------- Chargement des channels ----------
  async function loadChannels() {
    try {
      const res = await fetch("/api/chat/channels", { headers: window.IMJX.authHeaders(session) });
      channels = await res.json();
    } catch (e) { channels = ["GENERAL"]; }

    const select = document.getElementById("imjx-chat-channel");
    select.innerHTML = channels.map(c => `<option value="${c}">${c === "GENERAL" ? "🌐 Général" : c}</option>`).join("");
    channels.forEach(c => { if (!(c in unread)) unread[c] = 0; });
    activeChannel = channels[0];
    select.value = activeChannel;
    switchChannel(activeChannel, true);
  }

  let store = {}; // { channel: [messages] }

  function currentChannelMessages() { return store[activeChannel] || []; }

  async function switchChannel(channel, isInit) {
    activeChannel = channel;
    if (!store[channel]) {
      try {
        const res = await fetch(`/api/chat/messages?channel=${encodeURIComponent(channel)}&limit=50`, { headers: window.IMJX.authHeaders(session) });
        store[channel] = await res.json();
        store[channel].forEach(m => { messagesById[m.id] = m; });
      } catch (e) { store[channel] = []; }
    }
    renderMessages(store[channel]);
    if (panelOpen || isInit) markChannelRead(channel);
    updateBadge();
  }

  function markChannelRead(channel) {
    const msgs = store[channel] || [];
    if (msgs.length) setLastSeen(channel, msgs[msgs.length - 1].id);
    unread[channel] = 0;
    updateBadge();
  }

  function updateBadge() {
    const total = Object.values(unread).reduce((a, b) => a + b, 0);
    const badge = document.getElementById("imjx-chat-badge");
    if (total > 0) { badge.style.display = "block"; badge.innerText = total > 99 ? "99+" : String(total); }
    else { badge.style.display = "none"; }
  }

  // ---------- Rendu des messages ----------
  function renderMessages(msgs) {
    const container = document.getElementById("imjx-chat-messages");
    container.innerHTML = msgs.map(renderOneMessage).join("");
    container.scrollTop = container.scrollHeight;

    container.querySelectorAll("[data-reply-id]").forEach(btn => {
      btn.onclick = () => {
        const id = parseInt(btn.getAttribute("data-reply-id"), 10);
        const m = messagesById[id];
        if (m) setReplyTo(m);
      };
    });
    container.querySelectorAll("[data-delete-id]").forEach(btn => {
      btn.onclick = () => deleteMessage(parseInt(btn.getAttribute("data-delete-id"), 10));
    });
  }

  function renderOneMessage(m) {
    const mine = m.sender_username === session.username;
    const color = colorForName(m.sender_name);
    const online = onlineSet.has(m.sender_username);
    let body = "";

    if (m.deleted) {
      body = `<i style="color:#64748b;">🚫 Message supprimé</i>`;
    } else if (m.content_type === "image") {
      body = `<img src="${m.file_path}" style="max-width:100%;border-radius:6px;margin-top:2px;cursor:pointer;" onclick="window.open('${m.file_path}','_blank')">`;
    } else if (m.content_type === "video") {
      body = `<video src="${m.file_path}" controls style="max-width:100%;border-radius:6px;margin-top:2px;"></video>`;
    } else if (m.content_type === "file") {
      body = `<a href="${m.file_path}" target="_blank" style="color:#38bdf8;text-decoration:underline;">📄 ${escapeHtml(m.file_name || "Fichier")}</a>`;
    } else {
      body = `<span>${escapeHtml(m.text || "")}</span>`;
    }

    let replyBlock = "";
    if (m.reply_to_id && messagesById[m.reply_to_id]) {
      const r = messagesById[m.reply_to_id];
      replyBlock = `<div style="border-left:2px solid #475569;padding-left:6px;margin-bottom:2px;color:#94a3b8;font-size:9px;">↪ ${escapeHtml((r.text || r.file_name || "média")).slice(0, 60)}</div>`;
    }

    const actions = m.deleted ? "" : `
      <span data-reply-id="${m.id}" style="cursor:pointer;color:#64748b;font-size:9px;">↩ répondre</span>
      ${mine || session.role === "admin" ? `<span data-delete-id="${m.id}" style="cursor:pointer;color:#64748b;font-size:9px;margin-left:6px;">🗑 supprimer</span>` : ""}
    `;

    return `
      <div style="align-self:${mine ? "flex-end" : "flex-start"};max-width:85%;background:${mine ? "#065f46" : "#1e293b"};border-radius:8px;padding:5px 8px;">
        <div style="display:flex;align-items:center;gap:4px;font-size:9px;font-weight:bold;color:${color};">
          ${online ? "🟢" : "⚪"} ${escapeHtml(m.sender_name)} <span style="color:#64748b;font-weight:normal;">${fmtTime(m.timestamp)}</span>
        </div>
        ${replyBlock}
        <div style="margin-top:1px;">${body}</div>
        <div style="margin-top:3px;">${actions}</div>
      </div>
    `;
  }

  function escapeHtml(str) {
    const div = document.createElement("div");
    div.innerText = str;
    return div.innerHTML;
  }

  function setReplyTo(m) {
    replyTo = m;
    const bar = document.getElementById("imjx-chat-reply-preview");
    if (!m) { bar.style.display = "none"; return; }
    bar.style.display = "flex";
    document.getElementById("imjx-chat-reply-text").innerText = "↪ " + (m.text || m.file_name || "média").slice(0, 50);
  }

  // ---------- Envoi ----------
  async function sendTextMessage() {
    const input = document.getElementById("imjx-chat-text-input");
    const text = input.value.trim();
    if (!text) return;
    await postMessage({ channel: activeChannel, text, content_type: "text", reply_to_id: replyTo ? replyTo.id : null });
    input.value = "";
    setReplyTo(null);
  }

  async function postMessage(body) {
    try {
      const res = await fetch("/api/chat/messages", {
        method: "POST",
        headers: window.IMJX.authHeaders(session),
        body: JSON.stringify(body),
      });
      if (!res.ok) { alert("❌ فشل إرسال الرسالة."); return; }
    } catch (e) { alert("❌ فشل إرسال الرسالة."); }
  }

  async function deleteMessage(id) {
    if (!confirm("مسح هاد الرسالة؟")) return;
    try {
      await fetch(`/api/chat/messages/${id}`, { method: "DELETE", headers: window.IMJX.authHeaders(session) });
    } catch (e) { /* ignore */ }
  }

  // ---------- Fichiers (image / vidéo / document) ----------
  function handleFileSelected(e) {
    const file = e.target.files[0];
    e.target.value = "";
    if (!file) return;

    if (file.type.startsWith("image/")) {
      compressImage(file, (blob) => uploadAndSend(blob, file.name, "image"));
    } else if (file.type.startsWith("video/")) {
      checkVideoDuration(file, (ok) => {
        if (!ok) { alert("⚠️ الفيديو خاصو يكون أقل من 30 ثانية."); return; }
        uploadAndSend(file, file.name, "video");
      });
    } else {
      uploadAndSend(file, file.name, "file");
    }
  }

  function compressImage(file, cb) {
    const img = new Image();
    const reader = new FileReader();
    reader.onload = (ev) => {
      img.onload = () => {
        const maxDim = 1280;
        let { width, height } = img;
        if (width > maxDim || height > maxDim) {
          const ratio = Math.min(maxDim / width, maxDim / height);
          width *= ratio; height *= ratio;
        }
        const canvas = document.createElement("canvas");
        canvas.width = width; canvas.height = height;
        canvas.getContext("2d").drawImage(img, 0, 0, width, height);
        canvas.toBlob((blob) => cb(blob), "image/jpeg", 0.75);
      };
      img.src = ev.target.result;
    };
    reader.readAsDataURL(file);
  }

  function checkVideoDuration(file, cb) {
    const video = document.createElement("video");
    video.preload = "metadata";
    video.onloadedmetadata = () => {
      URL.revokeObjectURL(video.src);
      cb(video.duration <= 30.5);
    };
    video.src = URL.createObjectURL(file);
  }

  async function uploadAndSend(blobOrFile, fileName, expectedKind) {
    const formData = new FormData();
    formData.append("file", blobOrFile, fileName);
    try {
      const res = await fetch("/api/chat/upload", {
        method: "POST",
        headers: { "Authorization": "Bearer " + session.token },
        body: formData,
      });
      if (!res.ok) { alert("❌ فشل رفع الملف."); return; }
      const data = await res.json();
      await postMessage({
        channel: activeChannel,
        content_type: data.content_type || expectedKind,
        file_path: data.file_path,
        file_name: data.file_name,
        reply_to_id: replyTo ? replyTo.id : null,
      });
      setReplyTo(null);
    } catch (e) { alert("❌ فشل رفع الملف."); }
  }

  // ---------- Recherche ----------
  async function runSearch() {
    const q = document.getElementById("imjx-chat-search-input").value.trim();
    if (!q) { renderMessages(currentChannelMessages()); return; }
    try {
      const res = await fetch(`/api/chat/search?channel=${encodeURIComponent(activeChannel)}&q=${encodeURIComponent(q)}`, { headers: window.IMJX.authHeaders(session) });
      const results = await res.json();
      results.forEach(m => { messagesById[m.id] = m; });
      renderMessages(results);
    } catch (e) { /* ignore */ }
  }

  // ---------- Présence & WebSocket ----------
  async function loadPresence() {
    try {
      const res = await fetch("/api/chat/presence", { headers: window.IMJX.authHeaders(session) });
      const data = await res.json();
      onlineSet = new Set(data.online || []);
      document.getElementById("imjx-chat-online").innerText = `${onlineSet.size} en ligne`;
    } catch (e) { /* ignore */ }
  }

  function handleWSEvent(data) {
    if (data.type === "chat_message") {
      messagesById[data.id] = data;
      if (!store[data.channel]) store[data.channel] = [];
      store[data.channel].push(data);
      if (data.channel === activeChannel) {
        renderMessages(store[data.channel]);
        if (panelOpen) markChannelRead(data.channel);
        else { unread[data.channel] = (unread[data.channel] || 0) + 1; updateBadge(); }
      } else {
        unread[data.channel] = (unread[data.channel] || 0) + 1;
        updateBadge();
      }
    } else if (data.type === "chat_delete") {
      const m = messagesById[data.id];
      if (m) {
        m.deleted = true;
        m.text = "🚫 Message supprimé";
        if (data.channel === activeChannel) renderMessages(store[data.channel]);
      }
    } else if (data.type === "presence") {
      if (data.status === "online") onlineSet.add(data.username);
      else onlineSet.delete(data.username);
      const el = document.getElementById("imjx-chat-online");
      if (el) el.innerText = `${onlineSet.size} en ligne`;
    }
  }

  // ---------- Init ----------
  buildDOM();
  loadChannels();
  loadPresence();
  window.IMJX.onChatEvent = handleWSEvent;
})();

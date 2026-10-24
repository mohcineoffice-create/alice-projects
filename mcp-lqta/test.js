process.env.MCP_TOKEN = "t";
const m = require("./api/mcp.js");
(async () => {
  const call = (id, method, params) => m.traiter({ jsonrpc: "2.0", id, method, params });
  console.log("init", JSON.stringify((await call(1, "initialize", { protocolVersion: "2025-06-18" })).result.serverInfo));
  console.log("tools", (await call(2, "tools/list")).result.tools.map(t => t.name).join(","));
  for (const [n, a] of [
    ["lqta_lister_publications", { limite: 3 }],
    ["lqta_lister_publications", { limite: 99 }],
    ["lqta_lister_publications", { categorie: "x" }],
    ["lqta_voir_publication", { id: 5 }],
    ["lqta_voir_publication", { id: "5 or 1=1" }],
    ["lqta_rechercher", { q: "mohcine" }],
    ["lqta_rechercher", { q: "a),user_id.neq.0,(b" }],
    ["lqta_voir_profil", { username: "douaa" }],
    ["lqta_voir_profil", { username: "x&select=*" }],
  ]) {
    const r = (await call(3, "tools/call", { name: n, arguments: a })).result;
    console.log(n, JSON.stringify(a), r.isError ? "ERR: " : "OK: ", r.content[0].text.replace(/\s+/g, " ").slice(0, 160));
  }
  console.log("motif:", m.motif("a),user_id.neq.0,(b"));
})();

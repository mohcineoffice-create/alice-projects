const h = require("./api/mcp.js");
const assert = require("assert");

function appel(method, body, token) {
  return new Promise((ok) => {
    const res = { statusCode: 0, headers: {}, setHeader(k, v) { this.headers[k] = v; }, end(b) { this.body = b; ok(this); } };
    h({ method, query: { token }, body }, res);
  });
}

(async () => {
  process.env.MCP_TOKEN = "secret";
  assert.equal((await appel("POST", {}, "faux")).statusCode, 404);
  const init = await appel("POST", { jsonrpc: "2.0", id: 1, method: "initialize", params: { protocolVersion: "2025-06-18" } }, "secret");
  assert.equal(JSON.parse(init.body).result.serverInfo.name, "calcul-trs");
  const notif = await appel("POST", { jsonrpc: "2.0", method: "notifications/initialized" }, "secret");
  assert.equal(notif.statusCode, 202);
  const list = await appel("POST", { jsonrpc: "2.0", id: 2, method: "tools/list" }, "secret");
  assert.equal(JSON.parse(list.body).result.tools[0].name, "calculer_trs");
  // 480 min ouverts, 60 min d'arrêt, 60 p/h, 380 produites, 361 bonnes
  const call = await appel("POST", { jsonrpc: "2.0", id: 3, method: "tools/call", params: { name: "calculer_trs", arguments: { temps_ouverture_min: 480, arrets_min: 60, cadence_theorique_pph: 60, pieces_produites: 380, pieces_bonnes: 361 } } }, "secret");
  const r = JSON.parse(call.body).result.structuredContent;
  console.log(r);
  assert.equal(r.disponibilite_pct, 87.5);
  assert.equal(r.performance_pct, 90.5);
  assert.equal(r.qualite_pct, 95);
  assert.equal(r.trs_pct, 75.2); // 361/480
  const bad = await appel("POST", { jsonrpc: "2.0", id: 4, method: "tools/call", params: { name: "calculer_trs", arguments: { temps_ouverture_min: 480, arrets_min: 600, cadence_theorique_pph: 60, pieces_produites: 1, pieces_bonnes: 1 } } }, "secret");
  assert.equal(JSON.parse(bad.body).result.isError, true);
  console.log("OK - tous les tests passent");
})();

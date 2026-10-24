// Serveur MCP minimal (HTTP sans état) : outil de calcul du TRS / OEE.
// Le jeton secret est lu dans la variable d'environnement MCP_TOKEN
// et doit figurer dans l'URL : /mcp/<jeton>

const SERVER_INFO = { name: "calcul-trs", version: "1.0.0" };
const PROTOCOL_VERSIONS = ["2025-06-18", "2025-03-26", "2024-11-05"];

const TOOL = {
  name: "calculer_trs",
  description:
    "Calcule le TRS (OEE) d'une machine ou d'une ligne : disponibilité, performance, qualité et TRS global, avec les pertes associées.",
  inputSchema: {
    type: "object",
    properties: {
      temps_ouverture_min: { type: "number", description: "Temps d'ouverture (temps requis) en minutes" },
      arrets_min: { type: "number", description: "Total des arrêts (pannes, réglages, changements de série) en minutes" },
      cadence_theorique_pph: { type: "number", description: "Cadence théorique en pièces par heure" },
      pieces_produites: { type: "number", description: "Nombre total de pièces produites" },
      pieces_bonnes: { type: "number", description: "Nombre de pièces bonnes du premier coup" }
    },
    required: ["temps_ouverture_min", "arrets_min", "cadence_theorique_pph", "pieces_produites", "pieces_bonnes"]
  }
};

const pct = (x) => Math.round(x * 1000) / 10;

function calculerTrs(a) {
  const champs = TOOL.inputSchema.required;
  for (const c of champs) {
    if (typeof a[c] !== "number" || !Number.isFinite(a[c]) || a[c] < 0) {
      throw new Error(`Le champ « ${c} » doit être un nombre positif.`);
    }
  }
  const { temps_ouverture_min: to, arrets_min: ar, cadence_theorique_pph: cad,
          pieces_produites: pp, pieces_bonnes: pb } = a;
  if (to === 0) throw new Error("Le temps d'ouverture doit être supérieur à 0.");
  if (cad === 0) throw new Error("La cadence théorique doit être supérieure à 0.");
  if (ar > to) throw new Error("Les arrêts ne peuvent pas dépasser le temps d'ouverture.");
  if (pb > pp) throw new Error("Les pièces bonnes ne peuvent pas dépasser les pièces produites.");

  const fonctionnement = to - ar;
  const dispo = fonctionnement / to;
  const tempsCycleMin = 60 / cad;
  const perf = fonctionnement > 0 ? (pp * tempsCycleMin) / fonctionnement : 0;
  const qualite = pp > 0 ? pb / pp : 0;
  const trs = dispo * perf * qualite;

  const alertes = [];
  if (perf > 1) alertes.push("Performance > 100 % : vérifier la cadence théorique ou le comptage des pièces.");

  return {
    disponibilite_pct: pct(dispo),
    performance_pct: pct(perf),
    qualite_pct: pct(qualite),
    trs_pct: pct(trs),
    temps_fonctionnement_min: Math.round(fonctionnement * 10) / 10,
    temps_utile_min: Math.round(pb * tempsCycleMin * 10) / 10,
    pertes_min: {
      arrets: Math.round(ar * 10) / 10,
      ecarts_cadence: Math.round((fonctionnement - pp * tempsCycleMin) * 10) / 10,
      non_qualite: Math.round((pp - pb) * tempsCycleMin * 10) / 10
    },
    alertes
  };
}

function reponse(id, result) { return { jsonrpc: "2.0", id, result }; }
function erreur(id, code, message) { return { jsonrpc: "2.0", id, error: { code, message } }; }

function traiter(msg) {
  const { id, method, params } = msg || {};
  const estNotification = id === undefined || id === null;
  switch (method) {
    case "initialize": {
      const demandee = params && params.protocolVersion;
      const version = PROTOCOL_VERSIONS.includes(demandee) ? demandee : PROTOCOL_VERSIONS[0];
      return reponse(id, { protocolVersion: version, capabilities: { tools: {} }, serverInfo: SERVER_INFO });
    }
    case "ping":
      return reponse(id, {});
    case "tools/list":
      return reponse(id, { tools: [TOOL] });
    case "tools/call": {
      if (!params || params.name !== TOOL.name) return erreur(id, -32602, "Outil inconnu");
      try {
        const r = calculerTrs(params.arguments || {});
        return reponse(id, {
          content: [{ type: "text", text: JSON.stringify(r, null, 2) }],
          structuredContent: r
        });
      } catch (e) {
        return reponse(id, { content: [{ type: "text", text: e.message }], isError: true });
      }
    }
    default:
      if (estNotification) return null;
      return erreur(id, -32601, "Méthode non prise en charge");
  }
}

module.exports = async (req, res) => {
  const attendu = process.env.MCP_TOKEN;
  const recu = (req.query && req.query.token) || "";
  if (!attendu || recu !== attendu) {
    res.statusCode = 404;
    return res.end("Not found");
  }
  if (req.method !== "POST") {
    res.statusCode = 405;
    res.setHeader("Allow", "POST");
    return res.end();
  }

  let corps = req.body;
  if (typeof corps === "string") {
    try { corps = JSON.parse(corps); } catch { corps = undefined; }
  }
  if (corps === undefined) {
    res.statusCode = 400;
    res.setHeader("Content-Type", "application/json");
    return res.end(JSON.stringify(erreur(null, -32700, "JSON invalide")));
  }

  const resultat = Array.isArray(corps)
    ? corps.map(traiter).filter(Boolean)
    : traiter(corps);

  if (!resultat || (Array.isArray(resultat) && resultat.length === 0)) {
    res.statusCode = 202;
    return res.end();
  }
  res.statusCode = 200;
  res.setHeader("Content-Type", "application/json");
  res.end(JSON.stringify(resultat));
};

module.exports.calculerTrs = calculerTrs;
module.exports.traiter = traiter;

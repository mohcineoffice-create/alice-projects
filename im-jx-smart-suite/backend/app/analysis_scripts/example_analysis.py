"""
Exemple de script d'analyse — sert de modèle pour en ajouter de nouveaux.

`data` (le seul argument reçu) est le dict retourné par analytics.get_all_datasets() :
il contient déjà toutes les variables utiles (production_live, factory_kpis, machines,
alarms, maintenance, reclamations, traceability). Ce script ne touche jamais à la base
de données directement : il ne fait que lire ces variables et calculer un résultat.
"""

from . import register


@register("performance_par_ligne")
def run(data: dict) -> dict:
    """Exemple : classe les lignes de production du jour par performance décroissante."""
    production = data.get("production_live", [])
    ranked = sorted(production, key=lambda r: r.get("performance_pct", 0), reverse=True)
    return {
        "date": data.get("date"),
        "lines_ranked": [
            {"line_ref": r["line_ref"], "operator": r["operator"], "performance_pct": r["performance_pct"]}
            for r in ranked
        ],
    }

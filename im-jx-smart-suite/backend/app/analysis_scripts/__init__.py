"""
Registre des scripts d'analyse personnalisés.

BUT : permettre d'ajouter un nouveau calcul/rapport plus tard SANS modifier main.py,
analytics.py, ni aucune page HTML. Il suffit de :

  1. Créer un fichier dans ce dossier (backend/app/analysis_scripts/mon_script.py)
  2. L'importer ci-dessous dans _load_scripts()
  3. Dans mon_script.py, décorer une fonction avec @register("mon_nom")

Le script est alors automatiquement disponible via l'API existante :
  GET /api/analytics/custom            -> liste des scripts enregistrés
  GET /api/analytics/custom/mon_nom    -> exécute le script et retourne son résultat JSON

Chaque script reçoit toutes les données déjà en base via get_all_datasets() (voir
analytics.py) : il n'a jamais besoin de refaire une requête base de données lui-même,
juste de lire les variables du dict qu'on lui donne.
"""

from typing import Callable

REGISTRY: dict[str, Callable] = {}


def register(name: str):
    """Décorateur : @register("nom_du_script") sur une fonction run(data: dict) -> dict"""
    def _wrap(fn: Callable):
        REGISTRY[name] = fn
        return fn
    return _wrap


def _load_scripts():
    # Ajouter ici l'import de chaque nouveau fichier de script pour qu'il s'enregistre.
    from . import example_analysis  # noqa: F401 — exemple fourni, à garder ou supprimer


_load_scripts()


def list_scripts() -> list[str]:
    return sorted(REGISTRY.keys())


def run_script(name: str, data: dict) -> dict:
    if name not in REGISTRY:
        raise KeyError(name)
    return REGISTRY[name](data)

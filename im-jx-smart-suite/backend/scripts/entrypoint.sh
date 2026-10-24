#!/bin/sh
# Applique les migrations Alembic avant de démarrer l'application, en gérant proprement
# 2 cas :
#   - Base toute neuve (pas de fichier / pas de tables)      -> `alembic upgrade head`
#     crée le schéma directement à partir des migrations.
#   - Base existante d'avant l'introduction d'Alembic (tables déjà créées par
#     Base.metadata.create_all, pas de table alembic_version) -> on "stamp" la base à la
#     révision actuelle sans rejouer la création des tables, puis upgrade head (no-op ici,
#     prêt pour la prochaine vraie migration).
# Ne bloque jamais le démarrage de l'app si cette étape échoue pour une raison
# inattendue : on logue un avertissement et on démarre quand même (app/database.py fait
# aussi sa propre vérification de schéma par sécurité).
set -e

DB_PATH="${DATABASE_PATH:-/data/imjx.db}"

migrate() {
    if [ -f "$DB_PATH" ] && command -v sqlite3 >/dev/null 2>&1; then
        HAS_VERSION_TABLE=$(sqlite3 "$DB_PATH" "SELECT name FROM sqlite_master WHERE type='table' AND name='alembic_version';" 2>/dev/null || echo "")
        HAS_USERS_TABLE=$(sqlite3 "$DB_PATH" "SELECT name FROM sqlite_master WHERE type='table' AND name='users';" 2>/dev/null || echo "")
        if [ -z "$HAS_VERSION_TABLE" ] && [ -n "$HAS_USERS_TABLE" ]; then
            echo "Base existante détectée sans suivi Alembic -> stamp à la révision actuelle."
            alembic stamp head
        fi
    fi
    echo "Application des migrations Alembic en attente..."
    alembic upgrade head
}

if ! migrate; then
    echo "AVERTISSEMENT: la migration Alembic a échoué. Démarrage de l'application quand même (vérification de schéma de secours dans app/database.py)."
fi

exec "$@"

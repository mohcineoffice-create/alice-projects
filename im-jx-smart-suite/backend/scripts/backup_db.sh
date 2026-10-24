#!/bin/sh
# Sauvegarde la base de données IM JX. Sûr à exécuter pendant que l'application
# tourne : utilise `sqlite3 .backup` (copie cohérente même en mode WAL avec des écritures
# concurrentes), plutôt qu'un `cp` brut qui pourrait copier un fichier en cours d'écriture.
#
# Usage :
#   ./backup_db.sh                     # sauvegarde vers /data/backups/
#   BACKUP_DIR=/mnt/nas ./backup_db.sh # destination personnalisée
#
# Pour une sauvegarde automatique quotidienne, planifier via cron sur l'HÔTE (pas dans le
# conteneur, qui peut être recréé) :
#   0 2 * * * docker compose exec -T im-jx sh /app/scripts/backup_db.sh
# ou monter ce script sur l'hôte et l'exécuter directement contre le volume Docker.

set -eu

DB_PATH="${DATABASE_PATH:-/data/imjx.db}"
BACKUP_DIR="${BACKUP_DIR:-/data/backups}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
DEST="$BACKUP_DIR/imjx_$TIMESTAMP.db"
RETENTION_DAYS="${BACKUP_RETENTION_DAYS:-30}"

mkdir -p "$BACKUP_DIR"

if [ ! -f "$DB_PATH" ]; then
    echo "Base de données introuvable: $DB_PATH" >&2
    exit 1
fi

if command -v sqlite3 >/dev/null 2>&1; then
    sqlite3 "$DB_PATH" ".backup '$DEST'"
else
    # Repli si sqlite3 CLI n'est pas installé dans l'image : copie directe (moins sûre
    # en cas d'écriture concurrente, mais le mode WAL limite fortement le risque).
    cp "$DB_PATH" "$DEST"
fi

echo "Sauvegarde créée: $DEST"

# Nettoyage des sauvegardes trop anciennes
find "$BACKUP_DIR" -name "imjx_*.db" -mtime "+$RETENTION_DAYS" -delete 2>/dev/null || true

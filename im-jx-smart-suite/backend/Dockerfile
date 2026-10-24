FROM python:3.11-slim

WORKDIR /app

# sqlite3 : utilisé par scripts/backup_db.sh et scripts/entrypoint.sh (détection de schéma).
# curl : utilisé par le HEALTHCHECK Docker ci-dessous.
RUN apt-get update && apt-get install -y --no-install-recommends sqlite3 curl \
    && rm -rf /var/lib/apt/lists/*

COPY backend/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY backend/app ./app
COPY backend/alembic ./alembic
COPY backend/alembic.ini .
COPY backend/scripts ./scripts
COPY app ./static

RUN chmod +x ./scripts/entrypoint.sh ./scripts/backup_db.sh

RUN mkdir -p /data
VOLUME ["/data"]

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
    CMD curl -f http://localhost:8080/api/health || exit 1

ENTRYPOINT ["./scripts/entrypoint.sh"]
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8080"]

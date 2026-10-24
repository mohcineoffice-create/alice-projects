import os
from datetime import datetime
from sqlalchemy import create_engine, Column, Integer, String, DateTime, Text
from sqlalchemy.orm import declarative_base, sessionmaker

DB_PATH = os.environ.get("DATABASE_PATH", "/data/imjx.db")
os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)

engine = create_engine(f"sqlite:///{DB_PATH}", connect_args={"check_same_thread": False})
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)
Base = declarative_base()


class User(Base):
    __tablename__ = "users"
    id = Column(Integer, primary_key=True, index=True)
    username = Column(String, unique=True, index=True, nullable=False)
    password_hash = Column(String, nullable=False)
    password_salt = Column(String, nullable=True)   # salt aléatoire propre à l'utilisateur (voir auth.py).
    # NULL = compte pas encore migré vers le nouveau schéma de hachage (migration transparente au login).
    full_name = Column(String, nullable=False)
    role = Column(String, nullable=False)          # operator | technician | tl | admin
    department = Column(String, nullable=False)    # PROD | PED | MNT | ALL


class SyncEvent(Base):
    __tablename__ = "sync_events"
    id = Column(Integer, primary_key=True, index=True)
    department = Column(String, index=True, nullable=False)
    page = Column(String, nullable=False)           # e.g. PRODUCTION, TECHNIC, TL...
    operator = Column(String, nullable=True)
    raw_payload = Column(Text, nullable=False)
    created_at = Column(DateTime, default=datetime.utcnow, index=True)


class Reclamation(Base):
    __tablename__ = "reclamations"
    id = Column(Integer, primary_key=True, index=True)
    department = Column(String, index=True, nullable=False)
    operator = Column(String, nullable=True)
    target = Column(String, nullable=True)
    category = Column(String, nullable=True)
    raw_payload = Column(Text, nullable=False)
    status = Column(String, default="open")         # open | acknowledged | closed
    created_at = Column(DateTime, default=datetime.utcnow, index=True)


class Command(Base):
    __tablename__ = "commands"
    id = Column(Integer, primary_key=True, index=True)
    sender = Column(String, nullable=False)
    target_department = Column(String, nullable=False)   # PROD | PED | MNT | ALL
    target_role = Column(String, nullable=False)          # operator | technician | tl | ALL
    message = Column(Text, nullable=False)
    priority = Column(String, default="normal")            # normal | urgent
    created_at = Column(DateTime, default=datetime.utcnow, index=True)


class ChatMessage(Base):
    __tablename__ = "chat_messages"
    id = Column(Integer, primary_key=True, index=True)
    channel = Column(String, index=True, nullable=False)     # GENERAL | PROD | PED | MNT | QUALITY | ...
    sender_username = Column(String, nullable=False)
    sender_name = Column(String, nullable=False)
    content_type = Column(String, default="text")            # text | image | video | file
    text = Column(Text, nullable=True)
    file_path = Column(String, nullable=True)                # chemin relatif servi via /media/...
    file_name = Column(String, nullable=True)
    reply_to_id = Column(Integer, nullable=True)
    deleted = Column(Integer, default=0)                      # 0/1 (soft delete)
    created_at = Column(DateTime, default=datetime.utcnow, index=True)


class Machine(Base):
    __tablename__ = "machines"
    id = Column(Integer, primary_key=True, index=True)
    machine_code = Column(String, unique=True, index=True, nullable=False)   # ex: M035
    name = Column(String, nullable=False)
    machine_type = Column(String, nullable=True)
    department = Column(String, index=True, nullable=False)   # PROD | PED | MNT | ...
    location = Column(String, nullable=True)
    serial_number = Column(String, nullable=True)
    installation_date = Column(String, nullable=True)          # stocké en texte (YYYY-MM-DD)
    responsible_employee = Column(String, nullable=True)
    status = Column(String, default="operational")             # operational | maintenance | down | retired
    created_at = Column(DateTime, default=datetime.utcnow)


class Alarm(Base):
    __tablename__ = "alarms"
    id = Column(Integer, primary_key=True, index=True)
    machine_id = Column(Integer, index=True, nullable=False)
    machine_code = Column(String, index=True, nullable=False)
    level = Column(String, default="warning")          # critical | warning | info
    description = Column(Text, nullable=True)           # optionnel (signalement rapide)
    created_by = Column(String, nullable=False)
    department = Column(String, index=True, nullable=False)
    assigned_technician = Column(String, nullable=True)
    status = Column(String, default="open")             # open | in_progress | resolved
    created_at = Column(DateTime, default=datetime.utcnow, index=True)
    resolved_at = Column(DateTime, nullable=True)
    escalated_at = Column(DateTime, nullable=True)       # non-nulle si l'alarme a dépassé le délai d'escalade
    occurrence_count = Column(Integer, default=1)        # nb de signalements identiques regroupés (anti-doublon)


class MaintenanceRequest(Base):
    __tablename__ = "maintenance_requests"
    id = Column(Integer, primary_key=True, index=True)
    machine_id = Column(Integer, index=True, nullable=False)
    machine_code = Column(String, index=True, nullable=False)
    alarm_id = Column(Integer, nullable=True)            # lien optionnel vers l'alarme d'origine
    requested_by = Column(String, nullable=False)
    assigned_technician = Column(String, nullable=True)
    description = Column(Text, nullable=True)
    status = Column(String, default="open")              # open | in_progress | resolved
    created_at = Column(DateTime, default=datetime.utcnow, index=True)
    started_at = Column(DateTime, nullable=True)
    resolved_at = Column(DateTime, nullable=True)


class MaintenanceNote(Base):
    __tablename__ = "maintenance_notes"
    id = Column(Integer, primary_key=True, index=True)
    maintenance_request_id = Column(Integer, index=True, nullable=False)
    author = Column(String, nullable=False)
    note = Column(Text, nullable=True)
    file_path = Column(String, nullable=True)             # photo/vidéo jointe (optionnel)
    file_name = Column(String, nullable=True)
    created_at = Column(DateTime, default=datetime.utcnow, index=True)


def _ensure_columns():
    """Migration légère pour les installations existantes (avant la mise en place
    d'Alembic) : ajoute les colonnes manquantes sans jamais toucher aux données déjà
    présentes. Sans danger de la relancer à chaque démarrage (vérifie avant d'ajouter)."""
    import sqlite3
    if not DB_PATH.startswith("/") and "sqlite" not in str(engine.url):
        return
    with engine.connect() as conn:
        existing = {row[1] for row in conn.exec_driver_sql("PRAGMA table_info(users)").fetchall()}
        if existing and "password_salt" not in existing:
            conn.exec_driver_sql("ALTER TABLE users ADD COLUMN password_salt VARCHAR")
        existing_alarms = {row[1] for row in conn.exec_driver_sql("PRAGMA table_info(alarms)").fetchall()}
        if existing_alarms and "escalated_at" not in existing_alarms:
            conn.exec_driver_sql("ALTER TABLE alarms ADD COLUMN escalated_at DATETIME")
        if existing_alarms and "occurrence_count" not in existing_alarms:
            conn.exec_driver_sql("ALTER TABLE alarms ADD COLUMN occurrence_count INTEGER DEFAULT 1")


def init_db():
    Base.metadata.create_all(bind=engine)
    _ensure_columns()
    with engine.connect() as conn:
        conn.exec_driver_sql("PRAGMA journal_mode=WAL")
    _stamp_alembic_if_needed()


def _stamp_alembic_if_needed():
    """À partir de cette version, les évolutions de schéma futures passent par Alembic
    (voir backend/alembic/). Pour ne pas casser les installations existantes (schéma déjà
    créé par create_all + _ensure_columns ci-dessus), on marque simplement la base comme
    étant déjà à la révision la plus récente au premier démarrage après mise à jour, sans
    rejouer physiquement la migration initiale. Toute nouvelle colonne/table à partir de
    maintenant doit être ajoutée via `alembic revision --autogenerate` + `alembic upgrade head`."""
    try:
        import os as _os
        from alembic.config import Config
        from alembic import command
        backend_dir = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))
        ini_path = _os.path.join(backend_dir, "alembic.ini")
        if not _os.path.exists(ini_path):
            return
        with engine.connect() as conn:
            has_version_table = conn.exec_driver_sql(
                "SELECT name FROM sqlite_master WHERE type='table' AND name='alembic_version'"
            ).fetchone()
        if has_version_table:
            return  # déjà suivi par Alembic, rien à faire ici
        cfg = Config(ini_path)
        cfg.set_main_option("sqlalchemy.url", f"sqlite:///{DB_PATH}")
        cfg.set_main_option("script_location", _os.path.join(backend_dir, "alembic"))
        command.stamp(cfg, "head")
    except Exception as exc:  # pragma: no cover - ne doit jamais empêcher le démarrage
        import logging
        logging.getLogger("imjx").warning("Impossible de marquer la base pour Alembic: %s", exc)


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()

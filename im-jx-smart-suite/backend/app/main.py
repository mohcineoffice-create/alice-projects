import os
import io
import uuid
from datetime import datetime, timedelta

from fastapi import FastAPI, Depends, HTTPException, WebSocket, WebSocketDisconnect, UploadFile, File, Form
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import RedirectResponse, StreamingResponse
from pydantic import BaseModel
from sqlalchemy.orm import Session
from sqlalchemy import text
import openpyxl
from openpyxl.styles import Font
from docx import Document as DocxDocument
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4, landscape
from reportlab.platypus import SimpleDocTemplate, Table, TableStyle, Paragraph
from reportlab.lib.styles import getSampleStyleSheet

import logging

from .database import init_db, get_db, User, SyncEvent, Reclamation, Command, ChatMessage, Machine, Alarm, MaintenanceRequest, MaintenanceNote
from . import analytics
from . import analysis_scripts
from .auth import (
    hash_new_password, verify_password, create_token, get_current_user, get_ws_user,
    check_login_rate_limit, reset_login_rate_limit,
)
from . import auth as auth_module
from .ws_manager import manager
from .escalation import start_escalation_task

logging.basicConfig(
    level=os.environ.get("LOG_LEVEL", "INFO"),
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("imjx")

app = FastAPI(title="IM JX Smart Suite API")

APP_START_TIME = datetime.utcnow()

MAX_LIST_LIMIT = 500  # plafond serveur, quel que soit le "limit" demandé par le client


def _clamp_limit(limit: int, default: int = 100) -> int:
    if limit is None or limit <= 0:
        return default
    return min(limit, MAX_LIST_LIMIT)

CHAT_UPLOAD_DIR = os.environ.get("CHAT_UPLOAD_DIR", "/data/chat_uploads")
CHAT_UPLOAD_INLINE_DIR = os.path.join(CHAT_UPLOAD_DIR, "inline")   # images/vidéos (affichage direct)
CHAT_UPLOAD_FILES_DIR = os.path.join(CHAT_UPLOAD_DIR, "files")     # documents (téléchargement forcé)
os.makedirs(CHAT_UPLOAD_INLINE_DIR, exist_ok=True)
os.makedirs(CHAT_UPLOAD_FILES_DIR, exist_ok=True)
MAX_UPLOAD_BYTES = 25 * 1024 * 1024  # 25 Mo
ALLOWED_CONTENT_TYPES = {
    "image": {"image/jpeg", "image/png", "image/gif", "image/webp"},
    "video": {"video/mp4", "video/webm", "video/quicktime"},
    "file": None,  # tout type accepté pour les documents (validé côté extension/contenu)
}
# Extensions interdites en toute circonstance : capables de s'exécuter dans un navigateur
# si le fichier est ouvert directement -> vecteur de XSS stocké / vol de session.
DANGEROUS_EXTENSIONS = {
    ".html", ".htm", ".svg", ".js", ".mjs", ".php", ".phtml", ".exe", ".sh", ".bat",
    ".cmd", ".jar", ".py", ".ps1", ".msi", ".apk", ".dll",
}
# Signatures binaires (magic bytes) pour vérifier que le contenu réel correspond bien
# au type déclaré par le client (qui est facilement falsifiable).
_IMAGE_SIGNATURES = {
    b"\xff\xd8\xff": "image/jpeg",
    b"\x89PNG\r\n\x1a\n": "image/png",
    b"GIF87a": "image/gif",
    b"GIF89a": "image/gif",
}


def _sniff_real_kind(contents: bytes, declared_mime: str) -> str:
    """Détermine le type réel du fichier à partir de ses premiers octets plutôt que
    de faire confiance au Content-Type déclaré par le client (facilement falsifiable)."""
    head = contents[:16]
    if any(head.startswith(sig) for sig in _IMAGE_SIGNATURES):
        return "image"
    if head[:4] == b"RIFF" and contents[8:12] == b"WEBP":
        return "image"
    if contents[4:8] == b"ftyp" or head[:4] == b"\x1aE\xdf\xa3":  # mp4/mov (ftyp) ou webm (EBML)
        return "video"
    if declared_mime in ALLOWED_CONTENT_TYPES["image"] or declared_mime in ALLOWED_CONTENT_TYPES["video"]:
        # Le client prétendait un média mais les octets ne correspondent à rien de connu
        # -> on ne fait pas confiance, traité comme document générique.
        return "file"
    return "file"

_allowed_origins_env = os.environ.get("ALLOWED_ORIGINS", "").strip()
ALLOWED_ORIGINS = [o.strip() for o in _allowed_origins_env.split(",") if o.strip()] or [
    "http://localhost:8080", "http://127.0.0.1:8080",
]

app.add_middleware(
    CORSMiddleware,
    allow_origins=ALLOWED_ORIGINS,
    allow_methods=["*"],
    allow_headers=["*"],
)


# ---------- Mode démo public (DEMO_MODE=true) ----------
# Version de démonstration en ligne : données fictives, actions sensibles désactivées.
DEMO_MODE = os.environ.get("DEMO_MODE", "false").strip().lower() == "true"
_DEMO_BLOCKED = (
    ("POST", "/api/chat/upload"), ("POST", "/api/upload"),
    ("POST", "/api/users"), ("PUT", "/api/users/"), ("DELETE", "/api/users/"),
    ("DELETE", "/api/machines/"), ("DELETE", "/api/chat/messages/"),
)


@app.middleware("http")
async def demo_mode_middleware(request, call_next):
    if DEMO_MODE:
        path = request.url.path
        for method, prefix in _DEMO_BLOCKED:
            if request.method == method and (path == prefix or (prefix.endswith("/") and path.startswith(prefix))):
                from fastapi.responses import JSONResponse
                return JSONResponse(status_code=403, content={"detail": "Action désactivée dans la version de démonstration."})
    return await call_next(request)


@app.get("/api/demo-info")
def demo_info():
    return {"demo": DEMO_MODE}


@app.middleware("http")
async def security_headers_middleware(request, call_next):
    response = await call_next(request)
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["X-Frame-Options"] = "DENY"
    # NOTE : cdn.tailwindcss.com reste autorisé ici car le moteur Tailwind (compilation JIT
    # dans le navigateur) n'a pas pu être vendorisé localement sans risque de casser le
    # rendu visuel des 18 pages sans possibilité de vérification visuelle. Toutes les
    # autres librairies externes (html5-qrcode, qrious, JsBarcode, jspdf) sont déjà
    # servies localement depuis /shared/vendor/. Retirer cette exception dès que Tailwind
    # est vendorisé et validé visuellement (voir README.md, section "Dépendances").
    response.headers.setdefault(
        "Content-Security-Policy",
        "default-src 'self'; img-src 'self' data: blob:; media-src 'self' blob:; "
        "script-src 'self' 'unsafe-inline' https://cdn.tailwindcss.com; "
        "style-src 'self' 'unsafe-inline'; "
        "connect-src 'self' ws: wss:; object-src 'none'; base-uri 'self';",
    )
    return response


@app.exception_handler(Exception)
async def unhandled_exception_handler(request, exc: Exception):
    from fastapi.responses import JSONResponse
    logger.exception("Erreur non gérée sur %s %s", request.method, request.url.path)
    return JSONResponse(
        status_code=500,
        content={"detail": "Une erreur interne est survenue. L'équipe technique a été notifiée."},
    )


@app.on_event("startup")
async def on_startup():
    init_db()
    from .seed import run_seed
    run_seed()
    if DEMO_MODE:
        try:
            from .database import SessionLocal
            _db = SessionLocal()
            empty = _db.query(SyncEvent).count() == 0
            _db.close()
            if empty:
                from .seed_demo import run_demo_seed
                run_demo_seed()
        except Exception:
            logger.exception("Remplissage des données de démonstration impossible")
    start_escalation_task()
    import asyncio
    asyncio.create_task(manager.heartbeat_loop())
    logger.info("IM JX Smart Suite démarré. Origines CORS autorisées: %s", ALLOWED_ORIGINS)


# ---------- Schemas ----------

class LoginRequest(BaseModel):
    username: str
    password: str


class SyncRequest(BaseModel):
    department: str
    page: str
    operator: str | None = None
    rawData: str


class ReclamationRequest(BaseModel):
    department: str
    operator: str | None = None
    target: str | None = None
    category: str | None = None
    rawData: str


class CommandRequest(BaseModel):
    target_department: str   # PROD | PED | MNT | ALL
    target_role: str         # operator | technician | tl | ALL
    message: str
    priority: str = "normal"


class UserCreateRequest(BaseModel):
    username: str
    password: str
    full_name: str
    role: str          # operator | technician | tl | admin
    department: str    # PROD | PED | MNT | ALL


class UserUpdateRequest(BaseModel):
    full_name: str | None = None
    role: str | None = None
    department: str | None = None
    password: str | None = None  # si fourni -> réinitialise le mot de passe


class ChatMessageRequest(BaseModel):
    channel: str
    text: str | None = None
    content_type: str = "text"      # text | image | video | file
    file_path: str | None = None
    file_name: str | None = None
    reply_to_id: int | None = None


class MachineCreateRequest(BaseModel):
    machine_code: str
    name: str
    machine_type: str | None = None
    department: str
    location: str | None = None
    serial_number: str | None = None
    installation_date: str | None = None
    responsible_employee: str | None = None
    status: str = "operational"


class MachineUpdateRequest(BaseModel):
    name: str | None = None
    machine_type: str | None = None
    department: str | None = None
    location: str | None = None
    serial_number: str | None = None
    installation_date: str | None = None
    responsible_employee: str | None = None
    status: str | None = None


class AlarmCreateRequest(BaseModel):
    machine_code: str
    level: str = "warning"          # critical | warning | info
    description: str | None = None  # optionnel


class AlarmUpdateRequest(BaseModel):
    status: str | None = None
    assigned_technician: str | None = None


class MaintenanceCreateRequest(BaseModel):
    machine_code: str
    alarm_id: int | None = None
    assigned_technician: str | None = None
    description: str | None = None


class MaintenanceUpdateRequest(BaseModel):
    status: str | None = None
    assigned_technician: str | None = None


class MaintenanceNoteRequest(BaseModel):
    note: str | None = None
    file_path: str | None = None
    file_name: str | None = None


# ---------- Auth ----------

@app.post("/api/auth/login")
def login(body: LoginRequest, db: Session = Depends(get_db)):
    check_login_rate_limit(body.username.strip().lower())

    user = db.query(User).filter(User.username == body.username).first()
    if not user or not verify_password(body.password, user):
        logger.warning("Échec de connexion pour username=%s", body.username)
        raise HTTPException(status_code=401, detail="Identifiants incorrects")

    # Migration transparente : compte pas encore passé au salt individuel -> on le migre
    # maintenant qu'on a le mot de passe en clair (login réussi).
    if not user.password_salt:
        user.password_hash, user.password_salt = hash_new_password(body.password)
        db.commit()
        logger.info("Migration du hachage mot de passe pour username=%s", user.username)

    reset_login_rate_limit(body.username.strip().lower())
    return {
        "token": create_token(user),
        "role": user.role,
        "department": user.department,
        "full_name": user.full_name,
        "username": user.username,
    }


@app.get("/api/auth/me")
def me(user: dict = Depends(get_current_user)):
    return user


# ---------- Sync (remplace Google Sheets) ----------

@app.post("/api/sync")
async def sync_data(body: SyncRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    event = SyncEvent(
        department=body.department,
        page=body.page,
        operator=body.operator or user.get("full_name"),
        raw_payload=body.rawData,
    )
    db.add(event)
    db.commit()
    db.refresh(event)

    await manager.broadcast_to_admins({
        "type": "sync",
        "id": event.id,
        "department": event.department,
        "page": event.page,
        "operator": event.operator,
        "raw": event.raw_payload,
        "timestamp": event.created_at.isoformat(),
    })
    return {"status": "ok", "id": event.id}


@app.get("/api/sync/latest")
def sync_latest(limit: int = 100, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    if user.get("role") != "admin":
        raise HTTPException(status_code=403, detail="Réservé à l'administrateur")
    rows = db.query(SyncEvent).order_by(SyncEvent.created_at.desc()).limit(_clamp_limit(limit)).all()
    return [{
        "id": r.id, "department": r.department, "page": r.page,
        "operator": r.operator, "raw": r.raw_payload,
        "timestamp": r.created_at.isoformat(),
    } for r in rows]


# ---------- Reclamations / Alarms ----------

@app.post("/api/reclamations")
async def create_reclamation(body: ReclamationRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    rec = Reclamation(
        department=body.department,
        operator=body.operator or user.get("full_name"),
        target=body.target,
        category=body.category,
        raw_payload=body.rawData,
    )
    db.add(rec)
    db.commit()
    db.refresh(rec)

    payload = {
        "type": "reclamation",
        "id": rec.id,
        "department": rec.department,
        "operator": rec.operator,
        "target": rec.target,
        "category": rec.category,
        "raw": rec.raw_payload,
        "timestamp": rec.created_at.isoformat(),
    }
    await manager.broadcast_to_admins(payload)
    # alerte instantanée vers le département/rôle ciblé
    await manager.broadcast_command(
        {**payload, "type": "alarm"},
        target_department=rec.department,
        target_role="ALL",
    )
    return {"status": "ok", "id": rec.id}


@app.get("/api/reclamations")
def list_reclamations(limit: int = 100, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    if user.get("role") != "admin":
        raise HTTPException(status_code=403, detail="Réservé à l'administrateur")
    rows = db.query(Reclamation).order_by(Reclamation.created_at.desc()).limit(_clamp_limit(limit)).all()
    return [{
        "id": r.id, "department": r.department, "operator": r.operator,
        "target": r.target, "category": r.category, "raw": r.raw_payload,
        "status": r.status, "timestamp": r.created_at.isoformat(),
    } for r in rows]


# ---------- Commandes admin -> postes ----------

@app.post("/api/commands")
async def send_command(body: CommandRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    if user.get("role") != "admin":
        raise HTTPException(status_code=403, detail="Réservé à l'administrateur")
    cmd = Command(
        sender=user.get("full_name"),
        target_department=body.target_department,
        target_role=body.target_role,
        message=body.message,
        priority=body.priority,
    )
    db.add(cmd)
    db.commit()
    db.refresh(cmd)

    await manager.broadcast_command({
        "type": "command",
        "id": cmd.id,
        "sender": cmd.sender,
        "message": cmd.message,
        "priority": cmd.priority,
        "timestamp": cmd.created_at.isoformat(),
    }, target_department=cmd.target_department, target_role=cmd.target_role)

    return {"status": "ok", "id": cmd.id}


@app.get("/api/commands/mine")
def my_commands(since_id: int = 0, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    q = db.query(Command).filter(Command.id > since_id)
    q = q.filter(
        (Command.target_department == "ALL") | (Command.target_department == user.get("department"))
    ).filter(
        (Command.target_role == "ALL") | (Command.target_role == user.get("role"))
    )
    rows = q.order_by(Command.created_at.desc()).limit(50).all()
    return [{
        "id": r.id, "sender": r.sender, "message": r.message,
        "priority": r.priority, "timestamp": r.created_at.isoformat(),
    } for r in rows]


# ---------- Gestion des utilisateurs (admin uniquement) ----------

def _require_admin(user: dict):
    if user.get("role") != "admin":
        raise HTTPException(status_code=403, detail="Réservé à l'administrateur")


@app.get("/api/users")
def list_users(user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    _require_admin(user)
    rows = db.query(User).order_by(User.department, User.role, User.username).all()
    return [{
        "id": u.id, "username": u.username, "full_name": u.full_name,
        "role": u.role, "department": u.department,
    } for u in rows]


@app.post("/api/users")
def create_user(body: UserCreateRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    _require_admin(user)
    if db.query(User).filter(User.username == body.username).first():
        raise HTTPException(status_code=400, detail="Ce nom d'utilisateur existe déjà")
    pw_hash, pw_salt = hash_new_password(body.password)
    new_user = User(
        username=body.username,
        password_hash=pw_hash,
        password_salt=pw_salt,
        full_name=body.full_name,
        role=body.role,
        department=body.department,
    )
    db.add(new_user)
    db.commit()
    db.refresh(new_user)
    return {"status": "ok", "id": new_user.id}


@app.put("/api/users/{user_id}")
def update_user(user_id: int, body: UserUpdateRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    _require_admin(user)
    target = db.query(User).filter(User.id == user_id).first()
    if not target:
        raise HTTPException(status_code=404, detail="Utilisateur introuvable")
    if body.full_name is not None:
        target.full_name = body.full_name
    if body.role is not None:
        target.role = body.role
    if body.department is not None:
        target.department = body.department
    if body.password:
        target.password_hash, target.password_salt = hash_new_password(body.password)
    db.commit()
    return {"status": "ok"}


@app.delete("/api/users/{user_id}")
def delete_user(user_id: int, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    _require_admin(user)
    target = db.query(User).filter(User.id == user_id).first()
    if not target:
        raise HTTPException(status_code=404, detail="Utilisateur introuvable")
    if target.username == user.get("sub"):
        raise HTTPException(status_code=400, detail="Impossible de supprimer votre propre compte connecté")
    db.delete(target)
    db.commit()
    return {"status": "ok"}


# ---------- Chat interne (WhatsApp-like) ----------

CHAT_CHANNELS = ["GENERAL", "PROD", "PED", "MNT", "QUALITY", "METROLOGIE", "METALLURGIE", "ADMINISTRATION", "RH"]


def _channel_allowed(user: dict, channel: str) -> bool:
    if channel == "GENERAL":
        return True
    if user.get("role") == "admin":
        return True
    return user.get("department") == channel


def _msg_out(m: ChatMessage) -> dict:
    if m.deleted:
        return {
            "id": m.id, "channel": m.channel, "sender_username": m.sender_username,
            "sender_name": m.sender_name, "content_type": "text", "text": "🚫 Message supprimé",
            "file_path": None, "file_name": None, "reply_to_id": m.reply_to_id,
            "deleted": True, "timestamp": m.created_at.isoformat(),
        }
    return {
        "id": m.id, "channel": m.channel, "sender_username": m.sender_username,
        "sender_name": m.sender_name, "content_type": m.content_type, "text": m.text,
        "file_path": m.file_path, "file_name": m.file_name, "reply_to_id": m.reply_to_id,
        "deleted": False, "timestamp": m.created_at.isoformat(),
    }


@app.get("/api/chat/channels")
def chat_channels(user: dict = Depends(get_current_user)):
    if user.get("role") == "admin":
        return CHAT_CHANNELS
    visible = ["GENERAL"]
    if user.get("department") in CHAT_CHANNELS:
        visible.append(user.get("department"))
    return visible


@app.get("/api/chat/messages")
def chat_messages(channel: str, before_id: int = 0, limit: int = 50,
                   user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    if not _channel_allowed(user, channel):
        raise HTTPException(status_code=403, detail="Accès refusé à ce channel")
    q = db.query(ChatMessage).filter(ChatMessage.channel == channel)
    if before_id:
        q = q.filter(ChatMessage.id < before_id)
    rows = q.order_by(ChatMessage.id.desc()).limit(_clamp_limit(limit, default=50)).all()
    return [_msg_out(m) for m in reversed(rows)]


@app.post("/api/chat/messages")
async def chat_send(body: ChatMessageRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    if body.channel not in CHAT_CHANNELS:
        raise HTTPException(status_code=400, detail="Channel inconnu")
    if not _channel_allowed(user, body.channel):
        raise HTTPException(status_code=403, detail="Accès refusé à ce channel")
    if body.content_type == "text" and not (body.text or "").strip():
        raise HTTPException(status_code=400, detail="Message vide")

    msg = ChatMessage(
        channel=body.channel,
        sender_username=user.get("sub"),
        sender_name=user.get("full_name"),
        content_type=body.content_type,
        text=body.text,
        file_path=body.file_path,
        file_name=body.file_name,
        reply_to_id=body.reply_to_id,
    )
    db.add(msg)
    db.commit()
    db.refresh(msg)

    payload = {"type": "chat_message", **_msg_out(msg)}
    await manager.broadcast_scoped(payload, scope=body.channel)
    return payload


@app.delete("/api/chat/messages/{message_id}")
async def chat_delete(message_id: int, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    msg = db.query(ChatMessage).filter(ChatMessage.id == message_id).first()
    if not msg:
        raise HTTPException(status_code=404, detail="Message introuvable")
    if msg.sender_username != user.get("sub") and user.get("role") != "admin":
        raise HTTPException(status_code=403, detail="Vous ne pouvez supprimer que vos propres messages")
    msg.deleted = 1
    db.commit()

    await manager.broadcast_scoped({"type": "chat_delete", "id": msg.id, "channel": msg.channel}, scope=msg.channel)
    return {"status": "ok"}


@app.get("/api/chat/search")
def chat_search(channel: str, q: str, limit: int = 50,
                 user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    if not _channel_allowed(user, channel):
        raise HTTPException(status_code=403, detail="Accès refusé à ce channel")
    rows = (
        db.query(ChatMessage)
        .filter(ChatMessage.channel == channel, ChatMessage.deleted == 0, ChatMessage.text.ilike(f"%{q}%"))
        .order_by(ChatMessage.id.desc())
        .limit(_clamp_limit(limit, default=50))
        .all()
    )
    return [_msg_out(m) for m in rows]


@app.get("/api/chat/presence")
def chat_presence(user: dict = Depends(get_current_user)):
    return {"online": manager.get_online_usernames()}


@app.post("/api/chat/upload")
async def chat_upload(file: UploadFile = File(...), user: dict = Depends(get_current_user)):
    raw_ext = os.path.splitext(file.filename or "")[1].lower()
    if raw_ext in DANGEROUS_EXTENSIONS:
        raise HTTPException(status_code=400, detail="Type de fichier non autorisé pour des raisons de sécurité")

    declared_mime = (file.content_type or "").lower()

    # On lit par blocs pour ne jamais charger un fichier trop gros entièrement en mémoire
    # avant de vérifier la taille (protection contre un déni de service par upload massif).
    contents = bytearray()
    while True:
        chunk = await file.read(1024 * 1024)
        if not chunk:
            break
        contents.extend(chunk)
        if len(contents) > MAX_UPLOAD_BYTES:
            raise HTTPException(status_code=413, detail="Fichier trop volumineux (max 25 Mo)")
    contents = bytes(contents)

    real_kind = _sniff_real_kind(contents, declared_mime)
    # Un fichier "file" générique avec une extension dangereuse est déjà bloqué plus haut ;
    # ici on s'assure aussi qu'une image/vidéo dont le contenu ne matche pas son extension
    # web-active (ex: un .svg déclaré comme image) n'est jamais servie en ligne.
    if real_kind in ("image", "video") and raw_ext in DANGEROUS_EXTENSIONS:
        real_kind = "file"

    safe_name = f"{uuid.uuid4().hex}{raw_ext}"

    if real_kind in ("image", "video"):
        dest_path = os.path.join(CHAT_UPLOAD_INLINE_DIR, safe_name)
        url = f"/media/{safe_name}"
    else:
        dest_path = os.path.join(CHAT_UPLOAD_FILES_DIR, safe_name)
        url = f"/media/files/{safe_name}"

    with open(dest_path, "wb") as f:
        f.write(contents)

    return {
        "content_type": real_kind,
        "file_path": url,
        "file_name": file.filename,
    }


@app.get("/media/files/{filename}")
def download_file(filename: str, user: dict = Depends(get_current_user)):
    """Sert les documents génériques (tout ce qui n'est pas image/vidéo) en forçant le
    téléchargement (Content-Disposition: attachment) : même si un fichier malveillant
    passait les vérifications ci-dessus, il ne peut jamais s'exécuter dans l'origine de
    l'application puisqu'il n'est jamais affiché/interprété directement par le navigateur."""
    safe_name = os.path.basename(filename)  # empêche toute tentative de path traversal
    path = os.path.join(CHAT_UPLOAD_FILES_DIR, safe_name)
    if not os.path.isfile(path):
        raise HTTPException(status_code=404, detail="Fichier introuvable")
    return StreamingResponse(
        open(path, "rb"),
        media_type="application/octet-stream",
        headers={"Content-Disposition": f'attachment; filename="{safe_name}"'},
    )


# ---------- Upload générique (réutilisé par le chat ET les photos de maintenance) ----------

@app.post("/api/upload")
async def generic_upload(file: UploadFile = File(...), user: dict = Depends(get_current_user)):
    return await chat_upload(file=file, user=user)


# ---------- Machines (identité digitale + QR/Barcode) ----------

def _machine_out(m: Machine) -> dict:
    return {
        "id": m.id, "machine_code": m.machine_code, "name": m.name, "machine_type": m.machine_type,
        "department": m.department, "location": m.location, "serial_number": m.serial_number,
        "installation_date": m.installation_date, "responsible_employee": m.responsible_employee,
        "status": m.status, "created_at": m.created_at.isoformat(),
    }


def _effective_department_filter(user: dict, requested: str | None) -> str | None:
    """Contrôle d'accès en lecture par département.
    - admin : vue globale, filtre optionnel via le paramètre demandé.
    - technician / tl : vue globale aussi (MNT et QUALITY interviennent par nature sur
      plusieurs départements — restreindre casserait leur usage réel), mais le filtre
      demandé est respecté s'il est fourni.
    - operator : restreint à son propre département uniquement, quelle que soit la
      valeur demandée (c'est le rôle le plus proche du poste de travail, pas besoin de
      visibilité transverse)."""
    if user.get("role") == "operator":
        return user.get("department")
    return requested


@app.get("/api/machines")
def list_machines(department: str | None = None, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    q = db.query(Machine)
    dep = _effective_department_filter(user, department)
    if dep:
        q = q.filter(Machine.department == dep)
    rows = q.order_by(Machine.department, Machine.machine_code).all()
    return [_machine_out(m) for m in rows]


@app.get("/api/machines/{machine_code}")
def get_machine(machine_code: str, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    m = db.query(Machine).filter(Machine.machine_code == machine_code).first()
    if not m:
        raise HTTPException(status_code=404, detail="Machine introuvable")
    return _machine_out(m)


@app.post("/api/machines")
def create_machine(body: MachineCreateRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    _require_admin(user)
    if db.query(Machine).filter(Machine.machine_code == body.machine_code).first():
        raise HTTPException(status_code=400, detail="Ce code machine existe déjà")
    m = Machine(**body.dict())
    db.add(m)
    db.commit()
    db.refresh(m)
    return _machine_out(m)


@app.put("/api/machines/{machine_code}")
def update_machine(machine_code: str, body: MachineUpdateRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    _require_admin(user)
    m = db.query(Machine).filter(Machine.machine_code == machine_code).first()
    if not m:
        raise HTTPException(status_code=404, detail="Machine introuvable")
    for field, value in body.dict(exclude_unset=True).items():
        setattr(m, field, value)
    db.commit()
    return _machine_out(m)


@app.delete("/api/machines/{machine_code}")
def delete_machine(machine_code: str, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    _require_admin(user)
    m = db.query(Machine).filter(Machine.machine_code == machine_code).first()
    if not m:
        raise HTTPException(status_code=404, detail="Machine introuvable")
    db.delete(m)
    db.commit()
    return {"status": "ok"}


# ---------- Alarmes machines ----------

def _alarm_out(a: Alarm) -> dict:
    return {
        "id": a.id, "machine_id": a.machine_id, "machine_code": a.machine_code, "level": a.level,
        "description": a.description, "created_by": a.created_by, "department": a.department,
        "assigned_technician": a.assigned_technician, "status": a.status,
        "created_at": a.created_at.isoformat(),
        "resolved_at": a.resolved_at.isoformat() if a.resolved_at else None,
        "escalated_at": a.escalated_at.isoformat() if a.escalated_at else None,
        "occurrence_count": a.occurrence_count or 1,
    }


ALARM_DEDUP_WINDOW_MINUTES = 5


@app.post("/api/alarms")
async def create_alarm(body: AlarmCreateRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    machine = db.query(Machine).filter(Machine.machine_code == body.machine_code).first()
    if not machine:
        raise HTTPException(status_code=404, detail="Machine introuvable")

    # Anti-doublon : si une alarme identique (même machine, même niveau) est déjà ouverte
    # et a été créée il y a moins de 5 minutes, on ne recrée pas une alarme séparée —
    # on incrémente son compteur d'occurrences et on republie l'événement existant.
    # Objectif : éviter le flood de notifications quand un opérateur clique plusieurs fois.
    dedup_threshold = datetime.utcnow() - timedelta(minutes=ALARM_DEDUP_WINDOW_MINUTES)
    existing = (
        db.query(Alarm)
        .filter(
            Alarm.machine_code == body.machine_code,
            Alarm.level == body.level,
            Alarm.status.in_(["open", "in_progress"]),
            Alarm.created_at >= dedup_threshold,
        )
        .order_by(Alarm.created_at.desc())
        .first()
    )
    if existing:
        existing.occurrence_count = (existing.occurrence_count or 1) + 1
        db.commit()
        db.refresh(existing)
        payload = {"type": "alarm", **_alarm_out(existing), "machine_name": machine.name, "deduplicated": True}
        await manager.broadcast_to_admins(payload)
        await manager.broadcast_scoped(payload, scope=machine.department)
        return _alarm_out(existing)

    alarm = Alarm(
        machine_id=machine.id, machine_code=machine.machine_code, level=body.level,
        description=body.description, created_by=user.get("full_name"), department=machine.department,
    )
    db.add(alarm)
    db.commit()
    db.refresh(alarm)

    payload = {"type": "alarm", **_alarm_out(alarm), "machine_name": machine.name}
    await manager.broadcast_to_admins(payload)
    await manager.broadcast_scoped(payload, scope=machine.department)
    return _alarm_out(alarm)


@app.get("/api/alarms")
def list_alarms(machine_code: str | None = None, status: str | None = None, department: str | None = None,
                 limit: int = 100, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    q = db.query(Alarm)
    if machine_code:
        q = q.filter(Alarm.machine_code == machine_code)
    if status:
        q = q.filter(Alarm.status == status)
    dep = _effective_department_filter(user, department)
    if dep:
        q = q.filter(Alarm.department == dep)
    rows = q.order_by(Alarm.created_at.desc()).limit(_clamp_limit(limit)).all()
    return [_alarm_out(a) for a in rows]


@app.put("/api/alarms/{alarm_id}")
async def update_alarm(alarm_id: int, body: AlarmUpdateRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    a = db.query(Alarm).filter(Alarm.id == alarm_id).first()
    if not a:
        raise HTTPException(status_code=404, detail="Alarme introuvable")
    if body.status is not None:
        a.status = body.status
        if body.status == "resolved":
            a.resolved_at = datetime.utcnow()
    if body.assigned_technician is not None:
        a.assigned_technician = body.assigned_technician
    db.commit()

    payload = {"type": "alarm_update", **_alarm_out(a)}
    await manager.broadcast_to_admins(payload)
    await manager.broadcast_scoped(payload, scope=a.department)
    return _alarm_out(a)


# ---------- Maintenance ----------

def _maint_out(mr: MaintenanceRequest) -> dict:
    return {
        "id": mr.id, "machine_id": mr.machine_id, "machine_code": mr.machine_code, "alarm_id": mr.alarm_id,
        "requested_by": mr.requested_by, "assigned_technician": mr.assigned_technician,
        "description": mr.description, "status": mr.status,
        "created_at": mr.created_at.isoformat(),
        "started_at": mr.started_at.isoformat() if mr.started_at else None,
        "resolved_at": mr.resolved_at.isoformat() if mr.resolved_at else None,
    }


def _note_out(n: MaintenanceNote) -> dict:
    return {
        "id": n.id, "maintenance_request_id": n.maintenance_request_id, "author": n.author,
        "note": n.note, "file_path": n.file_path, "file_name": n.file_name,
        "created_at": n.created_at.isoformat(),
    }


@app.post("/api/maintenance")
async def create_maintenance(body: MaintenanceCreateRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    machine = db.query(Machine).filter(Machine.machine_code == body.machine_code).first()
    if not machine:
        raise HTTPException(status_code=404, detail="Machine introuvable")

    mr = MaintenanceRequest(
        machine_id=machine.id, machine_code=machine.machine_code, alarm_id=body.alarm_id,
        requested_by=user.get("full_name"), assigned_technician=body.assigned_technician,
        description=body.description,
    )
    db.add(mr)
    db.commit()
    db.refresh(mr)

    payload = {"type": "maintenance", **_maint_out(mr), "machine_name": machine.name}
    await manager.broadcast_to_admins(payload)
    await manager.broadcast_scoped(payload, scope="MNT")
    return _maint_out(mr)


@app.get("/api/maintenance")
def list_maintenance(machine_code: str | None = None, status: str | None = None,
                      limit: int = 100, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    q = db.query(MaintenanceRequest)
    if machine_code:
        q = q.filter(MaintenanceRequest.machine_code == machine_code)
    if status:
        q = q.filter(MaintenanceRequest.status == status)
    rows = q.order_by(MaintenanceRequest.created_at.desc()).limit(_clamp_limit(limit)).all()
    return [_maint_out(m) for m in rows]


@app.put("/api/maintenance/{maintenance_id}")
async def update_maintenance(maintenance_id: int, body: MaintenanceUpdateRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    mr = db.query(MaintenanceRequest).filter(MaintenanceRequest.id == maintenance_id).first()
    if not mr:
        raise HTTPException(status_code=404, detail="Intervention introuvable")
    if body.status is not None:
        if body.status == "in_progress" and not mr.started_at:
            mr.started_at = datetime.utcnow()
        if body.status == "resolved":
            mr.resolved_at = datetime.utcnow()
        mr.status = body.status
    if body.assigned_technician is not None:
        mr.assigned_technician = body.assigned_technician
    db.commit()

    payload = {"type": "maintenance_update", **_maint_out(mr)}
    await manager.broadcast_to_admins(payload)
    await manager.broadcast_scoped(payload, scope="MNT")
    return _maint_out(mr)


@app.post("/api/maintenance/{maintenance_id}/notes")
def add_maintenance_note(maintenance_id: int, body: MaintenanceNoteRequest, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    mr = db.query(MaintenanceRequest).filter(MaintenanceRequest.id == maintenance_id).first()
    if not mr:
        raise HTTPException(status_code=404, detail="Intervention introuvable")
    note = MaintenanceNote(
        maintenance_request_id=maintenance_id, author=user.get("full_name"),
        note=body.note, file_path=body.file_path, file_name=body.file_name,
    )
    db.add(note)
    db.commit()
    db.refresh(note)
    return _note_out(note)


@app.get("/api/maintenance/{maintenance_id}/notes")
def get_maintenance_notes(maintenance_id: int, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    rows = db.query(MaintenanceNote).filter(MaintenanceNote.maintenance_request_id == maintenance_id).order_by(MaintenanceNote.created_at).all()
    return [_note_out(n) for n in rows]


# ---------- Command Center : Analytics (données réelles, aucune donnée inventée) ----------

@app.get("/api/analytics/kpis")
def analytics_kpis(day: str | None = None, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    day = day or datetime.utcnow().strftime("%Y-%m-%d")
    return analytics.get_factory_kpis(db, day)


@app.get("/api/analytics/production-live")
def analytics_production_live(day: str | None = None, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    day = day or datetime.utcnow().strftime("%Y-%m-%d")
    return analytics.get_production_live(db, day)


@app.get("/api/analytics/traceability")
def analytics_traceability(department: str | None = None, q: str | None = None,
                            date_from: str | None = None, date_to: str | None = None, limit: int = 200,
                            user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    return analytics.get_traceability(db, department, q, date_from, date_to, limit)


@app.get("/api/analytics/datasets")
def analytics_datasets(day: str | None = None, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    """Toutes les données brutes du jour, regroupées en un seul JSON — le point d'entrée
    pensé pour être consommé par des outils d'analyse externes (Excel, Python, etc.)."""
    day = day or datetime.utcnow().strftime("%Y-%m-%d")
    return analytics.get_all_datasets(db, day)


# ---------- Scripts d'analyse personnalisés (voir backend/app/analysis_scripts/) ----------
# Ajouter un script = ajouter un fichier dans ce dossier, AUCUNE ligne à changer ici.

@app.get("/api/analytics/custom")
def analytics_custom_list(user: dict = Depends(get_current_user)):
    return {"scripts": analysis_scripts.list_scripts()}


@app.get("/api/analytics/custom/{script_name}")
def analytics_custom_run(script_name: str, day: str | None = None,
                          user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    day = day or datetime.utcnow().strftime("%Y-%m-%d")
    data = analytics.get_all_datasets(db, day)
    try:
        return analysis_scripts.run_script(script_name, data)
    except KeyError:
        raise HTTPException(status_code=404, detail=f"Script d'analyse inconnu: {script_name}")


# ---------- Export (Excel / Word / PDF) ----------

def _report_table(db: Session, report: str, day: str, department: str | None, q: str | None):
    if report == "production_live":
        rows = analytics.get_production_live(db, day)
        headers = ["Ligne/Process/Machine", "Opérateur", "Target", "Produit", "Bonnes pièces", "Rejetées", "Perf %", "Rejet %", "Statut", "Dernière MàJ"]
        data = [[r["line_ref"], r["operator"], r["target"], r["produced"], r["good_parts"], r["rejected"], r["performance_pct"], r["reject_pct"], r["status"], r["last_update"]] for r in rows]
        return "Production en direct — PROD", headers, data

    if report == "machines":
        rows = [_machine_out(m) for m in db.query(Machine).all()]
        headers = ["Code", "Nom", "Type", "Département", "Emplacement", "N° Série", "Responsable", "Statut"]
        data = [[r["machine_code"], r["name"], r["machine_type"], r["department"], r["location"], r["serial_number"], r["responsible_employee"], r["status"]] for r in rows]
        return "Machines enregistrées", headers, data

    if report == "maintenance":
        rows = [_maint_out(m) for m in db.query(MaintenanceRequest).order_by(MaintenanceRequest.created_at.desc()).all()]
        headers = ["Machine", "Demandé par", "Technicien", "Description", "Statut", "Créée le", "Résolue le"]
        data = [[r["machine_code"], r["requested_by"], r["assigned_technician"], r["description"], r["status"], r["created_at"], r["resolved_at"]] for r in rows]
        return "Historique Maintenance", headers, data

    if report == "traceability":
        rows = analytics.get_traceability(db, department, q, None, None, 500)
        headers = ["Type", "Département", "Utilisateur", "Contexte", "Détail", "Horodatage"]
        data = [[r["type"], r["department"], r["user"], r["page"], r["detail"], r["timestamp"]] for r in rows]
        return "Traçabilité complète", headers, data

    raise HTTPException(status_code=400, detail="Rapport inconnu")


@app.get("/api/export")
def export_report(report: str, format: str, day: str | None = None, department: str | None = None,
                   q: str | None = None, user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    day = day or datetime.utcnow().strftime("%Y-%m-%d")
    title, headers, data = _report_table(db, report, day, department, q)

    if format == "xlsx":
        wb = openpyxl.Workbook()
        ws = wb.active
        ws.title = title[:30]
        ws.append(["IM JX Smart Suite"])
        ws.append([title, f"Exporté le {datetime.utcnow().strftime('%Y-%m-%d %H:%M')} UTC par {user.get('full_name')}"])
        ws.append([])
        ws.append(headers)
        for cell in ws[4]:
            cell.font = Font(bold=True)
        for row in data:
            ws.append(row)
        buf = io.BytesIO()
        wb.save(buf)
        buf.seek(0)
        return StreamingResponse(buf, media_type="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
                                  headers={"Content-Disposition": f"attachment; filename={report}.xlsx"})

    if format == "docx":
        doc = DocxDocument()
        doc.add_heading("IM JX Smart Suite", level=1)
        doc.add_paragraph(f"{title} — exporté le {datetime.utcnow().strftime('%Y-%m-%d %H:%M')} UTC par {user.get('full_name')}")
        table = doc.add_table(rows=1, cols=len(headers))
        table.style = "Light Grid Accent 1"
        for i, h in enumerate(headers):
            table.rows[0].cells[i].text = str(h)
        for row in data:
            cells = table.add_row().cells
            for i, val in enumerate(row):
                cells[i].text = "" if val is None else str(val)
        buf = io.BytesIO()
        doc.save(buf)
        buf.seek(0)
        return StreamingResponse(buf, media_type="application/vnd.openxmlformats-officedocument.wordprocessingml.document",
                                  headers={"Content-Disposition": f"attachment; filename={report}.docx"})

    if format == "pdf":
        buf = io.BytesIO()
        doc = SimpleDocTemplate(buf, pagesize=landscape(A4))
        styles = getSampleStyleSheet()
        elements = [
            Paragraph("IM JX Smart Suite", styles["Title"]),
            Paragraph(f"{title} — exporté le {datetime.utcnow().strftime('%Y-%m-%d %H:%M')} UTC par {user.get('full_name')}", styles["Normal"]),
        ]
        table_data = [headers] + [[("" if v is None else str(v)) for v in row] for row in data]
        t = Table(table_data, repeatRows=1)
        t.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#1e293b")),
            ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
            ("FONTSIZE", (0, 0), (-1, -1), 7),
            ("GRID", (0, 0), (-1, -1), 0.5, colors.grey),
        ]))
        elements.append(t)
        doc.build(elements)
        buf.seek(0)
        return StreamingResponse(buf, media_type="application/pdf",
                                  headers={"Content-Disposition": f"attachment; filename={report}.pdf"})

    raise HTTPException(status_code=400, detail="Format inconnu (xlsx | docx | pdf)")


# ---------- Supervision système (page développeur) ----------

import shutil as _shutil
from . import escalation as _escalation_module


@app.get("/api/health")
def health_check(db: Session = Depends(get_db)):
    """Endpoint de santé consommé par le HEALTHCHECK Docker ET par la page développeur.
    Ne nécessite pas d'authentification (Docker doit pouvoir l'appeler sans token) mais ne
    renvoie que des indicateurs agrégés, aucune donnée métier."""
    status = "ok"
    checks = {}

    try:
        db.execute(text("SELECT 1"))
        checks["database"] = "ok"
    except Exception as exc:
        checks["database"] = f"down: {exc}"
        status = "down"

    try:
        total, used, free = _shutil.disk_usage(os.path.dirname(os.environ.get("DATABASE_PATH", "/data/imjx.db")))
        free_pct = round(free / total * 100, 1) if total else 0
        checks["disk_free_pct"] = free_pct
        if free_pct < 10 and status == "ok":
            status = "degraded"
    except Exception as exc:
        checks["disk_free_pct"] = None

    checks["websocket_connections"] = len(manager.connections)
    checks["uptime_seconds"] = int((datetime.utcnow() - APP_START_TIME).total_seconds())

    return {"status": status, "checks": checks, "timestamp": datetime.utcnow().isoformat()}


@app.get("/api/system/data-flow")
def system_data_flow(user: dict = Depends(get_current_user)):
    """Cartographie des flux de données : liste toutes les routes HTTP + WebSocket
    réellement enregistrées dans l'application (générée dynamiquement à partir des
    routes FastAPI, donc toujours à jour même si de nouveaux endpoints sont ajoutés)."""
    _require_admin(user)
    routes = []
    for route in app.routes:
        methods = sorted(getattr(route, "methods", None) or [])
        path = getattr(route, "path", None)
        if not path or path.startswith("/media") or path == "/{path:path}":
            continue
        if "HEAD" in methods:
            methods.remove("HEAD")
        if methods or path == "/ws":
            routes.append({
                "path": path,
                "methods": methods or (["WS"] if path == "/ws" else []),
                "name": getattr(route, "name", ""),
            })
    routes.sort(key=lambda r: r["path"])

    # Événements WebSocket émis par le serveur (extraits manuellement du code : la liste
    # des `manager.broadcast_*` dans main.py/escalation.py). Tenue à jour à chaque ajout
    # d'un nouveau type d'événement temps réel.
    ws_events = [
        {"type": "sync", "emitted_by": "POST /api/sync", "received_by": "admins"},
        {"type": "reclamation", "emitted_by": "POST /api/reclamations", "received_by": "admins"},
        {"type": "alarm", "emitted_by": "POST /api/reclamations (ciblée) / POST /api/alarms", "received_by": "département concerné + admins"},
        {"type": "alarm_update", "emitted_by": "PUT /api/alarms/{id}", "received_by": "département concerné + admins"},
        {"type": "alarm_escalated", "emitted_by": "tâche de fond escalation.py", "received_by": "département concerné + admins"},
        {"type": "command", "emitted_by": "POST /api/commands", "received_by": "département/rôle ciblé"},
        {"type": "maintenance", "emitted_by": "POST /api/maintenance", "received_by": "MNT + admins"},
        {"type": "maintenance_update", "emitted_by": "PUT /api/maintenance/{id}", "received_by": "MNT + admins"},
        {"type": "chat_message", "emitted_by": "POST /api/chat/messages", "received_by": "abonnés du channel"},
        {"type": "chat_delete", "emitted_by": "DELETE /api/chat/messages/{id}", "received_by": "abonnés du channel"},
        {"type": "presence", "emitted_by": "connexion/déconnexion WebSocket", "received_by": "tout le monde"},
        {"type": "ping", "emitted_by": "heartbeat serveur (toutes les 30s)", "received_by": "tous les clients connectés"},
    ]

    # Pages métier -> endpoints qu'elles appellent (statique, reflète l'architecture des
    # pages front-end qui ne changent pas aussi souvent que les routes backend).
    page_flows = [
        {"page": "pages/PROD/*.html", "sends_to": ["/api/sync", "/api/reclamations", "/api/alarms"]},
        {"page": "pages/PED/*.html", "sends_to": ["/api/sync", "/api/reclamations", "/api/alarms"]},
        {"page": "pages/MNT/*.html", "sends_to": ["/api/sync", "/api/maintenance", "/api/alarms"]},
        {"page": "pages/QUALITY/*.html", "sends_to": ["/api/sync", "/api/reclamations", "/api/alarms"]},
        {"page": "pages/METROLOGIE/*.html", "sends_to": ["/api/sync", "/api/reclamations"]},
        {"page": "pages/METALLURGIE/*.html", "sends_to": ["/api/sync", "/api/reclamations"]},
        {"page": "scan.html / machine.html", "sends_to": ["/api/machines/{code}", "/api/alarms", "/api/maintenance"]},
        {"page": "admin.html", "sends_to": ["/api/commands", "/api/users", "/api/analytics/*", "/api/export"]},
        {"page": "toutes les pages (widget partagé)", "sends_to": ["/ws (temps réel)", "/api/chat/*"]},
    ]

    return {"http_routes": routes, "websocket_events": ws_events, "page_flows": page_flows}


@app.get("/api/system/alarms-catalog")
def system_alarms_catalog(user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    """Registre de TOUTES les alarmes possibles dans le système : alarmes machine (données
    réelles) + alarmes système (dérivées de l'état réel du système, pas de texte statique)."""
    _require_admin(user)

    machine_alarms_open = db.query(Alarm).filter(Alarm.status != "resolved").count()
    critical_open = db.query(Alarm).filter(Alarm.level == "critical", Alarm.status != "resolved").count()
    escalated = db.query(Alarm).filter(Alarm.escalated_at.isnot(None), Alarm.status != "resolved").count()

    health = health_check(db=db)
    disk_free_pct = health["checks"].get("disk_free_pct")

    login_lockouts = sum(1 for attempts in auth_module._login_attempts.values() if len(attempts) >= 5)

    catalog = [
        {
            "category": "Alarme machine",
            "gravité": "critical | warning | info",
            "condition": "Créée manuellement par un opérateur/technicien (scan QR ou saisie)",
            "action_automatique": "Notification temps réel au département + admins ; anti-doublon 5 min ; "
                                   f"escalade automatique après {_escalation_module.ESCALATION_DELAY_MINUTES} min si critique et non traitée",
            "état_actuel": f"{machine_alarms_open} ouverte(s), dont {critical_open} critique(s), {escalated} escaladée(s)",
        },
        {
            "category": "Système — Base de données",
            "gravité": "critical",
            "condition": "Échec de connexion / requête à la base de données",
            "action_automatique": "Reflété dans /api/health (status=down) ; Docker redémarre le conteneur via HEALTHCHECK",
            "état_actuel": health["checks"].get("database"),
        },
        {
            "category": "Système — Espace disque",
            "gravité": "warning",
            "condition": "Espace disque libre sur /data < 10%",
            "action_automatique": "Reflété dans /api/health (status=degraded)",
            "état_actuel": f"{disk_free_pct}% libre" if disk_free_pct is not None else "inconnu",
        },
        {
            "category": "Système — WebSocket",
            "gravité": "info",
            "condition": "Connexion perdue / client déconnecté brutalement",
            "action_automatique": "Nettoyage automatique de la connexion morte (ws_manager.py) ; "
                                   "heartbeat serveur toutes les 30s ; reconnexion client avec backoff progressif",
            "état_actuel": f"{len(manager.connections)} connexion(s) active(s)",
        },
        {
            "category": "Système — Authentification",
            "gravité": "warning",
            "condition": "5 échecs de connexion en moins d'une minute pour un même compte (brute force potentiel)",
            "action_automatique": "Blocage temporaire (429) pendant 60s ; échec loggé",
            "état_actuel": f"{login_lockouts} compte(s) actuellement limité(s)",
        },
        {
            "category": "Système — Upload de fichiers",
            "gravité": "warning",
            "condition": "Fichier > 25 Mo, ou extension dangereuse (.html/.svg/.js/...), ou type réel "
                         "différent du type déclaré",
            "action_automatique": "Upload rejeté (413/400) avant écriture sur disque",
            "état_actuel": "—",
        },
        {
            "category": "Système — Format de données production",
            "gravité": "warning",
            "condition": "Payload reçu sur /api/sync qui ne correspond pas au format HOURS_DATA attendu",
            "action_automatique": "Averti dans les logs applicatifs (voir analytics.py) au lieu d'être "
                                   "silencieusement ignoré",
            "état_actuel": "—",
        },
    ]
    return {"catalog": catalog, "generated_at": datetime.utcnow().isoformat()}


@app.get("/api/system/status")
def system_status(user: dict = Depends(get_current_user), db: Session = Depends(get_db)):
    """Tableau de bord de supervision temps réel (partie C de la page développeur)."""
    _require_admin(user)
    health = health_check(db=db)

    last_sync_by_department = {}
    for dep in ["PROD", "PED", "MNT", "QUALITY", "METROLOGIE", "METALLURGIE"]:
        last = (
            db.query(SyncEvent)
            .filter(SyncEvent.department == dep)
            .order_by(SyncEvent.created_at.desc())
            .first()
        )
        last_sync_by_department[dep] = last.created_at.isoformat() if last else None

    critical_open = db.query(Alarm).filter(Alarm.level == "critical", Alarm.status != "resolved", Alarm.escalated_at.is_(None)).count()
    critical_escalated = db.query(Alarm).filter(Alarm.level == "critical", Alarm.status != "resolved", Alarm.escalated_at.isnot(None)).count()

    return {
        "health": health,
        "websocket_connections_by_department": manager.get_connection_count_by_department(),
        "last_sync_by_department": last_sync_by_department,
        "critical_alarms_open": critical_open,
        "critical_alarms_escalated": critical_escalated,
        "timestamp": datetime.utcnow().isoformat(),
    }


# ---------- WebSocket temps réel ----------

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    user = await get_ws_user(websocket)
    if user is None:
        await websocket.close(code=4401)
        return
    await manager.connect(websocket, user)
    await manager.broadcast_presence(user.get("sub"), "online")
    try:
        while True:
            await websocket.receive_text()  # ping/pong keepalive, on ignore le contenu
    except WebSocketDisconnect:
        manager.disconnect(websocket)
        await manager.broadcast_presence(user.get("sub"), "offline")


# ---------- Fichiers statiques (les pages HTML + médias du chat) ----------

app.mount("/media", StaticFiles(directory=CHAT_UPLOAD_INLINE_DIR), name="media")
app.mount("/", StaticFiles(directory="/app/static", html=True), name="static")

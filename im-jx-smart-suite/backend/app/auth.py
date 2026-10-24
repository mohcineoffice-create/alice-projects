import os
import time
import secrets
import hashlib
import hmac
import logging

import jwt
from fastapi import Header, HTTPException, WebSocket
from sqlalchemy.orm import Session

from .database import User

logger = logging.getLogger("imjx")

ALGORITHM = "HS256"
TOKEN_TTL_SECONDS = 60 * 60 * 12  # 12h

# ---------- SECRET_KEY : refuse de démarrer avec une clé faible/par défaut ----------
_INSECURE_DEFAULTS = {"", "change-me-in-production", "changeme", "secret"}
SECRET_KEY = os.environ.get("SECRET_KEY", "")
if SECRET_KEY.strip().lower() in _INSECURE_DEFAULTS:
    raise RuntimeError(
        "SECRET_KEY manquant ou non sécurisé. Définis une variable d'environnement "
        "SECRET_KEY forte avant de démarrer l'application (ex: `openssl rand -hex 32`). "
        "Voir .env.example / README.md."
    )

# Ancien salt partagé (une seule valeur pour tout le monde) — conservé UNIQUEMENT pour
# pouvoir migrer en douceur les comptes créés avant l'introduction du salt par utilisateur.
# Ne sert plus à hacher de nouveaux mots de passe.
_LEGACY_SHARED_SALT = "im-jx-salt"


# ---------- Hachage des mots de passe (PBKDF2-HMAC-SHA256, salt aléatoire par utilisateur) ----------

def generate_salt() -> str:
    return secrets.token_hex(16)


def hash_password(password: str, salt: str) -> str:
    return hashlib.pbkdf2_hmac("sha256", password.encode(), salt.encode(), 100_000).hex()


def hash_new_password(password: str) -> tuple[str, str]:
    """Utilisé à la création d'un compte / réinitialisation : génère un salt neuf."""
    salt = generate_salt()
    return hash_password(password, salt), salt


def verify_password_legacy(password: str) -> str:
    """Hash avec l'ancien salt partagé, pour vérifier les comptes pas encore migrés."""
    return hash_password(password, _LEGACY_SHARED_SALT)


def verify_password(password: str, user: User) -> bool:
    if user.password_salt:
        return hmac.compare_digest(hash_password(password, user.password_salt), user.password_hash)
    # Compte pas encore migré (créé avant le salt par utilisateur) : on vérifie avec
    # l'ancien schéma. L'appelant (login()) se charge de migrer si ça correspond.
    return hmac.compare_digest(verify_password_legacy(password), user.password_hash)


def create_token(user: User) -> str:
    payload = {
        "sub": user.username,
        "role": user.role,
        "department": user.department,
        "full_name": user.full_name,
        "exp": int(time.time()) + TOKEN_TTL_SECONDS,
    }
    return jwt.encode(payload, SECRET_KEY, algorithm=ALGORITHM)


def decode_token(token: str) -> dict:
    try:
        return jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
    except jwt.ExpiredSignatureError:
        raise HTTPException(status_code=401, detail="Session expirée, merci de vous reconnecter")
    except jwt.PyJWTError:
        raise HTTPException(status_code=401, detail="Token invalide ou expiré")


def get_current_user(authorization: str = Header(default=None)) -> dict:
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status_code=401, detail="Authentification requise")
    token = authorization.split(" ", 1)[1]
    return decode_token(token)


def require_admin(user: dict = None):
    if user is None or user.get("role") != "admin":
        raise HTTPException(status_code=403, detail="Réservé à l'administrateur")


async def get_ws_user(websocket: WebSocket) -> dict | None:
    token = websocket.query_params.get("token")
    if not token:
        return None
    try:
        return decode_token(token)
    except HTTPException:
        return None


# ---------- Rate limiting simple en mémoire sur le login ----------
# Note : en mémoire process -> suffisant pour un seul worker Uvicorn (cas actuel du
# docker-compose). Si l'app tourne un jour sur plusieurs workers/instances, remplacer
# par un store partagé (ex: Redis) pour que la limite soit globale.
LOGIN_MAX_ATTEMPTS = 5
LOGIN_WINDOW_SECONDS = 60
_login_attempts: dict[str, list[float]] = {}


def check_login_rate_limit(identifier: str):
    now = time.time()
    attempts = [t for t in _login_attempts.get(identifier, []) if now - t < LOGIN_WINDOW_SECONDS]
    if len(attempts) >= LOGIN_MAX_ATTEMPTS:
        logger.warning("Rate limit login dépassé pour %s", identifier)
        raise HTTPException(
            status_code=429,
            detail=f"Trop de tentatives. Réessayez dans {LOGIN_WINDOW_SECONDS} secondes.",
        )
    attempts.append(now)
    _login_attempts[identifier] = attempts


def reset_login_rate_limit(identifier: str):
    _login_attempts.pop(identifier, None)

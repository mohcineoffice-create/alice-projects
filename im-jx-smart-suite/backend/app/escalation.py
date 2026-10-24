"""Escalade automatique des alarmes critiques.

Une alarme de niveau "critical" toujours "open" après ESCALATION_DELAY_MINUTES minutes
est considérée comme non traitée : on la marque comme escaladée (escalated_at) et on
renvoie une notification de rappel au département concerné + aux admins, pour qu'elle
ne se perde jamais silencieusement dans la liste.
"""
import os
import asyncio
import logging
from datetime import datetime, timedelta

from .database import SessionLocal, Alarm
from .ws_manager import manager

logger = logging.getLogger("imjx")

ESCALATION_DELAY_MINUTES = int(os.environ.get("ALARM_ESCALATION_DELAY_MINUTES", "15"))
ESCALATION_CHECK_INTERVAL_SECONDS = 60

_task = None


async def _escalation_loop():
    while True:
        await asyncio.sleep(ESCALATION_CHECK_INTERVAL_SECONDS)
        try:
            await _run_escalation_check()
        except Exception:
            logger.exception("Erreur pendant la vérification d'escalade des alarmes")


async def _run_escalation_check():
    threshold = datetime.utcnow() - timedelta(minutes=ESCALATION_DELAY_MINUTES)
    db = SessionLocal()
    try:
        overdue = (
            db.query(Alarm)
            .filter(Alarm.level == "critical", Alarm.status == "open",
                    Alarm.escalated_at.is_(None), Alarm.created_at <= threshold)
            .all()
        )
        for alarm in overdue:
            alarm.escalated_at = datetime.utcnow()
            db.commit()
            logger.warning(
                "Alarme critique #%s (machine %s) escaladée : non traitée depuis %s min",
                alarm.id, alarm.machine_code, ESCALATION_DELAY_MINUTES,
            )
            payload = {
                "type": "alarm_escalated",
                "id": alarm.id,
                "machine_code": alarm.machine_code,
                "department": alarm.department,
                "description": alarm.description,
                "message": f"⏰ Alarme critique #{alarm.id} sur {alarm.machine_code} non traitée "
                           f"depuis {ESCALATION_DELAY_MINUTES} min — escalade automatique.",
                "timestamp": alarm.escalated_at.isoformat(),
            }
            await manager.broadcast_to_admins(payload)
            await manager.broadcast_scoped(payload, scope=alarm.department)
    finally:
        db.close()


def start_escalation_task():
    global _task
    if _task is None:
        _task = asyncio.create_task(_escalation_loop())

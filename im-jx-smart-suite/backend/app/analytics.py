"""
Analytics du Command Center — calculs 100% dérivés des données réellement soumises
par les pages existantes (aucune donnée inventée).

Source des quantités de production (Target / Actual / Reject) : les pages PROD
envoient à chaque autosave l'état cumulé complet de leur poste dans un payload texte
contenant un bloc HOURS_DATA:[Hour:HH-HH|Tgt:N|Act:N|Rej:N|Loss:...|Q:...],...
On prend, pour la date demandée, le DERNIER événement de chaque (opérateur + ligne)
— puisque chaque événement contient déjà le cumul de la journée, pas un delta — puis
on additionne les Tgt/Act/Rej de ses créneaux horaires.

Ce qui N'EST PAS calculable avec les données actuelles (et n'est donc pas fourni ici) :
- Numéro de pièce (part number), cause racine, action corrective : ces champs ne sont
  saisis nulle part dans les pages existantes.
- Performance par process nommé (CNC/Spline/Grooving/VICI) sur plusieurs semaines :
  possible en théorie (le process est dans la référence ligne), mais nécessite un
  historique multi-jours ; ici on ne calcule que le jour demandé.
"""

import re
from datetime import datetime, date
from sqlalchemy.orm import Session

from .database import SyncEvent, Machine, Alarm, MaintenanceRequest, Reclamation, Command

HOURS_RE = re.compile(r"\[Hour:([^|]+)\|Tgt:(-?\d+)\|Act:(-?\d+)\|Rej:(-?\d+)\|")
LINE_RE = re.compile(r"^L(\d+)-(.+)-([^\-\s]+)$")


def _parse_payload_date(raw_payload: str) -> str | None:
    parts = raw_payload.split(";")
    return parts[1] if len(parts) > 1 else None


def _parse_line_ref(raw_payload: str) -> str | None:
    parts = raw_payload.split(";")
    for p in parts:
        if p.startswith("L") and "-" in p:
            return p
    return None


def get_production_live(db: Session, day: str) -> list[dict]:
    """Dernier état cumulé du jour, par (opérateur, ligne), pour le département PROD."""
    events = (
        db.query(SyncEvent)
        .filter(SyncEvent.department == "PROD")
        .order_by(SyncEvent.created_at.desc())
        .limit(2000)
        .all()
    )

    latest_by_key: dict[str, SyncEvent] = {}
    for ev in events:
        if _parse_payload_date(ev.raw_payload) != day:
            continue
        line_ref = _parse_line_ref(ev.raw_payload) or "?"
        key = f"{ev.operator}|{line_ref}"
        if key not in latest_by_key:  # déjà trié par created_at desc -> 1er = plus récent
            latest_by_key[key] = ev

    rows = []
    for key, ev in latest_by_key.items():
        matches = HOURS_RE.findall(ev.raw_payload)
        tgt_total = sum(int(m[1]) for m in matches)
        act_total = sum(int(m[2]) for m in matches)
        rej_total = sum(int(m[3]) for m in matches)
        good = act_total - rej_total
        perf_pct = round((act_total / tgt_total) * 100, 1) if tgt_total > 0 else 0.0
        reject_pct = round((rej_total / act_total) * 100, 1) if act_total > 0 else 0.0
        line_ref = _parse_line_ref(ev.raw_payload) or "?"

        status = "operational"
        if tgt_total > 0 and act_total == 0:
            status = "stopped"
        elif perf_pct < 70:
            status = "underperforming"

        rows.append({
            "department": "PROD",
            "line_ref": line_ref,
            "operator": ev.operator,
            "target": tgt_total,
            "produced": act_total,
            "good_parts": good,
            "rejected": rej_total,
            "performance_pct": perf_pct,
            "reject_pct": reject_pct,
            "status": status,
            "last_update": ev.created_at.isoformat(),
        })

    rows.sort(key=lambda r: r["line_ref"])
    return rows


def get_factory_kpis(db: Session, day: str) -> dict:
    prod_rows = get_production_live(db, day)
    target_total = sum(r["target"] for r in prod_rows)
    produced_total = sum(r["produced"] for r in prod_rows)
    good_total = sum(r["good_parts"] for r in prod_rows)
    reject_total = sum(r["rejected"] for r in prod_rows)
    performance_pct = round((produced_total / target_total) * 100, 1) if target_total > 0 else 0.0

    machines = db.query(Machine).all()
    machines_running = sum(1 for m in machines if m.status == "operational")
    machines_stopped = sum(1 for m in machines if m.status in ("down", "retired"))
    machines_maintenance = sum(1 for m in machines if m.status == "maintenance")

    machines_in_alarm = (
        db.query(Alarm.machine_code).filter(Alarm.status != "resolved").distinct().count()
    )
    open_maintenance = db.query(MaintenanceRequest).filter(MaintenanceRequest.status != "resolved").count()
    open_quality_issues = db.query(Alarm).filter(Alarm.department == "QUALITY", Alarm.status != "resolved").count()

    return {
        "date": day,
        "production_target": target_total,
        "production_actual": produced_total,
        "good_parts": good_total,
        "rejected_parts": reject_total,
        "performance_pct": performance_pct,
        "reject_pct": round((reject_total / produced_total) * 100, 1) if produced_total > 0 else 0.0,
        "machines_running": machines_running,
        "machines_stopped": machines_stopped,
        "machines_maintenance": machines_maintenance,
        "machines_total": len(machines),
        "machines_in_alarm": machines_in_alarm,
        "open_maintenance": open_maintenance,
        "open_quality_issues": open_quality_issues,
        "lines_reporting": len(prod_rows),
    }


def get_traceability(db: Session, department: str | None, q: str | None,
                      date_from: str | None, date_to: str | None, limit: int = 200) -> list[dict]:
    """Journal d'audit permanent (rien n'est jamais supprimé) — combine les 3 tables
    déjà en place : sync_events, reclamations, commands."""
    limit = max(1, min(limit or 200, 2000))  # plafond serveur (les scripts d'analyse ont besoin de plus que 500)
    rows: list[dict] = []

    sq = db.query(SyncEvent)
    if department:
        sq = sq.filter(SyncEvent.department == department)
    for ev in sq.order_by(SyncEvent.created_at.desc()).limit(limit).all():
        if q and q.lower() not in (ev.raw_payload or "").lower():
            continue
        rows.append({
            "type": "sync", "department": ev.department, "user": ev.operator, "page": ev.page,
            "detail": (ev.raw_payload or "")[:200], "timestamp": ev.created_at.isoformat(),
        })

    rq = db.query(Reclamation)
    if department:
        rq = rq.filter(Reclamation.department == department)
    for r in rq.order_by(Reclamation.created_at.desc()).limit(limit).all():
        if q and q.lower() not in (r.raw_payload or "").lower():
            continue
        rows.append({
            "type": "reclamation", "department": r.department, "user": r.operator, "page": r.target,
            "detail": (r.raw_payload or "")[:200], "timestamp": r.created_at.isoformat(),
        })

    cq = db.query(Command)
    if department:
        cq = cq.filter((Command.target_department == department) | (Command.target_department == "ALL"))
    for c in cq.order_by(Command.created_at.desc()).limit(limit).all():
        if q and q.lower() not in (c.message or "").lower():
            continue
        rows.append({
            "type": "command", "department": c.target_department, "user": c.sender, "page": c.target_role,
            "detail": (c.message or "")[:200], "timestamp": c.created_at.isoformat(),
        })

    if date_from:
        rows = [r for r in rows if r["timestamp"] >= date_from]
    if date_to:
        rows = [r for r in rows if r["timestamp"] <= date_to + "T23:59:59"]

    rows.sort(key=lambda r: r["timestamp"], reverse=True)
    return rows[:limit]


# =====================================================================================
# ZONE D'EXTENSION — DONNÉES POUR SCRIPTS D'ANALYSE
# =====================================================================================
# get_all_datasets() rassemble TOUTES les données déjà en base dans un seul dict de
# variables prêtes à l'emploi. Un futur script d'analyse n'a jamais besoin de toucher
# au reste de l'app (main.py, les pages HTML, etc.) : il importe cette fonction,
# récupère le dict, et travaille sur les variables qu'il contient.
#
# Exemple d'utilisation dans un nouveau script placé dans backend/app/analysis_scripts/ :
#
#   from ..analytics import get_all_datasets
#   def run(db, day):
#       data = get_all_datasets(db, day)
#       production = data["production_live"]   # liste de dicts, une ligne par poste
#       kpis = data["factory_kpis"]             # dict des KPIs globaux du jour
#       machines = data["machines"]             # liste de toutes les machines
#       alarms = data["alarms"]                 # liste de toutes les alarmes
#       maintenance = data["maintenance"]       # liste des demandes de maintenance
#       traceability = data["traceability"]     # journal complet (sync/reclam/commands)
#       # ... calculs personnalisés ici, retourner un dict JSON-sérialisable
#
# Voir backend/app/analysis_scripts/__init__.py pour le mécanisme d'enregistrement
# automatique (aucune modification de main.py nécessaire pour ajouter un script).
# =====================================================================================

def get_all_datasets(db: Session, day: str) -> dict:
    """Point d'entrée unique : toutes les variables/données brutes utiles à l'analyse,
    regroupées dans un seul dict, pour un jour donné."""
    production_live = get_production_live(db, day)
    factory_kpis = get_factory_kpis(db, day)
    traceability = get_traceability(db, department=None, q=None, date_from=day, date_to=day, limit=2000)

    machines = [
        {
            "machine_code": m.machine_code, "name": m.name, "machine_type": m.machine_type,
            "department": m.department, "status": m.status, "location": m.location,
        }
        for m in db.query(Machine).all()
    ]
    alarms = [
        {
            "id": a.id, "machine_code": a.machine_code, "level": a.level, "status": a.status,
            "department": a.department, "created_by": a.created_by,
            "created_at": a.created_at.isoformat() if a.created_at else None,
            "resolved_at": a.resolved_at.isoformat() if a.resolved_at else None,
        }
        for a in db.query(Alarm).all()
    ]
    maintenance = [
        {
            "id": mr.id, "machine_code": mr.machine_code, "status": mr.status,
            "requested_by": mr.requested_by, "assigned_technician": mr.assigned_technician,
            "created_at": mr.created_at.isoformat() if mr.created_at else None,
            "resolved_at": mr.resolved_at.isoformat() if mr.resolved_at else None,
        }
        for mr in db.query(MaintenanceRequest).all()
    ]
    reclamations = [
        {
            "id": r.id, "department": r.department, "operator": r.operator, "category": r.category,
            "status": r.status, "created_at": r.created_at.isoformat() if r.created_at else None,
        }
        for r in db.query(Reclamation).all()
    ]

    return {
        "date": day,
        "production_live": production_live,
        "factory_kpis": factory_kpis,
        "machines": machines,
        "alarms": alarms,
        "maintenance": maintenance,
        "reclamations": reclamations,
        "traceability": traceability,
    }

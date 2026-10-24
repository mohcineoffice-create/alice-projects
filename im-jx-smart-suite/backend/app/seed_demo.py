"""
IM JX Smart Suite - seed_demo.py
------------------------------------
Script à part (ne s'exécute PAS automatiquement au démarrage) qui remplit la
base de données avec des données FICTIVES réalistes, uniquement pour montrer
le programme en mode démo/présentation :
    - Machines supplémentaires pour PED / MNT / QUALITY / METROLOGIE / METALLURGIE
    - Rapports de production (SyncEvent) pour toutes les pages/départements
    - Réclamations, Alarmes, Demandes de maintenance + notes
    - Commandes admin, messages de chat

Il n'invente rien pour l'utilisateur : ça reste 100% des comptes/valeurs de
démonstration (mêmes comptes que seed.py : prod_operator/prod123, etc.).

UTILISATION (le backend doit déjà tourner via docker compose) :
    docker compose exec backend python -m app.seed_demo

Le script est sûr à relancer : il ajoute toujours de nouvelles lignes
(horodatées à "aujourd'hui"), il ne supprime ni ne modifie rien d'existant.
Pour repartir de zéro : docker compose down -v puis redémarrer (comme indiqué
dans le README), ensuite relancer ce script.
"""

import random
from datetime import datetime, timedelta

from app.database import (
    SessionLocal, init_db, Machine, SyncEvent, Reclamation, Command,
    ChatMessage, Alarm, MaintenanceRequest, MaintenanceNote,
)

TODAY = datetime.utcnow().strftime("%Y-%m-%d")

# ---------------------------------------------------------------------------
# 1) Machines supplémentaires (seed.py ne couvre que PROD)
# ---------------------------------------------------------------------------
EXTRA_MACHINES = [
    ("M101", "Presse Forge L1", "Forging Press", "PED", "Line 1", "operational"),
    ("M102", "Four Traitement Thermique", "Heat Treatment Furnace", "PED", "Line 1", "operational"),
    ("M103", "Grenailleuse", "Shot Blasting", "PED", "Line 2", "maintenance"),
    ("M201", "Compresseur Air Principal", "Air Compressor", "MNT", "Utilities", "operational"),
    ("M202", "Groupe Electrogène", "Generator", "MNT", "Utilities", "operational"),
    ("M301", "Banc de Contrôle Dureté", "Hardness Tester", "QUALITY", "Lab", "operational"),
    ("M302", "Machine à Mesurer 3D (CMM)", "CMM", "METROLOGIE", "Metrology Room", "operational"),
    ("M401", "Spectromètre Optique", "Optical Spectrometer", "METALLURGIE", "Lab", "operational"),
]

OPERATORS_PROD = ["Karim El Amrani", "Youssef Bensalem", "Hicham Radi"]
OPERATORS_PED = ["Sara Idrissi", "Omar Chraibi"]
TECHS_MNT = ["Rachid Ouazzani", "Anas Benjelloun"]
TECHS_QUALITY = ["Nadia Fassi", "Ilyas Tazi"]
TL_NAMES = {"PROD": "Mehdi Alaoui", "PED": "Fatima Zahra Berrada", "MNT": "Younes Cherkaoui", "QUALITY": "Salma Bennani"}


def seed_machines(db):
    added = 0
    for code, name, mtype, dept, loc, status in EXTRA_MACHINES:
        if not db.query(Machine).filter(Machine.machine_code == code).first():
            db.add(Machine(
                machine_code=code, name=name, machine_type=mtype,
                department=dept, location=loc, status=status,
                serial_number=f"SN-{code}-2024", installation_date="2024-03-15",
                responsible_employee=TL_NAMES.get(dept, "N/A"),
            ))
            added += 1
    db.commit()
    print(f"[machines] {added} machine(s) de démo ajoutée(s)")


def _hours_block(target=60, variance=8):
    """Construit un bloc HOURS_DATA plausible pour une ligne PROD (format attendu par analytics.py)."""
    slots = ["06-07", "07-08", "08-09", "09-10", "10-11", "11-12"]
    parts = []
    for h in slots:
        tgt = target
        act = max(0, tgt - random.randint(0, variance))
        rej = random.randint(0, 3)
        q = "".join(random.choice("GGGGGGGGGR") for _ in range(25))  # majorité "Good"
        parts.append(f"[Hour:{h}|Tgt:{tgt}|Act:{act}|Rej:{rej}|Loss:NONE|Q:{q}]")
    return ",".join(parts)


def seed_sync_events(db):
    rows = []
    # --- PROD (format compris par analytics.get_production_live) ---
    lines = [("1", "CNC-TURNING", "M008"), ("2", "GROOVING", "M010"), ("3", "SPLINE-ROLLING", "M042")]
    for i, (line_no, process, machine) in enumerate(lines):
        op = OPERATORS_PROD[i % len(OPERATORS_PROD)]
        payload = ";".join([
            "IM_PROD_NODE", TODAY, op, f"L{line_no}-{process}-{machine}",
            "SHIFT:8H", "TEAM:A",
            f"TOOL:Punch-0{i+1};CORNER:1;LIFE:{random.randint(500,4000)}",
            f"TPM_LOCK:07:1[Visual:OK,Lubrification:OK,Vibration:OK]",
            f"HOURS_DATA:{_hours_block(target=random.choice([50, 60, 70]))}",
            f"META_FILL:{op}@06:05",
        ])
        rows.append(SyncEvent(department="PROD", page="PRODUCTION", operator=op, raw_payload=payload))

    # --- PED ---
    for i, op in enumerate(OPERATORS_PED):
        payload = ";".join([
            "IM_PED_OPER_NODE", TODAY, op, "SHIFT:A",
            f"FORGE_BATCH:HT-{2400+i};TEMP_C:{random.randint(1050,1150)};PRESS_TONS:{random.randint(180,260)}",
            "QC_VISUAL:OK",
        ])
        rows.append(SyncEvent(department="PED", page="OPER", operator=op, raw_payload=payload))
    payload = ";".join([
        "IM_PED_TL_NODE", TODAY, TL_NAMES["PED"], "SHIFT_A",
        f"GRID_STATUS:L1:OK,L2:OK,L3:DELAY(grenailleuse en maintenance)",
    ])
    rows.append(SyncEvent(department="PED", page="TL", operator=TL_NAMES["PED"], raw_payload=payload))

    # --- MNT ---
    for i, tech in enumerate(TECHS_MNT):
        payload = ";".join([
            "IM_MAINT_OPER_TECH_NODE", TODAY, tech, "SHIFT:A",
            f"INTERVENTION:M{200+i};TYPE:Preventive;DURATION_MIN:{random.randint(20,90)};RESULT:OK",
        ])
        rows.append(SyncEvent(department="MNT", page="OPER-TECH", operator=tech, raw_payload=payload))
    payload = ";".join([
        "IM_MAINT_TL_NODE", TODAY, TL_NAMES["MNT"], "SHIFT_A",
        "GRID_STATUS:Compresseur:OK,Groupe_Electrogene:OK,Grenailleuse_PED:EN_COURS",
    ])
    rows.append(SyncEvent(department="MNT", page="TL", operator=TL_NAMES["MNT"], raw_payload=payload))

    # --- QUALITY ---
    for i, tech in enumerate(TECHS_QUALITY):
        payload = ";".join([
            "IM_QUALITY_OPER_NODE" if i == 0 else "IM_QUALITY_TECH_NODE", TODAY, tech, "SHIFT:A",
            f"CONTROL:Dimension;LOT:LOT-{5000+i};RESULT:CONFORME;SAMPLE:12/12",
        ])
        rows.append(SyncEvent(department="QUALITY", page="OPER" if i == 0 else "TECH", operator=tech, raw_payload=payload))
    payload = ";".join([
        "IM_QUALITY_TL_NODE", TODAY, TL_NAMES["QUALITY"], "SHIFT_A",
        "LINES:L1:OK,L2:OK,L3:SOUS_SURVEILLANCE",
        "DECISIONS:Renforcement controle visuel L3 suite derive mineure | RAS sur L1/L2",
    ])
    rows.append(SyncEvent(department="QUALITY", page="TL", operator=TL_NAMES["QUALITY"], raw_payload=payload))

    # --- METROLOGIE / METALLURGIE ---
    rows.append(SyncEvent(
        department="METROLOGIE", page="TECH", operator="Amine Sabir",
        raw_payload=f"IM_METRO_TECH_NODE;{TODAY};Amine Sabir;CMM_CHECK:Piece-CTRL-118;RESULT:CONFORME;TOLERANCE:+/-0.01mm",
    ))
    rows.append(SyncEvent(
        department="METALLURGIE", page="OPER", operator="Zineb Kabbaj",
        raw_payload=f"IM_METALL_OPER_NODE;{TODAY};Zineb Kabbaj;ANALYSE:Composition_Chimique;LOT:HT-2401;RESULT:CONFORME",
    ))

    for r in rows:
        db.add(r)
    db.commit()
    print(f"[sync_events] {len(rows)} rapport(s) de démo ajouté(s)")


def seed_reclamations(db):
    demo = [
        ("PROD", "Karim El Amrani", "MNT", "Machine", "Bruit anormal sur Tour CNC L1-A, arrêt préventif demandé"),
        ("QUALITY", "Nadia Fassi", "PROD", "Dimension", "Dérive dimensionnelle détectée sur lot LOT-5002, contrôle renforcé en cours"),
        ("PED", "Sara Idrissi", "MNT", "Machine", "Grenailleuse en panne, batch en attente"),
    ]
    rows = []
    for dept, op, target, category, text in demo:
        raw = f"IM_RECLAM;{TODAY};{datetime.utcnow().strftime('%H:%M')};{op};{dept};{target};{category};{text}"
        rows.append(Reclamation(department=dept, operator=op, target=target, category=category,
                                 raw_payload=raw, status=random.choice(["open", "acknowledged"])))
    for r in rows:
        db.add(r)
    db.commit()
    print(f"[reclamations] {len(rows)} réclamation(s) de démo ajoutée(s)")


def seed_alarms_and_maintenance(db):
    alarms_data = [
        ("M103", "warning", "Vibration anormale détectée sur la grenailleuse", "PED", "Rachid Ouazzani", "in_progress"),
        ("M008", "critical", "Surchauffe broche détectée", "PROD", "Anas Benjelloun", "open"),
        ("M035", "info", "Maintenance préventive à planifier (500h atteintes)", "PROD", None, "resolved"),
    ]
    alarms = []
    for machine_code, level, desc, dept, tech, status in alarms_data:
        machine = db.query(Machine).filter(Machine.machine_code == machine_code).first()
        if not machine:
            continue
        a = Alarm(
            machine_id=machine.id, machine_code=machine_code, level=level, description=desc,
            created_by=TL_NAMES.get(dept, "Team Leader"), department=dept,
            assigned_technician=tech, status=status,
            resolved_at=datetime.utcnow() - timedelta(hours=2) if status == "resolved" else None,
        )
        db.add(a)
        alarms.append(a)
    db.commit()
    print(f"[alarms] {len(alarms)} alarme(s) de démo ajoutée(s)")

    # Une demande de maintenance liée à l'alarme critique + une note de suivi
    critical = next((a for a in alarms if a.level == "critical"), None)
    if critical:
        mr = MaintenanceRequest(
            machine_id=critical.machine_id, machine_code=critical.machine_code, alarm_id=critical.id,
            requested_by=TL_NAMES["PROD"], assigned_technician="Anas Benjelloun",
            description="Vérifier refroidissement broche + remplacer roulement si nécessaire",
            status="in_progress", started_at=datetime.utcnow() - timedelta(hours=1),
        )
        db.add(mr)
        db.commit()
        db.add(MaintenanceNote(
            maintenance_request_id=mr.id, author="Anas Benjelloun",
            note="Diagnostic en cours : roulement broche à changer, pièce commandée (ETA 2h).",
        ))
        db.commit()
        print("[maintenance] 1 demande de maintenance + 1 note ajoutées")


def seed_commands_and_chat(db):
    db.add(Command(
        sender="Administrateur", target_department="ALL", target_role="ALL",
        message="Réunion sécurité à 14h - présence obligatoire de tous les Team Leaders.",
        priority="normal",
    ))
    db.add(Command(
        sender="Administrateur", target_department="PROD", target_role="ALL",
        message="Urgent : ralentir cadence Line 1 suite alerte surchauffe broche M008.",
        priority="urgent",
    ))
    db.commit()

    chat_msgs = [
        ("GENERAL", "admin", "Administrateur", "Bienvenue sur IM JX Smart Suite - canal général."),
        ("PROD", "prod_tl", TL_NAMES["PROD"], "Ligne 1 sous surveillance suite alerte M008."),
        ("MNT", "mnt_tech", TECHS_MNT[0], "J'interviens sur M008 dans 10 min."),
    ]
    for channel, username, name, text in chat_msgs:
        db.add(ChatMessage(channel=channel, sender_username=username, sender_name=name,
                            content_type="text", text=text))
    db.commit()
    print("[commands/chat] messages de démo ajoutés")


def run_demo_seed():
    init_db()
    db = SessionLocal()
    try:
        seed_machines(db)
        seed_sync_events(db)
        seed_reclamations(db)
        seed_alarms_and_maintenance(db)
        seed_commands_and_chat(db)
        print("\n✅ Données de démonstration ajoutées avec succès.")
        print("   Connecte-toi avec un des comptes de seed.py (ex: admin/admin123,")
        print("   prod_operator/prod123, prod_tl/prod123, mnt_tech/mnt123, ...)")
    finally:
        db.close()


if __name__ == "__main__":
    run_demo_seed()

import os
import secrets
import logging

from .database import SessionLocal, User, Machine
from .auth import hash_new_password

logger = logging.getLogger("imjx")

# Par défaut, en production, on NE crée QUE le compte admin (mot de passe aléatoire,
# affiché une seule fois dans les logs). Pour retrouver le jeu de comptes de démo complet
# (utile en développement local), passer ENABLE_DEMO_SEED=true dans l'environnement.
ENABLE_DEMO_SEED = os.environ.get("ENABLE_DEMO_SEED", "false").strip().lower() == "true"

DEFAULT_USERS = [
    # username,        password,   full_name,             role,          department
    ("admin",          "admin123", "Administrateur",       "admin",       "ALL"),
    ("prod_operator",  "prod123",  "Opérateur Production",  "operator",    "PROD"),
    ("prod_tech",      "prod123",  "Technicien Production", "technician",  "PROD"),
    ("prod_tl",        "prod123",  "Team Leader Production","tl",          "PROD"),
    ("ped_operator",   "ped123",   "Opérateur PED",          "operator",    "PED"),
    ("ped_tech",       "ped123",   "Technicien PED",         "technician",  "PED"),
    ("ped_tl",         "ped123",   "Team Leader PED",        "tl",          "PED"),
    ("mnt_tech",       "mnt123",   "Technicien Maintenance", "technician",  "MNT"),
    ("mnt_tl",         "mnt123",   "Team Leader Maintenance","tl",          "MNT"),
    ("quality_operator","qual123", "Contrôleur Qualité",     "operator",    "QUALITY"),
    ("quality_tech",   "qual123",  "Technicien Instruments & Gauges", "technician",  "QUALITY"),
    ("quality_tl",     "qual123",  "Team Leader Qualité",    "tl",          "QUALITY"),
    ("metro_tech",     "metro123", "Technicien Métrologie CMM", "technician", "METROLOGIE"),
    ("metall_operator","metall123","Opérateur Labo Métallurgie", "operator", "METALLURGIE"),
]

DEFAULT_MACHINES = [
    # machine_code, name,                          machine_type,                department,    location,             serial_number,     installation_date, responsible_employee
    # --- Production : Ligne 1 & Ligne 2 (fabrication d'engrenages) ---
    ("M035", "Centreuse-Facing L1",       "Centring & Facing",          "PROD", "Ligne 1",            "CF-2019-1035", "2019-03-12", "Karim B."),
    ("M036", "Centreuse-Facing L2",       "Centring & Facing",          "PROD", "Ligne 2",            "CF-2020-1036", "2020-06-04", "Karim B."),
    ("M008", "Tour CNC L1-A",             "CNC Turning",                 "PROD", "Ligne 1",            "TCN-2018-0088", "2018-09-20", "Yassine T."),
    ("M009", "Tour CNC L1-B",             "CNC Turning",                 "PROD", "Ligne 1",            "TCN-2018-0089", "2018-09-20", "Yassine T."),
    ("M018", "Tour CNC L2-A",             "CNC Turning",                 "PROD", "Ligne 2",            "TCN-2021-0118", "2021-02-15", "Sara M."),
    ("M010", "Rainureuse L1",             "Grooving",                    "PROD", "Ligne 1",            "RN-2018-0110", "2018-11-02", "Yassine T."),
    ("M011", "Rainureuse L2",             "Grooving",                    "PROD", "Ligne 2",            "RN-2021-0211", "2021-02-15", "Sara M."),
    ("M042", "Rouleuse Cannelures L1",    "Spline Rolling",              "PROD", "Ligne 1",            "SR-2019-0142", "2019-07-08", "Karim B."),
    ("M043", "Rouleuse Cannelures L2",    "Spline Rolling",              "PROD", "Ligne 2",            "SR-2021-0243", "2021-03-01", "Sara M."),
    ("M050", "Tailleuse d'Engrenages L1", "Gear Hobbing",                "PROD", "Ligne 1",            "GH-2017-0150", "2017-05-19", "Yassine T."),
    ("M051", "Tailleuse d'Engrenages L2", "Gear Hobbing",                "PROD", "Ligne 2",            "GH-2021-0251", "2021-04-10", "Sara M."),
    ("M060", "Rectifieuse de Dentures L1","Gear Grinding",               "PROD", "Ligne 1",            "GG-2020-0160", "2020-01-22", "Karim B."),
    ("M070", "Chanfreineuse L1",          "Chamfering",                  "PROD", "Ligne 1",            "CH-2018-0170", "2018-10-30", "Yassine T."),

    # --- PED : presses & finition ---
    ("M101", "Presse Hydraulique P1",     "Hydraulic Press",             "PED",  "Atelier PED",        "PH-2016-0201", "2016-04-11", "Nabil E."),
    ("M102", "Presse Hydraulique P2",     "Hydraulic Press",             "PED",  "Atelier PED",        "PH-2019-0202", "2019-08-25", "Nabil E."),
    ("M110", "Ligne d'Ébavurage",         "Deburring Line",              "PED",  "Atelier PED",        "DB-2020-0310", "2020-05-14", "Imane R."),
    ("M120", "Laveuse Industrielle",      "Parts Washer",                "PED",  "Atelier PED",        "PW-2018-0420", "2018-12-03", "Imane R."),

    # --- Maintenance : équipements d'atelier ---
    ("M200", "Poste à Souder Atelier",    "Welding Station",             "MNT",  "Atelier Maintenance","WS-2017-0500", "2017-02-28", "Hamza L."),
    ("M201", "Compresseur Central",       "Air Compressor",              "MNT",  "Local Technique",    "AC-2015-0601", "2015-06-17", "Hamza L."),

    # --- Qualité : contrôle et inspection ---
    ("M301", "Duromètre Rockwell",        "Hardness Tester",             "QUALITY", "Labo Qualité",    "HR-2019-0701", "2019-01-09", "Fatima Z."),
    ("M302", "Rugosimètre",               "Surface Roughness Tester",    "QUALITY", "Labo Qualité",    "SR-2020-0801", "2020-03-16", "Fatima Z."),
    ("M303", "Banc de Contrôle Denture",  "Gear Inspection Bench",       "QUALITY", "Labo Qualité",    "GI-2021-0901", "2021-05-27", "Othmane K."),

    # --- Métrologie ---
    ("M401", "MMT Tridimensionnelle",     "Coordinate Measuring Machine","METROLOGIE", "Salle Métrologie", "CMM-2018-1001", "2018-07-05", "Othmane K."),
    ("M402", "Projecteur de Profil",      "Optical Profile Projector",   "METROLOGIE", "Salle Métrologie", "OP-2019-1101", "2019-09-13", "Othmane K."),

    # --- Métallurgie ---
    ("M501", "Four de Trempe",            "Heat Treatment Furnace",      "METALLURGIE", "Labo Métallurgie", "FT-2016-1201", "2016-10-21", "Salma N."),
    ("M502", "Spectromètre à Émission",   "Emission Spectrometer",       "METALLURGIE", "Labo Métallurgie", "SP-2019-1301", "2019-04-18", "Salma N."),
    ("M503", "Banc de Traction",          "Tensile Testing Machine",     "METALLURGIE", "Labo Métallurgie", "TT-2020-1401", "2020-11-09", "Salma N."),
]


def _add_user(db, username, password, full_name, role, department):
    password_hash, salt = hash_new_password(password)
    db.add(User(
        username=username,
        password_hash=password_hash,
        password_salt=salt,
        full_name=full_name,
        role=role,
        department=department,
    ))


def run_seed():
    db = SessionLocal()
    try:
        if db.query(User).count() == 0:
            if ENABLE_DEMO_SEED:
                for username, password, full_name, role, department in DEFAULT_USERS:
                    _add_user(db, username, password, full_name, role, department)
                db.commit()
                logger.warning(
                    "ENABLE_DEMO_SEED=true : comptes de démonstration créés avec des mots de "
                    "passe faibles connus. NE JAMAIS utiliser cette option en production."
                )
            else:
                admin_password = secrets.token_urlsafe(12)
                _add_user(db, "admin", admin_password, "Administrateur", "admin", "ALL")
                db.commit()
                logger.warning(
                    "=== Premier démarrage : compte admin créé ===\n"
                    "    username: admin\n"
                    "    password: %s\n"
                    "Notez ce mot de passe MAINTENANT, il ne sera plus jamais affiché. "
                    "Changez-le ensuite depuis la page Administration.",
                    admin_password,
                )

        if db.query(Machine).count() == 0:
            for machine_code, name, machine_type, department, location, serial_number, installation_date, responsible_employee in DEFAULT_MACHINES:
                db.add(Machine(
                    machine_code=machine_code, name=name, machine_type=machine_type,
                    department=department, location=location, status="operational",
                    serial_number=serial_number, installation_date=installation_date,
                    responsible_employee=responsible_employee,
                ))
            db.commit()
    finally:
        db.close()

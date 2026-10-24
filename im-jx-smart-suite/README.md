# IM JX Smart Suite

**Plateforme de gestion d'usine en temps réel** — remplace les tableurs Excel et les
tableaux blancs papier par un système unifié : saisie terrain, alarmes machine,
maintenance, qualité et supervision, le tout connecté en direct (WebSocket) sur un
seul backend Docker.

Conçu à l'origine pour une usine de pièces mécaniques (forge, usinage, traitement
thermique), avec 6 départements : **PROD · PED · MNT · QUALITY · METROLOGIE ·
METALLURGIE**, plus un poste **Admin** transverse.

---

## ✨ Fonctionnalités

- **Saisie terrain par poste et par rôle** — 13 formulaires dédiés (Opérateur /
  Technicien / Team Leader selon département), auto-sauvegarde en direct, aucune
  installation nécessaire (page web ouverte depuis n'importe quel navigateur).
- **Traçabilité QR/Code-barres** — chaque machine a un QR code imprimable ; un scan
  ouvre sa fiche complète (état, alarmes, historique de maintenance).
- **Alarmes → Maintenance en un tap** — un opérateur signale un problème machine
  (critique / avertissement / info) ; l'alerte arrive instantanément sur le poste
  admin et maintenance via WebSocket, sans rafraîchir la page.
- **Command Center admin** — KPIs usine en direct (cible vs réel, rejets, machines
  actives/en alarme, maintenance ouverte), monitoring machine par machine,
  traçabilité complète et filtrable, exports Excel / Word / PDF.
- **Messagerie interne** — chat façon WhatsApp intégré à chaque page (channels par
  département + général, images/vidéos/documents, réponses, recherche).
- **Notifications & commandes** — l'admin peut diffuser une instruction ciblée
  (département/rôle/priorité) qui apparaît instantanément sur les postes concernés.
- **Sécurité de base solide** — mots de passe hashés + salés, JWT, CORS/CSP,
  rate-limiting sur le login, `SECRET_KEY` obligatoire (pas de valeur par défaut).
- **Console développeur** (`/dev-console.html`, admin uniquement) — supervision
  temps réel du système, catalogue des alarmes, flux de données.
- **Zone d'extension pour scripts d'analyse** — ajouter un nouveau rapport/calcul
  sans toucher au reste du code (voir `backend/app/analysis_scripts/`).

## 🧱 Stack technique

| Composant | Choix |
|---|---|
| Backend | Python · FastAPI · WebSocket |
| Base de données | SQLite (mode WAL) + migrations Alembic |
| Frontend | HTML/JS/Tailwind — pages statiques, aucun framework à builder |
| Auth | JWT + hash de mot de passe salé par utilisateur |
| Déploiement | Docker / Docker Compose — un seul conteneur (API + statique) |
| Export | Excel (xlsx), Word (docx), PDF |

## 🚀 Démarrage rapide

```bash
git clone <url-du-repo>
cd im-jx-smart-suite

cp .env.example .env
openssl rand -hex 32          # copier le résultat dans .env → SECRET_KEY=...

docker compose up --build
```

Ouvrir **http://localhost:8080/login.html**

Au tout premier démarrage, un compte **admin** est créé automatiquement avec un mot
de passe généré aléatoirement, affiché une seule fois dans les logs :

```bash
docker compose logs im-jx | grep -A4 "Premier démarrage"
```

Pour un jeu complet de comptes de démonstration (dev local uniquement — jamais en
production), définir `ENABLE_DEMO_SEED=true` dans `.env` avant de démarrer. Le détail
des comptes est dans [CHANGELOG.md](./CHANGELOG.md#comptes-par-défaut).

## 🏭 Départements & rôles

| Département | Opérateur | Technicien | Team Leader |
|---|:---:|:---:|:---:|
| PROD | ✅ | ✅ | ✅ |
| PED | ✅ | ✅ | ✅ |
| MNT | — | ✅ (partagé) | ✅ |
| QUALITY | ✅ | ✅ | ✅ |
| METROLOGIE | — | ✅ | — |
| METALLURGIE | ✅ | — | — |
| **Admin** | — | — | accès transverse à tout |

## 📁 Structure du projet

```
im-jx-smart-suite/
├── docker-compose.yml
├── backend/
│   ├── Dockerfile
│   ├── requirements.txt
│   ├── alembic/                  # migrations de schéma
│   └── app/
│       ├── main.py               # routes API + WebSocket + sert le frontend
│       ├── database.py           # modèles SQLAlchemy (SQLite)
│       ├── auth.py               # JWT + hash mot de passe
│       ├── ws_manager.py         # diffusion temps réel
│       ├── analytics.py          # KPIs + get_all_datasets()
│       ├── analysis_scripts/     # scripts d'analyse enfichables
│       └── seed.py / seed_demo.py
└── app/                          # frontend statique
    ├── login.html · admin.html · dev-console.html
    ├── scan.html · machine.html · machines-admin.html
    ├── shared/im-jx-client.js    # auth, sync, notifications, chat (partagé)
    └── pages/{PROD,PED,MNT,QUALITY,METROLOGIE,METALLURGIE}/*.html
```

## 🔒 Sécurité

- `SECRET_KEY` obligatoire au démarrage (pas de valeur par défaut en dur).
- Mots de passe hashés + salés individuellement par utilisateur.
- CORS restreint via `ALLOWED_ORIGINS`, en-têtes CSP, rate-limiting sur `/api/auth/login`.
- Comptes de démonstration désactivés par défaut (`ENABLE_DEMO_SEED=false`).
- Sauvegarde de la base : `docker compose exec im-jx sh scripts/backup_db.sh`.

Voir [CHANGELOG.md](./CHANGELOG.md) pour le détail de chaque évolution technique
(audit sécurité, console développeur, migrations Alembic, etc.).

## 📌 Statut

Projet fonctionnel et utilisé comme base de démonstration. Départements **RH** et
**Administration** ainsi que certaines vues analytiques avancées (perf par process,
graphiques hebdo/mensuels) restent à intégrer — voir la section *Limites actuelles*
du [CHANGELOG](./CHANGELOG.md).

Vision d'ensemble du projet (contexte, objectifs) : [docs/VISION.md](./docs/VISION.md).

## 📄 Licence

Projet propriétaire — tous droits réservés. Pas de licence open-source associée pour
le moment ; contacter l'auteur pour toute utilisation, adaptation ou déploiement chez
un tiers.

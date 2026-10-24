# Changelog — IM JX Smart Suite

Historique détaillé des évolutions techniques du projet (à usage interne / développeurs).

Unification des pages HTML (PROD / PED / MNT / QUALITY / METROLOGIE / METALLURGIE ×
Operator / Technician / Team Leader) en une seule application, avec un vrai backend
Docker (remplace Google Sheets) et un dashboard admin temps réel type Kibana.

## Démarrage

1. Copier `.env.example` vers `.env` à la racine du projet et générer une clé secrète :
   ```bash
   cp .env.example .env
   # Remplacer SECRET_KEY= par une clé générée avec :
   openssl rand -hex 32
   ```
   L'application refuse de démarrer sans `SECRET_KEY` défini (voir `backend/app/auth.py`).

2. Lancer :
   ```bash
   cd im-jx-smart-suite
   docker compose up --build
   ```

Puis ouvrir **http://localhost:8080/login.html**

## Comptes par défaut

Par défaut (`ENABLE_DEMO_SEED=false`), seul un compte **admin** est créé au tout premier
démarrage, avec un **mot de passe généré aléatoirement** affiché une seule fois dans les
logs :

```bash
docker compose logs im-jx | grep -A4 "Premier démarrage"
```

Notez ce mot de passe immédiatement — il ne sera plus jamais affiché. Changez-le ensuite
depuis `admin.html`.

Pour retrouver le jeu de comptes de démonstration complet (développement local
uniquement — mots de passe faibles et connus, **jamais en production**), définir
`ENABLE_DEMO_SEED=true` dans `.env` :

| Département | Rôle        | Identifiant     | Mot de passe |
|---|---|---|---|
| — | Admin | `admin` | `admin123` |
| PROD | Opérateur | `prod_operator` | `prod123` |
| PROD | Technicien | `prod_tech` | `prod123` |
| PROD | Team Leader | `prod_tl` | `prod123` |
| PED | Opérateur | `ped_operator` | `ped123` |
| PED | Technicien | `ped_tech` | `ped123` |
| PED | Team Leader | `ped_tl` | `ped123` |
| MNT | Technicien | `mnt_tech` | `mnt123` |
| MNT | Team Leader | `mnt_tl` | `mnt123` |
| QUALITY | Contrôleur | `quality_operator` | `qual123` |
| QUALITY | Technicien Instruments & Gauges | `quality_tech` | `qual123` |
| QUALITY | Team Leader | `quality_tl` | `qual123` |
| METROLOGIE | Technicien CMM | `metro_tech` | `metro123` |
| METALLURGIE | Opérateur Labo | `metall_operator` | `metall123` |

Après connexion, chaque utilisateur est redirigé automatiquement vers **sa** page
(selon département + rôle). L'admin est redirigé vers `/admin.html`.

## Console développeur (`/dev-console.html`, admin uniquement)

Accessible depuis un lien en haut du dashboard admin. Trois onglets :
- **Supervision temps réel** : état de la base de données, espace disque, connexions
  WebSocket actives par département, dernière synchronisation reçue par département,
  alarmes critiques ouvertes/escaladées — tout est rafraîchi automatiquement toutes les
  10 secondes (`/api/system/status`).
- **Flux de données** : quelles pages envoient vers quels endpoints, quels événements
  WebSocket sont émis par le serveur et vers qui, et la liste complète des routes HTTP
  réellement enregistrées (générée dynamiquement, toujours à jour) — `/api/system/data-flow`.
- **Registre des alarmes** : toutes les alarmes que le système peut produire (machine
  ET système — base de données, disque, WebSocket, brute force login, upload, format de
  données), avec condition de déclenchement, action automatique, et état réel actuel —
  `/api/system/alarms-catalog`.

## Sauvegarde de la base de données

```bash
docker compose exec im-jx sh scripts/backup_db.sh
```

Crée une copie horodatée dans `/data/backups/` à l'intérieur du volume `im_jx_data`
(cohérente même si l'app tourne, grâce à `sqlite3 .backup`). Pour l'automatiser, planifier
cette commande via cron sur l'hôte (ex: tous les jours à 2h du matin), et copier
régulièrement `/data/backups/` vers un stockage externe (NAS, cloud...) — le volume Docker
seul ne protège pas contre une panne disque de l'hôte.

## Migrations de base de données (Alembic)

Toute évolution du schéma passe maintenant par Alembic — plus jamais de
`docker compose down -v` qui efface les données :

```bash
# Après avoir modifié un modèle dans backend/app/database.py :
docker compose exec im-jx alembic revision --autogenerate -m "description du changement"
docker compose exec im-jx alembic upgrade head
```

Les migrations en attente sont aussi appliquées automatiquement à chaque démarrage du
conteneur (voir `backend/scripts/entrypoint.sh`).

## Variables d'environnement (voir `.env.example`)

| Variable | Obligatoire | Défaut | Description |
|---|---|---|---|
| `SECRET_KEY` | ✅ | — | Signature des tokens JWT. `openssl rand -hex 32` |
| `ALLOWED_ORIGINS` | non | `http://localhost:8080` | Origines CORS autorisées (séparées par des virgules) |
| `ENABLE_DEMO_SEED` | non | `false` | `true` = comptes de démo (dev uniquement) |
| `ALARM_ESCALATION_DELAY_MINUTES` | non | `15` | Délai avant escalade auto d'une alarme critique non traitée |
| `LOG_LEVEL` | non | `INFO` | Niveau de log applicatif |

## Dépendances externes (CDN)

Toutes les librairies JS externes sont vendorisées localement dans `app/shared/vendor/`
(html5-qrcode, qrious, JsBarcode, jspdf) — l'application reste fonctionnelle même sans
accès internet, **sauf** pour le moteur Tailwind CSS (`cdn.tailwindcss.com`), qui n'a pas
pu être vendorisé sans risque de casser le rendu visuel sans possibilité de vérification
visuelle. À faire en priorité dans un futur incrément (avec test visuel de toutes les
pages avant de retirer l'exception correspondante dans le header `Content-Security-Policy`
de `backend/app/main.py`).

## Ce qui a changé par rapport aux fichiers originaux

- Chaque page (`app/pages/.../*.html`) est identique à l'originale, sauf :
  - Ajout d'un contrôle d'accès (redirection vers `/login.html` si non connecté ou
    mauvais poste/département).
  - Tous les anciens appels `fetch(googleScriptUrl, ...)` vers Google Sheets ont été
    remplacés par `IMJX.sync(...)`, qui envoie les données au nouveau backend
    (authentifié par token) et les diffuse en direct au dashboard admin.
  - Les paiements contenant des mots-clés d'alerte (RECLAM, ALARM, DISPATCH, TASK,
    ADMIN, ESCAL, VALIDATION) sont automatiquement routés comme alertes instantanées
    (poussées en direct au département concerné + à l'admin), les autres comme
    logs de routine.
  - Une petite barre en bas à droite affiche l'utilisateur connecté + bouton logout.
  - Les commandes envoyées par l'admin apparaissent en bandeau (toast) en direct sur
    les postes ciblés, via WebSocket.

## Gestion des utilisateurs (depuis `admin.html`)

Une section "👤 Gestion des utilisateurs" en haut du dashboard admin permet de :
- Voir tous les comptes existants.
- En créer un nouveau (nom, identifiant, mot de passe, département, rôle).
- Réinitialiser le mot de passe d'un compte.
- Supprimer un compte (sauf le compte actuellement connecté).

Pas besoin de toucher au code ni de relancer Docker — tout se fait en direct via
l'API (`/api/users`).

## Nouveau : `admin.html` — Command Center

- Flux temps réel de tous les événements (filtrable par département).
- Flux des alarmes/réclamations.
- KPIs (événements live, alarmes ouvertes, dernière commande envoyée).
- Formulaire pour envoyer une commande/instruction ciblée (département + rôle +
  priorité) → diffusée immédiatement aux postes concernés.

## Architecture

```
im-jx-smart-suite/
├── docker-compose.yml       # un seul service : backend + fichiers statiques
├── backend/
│   ├── Dockerfile
│   ├── requirements.txt
│   └── app/
│       ├── main.py          # routes API + WebSocket + sert les fichiers statiques
│       ├── database.py      # SQLite (fichier persistant dans un volume Docker)
│       ├── auth.py          # JWT + hash des mots de passe
│       ├── ws_manager.py    # diffusion temps réel (commandes, alarmes, sync)
│       ├── analytics.py     # calculs + get_all_datasets() (toutes les données brutes)
│       ├── analysis_scripts/  # AJOUTER UN SCRIPT D'ANALYSE ICI (voir section dédiée)
│       │   ├── __init__.py    # registre @register("nom") — aucune autre modif requise
│       │   └── example_analysis.py
│       └── seed.py          # crée les comptes par défaut au premier démarrage
└── app/                     # tout ce qui est servi au navigateur
    ├── login.html
    ├── admin.html
    ├── shared/im-jx-client.js   # auth, sync, notifications live (partagé)
    └── pages/
        ├── PROD/PRODUCTION.html · TECHNIC.html · TL.html
        ├── PED/OPER.html · TECH.html · TL.html
        ├── MNT/OPER-TECH.html · TL.html
        ├── QUALITY/OPER.html · TECH.html · TL.html
        ├── METROLOGIE/TECH.html
        └── METALLURGIE/OPER.html
```

Un seul conteneur Docker (FastAPI + SQLite) sert à la fois l'API, le WebSocket et
les pages HTML — pas besoin de nginx séparé. Les données sont persistées dans un
volume Docker nommé (`im_jx_data`), donc elles survivent aux redémarrages.

## Limites actuelles / suite possible

- Le dashboard admin est un **dashboard maison stylé façon Kibana**, pas un vrai
  Elasticsearch/Kibana (choix fait pour rester léger — dites-le-moi si vous voulez
  passer sur un vrai stack ELK).
- SQLite convient pour un seul site/usine ; si plusieurs sites en parallèle avec
  beaucoup d'écritures simultanées, migrer vers PostgreSQL (comme dans `yaqin-v2`).
- Pas encore de page de gestion des utilisateurs (créer/désactiver un compte se
  fait aujourd'hui directement en base) — à ajouter si besoin.
- `SECRET_KEY` et tous les mots de passe par défaut sont à changer avant toute
  utilisation réelle (variable d'environnement `SECRET_KEY` dans `docker-compose.yml`).

## Historique des changements

- **Nettoyage** : le dossier `im-jx-smart-suite/` était dupliqué (une copie sous
  `PED/im-jx-smart-suite/`, plus ancienne et incomplète — sans gestion des utilisateurs).
  Le doublon a été supprimé ; ce dossier (`im-jx-smart-suite/` à la racine) est
  désormais la seule source de vérité du projet.
- **Ajout du département QUALITY** (les fichiers `QHALITY/OPER.HTML`, `THECN.HTML`,
  `TL.HTML` d'origine étaient vides) : 3 nouvelles pages intégrées à l'app, avec les
  mêmes mécanismes d'auth / sync / alertes temps réel que PROD/PED/MNT :
  - `QUALITY/OPER.html` — Contrôleur qualité : inspection stop-light 5 pièces/heure,
    enregistrement des non-conformités, escalade d'alarmes.
  - `QUALITY/TECH.html` — Technicien métrologie : calibration des instruments,
    contrôle des gauges, génération de QR code de traçabilité (repris du contenu
    de `METROLOGIE/MET.HTML`).
  - `QUALITY/TL.html` — Team Leader qualité : vue d'ensemble PPM par ligne,
    décision blocage/libération de lot, dispatch d'actions correctives, escalade
    à l'administration.
  - Comptes par défaut, routing de connexion (`login.html`) et filtres du
    dashboard admin (`admin.html`) mis à jour en conséquence.

Restent à intégrer dans une prochaine étape : **RH**, **ADMINISTRATION**, ainsi que
la microapp de messagerie interne (chat + partage d'images/vidéos courtes type
WhatsApp) et le système QR Code de réclamation machine.

- **Ajout des départements METROLOGIE et METALLURGIE** (nouvelles interfaces dédiées,
  séparées de QUALITY) :
  - `METROLOGIE/TECH.html` — Inspection dimensionnelle CMM : tableau de
    caractéristiques (nominal / tolérance en µm / mesuré), verdict PASS/FAIL
    automatique par caractéristique et global, écart de circularité (roundness),
    escalade automatique vers QUALITY en cas de FAIL.
  - `METALLURGIE/OPER.html` — Laboratoire métallurgie : dureté HRC (2 points de
    mesure vs plage spec → verdict auto), validation traitement thermique
    (température four, temps de cycle, milieu de trempe, conformité recette),
    rugosité de surface Ra (mesuré vs max autorisé → verdict auto), check-sheet
    d'inspection micro-fissures (ressuage / magnétoscopie / visuel).
  - Comptes par défaut, routing (`login.html`) et dashboard admin (`admin.html`)
    mis à jour.

**Note de sécurité — portail développeur caché :** une demande de "backdoor" root
caché (invisible dans l'UI, sans trace, bypassant les rôles) a été volontairement
**non implémentée** — ce type d'accès dissimulé constitue une faille de sécurité
réelle. À la place, il est recommandé un rôle `superadmin` classique : mêmes
privilèges root, mais visible dans le système de rôles, protégé par une
authentification forte, et journalisé comme tout autre compte (traçabilité en cas
d'audit).

## Chat interne (façon WhatsApp)

Icône flottante 💬 en bas à gauche, présente automatiquement sur **toutes** les
pages authentifiées (injectée dynamiquement par `im-jx-client.js` — aucune page
existante n'a été modifiée).

- **Channels** : `GENERAL` (tout le monde) + un channel par département (`PROD`,
  `PED`, `MNT`, `QUALITY`, `METROLOGIE`, `METALLURGIE`, `ADMINISTRATION`, `RH`).
  Chaque utilisateur ne voit que `GENERAL` + son propre département ; l'admin voit
  tous les channels.
- **Contenu** : texte, emojis, images (compressées côté navigateur avant envoi),
  vidéos courtes (< 30s, vérifié côté client), documents (PDF/Word/Excel/texte).
- **Fonctions** : réponse à un message (reply), suppression de ses propres messages
  (soft delete, admin peut supprimer n'importe quel message), recherche texte par
  channel, horodatage, badge de messages non lus par channel, indicateur
  présence en ligne/hors ligne.
- **Temps réel** : diffusion via le même WebSocket (`/ws`) que le reste de l'app.
- **Stockage média** : fichiers uploadés dans `/data/chat_uploads` (persisté dans
  le même volume Docker que la base SQLite), servis via `/media/...`.
- Nouvelles tables : `chat_messages`. Nouveaux endpoints : `/api/chat/channels`,
  `/api/chat/messages` (GET/POST), `/api/chat/messages/{id}` (DELETE),
  `/api/chat/search`, `/api/chat/presence`, `/api/chat/upload`.

Limite connue : la compression vidéo n'est pas faite (seule la durée est vérifiée
côté client) — à ajouter plus tard si les fichiers deviennent trop lourds.

## Machine ID + QR/Barcode + Scanner + Alarmes + Maintenance

- **`machines-admin.html`** (admin uniquement) : CRUD des machines (code, nom, type,
  département, emplacement, n° série, date d'installation, responsable, statut),
  génération QR Code + Code128 + Code39 par machine, aperçu, téléchargement PNG,
  impression d'étiquettes (individuelle ou en lot via cases à cocher).
- **`scan.html`** (tous rôles) : scan caméra (téléphone/ordinateur, librairie
  html5-qrcode) + entrée manuelle compatible douchette USB (les douchettes émulent
  un clavier + Entrée). Redirige vers `machine.html?code=X`.
- **`machine.html`** (tous rôles) : fiche machine complète, alarmes actives,
  signalement rapide (1 tap, description optionnelle), historique de maintenance.
  Les contrôles de gestion (assigner un technicien, changer le statut, ajouter des
  notes + photo/vidéo) sont visibles uniquement pour MNT et l'admin.
- Nouvelles tables : `machines`, `alarms`, `maintenance_requests`,
  `maintenance_notes`. Nouveaux endpoints : `/api/machines` (CRUD),
  `/api/alarms` (GET/POST/PUT), `/api/maintenance` (GET/POST/PUT) et
  `/api/maintenance/{id}/notes`. Diffusion temps réel via le même WebSocket.
- Remplace la précédente méthode de réclamation par texte libre par un vrai système
  structuré et traçable.

## Command Center (dans `admin.html` — aucune nouvelle page, aucune page existante modifiée à part l'ajout d'onglets)

Un onglet **🏭 Command Center** a été ajouté à côté du flux temps réel existant
(qui reste identique, inchangé) :

- **KPIs usine en direct** : production cible/réelle/rejets/performance %,
  machines actives/en alarme, maintenance ouverte, alertes qualité ouvertes.
- **Production en direct** : calculée en parsant en temps réel les données déjà
  envoyées par les pages PROD (Target/Actual/Reject par créneau horaire) — aucune
  donnée inventée.
- **Monitoring machines** : grille colorée (vert/jaune/rouge/gris) basée sur les
  vraies données `Machine`/`Alarm`.
- **Dashboard Maintenance** : compteurs Ouvert/En cours/Résolu + liste, basés sur
  les vraies interventions.
- **Traçabilité complète** : audit permanent et filtrable (département, recherche
  texte) combinant `sync_events` + `reclamations` + `commands` — rien n'est jamais
  supprimé de ces tables.
- **Export** Excel / Word / PDF pour chaque section (production, machines,
  maintenance, traçabilité) via `/api/export?report=...&format=xlsx|docx|pdf`.

**Limites transparentes (volontairement non implémenté avec de fausses données) :**
Numéro de pièce, cause racine et action corrective ne sont saisis nulle part dans
les pages existantes actuelles → non affichés (plutôt que d'inventer des valeurs).
Performance par process nommé (CNC/Spline/Grooving...) sur plusieurs semaines, vues
Shift A/B/C dédiées et graphiques horaires/hebdomadaires/mensuels sont **faisables**
avec l'historique déjà stocké (`sync_events`) mais n'ont pas été construits dans
cette itération — à faire ensuite si besoin.

## Style unifié + signature "by NAJAR JX"

Aucune page n'a été modifiée une par une : tout passe par le seul fichier partagé
`app/shared/im-jx-client.js`, déjà chargé sur toutes les pages authentifiées.

- Un pied de page fin (`IM JX Smart Suite · by NAJAR JX`) est injecté
  automatiquement en bas de **toutes** les pages, y compris `login.html` (qui ne
  charge pas `im-jx-client.js` — la même signature y a été recopiée à
  l'identique, en dur, pour rester cohérente).
- Pour changer le texte, le style ou ajouter un logo plus tard : une seule fonction
  à éditer, `injectBrandFooter()` dans `im-jx-client.js` (+ le petit bloc
  identique dans `login.html`).

## Centre de notifications — la "place" où arrivent les commandes/alertes

Une cloche 🔔 (en bas à droite, au-dessus de la barre utilisateur) a été ajoutée
sur toutes les pages authentifiées, via le même fichier partagé
`im-jx-client.js` — aucune page individuelle à modifier :

- Historique chargé au login depuis `/api/commands/mine` (déjà existant côté
  backend).
- Mise à jour en direct via le WebSocket existant dès qu'une commande admin ou une
  alarme arrive (en plus du bandeau toast déjà en place, qui reste inchangé).
- Badge de compteur non-lu (mémorisé par utilisateur dans le navigateur).
- Toutes les commandes/alertes reçues restent donc consultables à un seul endroit,
  même après la disparition du toast.

## Zone d'extension pour les scripts d'analyse (données déjà centralisées en variables)

Pour ajouter un calcul/rapport plus tard **sans retoucher au reste du projet** :

1. `analytics.get_all_datasets(db, day)` regroupe déjà toutes les données de la
   base (production du jour, KPIs, machines, alarmes, maintenance, réclamations,
   traçabilité) en un seul dict de variables prêtes à l'emploi — un script n'a
   jamais besoin d'écrire de requête base de données lui-même.
2. Créer un fichier dans `backend/app/analysis_scripts/` (voir
   `example_analysis.py` comme modèle), l'importer dans `__init__.py`, décorer sa
   fonction avec `@register("nom_du_script")`.
3. Le script est alors automatiquement exposé, sans toucher à `main.py` :
   - `GET /api/analytics/custom` — liste les scripts disponibles
   - `GET /api/analytics/custom/{nom}?day=YYYY-MM-DD` — exécute un script
   - `GET /api/analytics/datasets?day=YYYY-MM-DD` — toutes les variables brutes en
     JSON, réutilisables aussi hors de l'app (Excel, Python, notebook, etc.)
4. Un 3e onglet **🧩 Analyse personnalisée** dans `admin.html` (à côté de
   "Flux Temps Réel" et "Command Center") liste automatiquement tous les scripts
   enregistrés dans un menu déroulant, avec un sélecteur de date et un bouton
   "▶️ Exécuter". Le résultat s'affiche automatiquement (cartes pour les valeurs
   simples, tableau pour les listes) — aucune adaptation de l'interface requise
   quand on ajoute un nouveau script, le rendu s'adapte à la forme du JSON retourné.

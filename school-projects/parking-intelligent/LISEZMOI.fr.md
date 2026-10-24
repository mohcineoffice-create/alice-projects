# XLab IC — Pack PFE : Parking intelligent à barrière automatique

Carte : Arduino Uno | Niveau : BTS / DUT / licence / école d'ingénieurs | Langue des documents : français

## Contenu
| Fichier / dossier | Rôle |
|---|---|
| 01_Cahier_des_Charges.docx | Besoin, exigences, planning, critères de validation |
| 02_Rapport_Technique.docx | Étude, conception, câblage, logiciel, essais, code en annexe |
| 03_Presentation_Soutenance.pptx | 11 diapositives avec notes de l'orateur |
| 04_Guide_Soutenance_QR.docx | 25 questions probables du jury et éléments de réponse |
| 05_Guide_Simulation_Proteus.docx | Procédure Proteus (pas de fichier .pdsprj fourni) et Wokwi |
| 06_Simulation_Wokwi/ | Projet Wokwi prêt à coller (sketch, diagram.json, libraries.txt) |
| Code/ | Croquis Arduino commenté |
| Schema/ | Schéma bloc, organigramme, schéma de câblage (PNG + sources) |
| Proteus/HEX/ | Fichier HEX compilé pour Arduino Uno |
| Outils/banc_test_simavr/ | Banc de test virtuel facultatif (Linux) |

## Ce qui a été vérifié par XLab IC
- Le code se compile sans erreur pour l'Arduino Uno (environ 6,2 Ko de flash, 291 octets de RAM).
- Le projet Wokwi s'ouvre, démarre et affiche « Places : 4/4 » ; le scénario d'entrée fait passer à 3/4 avec mouvement du servomoteur.
- Un banc de test virtuel (simavr, dossier Outils/) exécute le vrai programme compilé avec des capteurs, un écran, un servomoteur, des LED et un buzzer simulés : 12 vérifications sur 12 réussies (démarrage, passage rapide, entrée, fermeture temporisée, sortie, parking complet, sortie quand complet).

## Ce qui n'a PAS été vérifié
- Aucun essai sur matériel réel.
- Pas de test sous Proteus (le guide est rédigé, les noms de composants sont à vérifier dans votre version).
- Les modèles du banc de test sont simplifiés (capteur idéal, délais de l'écran non vérifiés).
- Les prix de la nomenclature sont indicatifs.

## Avant la soutenance
- Complétez les champs vides des pages de garde et de la première diapositive.
- Faites vos propres essais et remplacez les « résultats attendus » par vos mesures.
- Comprenez le code et sachez expliquer chaque choix : le jury posera des questions.

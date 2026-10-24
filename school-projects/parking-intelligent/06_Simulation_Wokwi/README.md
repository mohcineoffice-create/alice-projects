# Simulation Wokwi — Parking intelligent

Fichiers : `sketch.ino`, `diagram.json`, `libraries.txt`.

1. Ouvrez https://wokwi.com/projects/new/arduino-uno
2. Collez le contenu de `sketch.ino` dans l'onglet `sketch.ino`.
3. Collez le contenu de `diagram.json` dans l'onglet `diagram.json`.
4. Si Wokwi signale une bibliothèque manquante (Servo, LiquidCrystal), cliquez sur « Install » : le fichier `libraries.txt` est créé.
5. Lancez la simulation (bouton vert). L'écran affiche « Places : 4/4 » et « BIENVENUE ».
6. Cliquez sur un capteur HC-SR04 et réglez « Distance » sous 15 cm pour simuler un véhicule, puis au-dessus de 25 cm.

État de vérification (XLab IC) : démarrage et scénario d'entrée observés dans Wokwi. Les scénarios de sortie, de parking complet et de fermeture temporisée n'ont pas été exécutés dans le simulateur : à tester par l'étudiant.

Le croquis est identique à `Code/parking_intelligent/parking_intelligent.ino`.

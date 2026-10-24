# Banc de test virtuel (simavr) — outil facultatif

Ce banc exécute le **vrai programme compilé** (ELF de l'ATmega328P) dans le simulateur de microcontrôleur simavr, et simule autour de lui :
- deux capteurs HC-SR04 (impulsion TRIG -> écho de durée proportionnelle à la distance) ;
- un écran LCD HD44780 en mode 4 bits (décodage RS/E/D4-D7 ; le texte affiché est reconstitué) ;
- le servomoteur (largeur d'impulsion PWM convertie en angle), les deux LED et le buzzer.

Il déroule un scénario chronométré (démarrage, passage rapide, entrée, fermeture, sortie, parking complet, sortie quand complet) et compare l'état aux résultats attendus. Résultat sur ce projet : 12 vérifications, 12 réussites. Lors d'une première exécution, il a révélé qu'un texte de l'écran dépassait 16 caractères (corrigé dans le code).

## Limites
- Modèles simplifiés : capteur idéal, délais de l'écran non vérifiés, aucun bruit de mesure, pas de rebonds ni de chute de tension du servo.
- Ne remplace pas un essai sur le matériel réel.

## Utilisation (Linux / Debian-Ubuntu)
1. `sudo apt install gcc-avr avr-libc arduino-core-avr libsimavr-dev libelf-dev gcc`
2. Clonez les bibliothèques `Servo` et `LiquidCrystal` (github.com/arduino-libraries) dans un dossier `libs/`.
3. `LIBS_DIR=./libs ./build_uno.sh ../../Code/parking_intelligent/parking_intelligent.ino parking.hex` (crée aussi `parking.elf`)
4. `gcc -O1 -o avrbench avrbench.c -I/usr/include/simavr -lsimavr -lelf -lm`
5. `./avrbench parking.elf`

/*
  XLab IC - Parking intelligent a barriere automatique
  Carte : Arduino Uno (ATmega328P)
  Capteurs : 2 x HC-SR04 (entree, sortie) ; actionneur : servomoteur SG90
  Affichage : LCD 16x2 (LiquidCrystal, mode 4 bits) ; LED verte/rouge ; buzzer
*/
#include <Servo.h>
#include <LiquidCrystal.h>

// ---------- Brochage ----------
const uint8_t PIN_TRIG_ENTREE = 2;
const uint8_t PIN_ECHO_ENTREE = 3;
const uint8_t PIN_TRIG_SORTIE = 4;
const uint8_t PIN_ECHO_SORTIE = 5;
const uint8_t PIN_SERVO       = 6;
const uint8_t PIN_LED_VERTE   = 7;
const uint8_t PIN_LED_ROUGE   = 8;
const uint8_t PIN_BUZZER      = 13;
// LCD : RS, E, D4, D5, D6, D7 sur A0..A5
LiquidCrystal lcd(A0, A1, A2, A3, A4, A5);

// ---------- Parametres ----------
const uint8_t  PLACES_TOTAL        = 4;     // capacite du parking
const uint16_t DISTANCE_DETECTION  = 15;    // cm : vehicule present
const uint16_t DISTANCE_LIBRE      = 25;    // cm : zone degagee
const uint8_t  ANGLE_FERME         = 0;
const uint8_t  ANGLE_OUVERT        = 90;
const uint16_t DELAI_FERMETURE_MS  = 2000;  // zone libre avant fermeture
const uint16_t PERIODE_MESURE_MS   = 100;
const uint8_t  CONFIRMATIONS       = 3;     // mesures consecutives requises

// ---------- Etat ----------
Servo barriere;
uint8_t places = PLACES_TOTAL;
bool barriereOuverte = false;
unsigned long dernierMesure = 0;
unsigned long debutZoneLibre = 0;
uint8_t compteEntree = 0;
uint8_t compteSortie = 0;
bool entreeVerrou = false;   // evite de compter deux fois le meme vehicule
bool sortieVerrou = false;
unsigned long finBip = 0;

// Retourne la distance en cm (999 si pas d'echo)
uint16_t mesurerDistance(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long duree = pulseIn(echo, HIGH, 25000UL);
  if (duree == 0) return 999;
  return (uint16_t)(duree / 58UL);
}

void afficher() {
  lcd.setCursor(0, 0);
  lcd.print(F("Places: "));
  lcd.print(places);
  lcd.print('/');
  lcd.print(PLACES_TOTAL);
  lcd.print(F("    "));
  lcd.setCursor(0, 1);
  lcd.print(places == 0 ? F("   PARKING COMPLET ") : F("   BIENVENUE      "));
  digitalWrite(PIN_LED_VERTE, places > 0 ? HIGH : LOW);
  digitalWrite(PIN_LED_ROUGE, places == 0 ? HIGH : LOW);
}

void ouvrirBarriere() {
  if (!barriereOuverte) {
    barriere.write(ANGLE_OUVERT);
    barriereOuverte = true;
  }
  debutZoneLibre = 0;
}

void fermerBarriere() {
  barriere.write(ANGLE_FERME);
  barriereOuverte = false;
}

void bip(uint16_t ms) {
  digitalWrite(PIN_BUZZER, HIGH);
  finBip = millis() + ms;
}

void setup() {
  pinMode(PIN_TRIG_ENTREE, OUTPUT);
  pinMode(PIN_ECHO_ENTREE, INPUT);
  pinMode(PIN_TRIG_SORTIE, OUTPUT);
  pinMode(PIN_ECHO_SORTIE, INPUT);
  pinMode(PIN_LED_VERTE, OUTPUT);
  pinMode(PIN_LED_ROUGE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  barriere.attach(PIN_SERVO);
  barriere.write(ANGLE_FERME);
  lcd.begin(16, 2);
  lcd.print(F("XLab IC Parking"));
  delay(1200);
  lcd.clear();
  afficher();
}

void loop() {
  unsigned long maintenant = millis();

  if (finBip != 0 && (long)(maintenant - finBip) >= 0) {
    digitalWrite(PIN_BUZZER, LOW);
    finBip = 0;
  }

  if (maintenant - dernierMesure < PERIODE_MESURE_MS) return;
  dernierMesure = maintenant;

  uint16_t dEntree = mesurerDistance(PIN_TRIG_ENTREE, PIN_ECHO_ENTREE);
  delay(30);  // evite l'interference entre les deux capteurs
  uint16_t dSortie = mesurerDistance(PIN_TRIG_SORTIE, PIN_ECHO_SORTIE);

  // ----- Entree -----
  if (dEntree < DISTANCE_DETECTION) {
    if (compteEntree < CONFIRMATIONS) compteEntree++;
  } else {
    compteEntree = 0;
    if (dEntree > DISTANCE_LIBRE) entreeVerrou = false;
  }
  if (compteEntree >= CONFIRMATIONS && !entreeVerrou) {
    entreeVerrou = true;
    if (places > 0) {
      places--;
      ouvrirBarriere();
      bip(100);
      afficher();
    } else {
      bip(600);   // parking complet : barriere reste fermee
    }
  }

  // ----- Sortie -----
  if (dSortie < DISTANCE_DETECTION) {
    if (compteSortie < CONFIRMATIONS) compteSortie++;
  } else {
    compteSortie = 0;
    if (dSortie > DISTANCE_LIBRE) sortieVerrou = false;
  }
  if (compteSortie >= CONFIRMATIONS && !sortieVerrou) {
    sortieVerrou = true;
    if (places < PLACES_TOTAL) places++;
    ouvrirBarriere();
    bip(100);
    afficher();
  }

  // ----- Fermeture de la barriere quand les deux zones sont libres -----
  if (barriereOuverte) {
    bool libre = (dEntree > DISTANCE_LIBRE) && (dSortie > DISTANCE_LIBRE);
    if (libre) {
      if (debutZoneLibre == 0) debutZoneLibre = maintenant;
      if (maintenant - debutZoneLibre >= DELAI_FERMETURE_MS) {
        fermerBarriere();
        debutZoneLibre = 0;
      }
    } else {
      debutZoneLibre = 0;
    }
  }
}

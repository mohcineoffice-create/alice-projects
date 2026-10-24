// Boitier Arduino Uno pour le projet Parking intelligent (XLab IC)
// Unites : mm.  part = "base" | "couvercle" | "ensemble"
part = "ensemble";
paroi = 2.4; jeu = 1.0;
uno_l = 68.6; uno_w = 53.4;          // carte Arduino Uno
int_l = uno_l + 2*jeu + 30;          // place pour les fils a droite
int_w = uno_w + 2*jeu + 12;
int_h = 30;
ext_l = int_l + 2*paroi; ext_w = int_w + 2*paroi; ext_h = int_h + paroi;
module base() {
  difference() {
    cube([ext_l, ext_w, ext_h]);
    translate([paroi, paroi, paroi]) cube([int_l, int_w, int_h + 1]);
    // prise USB-B (cote gauche)
    translate([-1, paroi + 8, paroi + 4]) cube([paroi + 2, 14, 12]);
    // prise jack alimentation
    translate([-1, paroi + 30, paroi + 4]) cube([paroi + 2, 10, 11]);
    // passage des fils capteurs et servo (cote droit)
    translate([ext_l - paroi - 1, ext_w/2 - 15, paroi + 6]) cube([paroi + 2, 30, 12]);
  }
  // 4 plots de fixation de la carte Uno (trous 3.2 mm)
  for (p = [[14.0,2.5],[15.3,50.7],[66.1,7.6],[66.1,35.5]])
    translate([paroi + jeu + p[0], paroi + jeu + 3 + p[1], paroi - 0.5])
      difference() { cylinder(d = 7, h = 5.5, $fn = 40); cylinder(d = 2.6, h = 5.6, $fn = 30); }
  // 4 colonnes de vis du couvercle
  for (x = [paroi + 4, ext_l - paroi - 4], y = [paroi + 4, ext_w - paroi - 4])
    translate([x, y, paroi - 0.5]) difference() {
      cylinder(d = 7, h = int_h - 0.5, $fn = 40);
      translate([0,0,2.5]) cylinder(d = 2.6, h = int_h, $fn = 30);
    }
}
module couvercle() {
  difference() {
    cube([ext_l, ext_w, paroi]);
    // fenetre LCD 16x2 (71 x 24 mm)
    translate([ext_l/2 - 35.5 - 10, ext_w - 14 - 26, -1]) cube([71, 24, paroi + 2]);
    // LED verte, LED rouge (5 mm) et buzzer (12 mm)
    translate([ext_l - 20, 22, -1]) cylinder(d = 5.2, h = paroi + 2, $fn = 30);
    translate([ext_l - 20, 34, -1]) cylinder(d = 5.2, h = paroi + 2, $fn = 30);
    translate([ext_l - 20, 12, -1]) cylinder(d = 2.0, h = paroi + 2, $fn = 20);
    // trous de vis
    for (x = [paroi + 4, ext_l - paroi - 4], y = [paroi + 4, ext_w - paroi - 4])
      translate([x, y, -1]) cylinder(d = 3.4, h = paroi + 2, $fn = 30);
  }
}
if (part == "base") base();
else if (part == "couvercle") couvercle();
else { base(); translate([0, 0, ext_h + 12]) couvercle(); }

// Banc de test virtuel : simule l'ATmega328P (simavr) + capteurs HC-SR04 + LCD HD44780 + servo + LED + buzzer
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim_avr.h"
#include "sim_elf.h"
#include "avr_ioport.h"
#include "sim_irq.h"
#include "sim_cycle_timers.h"

static avr_t *avr;
#define MS(x) ((avr_cycle_count_t)(x) * 16000ULL)
#define US(x) ((avr_cycle_count_t)(x) * 16ULL)

// ---- Capteurs ----
typedef struct { char port; int trig, echo; int dist_cm; avr_cycle_count_t trig_rise; } sensor_t;
static sensor_t sens[2] = { { 'D', 2, 3, 100, 0 }, { 'D', 4, 5, 100, 0 } };
static void set_echo(sensor_t *s, int v) { avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(s->port), s->echo), v); }
static avr_cycle_count_t echo_fall(avr_t *a, avr_cycle_count_t when, void *p) { set_echo((sensor_t *)p, 0); return 0; }
static avr_cycle_count_t echo_rise(avr_t *a, avr_cycle_count_t when, void *p) {
  sensor_t *s = p; set_echo(s, 1);
  avr_cycle_timer_register(a, US(s->dist_cm * 58), echo_fall, s); return 0; }
static void trig_cb(avr_irq_t *irq, uint32_t v, void *p) {
  sensor_t *s = p;
  if (v) { s->trig_rise = avr->cycle; return; }
  if (avr->cycle - s->trig_rise >= US(9) && s->dist_cm < 400)           // impulsion >= 10 us
    avr_cycle_timer_register(avr, US(300), echo_rise, s);              // l'echo demarre ~300 us apres
}

// ---- LCD HD44780 (4 bits) : RS=A0(PC0) E=A1(PC1) D4..D7=A2..A5(PC2..PC5) ----
static int pc[6]; static char lcd[2][17]; static int cur = 0, pulses = 0, have_hi = 0, hi = 0;
static void lcd_byte(int rs, int b) {
  if (rs) { int line = cur >= 0x40, col = cur & 0x3F; if (col < 16) lcd[line][col] = (char)b; cur++; }
  else if (b == 0x01) { memset(lcd, ' ', sizeof lcd); lcd[0][16] = lcd[1][16] = 0; cur = 0; }
  else if (b == 0x02) cur = 0;
  else if (b & 0x80) cur = b & 0x7F;
}
static void pc_cb(avr_irq_t *irq, uint32_t v, void *p) {
  int pin = (int)(long)p; int old = pc[pin]; pc[pin] = v;
  if (pin == 1 && old && !v) {           // front descendant de E
    int nib = pc[2] | (pc[3] << 1) | (pc[4] << 2) | (pc[5] << 3);
    if (++pulses <= 4) return;           // sequence d'initialisation (nibbles seuls)
    if (!have_hi) { hi = nib; have_hi = 1; } else { lcd_byte(pc[0], (hi << 4) | nib); have_hi = 0; }
  }
}

// ---- Servo sur D6, LED D7 / D8, buzzer D13 ----
static avr_cycle_count_t servo_rise = 0; static double angle = -1;
static void servo_cb(avr_irq_t *irq, uint32_t v, void *p) {
  if (v) servo_rise = avr->cycle;
  else if (servo_rise) { double w = (double)(avr->cycle - servo_rise) / 16.0; angle = (w - 544.0) * 180.0 / (2400.0 - 544.0); }
}
static int led_g, led_r, buzzer, buzzer_seen;
static void led_g_cb(avr_irq_t *i, uint32_t v, void *p) { led_g = v; }
static void led_r_cb(avr_irq_t *i, uint32_t v, void *p) { led_r = v; }
static void buz_cb(avr_irq_t *i, uint32_t v, void *p) { buzzer = v; if (v) buzzer_seen = 1; }

// ---- Scenario ----
typedef struct { int t_ms; int set_in, set_out; const char *line1; const char *line2; int open; int g, r; int buz; const char *label; } step_t;
// set_in / set_out : -1 = inchange ; open : -1 = ne pas verifier ; line1/line2 NULL = ne pas verifier ; buz : -1 = ne pas verifier
static step_t steps[] = {
  { 2500,  -1, -1, "Places: 4/4", "BIENVENUE", 0, 1, 0, -1, "T1 demarrage" },
  { 2700, 10, -1, NULL, NULL, -1, -1, -1, -1, "vehicule bref debut" },
  { 2850, 100, -1, NULL, NULL, -1, -1, -1, -1, "vehicule bref fin (150 ms)" },
  { 3300,  -1, -1, "Places: 4/4", NULL, 0, -1, -1, -1, "T7 passage rapide non compte" },
  { 3500, 10, -1, NULL, NULL, -1, -1, -1, -1, "voiture a l'entree" },
  { 4100,  -1, -1, "Places: 3/4", NULL, 1, 1, 0, -1, "T2 entree : 3/4 et barriere ouverte" },
  { 4200, 100, -1, NULL, NULL, -1, -1, -1, -1, "zone entree degagee" },
  { 5200,  -1, -1, NULL, NULL, 1, -1, -1, -1, "T3a barriere encore ouverte apres 1 s" },
  { 7000,  -1, -1, NULL, NULL, 0, -1, -1, -1, "T3b barriere fermee apres 2 s" },
  { 7500, -1, 10, NULL, NULL, -1, -1, -1, -1, "voiture a la sortie" },
  { 8200,  -1, -1, "Places: 4/4", NULL, 1, -1, -1, -1, "T4a sortie : 4/4 et barriere ouverte" },
  { 8300, -1, 100, NULL, NULL, -1, -1, -1, -1, "zone sortie degagee" },
  { 10700, -1, -1, NULL, NULL, 0, -1, -1, -1, "T4b barriere refermee" },
  { 11000, 10, -1, NULL, NULL, -1, -1, -1, -1, "entree 1" }, { 11600, 100, -1, NULL, NULL, -1, -1, -1, -1, "" },
  { 12000, 10, -1, NULL, NULL, -1, -1, -1, -1, "entree 2" }, { 12600, 100, -1, NULL, NULL, -1, -1, -1, -1, "" },
  { 13000, 10, -1, NULL, NULL, -1, -1, -1, -1, "entree 3" }, { 13600, 100, -1, NULL, NULL, -1, -1, -1, -1, "" },
  { 14000, 10, -1, NULL, NULL, -1, -1, -1, -1, "entree 4" }, { 14600, 100, -1, NULL, NULL, -1, -1, -1, -1, "" },
  { 14900,  -1, -1, "Places: 0/4", "PARKING COMPLET", -1, 0, 1, -1, "T5 complet : LCD + LED rouge" },
  { 17000,  -1, -1, NULL, NULL, 0, -1, -1, -1, "barriere fermee avant la 5e voiture" },
  { 17100, 10, -1, NULL, NULL, -1, -1, -1, -1, "5e voiture" },
  { 17500,  -1, -1, NULL, NULL, -1, -1, -1, 1, "T6a bip long en cours" },
  { 17900,  -1, -1, "Places: 0/4", NULL, 0, -1, 1, -1, "T6b reste complet, barriere fermee" },
  { 18000, 100, -1, NULL, NULL, -1, -1, -1, -1, "" },
  { 18500, -1, 10, NULL, NULL, -1, -1, -1, -1, "sortie quand complet" },
  { 19200,  -1, -1, "Places: 1/4", NULL, -1, 1, 0, -1, "T8 sortie : 1/4 et LED verte" },
};

int main(int argc, char **argv) {
  elf_firmware_t f; memset(&f, 0, sizeof f);
  if (elf_read_firmware(argv[1], &f)) { fprintf(stderr, "ELF illisible\n"); return 2; }
  avr = avr_make_mcu_by_name("atmega328p"); avr_init(avr); avr_load_firmware(avr, &f); avr->frequency = 16000000;
  memset(lcd, ' ', sizeof lcd); lcd[0][16] = lcd[1][16] = 0;
  for (int i = 0; i < 2; i++) avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(sens[i].port), sens[i].trig), trig_cb, &sens[i]);
  for (int i = 0; i < 6; i++) avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), i), pc_cb, (void *)(long)i);
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), 6), servo_cb, NULL);
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), 7), led_g_cb, NULL);   // D7
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 0), led_r_cb, NULL);   // D8
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 5), buz_cb, NULL);     // D13
  int pass = 0, fail = 0; size_t n = sizeof steps / sizeof steps[0];
  for (size_t k = 0; k < n; k++) {
    step_t *s = &steps[k];
    while (avr->cycle < MS(s->t_ms)) { int st = avr_run(avr); if (st == cpu_Done || st == cpu_Crashed) { printf("CPU arretee (%d)\n", st); return 3; } }
    if (s->set_in >= 0) sens[0].dist_cm = s->set_in;
    if (s->set_out >= 0) sens[1].dist_cm = s->set_out;
    int checks = (s->line1 || s->line2 || s->open >= 0 || s->g >= 0 || s->r >= 0 || s->buz >= 0);
    if (!checks) continue;
    int ok = 1; char why[256] = "";
    if (s->line1 && !strstr(lcd[0], s->line1)) { ok = 0; strcat(why, " ligne1"); }
    if (s->line2 && !strstr(lcd[1], s->line2)) { ok = 0; strcat(why, " ligne2"); }
    int is_open = angle > 45;
    if (s->open >= 0 && is_open != s->open) { ok = 0; strcat(why, " barriere"); }
    if (s->g >= 0 && led_g != s->g) { ok = 0; strcat(why, " ledV"); }
    if (s->r >= 0 && led_r != s->r) { ok = 0; strcat(why, " ledR"); }
    if (s->buz >= 0 && buzzer != s->buz) { ok = 0; strcat(why, " buzzer"); }
    printf("[%s] t=%5d ms  %-44s | LCD «%.16s»/«%.16s» barriere=%s(%.0f°) LEDv=%d LEDr=%d buz=%d%s\n", ok ? "OK  " : "ECHEC", s->t_ms, s->label, lcd[0], lcd[1], is_open ? "ouverte" : "fermee", angle, led_g, led_r, buzzer, ok ? "" : why);
    ok ? pass++ : fail++;
  }
  printf("\nResultat : %d verifications reussies, %d echecs\n", pass, fail);
  return fail ? 1 : 0;
}

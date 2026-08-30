#pragma once
/* Réglages matériel — Lycée Antonin Artaud, banc SAUTER TVM 5000N230N
 * Cellule CZL301 500 kg + HX711 + ESP32 @ 3,3 V
 */

// ----- Wi-Fi point d'accès (WPA2, mot de passe ≥ 8 caractères) -----
#define WIFI_SSID        "TVM-TRACTION"
#define WIFI_PASS        "traction1"
#define WIFI_CHANNEL     6
#define WIFI_MAX_CLIENTS 4

// ----- Broches HX711 (niveaux 3,3 V uniquement — GPIO ESP32 non 5 V tolerant) -----
#define PIN_HX711_DT     16   // DOUT / DT
#define PIN_HX711_SCK    17   // SCK / CLK
#define PIN_HX711_RATE   33   // RATE du module : HIGH = 80 SPS, LOW = 10 SPS
                              // Si le module n'a pas la pastille RATE, laisser
                              // déconnecté : le firmware restera à 10 SPS.

// ----- Relais d'arrêt (optionnel) -----
// GPIO 26 → transistor (2N2222 / MOSFET canal N) → bobine relais 5 V.
// Ne jamais alimenter la bobine directement par le GPIO.
// Polarité par défaut : ACTIF HAUT.
//   GPIO HIGH → transistor passant → relais collé → contact NF s'ouvre
//   → coupure enable / arrêt moteur du TVM.
//   GPIO LOW  → relais relâché → contact fermé → mouvement autorisé.
// Inverser STOP_ACTIVE_HIGH à 0 si le câblage est actif bas.
#define PIN_STOP           26
#define STOP_ACTIVE_HIGH   1

#define PIN_LED            2  // LED onboard (actif bas sur beaucoup de DevKit)

// ----- Cellule CZL301 -----
#define CELL_CAPACITY_KG   500.0f
#define CELL_MV_PER_V      2.0030f
#define CELL_OHM           350.0f
#define CELL_OVERLOAD_PCT  120.0f   // 600 kg maxi constructeur
#define G_N_PER_KG         9.80665f

// 500 kg × 9,80665 ≈ 4903 N. Plafond logiciel STRICTEMENT sous 4900 N.
#define FORCE_LIMIT_DEFAULT_N  4000.0f
#define FORCE_HARD_CAP_N       4899.0f

// Échelle théorique (counts HX711 / newton) @ 3,3 V, gain 128, 2,003 mV/V.
// Ordre de grandeur seulement — étalonner en séance (CALIB).
#define DEFAULT_SCALE_RAW_PER_N  877.0f

// ----- Détection rupture / fatigue -----
#define BREAK_MIN_FMAX_N     40.0f   // ignorer le bruit de fond
#define BREAK_DROP_RATIO     0.22f   // chute de 22 % sous Fmax → rupture
#define FATIGUE_HYST_N       25.0f   // hystérésis min. pic / creux
#define FATIGUE_MIN_AMP_N    40.0f   // amplitude min. pour compter 1 cycle

// ----- Cadence -----
#define SPS_FAST  80
#define SPS_SLOW  10
#define STABLE_SD_N  1.8f  // si σ(force) à vide > ceci à 80 SPS → bascule 10 SPS

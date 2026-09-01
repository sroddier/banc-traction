#pragma once
/* Réglages matériel — Lycée Antonin Artaud, banc SAUTER TVM 5000N230N
 * Carte EasyEDA : ESP32-WROOM-32 DevKitC-V4 + 2× XW-HX711 @ 3,3 V
 * HX711 U1 : DT1 = GPIO 25, SCK1 = GPIO 33 (toutes cartes ESP32)
 * CZL301 4 fils soudé sur E+ / E- / A+ / A- (pas le connecteur 3 plots).
 */

#define FIRMWARE_VERSION "1.2.4"

// ----- Wi-Fi point d'accès (WPA2, mot de passe ≥ 8 caractères) -----
#define WIFI_SSID        "TVM-TRACTION"
#define WIFI_PASS        "traction1"
#define WIFI_CHANNEL     6
#define WIFI_MAX_CLIENTS 4

// ----- HX711 U1 — même brochage sur toutes les cartes ESP32 -----
// CZL301 4 fils soudé sur E+/E-/A+/A- du HX711 (pas le connecteur 3 plots + 1 kΩ).
#define PIN_HX711_DT     25   // DT1  → GPIO 25
#define PIN_HX711_SCK    33   // SCK1 → GPIO 33
#define HX711_RATE_WIRED 0

// ----- Relais d'arrêt (optionnel, DÉSACTIVÉ) -----
#define RELAY_INSTALLED    0
#define PIN_STOP           4
#define STOP_ACTIVE_HIGH   1

#define PIN_LED            2  // LED onboard (actif bas sur beaucoup de DevKit)

// ----- Cellule / jauge -----
#define CELL_CAPACITY_KG   500.0f
#define CELL_MV_PER_V      2.0030f
#define CELL_OHM           350.0f
#define CELL_OVERLOAD_PCT  120.0f   // 600 kg maxi constructeur — NE PAS utiliser comme consigne
#define G_N_PER_KG         9.80665f

// 500 kg × 9,80665 ≈ 4903 N. Plafond logiciel STRICTEMENT sous 4900 N.
#define FORCE_LIMIT_DEFAULT_N  4000.0f
#define FORCE_HARD_CAP_N       4899.0f

// Échelle théorique CZL301 4 fils @ 3,3 V, gain 128. La jauge U1 (pont R3/R4 1 kΩ)
// n'a PAS cette pente : étalonner en séance, masse ≥ 20 kg.
#define DEFAULT_SCALE_RAW_PER_N  877.0f
#define SCALE_ABS_MIN            0.5f
#define SCALE_ABS_MAX            2000000.0f
#define CAL_MIN_REF_N            0.05f   // ~5 g
#define CAL_MIN_DELTA            20.0f   // |brut − tare| minimum

// ----- Détection rupture / fatigue -----
#define BREAK_MIN_FMAX_N     40.0f
#define BREAK_DROP_RATIO     0.22f
#define FATIGUE_HYST_N       25.0f
#define FATIGUE_MIN_AMP_N    40.0f

// ----- Cadence / fail-safe -----
#define SPS_FAST  80
#define SPS_SLOW  10
#define STABLE_SD_N  1.8f
#define HX_FAIL_MIN_MS  400
#define OVER_CONFIRM    2

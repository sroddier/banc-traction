#pragma once
/* Protocole JSON partagé firmware ESP32 ↔ application web
 * Un objet par message WebSocket, UTF-8, sans BOM.
 * Port HTTP 80 (PWA) · Port WebSocket 81
 *
 * -------------------------------------------------------------------------
 * ESP32 → clients  (flux)
 * -------------------------------------------------------------------------
 * Échantillon, 10 ou 80 Hz :
 * {
 *   "type": "sample",
 *   "t":    123456,     // ms (millis())
 *   "N":    1234.5,     // force en newtons
 *   "kg":   125.87,     // N / 9.80665
 *   "raw":  412345,     // brut HX711 signé 24 bits (0 en simulation)
 *   "evt":  "",         // "" | "tare" | "break" | "peak" | "cycle"
 *                       //      | "limit" | "stop"
 *   "cyc":  0,          // compteur de cycles (fatigue)
 *   "fmin": 0.0,        // N — min du cycle en cours / dernier cycle
 *   "fmax": 0.0         // N — Fmax traction, ou max du cycle fatigue
 * }
 *
 * État / réponse à getStatus, tare, calib, limite, mode :
 * {
 *   "type": "status",
 *   "ok":   true,
 *   "mode": "traction" | "fatigue" | "calib",
 *   "sps":  10 | 80,
 *   "scale": 877.0,     // counts HX711 par newton
 *   "offset": 12345,    // tare (raw)
 *   "limit_N": 4000,
 *   "hard_N":  4899,
 *   "unit": "N",
 *   "cal":  true,
 *   "stop": false,      // relais d'arrêt collé
 *   "msg":  "Prêt"
 * }
 *
 * -------------------------------------------------------------------------
 * Client → ESP32  (commandes)
 * -------------------------------------------------------------------------
 * {"cmd":"tare"}
 * {"cmd":"calibrate","ref_kg":5.0}     // masse connue POSÉE, en kg
 * {"cmd":"calibrate","ref_N":49.03}    // idem, en newtons
 * {"cmd":"setScale","scale":877.0}     // counts / N
 * {"cmd":"setLimit","limit_N":4000}
 * {"cmd":"setMode","mode":"traction"}  // traction | fatigue | calib
 * {"cmd":"start"}
 * {"cmd":"stop"}                       // relais STOP + evt stop
 * {"cmd":"reset"}                      // Fmax, cycles, rupture
 * {"cmd":"setSps","sps":80}            // 10 ou 80
 * {"cmd":"setUnit","unit":"N"}         // N | kg
 * {"cmd":"getStatus"}
 *
 * Pas de champ déplacement / extensomètre : le banc n'en a pas côté ESP32.
 */

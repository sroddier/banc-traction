#pragma once
/* Protocole JSON partagé firmware ESP32 ↔ application web  (v1.1.1)
 * Un objet par message WebSocket, UTF-8, sans BOM.
 * HTTP port 80 (PWA + portail captif) · WebSocket chemin /ws (même port)
 *
 * -------------------------------------------------------------------------
 * ESP32 → clients  (flux)
 * -------------------------------------------------------------------------
 * Échantillon, cadence HX711, diffusé ~50 Hz (immédiat si événement) :
 * {
 *   "type": "sample",
 *   "t":    123456,     // ms (millis())
 *   "N":    1234.5,     // force en newtons (non filtrée)
 *   "kg":   125.87,     // N / 9.80665
 *   "raw":  412345,     // brut HX711 signé 24 bits (0 en simulation)
 *   "evt":  "",         // "" | "tare" | "break" | "peak" | "cycle"
 *                       //      | "limit" | "stop" | "hxfail"
 *   "cyc":  0,          // compteur de cycles (fatigue)
 *   "fmin": 0.0,        // N — min du cycle en cours / dernier cycle
 *   "fmax": 0.0,        // N — Fmax traction, ou max du cycle fatigue
 *   "over": false,      // |F| ≥ limite en ce moment
 *   "hx":   true        // HX711 vivant
 * }
 *
 * État / réponse à getStatus, tare, calib, limite, mode :
 * {
 *   "type": "status",
 *   "ok":   true,
 *   "ver":  "1.1.0",
 *   "mode": "traction" | "fatigue" | "calib",
 *   "sps":  10 | 80,
 *   "scale": 877.0,     // counts HX711 par newton
 *   "offset": 12345,    // tare (raw)
 *   "limit_N": 4000,
 *   "hard_N":  4899,
 *   "unit": "N",
 *   "cal":  true,
 *   "tared": true,
 *   "run":  false,
 *   "relay": false,     // RELAY_INSTALLED
 *   "stop": false,      // relais collé (toujours false si pas de relais)
 *   "over": false,      // |F| ≥ limite maintenant
 *   "seen": false,      // limite déjà dépassée depuis le dernier RAZ
 *   "hx_ok": true,
 *   "raw":  412345,
 *   "msg":  "Prêt"
 * }
 *
 * -------------------------------------------------------------------------
 * Client → ESP32  (commandes)
 * -------------------------------------------------------------------------
 * {"cmd":"tare"}
 * {"cmd":"calibrate","ref_kg":20.0}    // masse connue POSÉE, en kg (≥ 20 kg conseillé)
 * {"cmd":"calibrate","ref_N":196.13}
 * {"cmd":"setScale","scale":877.0}     // counts / N
 * {"cmd":"setLimit","limit_N":4000}
 * {"cmd":"setMode","mode":"traction"}  // traction | fatigue | calib
 * {"cmd":"start"}
 * {"cmd":"stop"}                       // arrête l'enregistrement ; relais si RELAY_INSTALLED
 * {"cmd":"reset"}                      // Fmax, cycles, rupture, drapeau limite
 * {"cmd":"break"}                      // rupture marquée par l'opérateur
 * {"cmd":"setSps","sps":80}            // 10 ou 80 (ignoré si RATE non câblé)
 * {"cmd":"setUnit","unit":"N"}         // N | kg
 * {"cmd":"getStatus"}
 *
 * Pas de champ déplacement / extensomètre : le banc n'en a pas côté ESP32.
 */

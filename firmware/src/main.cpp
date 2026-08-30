/* Banc de traction — ESP32 + HX711
 * Lycée Antonin Artaud, BTS MS / FabLab
 * SAUTER TVM 5000N230N · CZL301 500 kg · pas d'extensomètre
 *
 * Câblage (HX711 et GPIO à 3,3 V UNIQUEMENT) :
 *   CZL301 rouge  → E+   HX711
 *   CZL301 noir   → E-   HX711
 *   CZL301 vert   → A+   HX711
 *   CZL301 blanc  → A-   HX711
 *   CZL301 jaune  → GND  (blindage)
 *   HX711 VCC     → 3V3  ESP32     (pas 5 V : DT/SCK iraient en 5 V)
 *   HX711 GND     → GND  ESP32
 *   HX711 DT      → GPIO 16
 *   HX711 SCK     → GPIO 17
 *   HX711 RATE    → GPIO 33 (optionnel)
 *   Relais STOP   → GPIO 26 via transistor (voir README)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <math.h>

#include "config.h"
#include "protocol.h"

// ---------------------------------------------------------------------------
// HX711 minimal (canal A, gain 128)
// ---------------------------------------------------------------------------
static bool hxReady() {
  return digitalRead(PIN_HX711_DT) == LOW;
}

static long hxReadRaw() {
  // 24 bits, complément à 2, 25e impulsion = gain 128
  while (digitalRead(PIN_HX711_DT) == HIGH) {
    delayMicroseconds(10);
  }
  uint32_t v = 0;
  noInterrupts();
  for (int i = 0; i < 24; i++) {
    digitalWrite(PIN_HX711_SCK, HIGH);
    delayMicroseconds(1);
    v = (v << 1) | (uint32_t)digitalRead(PIN_HX711_DT);
    digitalWrite(PIN_HX711_SCK, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(PIN_HX711_SCK, HIGH);
  delayMicroseconds(1);
  digitalWrite(PIN_HX711_SCK, LOW);
  interrupts();
  if (v & 0x800000UL) v |= 0xFF000000UL;  // signe
  return (long)v;
}

static bool hxReadNb(long *out) {
  if (!hxReady()) return false;
  *out = hxReadRaw();
  return true;
}

// ---------------------------------------------------------------------------
// État
// ---------------------------------------------------------------------------
enum Mode { MODE_TRACTION, MODE_FATIGUE, MODE_CALIB };

static Preferences prefs;
static WebServer http(80);
static WebSocketsServer ws(81);

static float scaleRawPerN = DEFAULT_SCALE_RAW_PER_N;
static long  offsetRaw    = 0;
static float limitN       = FORCE_LIMIT_DEFAULT_N;
static bool  calibrated   = false;
static int   sps          = SPS_FAST;
static Mode  mode         = MODE_TRACTION;
static char  unitStr[4]   = "N";

static bool  running      = false;
static bool  stopLatched  = false;
static bool  broken       = false;

static float forceN       = 0;
static float fmaxN        = 0;
static float cycMinN      = 0;
static float cycMaxN      = 0;
static uint32_t cycles    = 0;
static long  lastRaw      = 0;

static uint32_t tStartMs  = 0;
static char lastEvt[12]   = "";

// Fatigue : machine à états pic / creux
enum FatSt { FAT_SEEK_RISE, FAT_SEEK_PEAK, FAT_SEEK_FALL, FAT_SEEK_TROUGH };
static FatSt fatSt = FAT_SEEK_RISE;
static float fatExt = 0;  // extremum local

static uint32_t lastStatusMs = 0;
static uint32_t bootMs       = 0;

// ---------------------------------------------------------------------------
// Relais STOP
// ---------------------------------------------------------------------------
static void applyStopPin(bool on) {
  stopLatched = on;
#if STOP_ACTIVE_HIGH
  digitalWrite(PIN_STOP, on ? HIGH : LOW);
#else
  digitalWrite(PIN_STOP, on ? LOW : HIGH);
#endif
  digitalWrite(PIN_LED, on ? LOW : HIGH);  // LED onboard souvent actif bas
}

static void tripStop(const char *evt) {
  applyStopPin(true);
  running = false;
  strncpy(lastEvt, evt, sizeof(lastEvt) - 1);
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------
static float rawToN(long raw) {
  if (scaleRawPerN < 1e-6f) return 0;
  return (float)(raw - offsetRaw) / scaleRawPerN;
}

static float nToKg(float n) { return n / G_N_PER_KG; }

// ---------------------------------------------------------------------------
// JSON mini-extracteur
// ---------------------------------------------------------------------------
static const char *findKey(const char *json, const char *key) {
  static char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *p = strstr(json, pat);
  if (!p) return nullptr;
  p = strchr(p + strlen(pat), ':');
  if (!p) return nullptr;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  return p;
}

static bool jsonStr(const char *json, const char *key, char *out, size_t n) {
  const char *p = findKey(json, key);
  if (!p) return false;
  if (*p == '"') {
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
    out[i] = 0;
    return true;
  }
  // valeur non quotée (identifiant)
  size_t i = 0;
  while (*p && *p != ',' && *p != '}' && *p != ' ' && i + 1 < n) out[i++] = *p++;
  out[i] = 0;
  return i > 0;
}

static bool jsonFloat(const char *json, const char *key, float *out) {
  const char *p = findKey(json, key);
  if (!p) return false;
  *out = strtof(p, nullptr);
  return true;
}

static bool jsonInt(const char *json, const char *key, int *out) {
  const char *p = findKey(json, key);
  if (!p) return false;
  *out = atoi(p);
  return true;
}

// ---------------------------------------------------------------------------
// Émission
// ---------------------------------------------------------------------------
static char txbuf[384];

static void wsBroadcast(const char *s) { ws.broadcastTXT(s); }

static void sendSample() {
  snprintf(txbuf, sizeof(txbuf),
           "{\"type\":\"sample\",\"t\":%lu,\"N\":%.2f,\"kg\":%.3f,\"raw\":%ld,"
           "\"evt\":\"%s\",\"cyc\":%lu,\"fmin\":%.2f,\"fmax\":%.2f}",
           (unsigned long)millis(),
           (double)forceN,
           (double)nToKg(forceN),
           lastRaw,
           lastEvt,
           (unsigned long)cycles,
           (double)cycMinN,
           (double)fmaxN);
  wsBroadcast(txbuf);
  lastEvt[0] = 0;  // one-shot
}

static const char *modeStr() {
  if (mode == MODE_FATIGUE) return "fatigue";
  if (mode == MODE_CALIB) return "calib";
  return "traction";
}

static void sendStatus(const char *msg) {
  snprintf(txbuf, sizeof(txbuf),
           "{\"type\":\"status\",\"ok\":true,\"mode\":\"%s\",\"sps\":%d,"
           "\"scale\":%.4f,\"offset\":%ld,\"limit_N\":%.1f,\"hard_N\":%.1f,"
           "\"unit\":\"%s\",\"cal\":%s,\"stop\":%s,\"msg\":\"%s\"}",
           modeStr(),
           sps,
           (double)scaleRawPerN,
           offsetRaw,
           (double)limitN,
           (double)FORCE_HARD_CAP_N,
           unitStr,
           calibrated ? "true" : "false",
           stopLatched ? "true" : "false",
           msg ? msg : "Prêt");
  wsBroadcast(txbuf);
}

// ---------------------------------------------------------------------------
// Tare / calib / reset
// ---------------------------------------------------------------------------
static void doTare(int nAvg = 16) {
  long acc = 0;
  int got = 0;
  uint32_t t0 = millis();
  while (got < nAvg && millis() - t0 < 2000) {
    long r;
    if (hxReadNb(&r)) {
      acc += r;
      got++;
    }
    yield();
  }
  if (got > 0) {
    offsetRaw = acc / got;
    prefs.putLong("offset", offsetRaw);
    strncpy(lastEvt, "tare", sizeof(lastEvt) - 1);
  }
}

static void doCalibrate(float refN) {
  if (refN < 0.5f) return;
  long acc = 0;
  int got = 0;
  uint32_t t0 = millis();
  while (got < 20 && millis() - t0 < 2500) {
    long r;
    if (hxReadNb(&r)) {
      acc += r;
      got++;
    }
    yield();
  }
  if (got < 8) return;
  long raw = acc / got;
  float den = (float)(raw - offsetRaw);
  if (fabsf(den) < 50.0f) return;
  scaleRawPerN = den / refN;  // signe conservé : force > 0 dans le sens de l'étalonnage
  calibrated = true;
  prefs.putFloat("scale", scaleRawPerN);
  prefs.putBool("cal", true);
}

static void doResetMeas() {
  fmaxN = 0;
  cycMinN = 0;
  cycMaxN = 0;
  cycles = 0;
  broken = false;
  fatSt = FAT_SEEK_RISE;
  fatExt = 0;
  running = false;
  tStartMs = millis();
}

// ---------------------------------------------------------------------------
// Rupture + fatigue
// ---------------------------------------------------------------------------
static void detectBreak(float f) {
  if (mode != MODE_TRACTION) return;
  if (broken) return;
  if (fmaxN < BREAK_MIN_FMAX_N) return;
  if (f <= fmaxN * (1.0f - BREAK_DROP_RATIO)) {
    broken = true;
    strncpy(lastEvt, "break", sizeof(lastEvt) - 1);
    running = false;
    // On ne coupe pas le relais sur rupture : le TVM a ses fins de course.
    // L'opérateur arrête le vérin. STOP reste dispo manuellement.
  }
}

static void detectCycle(float f) {
  if (mode != MODE_FATIGUE) return;
  // Amplitude min. : on s'appuie sur l'hystérésis fixe.
  switch (fatSt) {
    case FAT_SEEK_RISE:
      fatExt = f;
      if (f > FATIGUE_HYST_N) {
        fatSt = FAT_SEEK_PEAK;
        cycMinN = f;
      }
      break;
    case FAT_SEEK_PEAK:
      if (f > fatExt) fatExt = f;
      if (f < fatExt - FATIGUE_HYST_N) {
        cycMaxN = fatExt;
        fmaxN = fatExt;
        strncpy(lastEvt, "peak", sizeof(lastEvt) - 1);
        fatSt = FAT_SEEK_FALL;
        fatExt = f;
      }
      break;
    case FAT_SEEK_FALL:
      if (f < fatExt) fatExt = f;
      if (f > fatExt + FATIGUE_HYST_N) {
        cycMinN = fatExt;
        fatSt = FAT_SEEK_TROUGH;
        fatExt = f;
      }
      break;
    case FAT_SEEK_TROUGH:
      // Un cycle = un pic puis un creux, amplitude suffisante
      if (cycMaxN - cycMinN >= FATIGUE_MIN_AMP_N) {
        cycles++;
        strncpy(lastEvt, "cycle", sizeof(lastEvt) - 1);
      }
      fatSt = FAT_SEEK_PEAK;
      fatExt = f;
      cycMaxN = f;
      break;
  }
}

// ---------------------------------------------------------------------------
// Limite de force
// ---------------------------------------------------------------------------
static void enforceLimit(float f) {
  if (f >= limitN || f >= FORCE_HARD_CAP_N) {
    tripStop("limit");
  }
}

// ---------------------------------------------------------------------------
// Commandes
// ---------------------------------------------------------------------------
static void setModeFromStr(const char *s) {
  if (!s) return;
  if (!strcmp(s, "fatigue")) mode = MODE_FATIGUE;
  else if (!strcmp(s, "calib")) mode = MODE_CALIB;
  else mode = MODE_TRACTION;
  prefs.putUChar("mode", (uint8_t)mode);
}

static void handleCmd(const char *json) {
  char cmd[24] = {0};
  if (!jsonStr(json, "cmd", cmd, sizeof(cmd))) return;

  if (!strcmp(cmd, "tare")) {
    doTare();
    sendStatus("Tare OK");
    return;
  }
  if (!strcmp(cmd, "calibrate")) {
    float refN = 0, refKg = 0;
    jsonFloat(json, "ref_N", &refN);
    jsonFloat(json, "ref_kg", &refKg);
    if (refN < 0.5f && refKg >= 0.05f) refN = refKg * G_N_PER_KG;
    doCalibrate(refN);
    sendStatus(calibrated ? "Étalonnage OK" : "Étalonnage échoué");
    return;
  }
  if (!strcmp(cmd, "setScale")) {
    float sc;
    if (jsonFloat(json, "scale", &sc) && sc > 1.0f) {
      scaleRawPerN = sc;
      calibrated = true;
      prefs.putFloat("scale", scaleRawPerN);
      prefs.putBool("cal", true);
      sendStatus("Échelle enregistrée");
    }
    return;
  }
  if (!strcmp(cmd, "setLimit")) {
    float lim;
    if (jsonFloat(json, "limit_N", &lim)) {
      if (lim < 50.0f) lim = 50.0f;
      if (lim > FORCE_HARD_CAP_N) lim = FORCE_HARD_CAP_N;
      limitN = lim;
      prefs.putFloat("limit", limitN);
      sendStatus("Limite enregistrée");
    }
    return;
  }
  if (!strcmp(cmd, "setMode")) {
    char m[16] = {0};
    jsonStr(json, "mode", m, sizeof(m));
    setModeFromStr(m);
    doResetMeas();
    sendStatus("Mode changé");
    return;
  }
  if (!strcmp(cmd, "start")) {
    applyStopPin(false);
    broken = false;
    running = true;
    tStartMs = millis();
    if (mode == MODE_TRACTION) {
      fmaxN = forceN > 0 ? forceN : 0;
    }
    sendStatus("Mesure démarrée");
    return;
  }
  if (!strcmp(cmd, "stop")) {
    tripStop("stop");
    sendStatus("STOP relais");
    return;
  }
  if (!strcmp(cmd, "reset")) {
    applyStopPin(false);
    doResetMeas();
    sendStatus("RAZ mesure");
    return;
  }
  if (!strcmp(cmd, "setSps")) {
    int v = sps;
    jsonInt(json, "sps", &v);
    if (v >= 40) {
      sps = SPS_FAST;
      digitalWrite(PIN_HX711_RATE, HIGH);
    } else {
      sps = SPS_SLOW;
      digitalWrite(PIN_HX711_RATE, LOW);
    }
    prefs.putUChar("sps", (uint8_t)sps);
    sendStatus("Cadence");
    return;
  }
  if (!strcmp(cmd, "setUnit")) {
    char u[8] = {0};
    jsonStr(json, "unit", u, sizeof(u));
    if (!strcmp(u, "kg")) strncpy(unitStr, "kg", sizeof(unitStr));
    else strncpy(unitStr, "N", sizeof(unitStr));
    sendStatus("Unité");
    return;
  }
  if (!strcmp(cmd, "getStatus")) {
    sendStatus("Prêt");
    return;
  }
}

// ---------------------------------------------------------------------------
// HTTP / LittleFS
// ---------------------------------------------------------------------------
static const char FALLBACK_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="fr"><meta charset="utf-8">
<title>TVM Traction</title>
<body style="font-family:sans-serif;background:#12151c;color:#eee;padding:2rem">
<h1>Banc de traction TVM</h1>
<p>Le système de fichiers LittleFS est vide. Flashez les pages web :</p>
<pre>cd firmware
pio run -t uploadfs</pre>
<p>Ou ouvrez <code>web/index.html</code> en simulation sur le PC.</p>
</body></html>
)HTML";

static String mimeOf(const String &p) {
  if (p.endsWith(".html")) return "text/html; charset=utf-8";
  if (p.endsWith(".css"))  return "text/css; charset=utf-8";
  if (p.endsWith(".js"))   return "application/javascript; charset=utf-8";
  if (p.endsWith(".json")) return "application/json";
  if (p.endsWith(".svg"))  return "image/svg+xml";
  if (p.endsWith(".png"))  return "image/png";
  if (p.endsWith(".ico"))  return "image/x-icon";
  if (p.endsWith(".webmanifest")) return "application/manifest+json";
  return "text/plain";
}

static bool serveFile(String path) {
  if (path.endsWith("/")) path += "index.html";
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  http.streamFile(f, mimeOf(path));
  f.close();
  return true;
}

static void onHttp() {
  String path = http.uri();
  if (path == "/") path = "/index.html";
  if (serveFile(path)) return;
  http.send_P(200, "text/html; charset=utf-8", FALLBACK_HTML);
}

static void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len) {
  if (type == WStype_CONNECTED) {
    sendStatus("Client connecté");
  } else if (type == WStype_TEXT) {
    // payload n'est pas forcément terminé par 0
    char buf[256];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = 0;
    handleCmd(buf);
    (void)num;
  }
}

// ---------------------------------------------------------------------------
// Auto 80 → 10 SPS si instable à vide
// ---------------------------------------------------------------------------
static void chooseSps() {
  digitalWrite(PIN_HX711_RATE, HIGH);
  delay(250);
  float acc = 0, acc2 = 0;
  int n = 0;
  uint32_t t0 = millis();
  while (n < 24 && millis() - t0 < 1500) {
    long r;
    if (hxReadNb(&r)) {
      float f = rawToN(r);
      acc += f;
      acc2 += f * f;
      n++;
    }
    yield();
  }
  bool ok80 = false;
  if (n >= 12) {
    float mean = acc / n;
    float var = acc2 / n - mean * mean;
    if (var < 0) var = 0;
    float sd = sqrtf(var);
    ok80 = (sd <= STABLE_SD_N);
  }
  if (ok80) {
    sps = SPS_FAST;
    digitalWrite(PIN_HX711_RATE, HIGH);
  } else {
    sps = SPS_SLOW;
    digitalWrite(PIN_HX711_RATE, LOW);
  }
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[TVM] Banc de traction Artaud");

  pinMode(PIN_HX711_DT, INPUT);
  pinMode(PIN_HX711_SCK, OUTPUT);
  digitalWrite(PIN_HX711_SCK, LOW);
  pinMode(PIN_HX711_RATE, OUTPUT);
  pinMode(PIN_STOP, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  applyStopPin(false);

  prefs.begin("tvm", false);
  scaleRawPerN = prefs.getFloat("scale", DEFAULT_SCALE_RAW_PER_N);
  offsetRaw    = prefs.getLong("offset", 0);
  limitN       = prefs.getFloat("limit", FORCE_LIMIT_DEFAULT_N);
  if (limitN > FORCE_HARD_CAP_N) limitN = FORCE_HARD_CAP_N;
  calibrated   = prefs.getBool("cal", false);
  mode         = (Mode)prefs.getUChar("mode", MODE_TRACTION);
  uint8_t sp   = prefs.getUChar("sps", 0);  // 0 = auto

  if (!LittleFS.begin(true)) {
    Serial.println("[TVM] LittleFS indisponible");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS, WIFI_CHANNEL, 0, WIFI_MAX_CLIENTS);
  delay(100);
  Serial.print("[TVM] AP ");
  Serial.print(WIFI_SSID);
  Serial.print("  pass ");
  Serial.print(WIFI_PASS);
  Serial.print("  IP ");
  Serial.println(WiFi.softAPIP());

  http.onNotFound(onHttp);
  http.begin();
  ws.begin();
  ws.onEvent(onWsEvent);

  if (sp == SPS_SLOW) {
    sps = SPS_SLOW;
    digitalWrite(PIN_HX711_RATE, LOW);
  } else if (sp == SPS_FAST) {
    sps = SPS_FAST;
    digitalWrite(PIN_HX711_RATE, HIGH);
  } else {
    chooseSps();
  }

  delay(300);
  doTare(12);
  bootMs = millis();
  tStartMs = bootMs;
  Serial.printf("[TVM] sps=%d scale=%.2f tare=%ld limit=%.0f\n",
                sps, scaleRawPerN, offsetRaw, limitN);
}

void loop() {
  http.handleClient();
  ws.loop();

  long raw;
  if (hxReadNb(&raw)) {
    lastRaw = raw;
    forceN = rawToN(raw);
    if (forceN > fmaxN) fmaxN = forceN;
    if (mode == MODE_FATIGUE) {
      if (cycles == 0 && fatSt == FAT_SEEK_RISE) cycMinN = forceN;
    }
    detectBreak(forceN);
    detectCycle(forceN);
    enforceLimit(forceN);
    sendSample();
  }

  if (millis() - lastStatusMs > 2000) {
    lastStatusMs = millis();
    // heartbeat léger si aucun client n'a demandé
  }

  // Watchdog amical
  yield();
}

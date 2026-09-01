/* Banc de traction — ESP32 + HX711  v1.1.1
 * Lycée Antonin Artaud, BTS MS / FabLab
 * SAUTER TVM 5000N230N · jauge sur HX711 U1 · pas d'extensomètre
 *
 * Carte EasyEDA ESP32-WROOM-32 DevKitC-V4 + 2× XW-HX711 @ 3,3 V
 *   U1 jauge 3 fils (lue) : DT1 = GPIO 25, SCK1 = GPIO 33
 *   U2 cellule 4 fils (non lue) : DT2 = GPIO 14, SCK2 = GPIO 26
 *   VCC HX711 → 3V3   GND → GND     (pas 5 V)
 *
 * Relais désactivé : dépassement 4000 N = alarme « ARRÊTER LA MACHINE ».
 */

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <math.h>

#include "config.h"
#include "protocol.h"

// ---------------------------------------------------------------------------
// HX711 minimal (canal A, gain 128)
// ---------------------------------------------------------------------------
static bool hxReady() {
  return digitalRead(PIN_HX711_DT) == LOW;
}

static bool hxReadRaw(long *out) {
  uint32_t t0 = micros();
  while (digitalRead(PIN_HX711_DT) == HIGH) {
    if (micros() - t0 > 200000UL) return false;
  }
  uint32_t v = 0;
  noInterrupts();
  for (int i = 0; i < 24; i++) {
    digitalWrite(PIN_HX711_SCK, HIGH);
    delayMicroseconds(2);
    v = (v << 1) | (uint32_t)digitalRead(PIN_HX711_DT);
    digitalWrite(PIN_HX711_SCK, LOW);
    delayMicroseconds(2);
  }
  digitalWrite(PIN_HX711_SCK, HIGH);
  delayMicroseconds(2);
  digitalWrite(PIN_HX711_SCK, LOW);
  interrupts();
  if (v & 0x800000UL) v |= 0xFF000000UL;
  *out = (long)v;
  return true;
}

static bool hxReadNb(long *out) {
  if (!hxReady()) return false;
  return hxReadRaw(out);
}

// ---------------------------------------------------------------------------
// État
// ---------------------------------------------------------------------------
enum Mode { MODE_TRACTION, MODE_FATIGUE, MODE_CALIB };

static Preferences prefs;
static AsyncWebServer http(80);
static AsyncWebSocket ws("/ws");
static DNSServer dns;

static float scaleRawPerN = DEFAULT_SCALE_RAW_PER_N;
static long  offsetRaw    = 0;
static float limitN       = FORCE_LIMIT_DEFAULT_N;
static bool  calibrated   = false;
static bool  tared        = false;
static int   sps          = SPS_SLOW;
static Mode  mode         = MODE_TRACTION;
static char  unitStr[4]   = "N";

static bool  running      = false;
static bool  stopLatched  = false;
static bool  broken       = false;
static bool  overLive     = false;
static bool  overSeen     = false;
static uint8_t overCount  = 0;
static bool  hxOk         = false;
static bool  hxFailLatched = false;

static float forceN       = 0;
static float fmaxN        = 0;
static float cycMinN      = 0;
static float cycMaxN      = 0;
static uint32_t cycles    = 0;
static long  lastRaw      = 0;

static uint32_t tStartMs     = 0;
static uint32_t lastHxMs     = 0;
static uint32_t lastTxMs     = 0;
static uint32_t lastStatusMs = 0;
static uint32_t lastLedMs    = 0;
static uint32_t bootMs       = 0;
static bool    hxEver        = false;
static char lastEvt[12]      = "";

enum FatSt { FAT_SEEK_RISE, FAT_SEEK_PEAK, FAT_SEEK_FALL, FAT_SEEK_TROUGH };
static FatSt fatSt = FAT_SEEK_RISE;
static float fatExt = 0;

// ---------------------------------------------------------------------------
// Relais STOP (no-op matériel si RELAY_INSTALLED 0)
// ---------------------------------------------------------------------------
static void applyStopPin(bool on) {
  stopLatched = on;
#if RELAY_INSTALLED
#if STOP_ACTIVE_HIGH
  digitalWrite(PIN_STOP, on ? HIGH : LOW);
#else
  digitalWrite(PIN_STOP, on ? LOW : HIGH);
#endif
#endif
}

static void setEvt(const char *evt) {
  strncpy(lastEvt, evt, sizeof(lastEvt) - 1);
  lastEvt[sizeof(lastEvt) - 1] = 0;
}

static void ledAlarm(bool on) {
  // Onboard souvent actif bas
  digitalWrite(PIN_LED, on ? LOW : HIGH);
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------
static float rawToN(long raw) {
  if (fabsf(scaleRawPerN) < 1e-6f) return 0;
  return (float)(raw - offsetRaw) / scaleRawPerN;
}

static float nToKg(float n) { return n / G_N_PER_KG; }

static void pumpNet() {
  dns.processNextRequest();
  ws.cleanupClients();
  yield();
}

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
static char txbuf[640];

static const char *modeStr() {
  if (mode == MODE_FATIGUE) return "fatigue";
  if (mode == MODE_CALIB) return "calib";
  return "traction";
}

static void sendSample() {
  if (ws.count() == 0 && lastEvt[0] == 0) {
    lastEvt[0] = 0;
    return;
  }
  snprintf(txbuf, sizeof(txbuf),
           "{\"type\":\"sample\",\"t\":%lu,\"N\":%.2f,\"kg\":%.3f,\"raw\":%ld,"
           "\"evt\":\"%s\",\"cyc\":%lu,\"fmin\":%.2f,\"fmax\":%.2f,"
           "\"over\":%s,\"hx\":%s}",
           (unsigned long)millis(),
           (double)forceN,
           (double)nToKg(forceN),
           lastRaw,
           lastEvt,
           (unsigned long)cycles,
           (double)cycMinN,
           (double)fmaxN,
           overLive ? "true" : "false",
           hxFailLatched ? "false" : "true");
  ws.textAll(txbuf);
  lastEvt[0] = 0;
  lastTxMs = millis();
}

static void sendStatus(const char *msg) {
  snprintf(txbuf, sizeof(txbuf),
           "{\"type\":\"status\",\"ok\":true,\"ver\":\"%s\",\"mode\":\"%s\","
           "\"sps\":%d,\"scale\":%.4f,\"offset\":%ld,\"limit_N\":%.1f,"
           "\"hard_N\":%.1f,\"unit\":\"%s\",\"cal\":%s,\"tared\":%s,"
           "\"run\":%s,\"relay\":%s,\"stop\":%s,\"over\":%s,\"seen\":%s,"
           "\"hx_ok\":%s,\"raw\":%ld,\"msg\":\"%s\"}",
           FIRMWARE_VERSION,
           modeStr(),
           sps,
           (double)scaleRawPerN,
           offsetRaw,
           (double)limitN,
           (double)FORCE_HARD_CAP_N,
           unitStr,
           calibrated ? "true" : "false",
           tared ? "true" : "false",
           running ? "true" : "false",
#if RELAY_INSTALLED
           "true",
#else
           "false",
#endif
           stopLatched ? "true" : "false",
           overLive ? "true" : "false",
           overSeen ? "true" : "false",
           hxFailLatched ? "false" : "true",
           lastRaw,
           msg ? msg : "Prêt");
  ws.textAll(txbuf);
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
      lastRaw = r;
      lastHxMs = millis();
      hxOk = true;
    }
    pumpNet();
  }
  if (got > 0) {
    offsetRaw = acc / got;
    tared = true;
    prefs.putLong("offset", offsetRaw);
    prefs.putBool("tared", true);
    setEvt("tare");
  }
}

/* Échelle SIGNÉE : si la jauge descend en négatif, den < 0 → scale < 0
 * et F = (brut − offset) / scale redevient positif (même sens que la masse). */
static const char *doCalibrate(float refN) {
  if (!tared) return "Échec : tare à vide d'abord (sans la masse)";
  if (!(refN >= CAL_MIN_REF_N)) return "Échec : masse / force de référence trop faible";
  long acc = 0;
  int got = 0;
  uint32_t t0 = millis();
  while (got < 24 && millis() - t0 < 4000) {
    long r;
    if (hxReadNb(&r)) {
      acc += r;
      got++;
      lastRaw = r;
      lastHxMs = millis();
      hxOk = true;
      hxEver = true;
    }
    pumpNet();
  }
  if (got < 10) return "Échec : trop peu d'échantillons HX711";
  long raw = acc / got;
  lastRaw = raw;
  float den = (float)(raw - offsetRaw);
  if (fabsf(den) < CAL_MIN_DELTA) {
    return "Échec : le brut n'a presque pas bougé — masse trop légère, ou tare faite AVEC la masse";
  }
  float sc = den / refN;
  if (fabsf(sc) < SCALE_ABS_MIN || fabsf(sc) > SCALE_ABS_MAX) {
    return "Échec : échelle hors plage (jauge trop peu / trop sensible)";
  }
  scaleRawPerN = sc;
  forceN = rawToN(raw);
  calibrated = true;
  prefs.putFloat("scale", scaleRawPerN);
  prefs.putBool("cal", true);
  return nullptr;
}

static void doResetMeas() {
  fmaxN = 0;
  cycMinN = 0;
  cycMaxN = 0;
  cycles = 0;
  broken = false;
  overLive = false;
  overSeen = false;
  overCount = 0;
  hxFailLatched = false;
  fatSt = FAT_SEEK_RISE;
  fatExt = 0;
  running = false;
  tStartMs = millis();
}

// ---------------------------------------------------------------------------
// Rupture + fatigue (uniquement pendant l'enregistrement)
// ---------------------------------------------------------------------------
static void detectBreak(float f) {
  if (!running) return;
  if (mode != MODE_TRACTION) return;
  if (broken) return;
  if (fmaxN < BREAK_MIN_FMAX_N) return;
  if (f <= fmaxN * (1.0f - BREAK_DROP_RATIO)) {
    broken = true;
    setEvt("break");
    running = false;
  }
}

static void detectCycle(float f) {
  if (!running) return;
  if (mode != MODE_FATIGUE) return;
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
        setEvt("peak");
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
      if (cycMaxN - cycMinN >= FATIGUE_MIN_AMP_N) {
        cycles++;
        setEvt("cycle");
      }
      fatSt = FAT_SEEK_PEAK;
      fatExt = f;
      break;
  }
}

// ---------------------------------------------------------------------------
// Limite de force — sans relais : alarme, l'enregistrement continue
// ---------------------------------------------------------------------------
static void enforceLimit(float f) {
  float a = fabsf(f);
  if (a >= limitN || a >= FORCE_HARD_CAP_N) {
    if (overCount < 10) overCount++;
    if (overCount >= OVER_CONFIRM) {
      if (!overLive) setEvt("limit");
      overLive = true;
      overSeen = true;
#if RELAY_INSTALLED
      applyStopPin(true);
      running = false;
#endif
    }
  } else if (a < limitN * 0.95f && a < FORCE_HARD_CAP_N * 0.95f) {
    overCount = 0;
    overLive = false;
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
    sendSample();
    sendStatus(tared ? "Tare OK — vérifier à vide ~0 N" : "Tare échouée (HX711 ?)");
    return;
  }
  if (!strcmp(cmd, "calibrate")) {
    float refN = 0, refKg = 0;
    jsonFloat(json, "ref_N", &refN);
    jsonFloat(json, "ref_kg", &refKg);
    if (refN < CAL_MIN_REF_N && refKg >= 0.005f) refN = refKg * G_N_PER_KG;
    const char *err = doCalibrate(refN);
    sendSample();
    if (!err) {
      char okmsg[160];
      snprintf(okmsg, sizeof(okmsg),
               "Étalonnage OK — échelle %.3f counts/N%s",
               (double)scaleRawPerN,
               scaleRawPerN < 0 ? " (jauge inverse, F>0)" : "");
      sendStatus(okmsg);
    } else {
      sendStatus(err);
    }
    return;
  }
  if (!strcmp(cmd, "invert")) {
    scaleRawPerN = -scaleRawPerN;
    calibrated = true;
    prefs.putFloat("scale", scaleRawPerN);
    prefs.putBool("cal", true);
    forceN = rawToN(lastRaw);
    sendSample();
    sendStatus(scaleRawPerN < 0
               ? "Sens inversé — échelle négative (F>0 si la jauge descend)"
               : "Sens inversé — échelle positive");
    return;
  }
  if (!strcmp(cmd, "setScale")) {
    float sc;
    if (jsonFloat(json, "scale", &sc) && fabsf(sc) >= SCALE_ABS_MIN && fabsf(sc) <= SCALE_ABS_MAX) {
      scaleRawPerN = sc;
      calibrated = true;
      prefs.putFloat("scale", scaleRawPerN);
      prefs.putBool("cal", true);
      sendStatus("Échelle enregistrée");
    } else {
      sendStatus("Échelle hors plage");
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
#if RELAY_INSTALLED
    if (overLive) {
      sendStatus("STOP maintenu : |F| encore au-dessus de la limite");
      return;
    }
    applyStopPin(false);
#endif
    broken = false;
    running = true;
    tStartMs = millis();
    if (mode == MODE_TRACTION) {
      fmaxN = forceN > 0 ? forceN : 0;
    }
    sendStatus("Mesure démarrée — le TVM se commande au pupitre");
    return;
  }
  if (!strcmp(cmd, "stop")) {
    running = false;
    setEvt("stop");
#if RELAY_INSTALLED
    applyStopPin(true);
    sendSample();
    sendStatus("STOP relais");
#else
    sendSample();
    sendStatus("Enregistrement arrêté (pas de relais : STOP au pupitre TVM)");
#endif
    return;
  }
  if (!strcmp(cmd, "reset")) {
#if RELAY_INSTALLED
    if (!overLive) applyStopPin(false);
#endif
    doResetMeas();
    sendStatus("RAZ mesure");
    return;
  }
  if (!strcmp(cmd, "break")) {
    broken = true;
    running = false;
    setEvt("break");
    sendSample();
    sendStatus("Rupture marquée (opérateur)");
    return;
  }
  if (!strcmp(cmd, "setSps")) {
#if HX711_RATE_WIRED
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
#else
    sps = SPS_SLOW;
    sendStatus("RATE non câblé — 10 SPS (module)");
#endif
    return;
  }
  if (!strcmp(cmd, "setUnit")) {
    char u[8] = {0};
    jsonStr(json, "unit", u, sizeof(u));
    if (!strcmp(u, "kg")) strncpy(unitStr, "kg", sizeof(unitStr));
    else strncpy(unitStr, "N", sizeof(unitStr));
    unitStr[sizeof(unitStr) - 1] = 0;
    sendStatus("Unité");
    return;
  }
  if (!strcmp(cmd, "getStatus")) {
    sendStatus(tared ? "Prêt — tare à vide en début de séance" : "Tare à vide obligatoire");
    return;
  }
}

// ---------------------------------------------------------------------------
// HTTP / LittleFS / portail captif
// ---------------------------------------------------------------------------
static const char FALLBACK_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="fr"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TVM Traction</title>
<body style="font-family:sans-serif;background:#12151c;color:#eee;padding:2rem">
<h1>Banc de traction TVM</h1>
<p>LittleFS vide. Flashez les pages web :</p>
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

static void sendIndex(AsyncWebServerRequest *req) {
  if (LittleFS.exists("/index.html")) {
    req->send(LittleFS, "/index.html", "text/html; charset=utf-8");
  } else {
    req->send_P(200, "text/html; charset=utf-8", FALLBACK_HTML);
  }
}

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)server;
  (void)client;
  if (type == WS_EVT_CONNECT) {
    sendStatus(tared ? "Client connecté" : "Client connecté — tare à vide obligatoire");
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      char buf[256];
      size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
      memcpy(buf, data, n);
      buf[n] = 0;
      handleCmd(buf);
    }
  }
}

#if HX711_RATE_WIRED
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
    pumpNet();
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
#endif

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[TVM] Banc de traction Artaud v" FIRMWARE_VERSION);
#if RELAY_INSTALLED
  Serial.println("[TVM] Relais STOP activé");
#else
  Serial.println("[TVM] Pas de relais — alarme 4000 N, STOP au pupitre TVM");
#endif

  pinMode(PIN_HX711_DT, INPUT);
  pinMode(PIN_HX711_SCK, OUTPUT);
  digitalWrite(PIN_HX711_SCK, LOW);
#if HX711_RATE_WIRED
  pinMode(PIN_HX711_RATE, OUTPUT);
#endif
#if RELAY_INSTALLED
  pinMode(PIN_STOP, OUTPUT);
  applyStopPin(false);
#else
  pinMode(PIN_STOP, INPUT);
#endif
  pinMode(PIN_LED, OUTPUT);
  ledAlarm(false);

  prefs.begin("tvm", false);
  scaleRawPerN = prefs.getFloat("scale", DEFAULT_SCALE_RAW_PER_N);
  offsetRaw    = prefs.getLong("offset", 0);
  limitN       = prefs.getFloat("limit", FORCE_LIMIT_DEFAULT_N);
  if (limitN > FORCE_HARD_CAP_N) limitN = FORCE_HARD_CAP_N;
  calibrated   = prefs.getBool("cal", false);
  tared        = prefs.getBool("tared", false);
  mode         = (Mode)prefs.getUChar("mode", MODE_TRACTION);
  uint8_t sp   = prefs.getUChar("sps", 0);

  // Ne pas formater en cas de corruption : ça effacerait l'appli web.
  if (!LittleFS.begin(false)) {
    Serial.println("[TVM] LittleFS non monté — uploadfs requis");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS, WIFI_CHANNEL, 0, WIFI_MAX_CLIENTS);
  delay(100);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("[TVM] AP ");
  Serial.print(WIFI_SSID);
  Serial.print("  pass ");
  Serial.print(WIFI_PASS);
  Serial.print("  http://");
  Serial.println(ip);

  dns.start(53, "*", ip);

  ws.onEvent(onWsEvent);
  http.addHandler(&ws);

  http.on("/", HTTP_GET, sendIndex);
  http.on("/generate_204", HTTP_GET, sendIndex);
  http.on("/gen_204", HTTP_GET, sendIndex);
  http.on("/hotspot-detect.html", HTTP_GET, sendIndex);
  http.on("/library/test/success.html", HTTP_GET, sendIndex);
  http.on("/canonical.html", HTTP_GET, sendIndex);
  http.on("/fwlink", HTTP_GET, sendIndex);
  http.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain", "Microsoft Connect Test");
  });
  http.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain", "Microsoft NCSI");
  });
  http.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain", "success");
  });
  http.serveStatic("/", LittleFS, "/")
      .setCacheControl("no-cache, no-store, must-revalidate");
  http.onNotFound([](AsyncWebServerRequest *req) {
    String p = req->url();
    if (p.endsWith("/")) p += "index.html";
    if (LittleFS.exists(p)) {
      req->send(LittleFS, p, mimeOf(p));
      return;
    }
    sendIndex(req);
  });
  http.begin();

#if HX711_RATE_WIRED
  if (sp == SPS_SLOW) {
    sps = SPS_SLOW;
    digitalWrite(PIN_HX711_RATE, LOW);
  } else if (sp == SPS_FAST) {
    sps = SPS_FAST;
    digitalWrite(PIN_HX711_RATE, HIGH);
  } else {
    chooseSps();
  }
#else
  (void)sp;
  sps = SPS_SLOW;
#endif

  tStartMs = millis();
  lastHxMs = millis();
  bootMs = millis();
  Serial.printf("[TVM] sps=%d scale=%.2f tare=%ld limit=%.0f cal=%d\n",
                (int)sps, (double)scaleRawPerN, offsetRaw, (double)limitN,
                calibrated ? 1 : 0);
  Serial.println("[TVM] Tare manuelle obligatoire à vide en début de séance.");
}

void loop() {
  pumpNet();

  long raw;
  if (hxReadNb(&raw)) {
    lastRaw = raw;
    lastHxMs = millis();
    hxEver = true;
    hxOk = true;
    if (hxFailLatched) {
      hxFailLatched = false;
    }
    forceN = rawToN(raw);
    if (running && forceN > fmaxN) fmaxN = forceN;
    if (mode == MODE_FATIGUE && running) {
      if (cycles == 0 && fatSt == FAT_SEEK_RISE) cycMinN = forceN;
    }
    detectBreak(forceN);
    detectCycle(forceN);
    enforceLimit(forceN);

    bool ev = lastEvt[0] != 0;
    if (ev || (ws.count() && millis() - lastTxMs >= 20)) {
      sendSample();
    }
  }

  uint32_t hxTo = HX_FAIL_MIN_MS;
  if (sps <= SPS_SLOW) hxTo = 500;
  bool hxTimeout = hxEver && hxOk && (millis() - lastHxMs > hxTo);
  bool hxSilentBoot = !hxEver && (millis() - bootMs > 2000);
  if ((hxTimeout || hxSilentBoot) && !hxFailLatched) {
    hxOk = false;
    hxFailLatched = true;
    setEvt("hxfail");
    sendSample();
    sendStatus("MESURE PERDUE — ARRÊTER LE TVM au pupitre");
  }

  if (millis() - lastStatusMs > 2000) {
    lastStatusMs = millis();
    if (ws.count()) {
      if (overLive) {
        sendStatus("ARRÊTER LA MACHINE — F > limite — STOP pupitre TVM");
      } else if (hxFailLatched) {
        sendStatus("MESURE PERDUE — ARRÊTER LE TVM au pupitre");
      } else if (!tared) {
        sendStatus("Tare à vide obligatoire");
      }
    }
  }

  // LED : clignote si alarme (surcharge ou HX711 mort)
  bool alarm = overLive || hxFailLatched;
  if (alarm) {
    if (millis() - lastLedMs > 200) {
      lastLedMs = millis();
      static bool blink;
      blink = !blink;
      ledAlarm(blink);
    }
  } else {
    ledAlarm(false);
  }
}

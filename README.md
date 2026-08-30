# Banc de traction — Lycée Antonin Artaud

**BTS Maintenance des systèmes / FabLab**
SAUTER **TVM 5000N230N** (5 kN, mode Repeat) · cellule **CZL301 500 kg** · **ESP32 + HX711 à 3,3 V**
Pas de dynamomètre SAUTER FH · **pas d’extensomètre / pas de capteur de déplacement** sur l’ESP32.

Ce dépôt est autonome : firmware PlatformIO, application web française, simulation sans carte.

---

## 1. Sécurité (à coller près du banc)

- Limite **logicielle par défaut : 4000 N**. Plafond dur **&lt; 4900 N** (la cellule fait 500 kg ≈ 4903 N).
- Surcharge constructeur CZL301 : **120 % FS** (600 kg). Ne pas s’en servir comme consigne.
- Le TVM développe **5 kN**. L’ESP32 coupe un **relais optionnel** si la force dépasse la limite.
- **HX711 et GPIO ESP32 en 3,3 V uniquement.** Les GPIO ne sont **pas 5 V tolerant**. Si le module HX711 est alimenté en 5 V, DT/SCK envoient 5 V dans l’ESP32 : carte grillée.
- Le firmware **ne commande pas** la traverse. Le mouvement vient du pupitre TVM (manuel ou Repeat).

---

## 2. Câblage

```
CZL301 (S, 350 Ω, 2,0030 mV/V, câble 3 m)
  rouge  R  →  E+   HX711      (excitation +)
  noir   B  →  E-   HX711      (excitation −)
  vert   G  →  A+   HX711      (signal +)
  blanc  W  →  A-   HX711      (signal −)
  jaune  Y  →  GND  commun     (blindage)

HX711
  VCC    →  3V3  ESP32         ← PAS 5 V
  GND    →  GND  ESP32
  DT     →  GPIO 16
  SCK    →  GPIO 17
  RATE   →  GPIO 33            (optionnel : HIGH = 80 SPS, LOW = 10 SPS)

Relais STOP (optionnel)
  GPIO 26 → base transistor (2N2222 / MOSFET N) + résistance 1 kΩ
  collecteur/drain → bobine relais 5 V (l’autre côté au +5 V relais)
  diode de roue libre aux bornes de la bobine
  contact NF du relais en série avec l’enable / l’arrêt moteur du TVM
```

### Polarité du STOP

| Constante dans `firmware/include/config.h` | GPIO 26 | Effet |
|---|---|---|
| `STOP_ACTIVE_HIGH 1` (**défaut**) | HIGH | transistor passant → relais **collé** → contact NF **ouvre** → moteur coupé |
| `STOP_ACTIVE_HIGH 0` | LOW | idem si le câblage est actif bas |

**Ne jamais** alimenter la bobine directement par un GPIO. Au repos le GPIO est LOW (relais relâché, mouvement autorisé).

Si la force affichée est **négative** en traction après tare : inverser vert et blanc (A+ / A−), puis retarer et ré-étalonner.

Cadence : le firmware tente **80 SPS** (broche RATE à 1). Si le bruit à vide dépasse ~1,8 N, il bascule tout seul à **10 SPS**. Beaucoup de modules HX711 bon marché ont RATE collé à GND (10 SPS seulement) : dans ce cas laisser GPIO 33 déconnecté et choisir 10 SPS dans Calib.

---

## 3. Flasher l’ESP32

Prérequis : [PlatformIO](https://platformio.org/) (CLI ou extension VS Code / Cursor), câble USB, drivers CP2102 ou CH340.

```bash
./sync-web-to-fs.sh                # si tu as modifié web/
cd firmware
pio run -e esp32dev -t upload      # programme
pio run -e esp32dev -t uploadfs    # pages web (LittleFS) — obligatoire une fois
pio device monitor -b 115200       # optionnel : IP, tare, cadence
```

Sans LittleFS, l’ESP32 sert une page de secours qui rappelle `uploadfs`.

Broches et SSID se changent dans `firmware/include/config.h`. L’échelle, la tare et la limite sont stockées en NVS (survivent au reset).

---

## 4. Ouvrir l’application

### Sur le banc (mesure réelle)

1. Flasher firmware + LittleFS.
2. Sur le téléphone / PC atelier : joindre le Wi-Fi **`TVM-TRACTION`** / mot de passe **`traction1`**.
3. Navigateur : **http://192.168.4.1**
4. Pastille verte « ESP32 192.168.4.1:81 » = WebSocket OK.

L’ESP32 est un **point d’accès** : pas besoin du Wi-Fi lycée. HTTP port 80, WebSocket **port 81**.

### Sans ESP32 (démo / préparation de séance)

- Double-clic sur `web/index.html` (simulation immédiate), **ou**
- `python3 web/serve.py` puis **http://127.0.0.1:8080/** (PWA installable sur localhost).

Le mode simulation rejoue le **même JSON** que le firmware (courbe de traction jusqu’à rupture, fatigue sinusoïdale + comptage de cycles). Bouton **Forcer simulation** / **Reconnecter ESP32**.

---

## 5. Séance traction

1. Monter l’éprouvette dans les pinces (AC 18 + AFM 16 M12/M10, hors de ce dépôt).
2. Onglet **Traction** → **Tare** à vide (ou mâchoires fermées sans charge utile).
3. Nommer l’éprouvette → **Démarrer**.
4. Lancer la descente **sur le pupitre TVM** (vitesse affichée 10–230 mm/min). L’appli trace **F(t)**, **Fmax**.
5. À la rupture : détection auto (chute ~22 % sous Fmax) ou bouton **Rupture manuelle**.
6. **CSV** : `time_ms,force_N,force_kg,event` (point décimal, virgule séparateur).
7. Si F → 4000 N : événement `limit` + relais STOP.

Pas de colonne déplacement : il n’y a pas de capteur de course sur l’ESP32 (le TVM a son afficheur de traverse, non relié).

---

## 6. Séance fatigue

1. Régler le **mode Repeat** et les butées **sur le TVM**. C’est le bâti qui fait les allers-retours.
2. Onglet **Fatigue** → Tare → **Démarrer comptage**.
3. L’ESP32 compte un cycle à chaque **pic puis creux** de force (hystérésis 25 N, amplitude min. 40 N).
4. Affichage : **N cycles**, **Fmin / Fmax du dernier cycle**.
5. CSV : `cycle,t_ms,Fmin_N,Fmax_N`.

---

## 7. Calib / limites

1. Tare à vide.
2. Poser une **masse connue** (ou un poids étalon en traction, selon le montage) → indiquer la masse en kg → **Étalonner**.
3. Limite logicielle : 4000 N par défaut, max 4899 N.
4. Unités N ou kgf (1 kgf = 9,80665 N).

Échelle théorique de départ : ~877 counts HX711 / N (3,3 V, gain 128, 2,003 mV/V). À remplacer par l’étalonnage.

---

## 8. Protocole JSON (firmware = web)

Un objet UTF-8 par message WebSocket. **Pas de champ de déplacement.**

### ESP32 → page (`type: sample`, 10 ou 80 Hz)

```json
{
  "type": "sample",
  "t": 123456,
  "N": 1234.5,
  "kg": 125.87,
  "raw": 412345,
  "evt": "",
  "cyc": 0,
  "fmin": 0.0,
  "fmax": 1234.5
}
```

| Champ | Sens |
|---|---|
| `t` | `millis()` ESP32 (ms) ; en simulation, temps relatif |
| `N` | force en newtons |
| `kg` | `N / 9.80665` |
| `raw` | brut HX711 24 bits signé ; `0` en simulation |
| `evt` | `""` \| `tare` \| `break` \| `peak` \| `cycle` \| `limit` \| `stop` |
| `cyc` | compteur de cycles fatigue |
| `fmin` / `fmax` | N, min du cycle / Fmax traction ou max du cycle |

### ESP32 → page (`type: status`)

```json
{
  "type": "status",
  "ok": true,
  "mode": "traction",
  "sps": 80,
  "scale": 877.0,
  "offset": 12345,
  "limit_N": 4000,
  "hard_N": 4899,
  "unit": "N",
  "cal": true,
  "stop": false,
  "msg": "Prêt"
}
```

`mode` : `traction` \| `fatigue` \| `calib`. `scale` = counts HX711 **par newton**.

### Page → ESP32

```json
{"cmd":"tare"}
{"cmd":"calibrate","ref_kg":5.0}
{"cmd":"calibrate","ref_N":49.03}
{"cmd":"setScale","scale":877.0}
{"cmd":"setLimit","limit_N":4000}
{"cmd":"setMode","mode":"traction"}
{"cmd":"start"}
{"cmd":"stop"}
{"cmd":"reset"}
{"cmd":"setSps","sps":80}
{"cmd":"setUnit","unit":"N"}
{"cmd":"getStatus"}
```

Fichiers jumeaux : `firmware/include/protocol.h` et `web/js/protocol.js`.

---

## 9. Arborescence

```
banc-traction/
  README.md
  firmware/
    platformio.ini
    include/config.h          broches, SSID, limites
    include/protocol.h        contrat JSON
    src/main.cpp              HX711, tare, rupture, fatigue, AP, WS
    data/                     copie de web/ pour LittleFS
  web/
    index.html                PWA française
    css/app.css
    js/protocol.js            même JSON
    js/chart.js               courbe canvas
    js/app.js                 traction / fatigue / calib + simulation
    manifest.json
    sw.js
    icon.svg
    serve.py                  python3 serve.py → http://127.0.0.1:8080/
```

---

## 10. Dépannage express

| Symptôme | Piste |
|---|---|
| Pastille « Simulation » sur le banc | Wi-Fi TVM-TRACTION ? http://192.168.4.1 (pas 192.168.1.x) ? `uploadfs` fait ? |
| Force figée / brute absurde | 3,3 V HX711 ? DT=16 SCK=4 ? Tare à vide puis masse connue |
| Force négative | Inverser A+ / A−, tare, étalonner |
| 80 SPS ignore | Module sans pastille RATE, ou trop bruyant → 10 SPS auto |
| Relais ne coupe pas | Transistor + diode ? `STOP_ACTIVE_HIGH` ? Contact NF bien en série ? |
| Rupture non détectée | Fmax trop bas (&lt; 40 N) ou chute trop douce → rupture manuelle |

Version atelier, pas un cahier des charges. Les constantes se touchent dans `config.h`.

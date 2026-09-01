# Banc de traction — Lycée Antonin Artaud

**BTS Maintenance des systèmes / FabLab**
SAUTER **TVM 5000N230N** (5 kN, mode Repeat) · cellule **CZL301 500 kg** · **ESP32 + HX711 à 3,3 V**
Pas de dynamomètre SAUTER FH · **pas d’extensomètre / pas de capteur de déplacement** sur l’ESP32.

Version **1.1.1** — firmware PlatformIO, application web française, simulation sans carte. Carte EasyEDA : jauge sur **HX711 U1** (DT1 = GPIO 25, SCK1 = GPIO 33).

**Pas de relais de coupure pour l’instant.** Si la force atteint **4000 N**, l’appli affiche une alarme rouge **ARRÊTER LA MACHINE** (STOP au pupitre TVM). L’enregistrement continue.

---

## 1. Sécurité (à coller près du banc)

- Limite **logicielle par défaut : 4000 N**. Plafond dur **&lt; 4900 N** (la cellule fait 500 kg ≈ 4903 N).
- **Sans relais : le logiciel n’arrête pas le vérin.** Au-delà de 4000 N → **STOP sur le pupitre TVM**.
- Surcharge constructeur CZL301 : **120 % FS** (600 kg). Ne pas s’en servir comme consigne.
- Le TVM développe **5 kN** : il peut détruire la cellule 500 kg si on n’arrête pas à temps.
- **HX711 et GPIO ESP32 en 3,3 V uniquement.** Les GPIO ne sont **pas 5 V tolerant**. Si le module HX711 est alimenté en 5 V, DT/SCK envoient 5 V dans l’ESP32 : carte grillée.
- Le firmware **ne commande pas** la traverse. Le mouvement vient du pupitre TVM (manuel ou Repeat).
- Carte **ESP32-WROOM-32 DevKitC-V4** (EasyEDA). Jauge lue sur **U1** uniquement.
- Tare **manuelle à vide** en début de séance (plus de tare automatique au boot).

---

## 2. Câblage (carte EasyEDA, schéma 2020-11-26)

ESP32-WROOM-32 **DevKitC-V4** · deux HX711 **XW-HX711** alimentés en **3,3 V**.

```
U1  XW-HX711  — jauge 3 fils (UTILISÉE par le firmware)
  VCC  →  3V3  ESP32
  GND  →  GND
  SCK  →  SCK1 → GPIO 33
  DT   →  DT1  → GPIO 25
  Connecteur 3 plots + pont R3/R4 1 kΩ (complétion de pont)

U2  XW-HX711  — cellule 4 fils (non lue)
  SCK2 → GPIO 26    DT2 → GPIO 14
  Connecteur 4 plots E+ / A− / A+ / B+
```

Pas de broche RATE sur ces modules : cadence **10 SPS**. GPIO 33 est **SCK1**, pas RATE.

Relais STOP (plus tard, `RELAY_INSTALLED 1`) : GPIO **4** (libre). Ne pas prendre GPIO 26 (SCK2).

Si la force affichée est **négative** en traction après tare : inverser les fils de la jauge sur le connecteur U1, puis retarer et ré-étalonner.

---

## 3. Flasher l’ESP32

Prérequis : [PlatformIO](https://platformio.org/) (CLI ou extension VS Code / Cursor), câble USB, drivers CP2102 ou CH340.

```bash
python sync_web.py                 # Windows : sync-web-to-fs.bat
cd firmware
pio run -e esp32dev -t upload      # programme
pio run -e esp32dev -t uploadfs    # pages web (LittleFS) — obligatoire une fois
pio device monitor -b 115200       # optionnel : IP, tare, cadence
```

`uploadfs` recopie `web/` → `firmware/data/` tout seul (script PlatformIO). Sans LittleFS, l’ESP32 sert une page de secours.

Broches U1 (25 / 33), SSID et relais se changent dans `firmware/include/config.h`. L’échelle, la tare et la limite sont stockées en NVS. **La tare n’est plus faite au boot.** L’échelle 877 counts/N est théorique CZL301 : la jauge U1 **doit** être étalonnée.

---

## 4. Ouvrir l’application

Fiche élève à coller près du banc (QR Wi-Fi + QR appli) : [`docs/fiche-etudiant.pdf`](docs/fiche-etudiant.pdf).  
Étalonnage jauge (enseignant) : [`docs/fiche-enseignant-etalonnage.pdf`](docs/fiche-enseignant-etalonnage.pdf).

### Sur le banc (mesure réelle)

1. Flasher firmware + LittleFS.
2. Téléphone / PC atelier : Wi-Fi **`TVM-TRACTION`** / mot de passe **`traction1`**.
3. Navigateur : **http://192.168.4.1** (portail captif : la page s’ouvre souvent toute seule).
4. Pastille verte « ESP32 192.168.4.1/ws » = WebSocket OK (même port 80, chemin `/ws`).

L’ESP32 est un **point d’accès** : pas besoin du Wi-Fi lycée.

Si le téléphone affiche « Réseau sans Internet » : rester connecté, puis ouvrir Chrome sur http://192.168.4.1.

### Sans ESP32 (démo / préparation de séance)

- Double-clic sur `web/index.html` (simulation immédiate), **ou**
- `python web/serve.py` puis **http://127.0.0.1:8080/** (PWA installable sur localhost).

Le mode simulation rejoue le **même JSON** que le firmware. En Calib : bouton **Simuler F ≥ 4000 N** pour répéter le geste « STOP pupitre » en salle.

---

## 5. Séance traction

1. Monter l’éprouvette dans les pinces (AC 18 + AFM 16 M12/M10, hors de ce dépôt).
2. Onglet **Traction** → **Tare à vide** (mâchoires sans charge utile).
3. Nommer l’éprouvette → **Démarrer**.
4. Lancer la descente **sur le pupitre TVM** (vitesse affichée 10–230 mm/min). L’appli trace **F(t)**, **Fmax**.
5. Si le bandeau rouge **ARRÊTER LA MACHINE** apparaît : **STOP immédiat au pupitre TVM**.
6. À la rupture : détection auto (chute ~22 % sous Fmax) ou bouton **Rupture manuelle**.
7. **CSV** (Excel FR par défaut) : `time_ms;force_N;force_kg;event` — **PNG** pour la courbe.

Pas de colonne déplacement : pas de capteur de course sur l’ESP32.

---

## 6. Séance fatigue

1. Régler le **mode Repeat** et les butées **sur le TVM**. C’est le bâti qui fait les allers-retours.
2. Onglet **Fatigue** → Tare → **Démarrer comptage**.
3. L’ESP32 compte un cycle à chaque **pic puis creux** de force (hystérésis 25 N, amplitude min. 40 N) **seulement pendant l’enregistrement**.
4. Affichage : **N cycles**, **Fmin / Fmax du dernier cycle**.
5. CSV : `cycle;t_ms;Fmin_N;Fmax_N`.

---

## 7. Calib / limites

1. Tare à vide.
2. Poser une **masse connue ≥ 20 kg** (5 kg = 1 % de la cellule, trop juste) → **Étalonner**.
3. Limite logicielle : 4000 N par défaut, max 4899 N. Au-delà : alarme, pas de coupure moteur.
4. Unités N ou kgf (1 kgf = 9,80665 N).
5. Brut HX711 et σ récente affichés pour diagnostiquer polarité / bruit / 3,3 V.

Échelle théorique de départ : ~877 counts HX711 / N (3,3 V, gain 128, 2,003 mV/V). À remplacer par l’étalonnage.

---

## 8. Protocole JSON (firmware = web)

Un objet UTF-8 par message WebSocket **`ws://192.168.4.1/ws`**. **Pas de champ de déplacement.**

### ESP32 → page (`type: sample`)

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
  "fmax": 1234.5,
  "over": false,
  "hx": true
}
```

| Champ | Sens |
|---|---|
| `t` | `millis()` ESP32 (ms) ; en simulation, temps relatif |
| `N` | force en newtons (non filtrée) |
| `kg` | `N / 9.80665` |
| `raw` | brut HX711 24 bits signé ; `0` en simulation |
| `evt` | `""` \| `tare` \| `break` \| `peak` \| `cycle` \| `limit` \| `stop` \| `hxfail` |
| `over` | `\|F\|` ≥ limite en ce moment |
| `hx` | HX711 vivant |

### ESP32 → page (`type: status`)

Champs utiles : `ver`, `relay` (false), `tared`, `run`, `over`, `seen`, `hx_ok`, `raw`, `msg`.

### Page → ESP32

```json
{"cmd":"tare"}
{"cmd":"calibrate","ref_kg":20.0}
{"cmd":"start"}
{"cmd":"stop"}
{"cmd":"reset"}
{"cmd":"break"}
{"cmd":"setLimit","limit_N":4000}
{"cmd":"getStatus"}
```

`stop` arrête **l’enregistrement** seulement (pas de relais). `limit` n’arrête pas l’enregistrement : il faut le pupitre.

Fichiers jumeaux : `firmware/include/protocol.h` et `web/js/protocol.js`.

---

## 9. Arborescence

```
banc-traction/
  README.md
  LICENSE
  docs/fiche-etudiant.pdf                  QR Wi-Fi + appli (élèves)
  docs/fiche-enseignant-etalonnage.pdf     tare, masse connue, échelle
  sync_web.py / sync-web-to-fs.bat / .sh
  firmware/
    platformio.ini
    extra_script.py           copie web/ → data/ avant uploadfs
    include/config.h          U1 DT=25 SCK=33, SSID, RELAY_INSTALLED 0
    include/protocol.h
    src/main.cpp              HX711, tare, rupture, fatigue, AP, WS /ws, captif
    data/                     copie de web/ pour LittleFS
  web/
    index.html
    css/app.css
    js/protocol.js  js/chart.js  js/app.js
    test/fatigue.test.js      node web/test/fatigue.test.js
    serve.py
```

Pour activer un relais plus tard : `RELAY_INSTALLED 1` dans `config.h`, transistor + contact NF, reflash.

---

## 10. Dépannage express

| Symptôme | Piste |
|---|---|
| Pastille « Simulation » sur le banc | Wi-Fi TVM-TRACTION ? http://192.168.4.1 ? `uploadfs` fait ? |
| Téléphone quitte le Wi-Fi | « Rester connecté » malgré l’absence d’Internet ; Chrome (pas Safari mini) |
| Force figée / brute absurde | 3,3 V U1 ? **DT1=GPIO25 SCK1=GPIO33** ? Tare à vide puis masse ≥ 20 kg |
| Force négative | Inverser les fils jauge U1, tare, étalonner |
| 80 SPS ignore | Normal : XW-HX711 sans RATE, 10 SPS |
| Bandeau ARRÊTER LA MACHINE | **STOP pupitre TVM** — pas de relais. L’appli continue d’enregistrer |
| MESURE PERDUE | HX711 / fils DT-SCK / alim 3,3 V — **arrêter le TVM** |
| Rupture non détectée | Fmax trop bas (&lt; 40 N) ou chute trop douce → rupture manuelle |
| Excel : une seule colonne | CSV déjà en Excel FR (`;` et virgule). Sinon Calib → CSV ISO |

Licence MIT. Version atelier, pas un cahier des charges. Les constantes se touchent dans `config.h`.

#!/usr/bin/env python3
"""Fiche A4 : câblage CZL301 500 kg sur la carte ESP32 + 2× HX711."""
from __future__ import print_function
import os

from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "fiche-cablage-czl301.pdf")
LOGO = os.path.join(ROOT, "docs", "logo-artaud.png")

BRAND = (152 / 255.0, 56 / 255.0, 48 / 255.0)
RED = (0.77, 0.09, 0.09)
INK = (44 / 255.0, 28 / 255.0, 24 / 255.0)
MUTED = (0.48, 0.40, 0.36)
CARD = (0.99, 0.97, 0.95)
LINE = (0.88, 0.82, 0.77)
CREAM = (0.965, 0.937, 0.910)
OK = (0.12, 0.42, 0.32)

pdfmetrics.registerFont(TTFont("Ui", r"C:\Windows\Fonts\segoeui.ttf"))
pdfmetrics.registerFont(TTFont("UiB", r"C:\Windows\Fonts\segoeuib.ttf"))


def rounded(c, x, y, w, h, r=7, fill=None, stroke=None, sw=0.8):
    c.saveState()
    p = c.beginPath()
    p.roundRect(x, y, w, h, r)
    if fill:
        c.setFillColorRGB(*fill)
    if stroke:
        c.setStrokeColorRGB(*stroke)
        c.setLineWidth(sw)
    if fill and stroke:
        c.drawPath(p, fill=1, stroke=1)
    elif fill:
        c.drawPath(p, fill=1, stroke=0)
    else:
        c.drawPath(p, fill=0, stroke=1)
    c.restoreState()


def wrap(c, text, fn, size, max_w):
    words = text.split()
    lines, cur = [], ""
    for w in words:
        trial = (cur + " " + w).strip()
        if c.stringWidth(trial, fn, size) <= max_w:
            cur = trial
        else:
            if cur:
                lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def wire_dot(c, x, y, rgb, r=3.2 * mm):
    c.setFillColorRGB(*rgb)
    c.circle(x, y, r, fill=1, stroke=0)
    if rgb[0] + rgb[1] + rgb[2] > 2.2:
        c.setStrokeColorRGB(0.5, 0.5, 0.5)
        c.setLineWidth(0.4)
        c.circle(x, y, r, fill=0, stroke=1)


def draw():
    W, H = A4
    c = canvas.Canvas(OUT, pagesize=A4)
    c.setTitle("Câblage CZL301 500 kg — carte ESP32 HX711")
    c.setAuthor("Lycée Antonin Artaud — BTS MS / FabLab")

    c.setFillColorRGB(*CREAM)
    c.rect(0, 0, W, H, fill=1, stroke=0)

    c.setFillColorRGB(*CREAM)
    c.rect(0, H - 38 * mm, W, 38 * mm, fill=1, stroke=0)
    logo_h = 20 * mm
    logo_w = logo_h * (228.0 / 120.0)
    c.drawImage(LOGO, 14 * mm, H - 30 * mm, width=logo_w, height=logo_h, mask="auto", preserveAspectRatio=True)
    tx = 14 * mm + logo_w + 8 * mm
    rounded(c, W - 52 * mm, H - 13 * mm, 38 * mm, 7 * mm, r=3, fill=BRAND)
    c.setFillColorRGB(1, 1, 1)
    c.setFont("UiB", 8)
    c.drawCentredString(W - 33 * mm, H - 10.6 * mm, "CÂBLAGE")
    c.setFillColorRGB(*BRAND)
    c.setFont("UiB", 15)
    c.drawString(tx, H - 17 * mm, "CZL301 500 kg  →  carte ESP32")
    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 8)
    c.drawString(tx, H - 24 * mm, "Gotronic 32384  ·  pont 4 fils soudé sur U1  ·  DT=GPIO25 SCK=GPIO33  ·  3,3 V")

    stripe_h = 5 * mm
    y0 = H - 38 * mm - stripe_h
    x = 0
    while x < W:
        c.setFillColorRGB(*BRAND)
        c.saveState()
        c.translate(x, y0)
        p = c.beginPath()
        p.moveTo(0, 0)
        p.lineTo(4 * mm, 0)
        p.lineTo(4 * mm + stripe_h, stripe_h)
        p.lineTo(stripe_h, stripe_h)
        p.close()
        c.drawPath(p, fill=1, stroke=0)
        c.restoreState()
        x += 7 * mm

    # Alerte U1 vs U2
    y = H - 52 * mm
    rounded(c, 14 * mm, y - 22 * mm, W - 28 * mm, 24 * mm, r=6, fill=RED)
    c.setFillColorRGB(1, 1, 1)
    c.setFont("UiB", 11)
    c.drawString(18 * mm, y - 6 * mm, "Soudure directe sur U1  —  pas le connecteur 3 plots")
    c.setFont("Ui", 8.5)
    c.drawString(18 * mm, y - 12.5 * mm, "Le connecteur 3 plots de U1 a des résistances 1 kΩ (demi-pont). Un CZL301 est un pont 350 Ω complet :")
    c.drawString(18 * mm, y - 17.5 * mm, "souder rouge/noir/vert/blanc sur E+, E−, A+, A− du HX711. Firmware : DT = GPIO 25, SCK = GPIO 33 (toutes cartes).")

    # Tableau fils
    box_top = y - 28 * mm
    left_w = 118 * mm
    rounded(c, 14 * mm, box_top - 78 * mm, left_w, 78 * mm, r=7, fill=CARD, stroke=LINE)
    c.setFillColorRGB(*INK)
    c.setFont("UiB", 11)
    c.drawString(18 * mm, box_top - 7 * mm, "Correspondance des fils (fiche Gotronic)")

    rows = [
        ("Fil CZL301", "Rôle", "Soudure HX711 U1"),
        ("Rouge", "E+  excitation +", "Pastille E+"),
        ("Noir", "E−  excitation −", "Pastille E− / GND"),
        ("Vert", "A+  signal +", "Pastille A+"),
        ("Blanc", "A−  signal −", "Pastille A−"),
        ("Jaune", "Blindage", "GND (avec le noir)"),
    ]
    colors = {
        "Rouge": (0.85, 0.12, 0.12),
        "Noir": (0.12, 0.12, 0.12),
        "Vert": (0.15, 0.48, 0.28),
        "Blanc": (0.96, 0.96, 0.96),
        "Jaune": (0.90, 0.75, 0.12),
    }
    rh = 9.4 * mm
    ty = box_top - 18 * mm
    for i, (a, b, d) in enumerate(rows):
        yy = ty - i * rh
        if i == 0:
            c.setFillColorRGB(*BRAND)
            c.roundRect(18 * mm, yy - 3.2 * mm, left_w - 8 * mm, rh, 2, fill=1, stroke=0)
            c.setFillColorRGB(1, 1, 1)
            c.setFont("UiB", 8)
            c.drawString(20 * mm, yy, a)
            c.drawString(48 * mm, yy, b)
            c.drawString(88 * mm, yy, d)
            continue
        if i % 2 == 0:
            c.setFillColorRGB(0.96, 0.93, 0.90)
            c.rect(18 * mm, yy - 3.2 * mm, left_w - 8 * mm, rh, fill=1, stroke=0)
        wire_dot(c, 23 * mm, yy + 1.2 * mm, colors[a])
        c.setFillColorRGB(*INK)
        c.setFont("UiB", 8.5)
        c.drawString(28 * mm, yy, a)
        c.setFont("Ui", 8)
        c.setFillColorRGB(*MUTED)
        c.drawString(48 * mm, yy, b)
        c.setFillColorRGB(*INK)
        c.drawString(88 * mm, yy, d)

    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 7)
    c.drawString(18 * mm, box_top - 75 * mm, "Ne pas utiliser B+ / B−. Ne pas passer par le connecteur 3 plots (R3/R4).")

    # Schéma connecteur
    rx = 14 * mm + left_w + 6 * mm
    rw = W - 14 * mm - rx
    rounded(c, rx, box_top - 78 * mm, rw, 78 * mm, r=7, fill=CARD, stroke=LINE)
    c.setFillColorRGB(*INK)
    c.setFont("UiB", 11)
    c.drawString(rx + 4 * mm, box_top - 7 * mm, "ESP32 — toutes cartes")
    c.setFont("Ui", 7.5)
    c.setFillColorRGB(*MUTED)
    c.drawString(rx + 4 * mm, box_top - 12 * mm, "Même GPIO, n'importe quel DevKit")

    pins = [
        ("DT", "GPIO 25", "DOUT", (0.85, 0.12, 0.12)),
        ("SCK", "GPIO 33", "horloge", (0.15, 0.48, 0.28)),
        ("VCC", "3V3", "pas 5 V", (0.90, 0.75, 0.12)),
        ("GND", "GND", "commun", (0.12, 0.12, 0.12)),
    ]
    px = rx + rw / 2 - 7 * mm
    py = box_top - 22 * mm
    c.setStrokeColorRGB(*INK)
    c.setLineWidth(1.2)
    c.roundRect(px - 4 * mm, py - 44 * mm, 22 * mm, 46 * mm, 2, fill=0, stroke=1)
    for i, (n, net, fil, col) in enumerate(pins):
        yy = py - 6 * mm - i * 10 * mm
        c.setFillColorRGB(*col)
        c.circle(px + 7 * mm, yy, 2.4 * mm, fill=1, stroke=0)
        if col[0] > 0.8 and col[1] > 0.8:
            c.setStrokeColorRGB(0.4, 0.4, 0.4)
            c.setLineWidth(0.4)
            c.circle(px + 7 * mm, yy, 2.4 * mm, fill=0, stroke=1)
        c.setFillColorRGB(*INK)
        c.setFont("UiB", 8)
        c.drawRightString(px + 2 * mm, yy - 1.2 * mm, n)
        c.setFont("Ui", 7.5)
        c.drawString(px + 12 * mm, yy + 1.2 * mm, net)
        c.setFillColorRGB(*MUTED)
        c.drawString(px + 12 * mm, yy - 2.4 * mm, fil)

    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 7)
    for i, line in enumerate(wrap(c, "DT et SCK du HX711 U1 vers GPIO 25 et 33. VCC du module en 3,3 V uniquement.", "Ui", 7, rw - 10 * mm)):
        c.drawString(rx + 4 * mm, box_top - 72 * mm - i * 3.2 * mm, line)

    # 3,3 V + méca
    y2 = box_top - 84 * mm
    rounded(c, 14 * mm, y2 - 32 * mm, W - 28 * mm, 32 * mm, r=7, fill=(1.0, 0.95, 0.90), stroke=(0.75, 0.35, 0.28))
    c.setFillColorRGB(*RED)
    c.setFont("UiB", 10)
    c.drawString(18 * mm, y2 - 6 * mm, "Alimentation 3,3 V  —  ne pas mettre le rouge sur le 5 V de l'ESP32")
    c.setFillColorRGB(*INK)
    c.setFont("Ui", 8)
    notes = [
        "Sur cette carte, U2 est alimenté en 3,3 V. L'excitation E+ sort du HX711 à 3,3 V (pas 5 V comme la fiche Phidgets). C'est volontaire : les GPIO ESP32 ne supportent pas 5 V.",
        "Ne jamais alimenter le HX711 en 5 V : DT/SCK enverraient 5 V dans l'ESP32. 3,3 V suffit (échelle théorique ~877 counts/N). Surcharge cellule 120 % = 600 kg : limite logicielle 4000 N.",
    ]
    ny = y2 - 12 * mm
    for t in notes:
        for line in wrap(c, t, "Ui", 8, W - 40 * mm):
            c.drawString(18 * mm, ny, line)
            ny -= 3.5 * mm
        ny -= 1.2 * mm

    # Après câblage
    y3 = y2 - 38 * mm
    rounded(c, 14 * mm, 14 * mm, W - 28 * mm, y3 - 14 * mm, r=7, fill=CARD, stroke=LINE)
    c.setFillColorRGB(*INK)
    c.setFont("UiB", 11)
    c.drawString(18 * mm, y3 - 7 * mm, "Après câblage")
    steps = [
        ("1", "Reflasher firmware + LittleFS v1.2.4 (DT=25, SCK=33). Ces GPIO restent les mêmes sur toutes les cartes ESP32."),
        ("2", "Wi-Fi TVM-TRACTION → http://192.168.4.1 → Calib. Le brut HX711 doit bouger en appuyant légèrement sur le S."),
        ("3", "Tare à vide, puis masse connue en TRACTION (tirer les deux œillets M12). Étalonner. Force négative : Inverser le sens, ou échanger vert et blanc, retarer, ré-étalonner."),
        ("4", "Mécanique : charge dans l'axe du S, pas de moment de flexion. Surcharge max 600 kg. STOP pupitre TVM si F ≥ 4000 N (pas de relais)."),
    ]
    sy = y3 - 14 * mm
    for n, t in steps:
        c.setFillColorRGB(*BRAND)
        c.circle(21 * mm, sy + 1.2 * mm, 3 * mm, fill=1, stroke=0)
        c.setFillColorRGB(1, 1, 1)
        c.setFont("UiB", 8)
        c.drawCentredString(21 * mm, sy - 0.2 * mm, n)
        c.setFillColorRGB(*MUTED)
        c.setFont("Ui", 8)
        for line in wrap(c, t, "Ui", 8, W - 48 * mm):
            c.drawString(27 * mm, sy, line)
            sy -= 3.4 * mm
        sy -= 3.4 * mm

    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 7)
    c.drawString(16 * mm, 8 * mm, "Sources : Gotronic 32384 / pj-1081.pdf  ·  schéma EasyEDA 2020-11-26  ·  pont 350 Ω, 2,003 mV/V")
    c.drawRightString(W - 16 * mm, 8 * mm, "À coller près du banc")

    c.showPage()
    c.save()
    print("Ecrit :", OUT)


if __name__ == "__main__":
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    draw()

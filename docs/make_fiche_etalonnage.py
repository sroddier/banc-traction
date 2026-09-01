#!/usr/bin/env python3
"""Fiche A4 enseignant : étalonnage de la jauge. python docs/make_fiche_etalonnage.py"""
from __future__ import print_function
import os

from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "fiche-enseignant-etalonnage.pdf")

BG = (0x12 / 255.0, 0x15 / 255.0, 0x1C / 255.0)
AMBER = (1.0, 0xB0 / 255.0, 0x20 / 255.0)
RED = (0x9B / 255.0, 0x12 / 255.0, 0x12 / 255.0)
INK = (0.12, 0.14, 0.18)
MUTED = (0.35, 0.40, 0.48)
OK = (0.12, 0.42, 0.32)
CARD = (0.97, 0.97, 0.98)
LINE = (0.82, 0.84, 0.88)
G = 9.80665


def font(name, path):
    pdfmetrics.registerFont(TTFont(name, path))


font("Ui", r"C:\Windows\Fonts\segoeui.ttf")
font("UiB", r"C:\Windows\Fonts\segoeuib.ttf")


def rounded(c, x, y, w, h, r=8, fill=None, stroke=None, sw=0.8):
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


def wrap(c, text, font_name, size, max_w):
    words = text.split()
    lines, cur = [], ""
    for w in words:
        trial = (cur + " " + w).strip()
        if c.stringWidth(trial, font_name, size) <= max_w:
            cur = trial
        else:
            if cur:
                lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def bullet_num(c, x, y, n, title, body, width, fill_rgb=None):
    fill_rgb = fill_rgb or AMBER
    c.setFillColorRGB(*fill_rgb)
    c.circle(x + 3.2 * mm, y + 1.4 * mm, 3.3 * mm, fill=1, stroke=0)
    c.setFillColorRGB(*BG)
    c.setFont("UiB", 8.5)
    c.drawCentredString(x + 3.2 * mm, y, str(n))
    c.setFillColorRGB(*INK)
    c.setFont("UiB", 10)
    c.drawString(x + 9 * mm, y, title)
    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 8)
    by = y - 4.8 * mm
    last = by
    for line in wrap(c, body, "Ui", 8, width - 11 * mm):
        c.drawString(x + 9 * mm, by, line)
        last = by
        by -= 3.4 * mm
    return last


def draw():
    W, H = A4
    c = canvas.Canvas(OUT, pagesize=A4)
    c.setTitle("Banc de traction TVM — Étalonnage jauge (enseignant)")
    c.setAuthor("Lycée Antonin Artaud — BTS MS / FabLab")

    c.setFillColorRGB(1, 1, 1)
    c.rect(0, 0, W, H, fill=1, stroke=0)

    c.setFillColorRGB(*BG)
    c.rect(0, H - 40 * mm, W, 40 * mm, fill=1, stroke=0)
    stripe_h = 5.5 * mm
    y0 = H - 40 * mm - stripe_h
    x = 0
    step = 7 * mm
    while x < W:
        c.setFillColorRGB(*AMBER)
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
        x += step

    c.setFillColorRGB(*AMBER)
    c.setFont("UiB", 10)
    c.drawString(16 * mm, H - 9 * mm, "Lycée Antonin Artaud  ·  BTS MS / FabLab")
    c.setFillColorRGB(1, 0.85, 0.45)
    rounded(c, W - 48 * mm, H - 13.5 * mm, 32 * mm, 7 * mm, r=3, fill=AMBER)
    c.setFillColorRGB(*BG)
    c.setFont("UiB", 8)
    c.drawCentredString(W - 32 * mm, H - 11.2 * mm, "ENSEIGNANT")
    c.setFillColorRGB(1, 1, 1)
    c.setFont("UiB", 18)
    c.drawString(16 * mm, H - 19.5 * mm, "Étalonnage de la jauge de contrainte")
    c.setFillColorRGB(0.75, 0.80, 0.88)
    c.setFont("Ui", 9.5)
    c.drawString(
        16 * mm,
        H - 27.5 * mm,
        "HX711 U1  ·  DT1 = GPIO 25  ·  SCK1 = GPIO 33  ·  3,3 V  ·  pont R3/R4 1 kΩ  ·  10 SPS",
    )

    # Principe
    y = H - 54 * mm
    rounded(c, 14 * mm, y - 26 * mm, W - 28 * mm, 28 * mm, r=6, fill=CARD, stroke=LINE)
    c.setFillColorRGB(*INK)
    c.setFont("UiB", 11)
    c.drawString(18 * mm, y - 5 * mm, "Principe  —  ce que calcule le firmware")
    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 8.5)
    c.drawString(
        18 * mm,
        y - 11.5 * mm,
        "Après tare, l'offset (brut à vide) est mémorisé. L'étalonnage fixe l'échelle en counts HX711 par newton :",
    )
    c.setFillColorRGB(*INK)
    c.setFont("UiB", 10.5)
    c.drawString(18 * mm, y - 18.5 * mm, "F (N)  =  (brut  −  offset)  /  échelle     avec     1 kg  =  9,80665 N")
    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 8)
    c.drawString(
        18 * mm,
        y - 24 * mm,
        "L'échelle 877 counts/N est théorique CZL301 4 fils : elle est FAUSSE pour cette jauge U1. Il faut étalonner.",
    )

    # Colonne gauche : procédure
    left_x = 14 * mm
    col_w = 112 * mm
    box_top = y - 30 * mm
    box_bot = 42 * mm
    rounded(c, left_x, box_bot, col_w, box_top - box_bot, r=7, fill=CARD, stroke=LINE)
    c.setFillColorRGB(*INK)
    c.setFont("UiB", 12)
    c.drawString(left_x + 5 * mm, box_top - 8 * mm, "Procédure (banc arrêté)")

    steps = [
        (
            "Connexion",
            "Wi-Fi TVM-TRACTION / traction1 puis http://192.168.4.1. Pastille verte ESP32. Onglet Calib / Limites. Le TVM reste à l'arrêt.",
        ),
        (
            "Contrôle brut",
            "Le brut HX711 doit bouger si on appuie légèrement sur la jauge. Ecart-type à vide : quelques newtons max. Brut figé = alim 3,3 V, DT=25 ou SCK=33.",
        ),
        (
            "Tare à vide",
            "Pinces / montage sans charge utile. Tare à vide. La force doit passer près de 0 N. Sans tare, l'étalonnage est refusé ou faux.",
        ),
        (
            "Masse connue",
            "Suspendre la masse dans le MÊME sens que la traction (traction, pas compression). Le plus lourd possible, très en dessous de 4000 N. 20 kg (196 N) est un bon minimum.",
        ),
        (
            "Étalonner",
            "Saisir la masse en kg (ex. 20) → Étalonner. Message « Étalonnage OK ». L'échelle est écrite en mémoire NVS (elle survit au reset).",
        ),
        (
            "Vérifier",
            "L'affichage doit coller à la masse posée (N ou kgf). Enlever la masse : ~0 N. Reposer : même valeur. Noter date, masse et échelle dans le cahier d'atelier.",
        ),
    ]
    sy = box_top - 16 * mm
    for i, (t, b) in enumerate(steps):
        last = bullet_num(c, left_x + 4 * mm, sy, i + 1, t, b, col_w - 10 * mm)
        sy = last - 5.4 * mm

    # Cahier d'atelier (champs à remplir)
    log_h = 32 * mm
    log_y = box_bot + 4 * mm
    rounded(c, left_x + 4 * mm, log_y, col_w - 8 * mm, log_h, r=5, fill=(1, 1, 1), stroke=LINE)
    c.setFillColorRGB(*INK)
    c.setFont("UiB", 9)
    c.drawString(left_x + 7 * mm, log_y + log_h - 6 * mm, "Cahier d'atelier  (à compléter)")
    c.setFont("Ui", 8)
    c.setFillColorRGB(*MUTED)
    fields = [
        "Date : .....................    Masse : ............. kg    g = 9,80665 N/kg",
        "Brut à vide (après tare) : .....................    Brut chargé : .....................",
        "Échelle affichée : ..................... counts/N     Force affichée : ............. N",
        "Contrôle 2e masse : ............. kg  →  affiché ............. N  (attendu ............. N)",
    ]
    fy = log_y + log_h - 12 * mm
    for t in fields:
        c.drawString(left_x + 7 * mm, fy, t)
        fy -= 5.6 * mm

    # Colonne droite
    rx = 14 * mm + col_w + 6 * mm
    rw = W - 14 * mm - rx

    # Tableau masses
    th = 48 * mm
    ty = box_top - th
    rounded(c, rx, ty, rw, th, r=7, fill=CARD, stroke=LINE)
    c.setFillColorRGB(*INK)
    c.setFont("UiB", 11)
    c.drawString(rx + 4 * mm, box_top - 7 * mm, "Force attendue  F = m × g")
    rows = [
        ("Masse", "Force"),
        ("5 kg", "49,0 N"),
        ("10 kg", "98,1 N"),
        ("20 kg", "196,1 N"),
        ("50 kg", "490,3 N"),
    ]
    rh = 6.4 * mm
    table_top = box_top - 12 * mm
    for i, (a, b) in enumerate(rows):
        yy = table_top - i * rh
        if i == 0:
            c.setFillColorRGB(*BG)
            c.roundRect(rx + 4 * mm, yy - 1.6 * mm, rw - 8 * mm, rh, 2, fill=1, stroke=0)
            c.setFillColorRGB(1, 1, 1)
            c.setFont("UiB", 8)
        else:
            if i % 2 == 0:
                c.setFillColorRGB(0.93, 0.94, 0.96)
                c.rect(rx + 4 * mm, yy - 1.6 * mm, rw - 8 * mm, rh, fill=1, stroke=0)
            c.setFillColorRGB(*INK)
            c.setFont("Ui", 8.5)
        c.drawString(rx + 7 * mm, yy, a)
        c.drawRightString(rx + rw - 7 * mm, yy, b)
    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 7)
    c.drawString(rx + 4 * mm, ty + 3.5 * mm, "5 kg = 1 % d'une cellule 500 kg : trop juste.")

    # Polarité / sécu
    sy2 = ty - 6 * mm
    sh = 38 * mm
    rounded(c, rx, sy2 - sh, rw, sh, r=7, fill=(1.0, 0.95, 0.92), stroke=(0.75, 0.35, 0.28))
    c.setFillColorRGB(*RED)
    c.setFont("UiB", 10)
    c.drawString(rx + 4 * mm, sy2 - 6 * mm, "Polarité et sécurité")
    c.setFillColorRGB(*INK)
    c.setFont("Ui", 8)
    notes = [
        "Force négative en traction : inverser les fils jauge sur U1, retarer, ré-étalonner.",
        "Ne pas lancer le TVM pendant l'étalonnage. Pas de choc sur la jauge.",
        "Pas de relais : F ≥ 4000 N → STOP pupitre. Limite 4000 N (max 4899 N).",
        "U2 (4 fils) n'est pas lue. Relais éventuel : GPIO 4, jamais GPIO 26.",
    ]
    ny = sy2 - 12 * mm
    for t in notes:
        for line in wrap(c, t, "Ui", 8, rw - 10 * mm):
            c.drawString(rx + 4 * mm, ny, line)
            ny -= 3.5 * mm
        ny -= 1.2 * mm

    # Dépannage
    dy = sy2 - sh - 6 * mm
    rounded(c, rx, box_bot, rw, dy - box_bot, r=7, fill=CARD, stroke=LINE)
    c.setFillColorRGB(*INK)
    c.setFont("UiB", 11)
    c.drawString(rx + 4 * mm, dy - 7 * mm, "Si « Étalonnage échoué »")
    fails = [
        "Tare oubliée, ou masse encore posée pendant la tare.",
        "Masse trop faible : le firmware exige un écart brut > 50 counts.",
        "Sens inverse (force négative) : inverser U1 puis recommencer.",
        "HX711 muet : 3,3 V, DT=25, SCK=33, jauge sur le connecteur 3 plots de U1.",
        "Échelle hors 50–50000 counts/N : masse ou tare incohérente.",
    ]
    fy = dy - 13 * mm
    c.setFont("Ui", 8)
    c.setFillColorRGB(*MUTED)
    for t in fails:
        c.setFillColorRGB(*AMBER)
        c.circle(rx + 6 * mm, fy + 1.2 * mm, 1.1 * mm, fill=1, stroke=0)
        c.setFillColorRGB(*MUTED)
        for line in wrap(c, t, "Ui", 8, rw - 14 * mm):
            c.drawString(rx + 10 * mm, fy, line)
            fy -= 3.4 * mm
        fy -= 1.6 * mm

    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 7)
    c.drawString(
        16 * mm,
        8 * mm,
        "Échelle conservée en NVS après extinction. Recalibrer si on démonte la jauge, si la polarité change, ou si le zéro dérive.  Fiche enseignant — pas d'autonomie élève.",
    )

    c.showPage()
    c.save()
    print("Ecrit :", OUT)


if __name__ == "__main__":
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    draw()

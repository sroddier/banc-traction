#!/usr/bin/env python3
"""Fiche A4 étudiant : QR Wi-Fi + QR appli. python docs/make_fiche_etudiant.py"""
from __future__ import print_function
import os
from io import BytesIO

import qrcode
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "fiche-etudiant.pdf")

SSID = "TVM-TRACTION"
PASS = "traction1"
APP_URL = "http://192.168.4.1"
WIFI_QR = "WIFI:T:WPA;S:%s;P:%s;H:false;;" % (SSID, PASS)

BRAND = (152 / 255.0, 56 / 255.0, 48 / 255.0)
BRAND_DARK = (110 / 255.0, 36 / 255.0, 28 / 255.0)
BG = (0.12, 0.11, 0.10)
AMBER = BRAND
RED = (0xC4 / 255.0, 0x16 / 255.0, 0x16 / 255.0)
INK = (44 / 255.0, 28 / 255.0, 24 / 255.0)
MUTED = (0.48, 0.40, 0.36)
OK = (0.18, 0.42, 0.31)
CARD = (0.99, 0.97, 0.95)
CREAM = (0.965, 0.937, 0.910)
LOGO = os.path.join(ROOT, "docs", "logo-artaud.png")


def font(name, path):
    pdfmetrics.registerFont(TTFont(name, path))


font("Ui", r"C:\Windows\Fonts\segoeui.ttf")
font("UiB", r"C:\Windows\Fonts\segoeuib.ttf")
font("UiL", r"C:\Windows\Fonts\segoeuil.ttf")


def qr_image(data, box=12):
    qr = qrcode.QRCode(
        version=None,
        error_correction=qrcode.constants.ERROR_CORRECT_M,
        box_size=box,
        border=2,
    )
    qr.add_data(data)
    qr.make(fit=True)
    img = qr.make_image(fill_color="black", back_color="white").convert("RGB")
    buf = BytesIO()
    img.save(buf, format="PNG")
    buf.seek(0)
    return ImageReader(buf)


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


def draw():
    W, H = A4
    c = canvas.Canvas(OUT, pagesize=A4)
    c.setTitle("Banc de traction TVM — Fiche étudiant")
    c.setAuthor("Lycée Antonin Artaud — BTS MS / FabLab")

    # Fond
    c.setFillColorRGB(*CREAM)
    c.rect(0, 0, W, H, fill=1, stroke=0)

    # En-tête charte (papier + logo)
    c.setFillColorRGB(*CREAM)
    c.rect(0, H - 40 * mm, W, 40 * mm, fill=1, stroke=0)
    logo_h = 22 * mm
    logo_w = logo_h * (228.0 / 120.0)
    c.drawImage(LOGO, 14 * mm, H - 32 * mm, width=logo_w, height=logo_h, mask="auto", preserveAspectRatio=True)
    tx = 14 * mm + logo_w + 8 * mm
    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 9)
    c.drawString(tx, H - 12 * mm, "BTS MS  /  FabLab  ·  v1.2.0")
    c.setFillColorRGB(*BRAND)
    c.setFont("UiB", 18)
    c.drawString(tx, H - 20 * mm, "Banc de traction — fiche étudiant")
    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 9)
    c.drawString(tx, H - 27 * mm, "SAUTER TVM 5000N230N  ·  l'appli mesure la force, elle ne commande pas le vérin")
    stripe_h = 6 * mm
    y0 = H - 40 * mm - stripe_h
    step = 7 * mm
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
        x += step

    # Alerte sécurité
    y = H - 58 * mm
    rounded(c, 14 * mm, y - 28 * mm, W - 28 * mm, 30 * mm, r=6, fill=RED)
    c.setFillColorRGB(1, 1, 1)
    c.setFont("UiB", 13)
    c.drawString(18 * mm, y - 4 * mm, "SÉCURITÉ  —  pas de relais de coupure")
    c.setFont("Ui", 10)
    lines = [
        "Si un bandeau rouge « ARRÊTER LA MACHINE » s'affiche : STOP immédiat sur le pupitre TVM.",
        "Limite 4000 N. Cette page n'arrête pas le moteur. Le mouvement se commande uniquement au pupitre.",
    ]
    yy = y - 12 * mm
    for line in lines:
        c.drawString(18 * mm, yy, line)
        yy -= 5.2 * mm

    # Deux cartes QR
    card_w = (W - 28 * mm - 8 * mm) / 2
    card_h = 92 * mm
    card_y = y - 28 * mm - 8 * mm - card_h
    qr_wifi = qr_image(WIFI_QR)
    qr_app = qr_image(APP_URL)
    qr_size = 48 * mm

    def qr_card(x, title, step, img, lines_txt, accent):
        rounded(c, x, card_y, card_w, card_h, r=8, fill=CARD, stroke=(0.82, 0.84, 0.88), sw=1)
        c.setFillColorRGB(*accent)
        c.circle(x + 8 * mm, card_y + card_h - 8 * mm, 4.2 * mm, fill=1, stroke=0)
        c.setFillColorRGB(1, 1, 1)
        c.setFont("UiB", 11)
        c.drawCentredString(x + 8 * mm, card_y + card_h - 9.6 * mm, step)
        c.setFillColorRGB(*INK)
        c.setFont("UiB", 13)
        c.drawString(x + 15 * mm, card_y + card_h - 10 * mm, title)
        qx = x + (card_w - qr_size) / 2
        qy = card_y + 28 * mm
        c.drawImage(img, qx, qy, qr_size, qr_size, mask="auto")
        c.setFillColorRGB(*MUTED)
        c.setFont("Ui", 8.5)
        ty = card_y + 22 * mm
        for t in lines_txt:
            c.setFont("UiB" if t.startswith("→") or ":" in t[:18] else "Ui", 8.5)
            c.setFillColorRGB(*INK if ":" in t[:22] else MUTED)
            c.drawCentredString(x + card_w / 2, ty, t)
            ty -= 4.2 * mm

    qr_card(
        14 * mm,
        "Joindre le Wi-Fi du banc",
        "1",
        qr_wifi,
        [
            "SSID :  TVM-TRACTION",
            "Mot de passe :  traction1",
            "Le téléphone dira « Pas d'Internet » : rester connecté.",
        ],
        BRAND,
    )
    qr_card(
        14 * mm + card_w + 8 * mm,
        "Ouvrir l'application",
        "2",
        qr_app,
        [
            "http://192.168.4.1",
            "Pastille verte « ESP32 » = connexion OK.",
            "Sinon : bouton Reconnecter ESP32.",
        ],
        (0.15, 0.55, 0.40),
    )

    # Étapes d'utilisation
    box_y = 16 * mm
    box_h = card_y - 8 * mm - box_y
    rounded(c, 14 * mm, box_y, W - 28 * mm, box_h, r=8, fill=CARD, stroke=(0.82, 0.84, 0.88), sw=1)
    c.setFillColorRGB(*INK)
    c.setFont("UiB", 13)
    c.drawString(20 * mm, box_y + box_h - 8 * mm, "Utiliser l'appli")

    steps = [
        ("Tare", "Onglet Traction (ou Fatigue). Pinces vides ou sans charge utile → Tare. La force doit passer près de 0 N."),
        ("Nommer", "Indiquer le nom de l'éprouvette (ex. PLA-01). Il sera dans le nom du fichier CSV."),
        ("Démarrer", "Appuyer sur Démarrer. Puis lancer la descente uniquement sur le pupitre TVM (10–230 mm/min)."),
        ("Pendant l'essai", "L'appli trace F(t) et Fmax. À la rupture : détection auto, ou bouton Rupture manuelle."),
        ("Alarme rouge", "F ≥ 4000 N → STOP immédiat au pupitre TVM. L'enregistrement continue, le moteur pas."),
        ("Fin", "Arrêter l'enreg. → CSV (Excel FR) et/ou PNG. Fatigue : Repeat + butées au TVM, puis Démarrer comptage."),
    ]
    col_gap = 4 * mm
    col_w = (W - 28 * mm - 12 * mm - col_gap) / 2
    x0 = 20 * mm
    y_start = box_y + box_h - 16 * mm
    for i, (title, body) in enumerate(steps):
        col = i % 2
        row = i // 2
        x = x0 + col * (col_w + col_gap)
        y = y_start - row * 22 * mm
        c.setFillColorRGB(*AMBER)
        c.circle(x + 3.2 * mm, y + 1.6 * mm, 3.4 * mm, fill=1, stroke=0)
        c.setFillColorRGB(1, 1, 1)
        c.setFont("UiB", 9)
        c.drawCentredString(x + 3.2 * mm, y + 0.2 * mm, str(i + 3))
        c.setFillColorRGB(*INK)
        c.setFont("UiB", 10)
        c.drawString(x + 9 * mm, y, title)
        c.setFillColorRGB(*MUTED)
        c.setFont("Ui", 8)
        by = y - 5 * mm
        for line in wrap(c, body, "Ui", 8, col_w - 10 * mm):
            c.drawString(x + 9 * mm, by, line)
            by -= 3.5 * mm

    c.setFillColorRGB(*MUTED)
    c.setFont("Ui", 7.5)
    c.drawString(
        16 * mm,
        8 * mm,
        "Calib / limites : uniquement si le professeur le demande.  Ne pas quitter le Wi-Fi TVM-TRACTION pendant l'essai.",
    )
    c.drawRightString(W - 16 * mm, 8 * mm, "À coller près du banc")

    c.showPage()
    c.save()
    print("Ecrit :", OUT)


if __name__ == "__main__":
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    draw()

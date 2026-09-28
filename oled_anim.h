// oled_anim.h — Animations OLED SSD1306 128×64 — NeuralSpeech
// Connexion : SDA→D20  SCL→D21  VCC→3.3V  GND→GND  (adresse I2C : 0x3C)
#pragma once
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

extern Adafruit_SSD1306 oled;

// ─── Helpers internes ────────────────────────────────────────────────────────

static void _label(const char* txt) {
    int16_t x1, y1; uint16_t w, h;
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.getTextBounds(txt, 0, 54, &x1, &y1, &w, &h);
    oled.setCursor((128 - (int16_t)w) / 2, 54);
    oled.print(txt);
}

// Flèche ← : pointe en (tx, my), corps vers la droite
static void _arrowLeft(int tx, int my) {
    oled.fillTriangle(tx, my, tx+26, my-18, tx+26, my+18, SSD1306_WHITE);
    oled.fillRect(tx+26, my-9, 36, 18, SSD1306_WHITE);
}

// Flèche → : pointe en (tx, my), corps vers la gauche
static void _arrowRight(int tx, int my) {
    oled.fillTriangle(tx, my, tx-26, my-18, tx-26, my+18, SSD1306_WHITE);
    oled.fillRect(tx-62, my-9, 36, 18, SSD1306_WHITE);
}

// Flèche ↑ : sommet en (cx, ty), corps vers le bas
static void _arrowUp(int cx, int ty) {
    oled.fillTriangle(cx, ty, cx-16, ty+22, cx+16, ty+22, SSD1306_WHITE);
    oled.fillRect(cx-7, ty+22, 14, 14, SSD1306_WHITE);
}

// ─── GAUCHE — balayage droite → gauche ───────────────────────────────────────
void animGauche(int pct) {
    char buf[14]; snprintf(buf, sizeof(buf), "GAUCHE  %d%%", pct);
    const uint8_t px[6] = {62, 49, 36, 23, 10, 2};
    for (int f = 0; f < 6; f++) {
        oled.clearDisplay();
        _arrowLeft(px[f], 26);
        int lx = px[f] + 64;
        if (lx < 124) {
            int lw = min(16, 127 - lx);
            oled.drawFastHLine(lx,     20, lw, SSD1306_WHITE);
            oled.drawFastHLine(lx + 4, 26, max(0, lw - 4), SSD1306_WHITE);
            oled.drawFastHLine(lx,     32, lw, SSD1306_WHITE);
        }
        _label(buf);
        oled.display();
        delay(45);
    }
    delay(250);
}

// ─── DROITE — balayage gauche → droite ───────────────────────────────────────
void animDroite(int pct) {
    char buf[14]; snprintf(buf, sizeof(buf), "DROITE  %d%%", pct);
    const uint8_t px[6] = {66, 79, 92, 105, 118, 126};
    for (int f = 0; f < 6; f++) {
        oled.clearDisplay();
        _arrowRight(px[f], 26);
        int lx = px[f] - 64;
        if (lx > 4) {
            int lw = min(16, lx);
            oled.drawFastHLine(lx - lw,     20, lw, SSD1306_WHITE);
            oled.drawFastHLine(lx - lw + 4, 26, max(0, lw - 4), SSD1306_WHITE);
            oled.drawFastHLine(lx - lw,     32, lw, SSD1306_WHITE);
        }
        _label(buf);
        oled.display();
        delay(45);
    }
    delay(250);
}

// ─── TETE — flèche rebondissant vers le haut ─────────────────────────────────
void animTete(int pct) {
    char buf[12]; snprintf(buf, sizeof(buf), "TETE  %d%%", pct);
    const uint8_t ty[7] = {17, 11, 5, 1, 0, 5, 11};
    for (int f = 0; f < 7; f++) {
        oled.clearDisplay();
        _arrowUp(64, ty[f]);
        _label(buf);
        oled.display();
        delay(55);
    }
    delay(250);
}

// ─── BRUIT — égaliseur animé ─────────────────────────────────────────────────
// 10 barres, 10px larges, gap 2px, base à y=50
void animBruit(int pct) {
    char buf[12]; snprintf(buf, sizeof(buf), "BRUIT  %d%%", pct);
    static const uint8_t H[6][10] = {
        {20, 32, 14, 42, 10, 36, 24, 12, 40, 18},
        {30, 16, 40, 10, 44, 20, 34, 28, 12, 36},
        {10, 44, 26, 36, 18, 44,  8, 32, 28, 14},
        {36, 12, 44, 28, 22,  8, 42, 14, 38, 24},
        {18, 40, 10, 44, 32, 14, 26, 42,  8, 30},
        {42, 24, 38, 12, 40, 30, 16, 44, 22, 10},
    };
    for (int f = 0; f < 6; f++) {
        oled.clearDisplay();
        for (int b = 0; b < 10; b++)
            oled.fillRect(5 + b * 12, 50 - H[f][b], 10, H[f][b], SSD1306_WHITE);
        _label(buf);
        oled.display();
        delay(70);
    }
    delay(200);
}

// ─── INCERTAIN — "?" avec ondes expansives ───────────────────────────────────
void animIncertain() {
    for (int f = 0; f < 5; f++) {
        oled.clearDisplay();
        int r = f * 10 + 6;
        oled.drawCircle(64, 26, r, SSD1306_WHITE);
        if (r > 12) oled.drawCircle(64, 26, r - 12, SSD1306_WHITE);
        oled.setTextSize(3);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(50, 12);
        oled.print("?");
        _label("INCERTAIN");
        oled.display();
        delay(80);
    }
    delay(200);
}

// ─── ECOUTE (idle) — barres pulsantes en vague ───────────────────────────────
// Appeler avec frame++ toutes les 500 ms depuis loop()
void oledListening(int frame) {
    static const uint8_t PULSE[5][8] = {
        {14, 18, 20, 18, 14, 10,  8, 10},
        {10, 14, 18, 20, 18, 14, 10,  8},
        { 8, 10, 14, 18, 20, 18, 14, 10},
        {10,  8, 10, 14, 18, 20, 18, 14},
        {14, 10,  8, 10, 14, 18, 20, 18},
    };
    int f = frame & 7;
    oled.clearDisplay();
    oled.setTextSize(2);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(28, 6);
    oled.print("ECOUTE");
    for (int b = 0; b < 5; b++)
        oled.fillRect(29 + b * 12, 50 - PULSE[b][f], 10, PULSE[b][f], SSD1306_WHITE);
    oled.display();
}

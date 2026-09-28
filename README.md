# NeuralSpeech : reconnaissance vocale embarquée sur Arduino Due

Un robot à trois servomoteurs (bras droit, bras gauche, tête) qui réagit à des commandes vocales. Toute la chaîne tourne sur l'Arduino Due, sans PC ni cloud : acquisition audio, filtrage, extraction des MFCC et inférence d'un petit réseau de neurones convolutif.

Mots reconnus :

| Classe | Mot | Réaction du robot |
| --- | --- | --- |
| 0 | *bruit* | aucune (bruit de fond ignoré) |
| 1 | **droite** | le bras droit se lève et s'abaisse 4 fois |
| 2 | **gauche** | le bras gauche se lève une fois |
| 3 | **tête** | la tête tourne à gauche, puis à droite, puis revient au centre |

Le mot n'est validé que si la confiance du réseau atteint **85 %**. En dessous, le robot affiche « MOT NON RECONNU (Incertain) » et ne bouge pas.

## Chaîne de traitement

```
Micro (A0)
  │  ADC 12 bits déclenché par timer TC8 à 32 kHz
  ▼
Centrage (-2048) → filtre RIF 25 coefficients (buffer circulaire)
  │
  ▼
Sous-échantillonnage ÷4 → 8 kHz, 1 s = 8000 échantillons
  │
  ▼
Découpage en 61 trames de 256 échantillons (pas de 128)
  │
  ▼
MFCC par trame : pré-accentuation 0,97 → fenêtre de Hamming → FFT
  → spectre de puissance → 26 filtres Mel → log → DCT-II → 13 coefficients
  │
  ▼
Normalisation (moyenne/écart-type du jeu d'entraînement)
  │
  ▼
CNN : Conv1D(8, k=3) → MaxPool → Conv1D(16, k=3) → MaxPool
      → GlobalAvgPool → Dense(16, ReLU) → Dense(4, softmax)
  │
  ▼
Décision (seuil 85 %) → servomoteurs + moniteur série
```

Performances du modèle sur le jeu de test (en-tête de `neural_speech_model_4mots.h`) : **95,8 % de précision**, MSE de 0,025.

## Matériel

- **Arduino Due** (SAM3X8E) obligatoire : le code accède directement aux registres ADC et timer du SAM3X, et `arduinoMFCC.cpp` refuse de compiler sur une autre carte.
- Micro avec préampli sortant sur **A0**, centré autour de VCC/2.
- Bouton poussoir sur **D2** (vers GND, pull-up interne).
- 3 servomoteurs :

| Servo | Broche |
| --- | --- |
| Tête | D9 |
| Bras gauche | D10 |
| Bras droit | D11 |

- Optionnel : écran OLED SSD1306 128×64 en I2C (SDA → D20, SCL → D21, 3,3 V, adresse `0x3C`). Voir [oled_anim.h](#fichiers).

## Installation

1. Installer l'**Arduino IDE** et le support de carte **Arduino SAM Boards (32-bits ARM Cortex-M3)** via le gestionnaire de cartes.
2. Installer les bibliothèques suivantes via le gestionnaire de bibliothèques :
   - **arduinoFFT** (version 2.x, qui fournit l'API template `ArduinoFFT<double>`)
   - **Servo**
   - pour l'OLED seulement : **Adafruit GFX** et **Adafruit SSD1306**
3. Ouvrir `projet_elec.ino`, choisir la carte **Arduino Due (Programming Port)** et téléverser.

## Utilisation

1. Ouvrir le moniteur série à **250000 bauds**.
2. Appuyer sur le bouton : le Due enregistre **1 seconde** d'audio.
3. Prononcer le mot pendant cette seconde.
4. Le moniteur série affiche la probabilité de chaque classe, puis la décision, et le robot exécute le mouvement correspondant.

```
Analyse IA en cours...
===========================
Bruit  : 0.42 %
Droite : 97.81 %
Gauche : 1.20 %
Tete   : 0.57 %
===========================
>>> MOT DETECTE : DROITE <<<
```

## Fichiers

| Fichier | Rôle |
| --- | --- |
| `projet_elec.ino` | Sketch principal : ADC, timer 32 kHz, filtre RIF, sous-échantillonnage, découpage en trames, inférence du CNN et pilotage des servos |
| `arduinoMFCC.h` / `.cpp` | Calcul des MFCC, adapté de [FouedDrz/arduinoMFCC](https://github.com/FouedDrz/arduinoMFCC) (voir ci-dessous) |
| `neural_speech_model_4mots.h` | Poids du CNN à 4 classes et statistiques de normalisation, générés par `train_neural_speech.py` (script d'entraînement absent de ce dossier) |
| `oled_anim.h` | Animations OLED pour chaque mot (flèches, égaliseur pour le bruit, « ? » si incertain, écran « ECOUTE » au repos). **Il n'est pas encore inclus dans le sketch.** |
| `poids_nn.h` | Ancien modèle à 5 classes (Conv + Dense 18560 poids), **plus utilisé** |
| `arduinoMFCC.cpp.bak` | Version de `arduinoMFCC.cpp` avant corrections |
| `rapport_projet.pdf` | Rapport du projet |

### Corrections apportées à la bibliothèque MFCC

Par rapport à la version d'origine (`arduinoMFCC.cpp.bak`), la bibliothèque produit maintenant des MFCC conformes aux conventions standard (HTK/librosa). C'est indispensable pour que les coefficients calculés sur l'Arduino correspondent à ceux utilisés pendant l'entraînement :

- **Pré-accentuation** : coefficient 0,97 au lieu de 0,95, et parcours de la trame à l'envers pour ne pas réinjecter les échantillons déjà modifiés.
- **Banc de filtres Mel** : il couvre 0 Hz → Nyquist (`samplerate / 2`). L'ancienne version commençait à 300 Hz et contenait un facteur `/2` parasite.
- **Spectre** : le banc Mel reçoit le spectre de puissance `|FFT|²` et non plus la magnitude, suivi d'un logarithme népérien (au lieu de `log10`) avec un plancher à 1e-10.
- **DCT** : DCT-II orthonormée, avec le bon dénominateur (26 filtres, et non 13) et la normalisation appliquée.

## Intégrer l'écran OLED

`oled_anim.h` déclare `extern Adafruit_SSD1306 oled;`. Pour l'utiliser :

```cpp
#include <Wire.h>
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
#include "oled_anim.h"

// dans setup()
oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
```

Appeler ensuite `animDroite(pct)`, `animGauche(pct)`, `animTete(pct)`, `animBruit(pct)` ou `animIncertain()` dans `faireInference()`, et `oledListening(frame++)` toutes les 500 ms dans `loop()` pendant l'attente.

## Notes techniques

- La bibliothèque `Servo` du Due occupe les timers TC0 à TC5. L'échantillonnage utilise donc le canal 2 du bloc TC2 (`TC8_Handler`).
- Les tampons du CNN sont déclarés en `static` global pour ne pas saturer la pile (96 Ko de SRAM sur le Due).
- Un réentraînement change `neural_speech_model_4mots.h`. Si le nombre de trames, de MFCC ou de classes change, il faut aussi adapter les dimensions codées en dur dans `faireInference()`.

## Licence

`arduinoMFCC` est distribuée sous licence **GPL v3** (© 2023 Foued Derraz, mise à jour par Raphaël Jeantet).

// --- PARAMÈTRES DU PROJET ---
#define BUTTON_PIN 2
#define RECORD_TIME 1       // 1 seconde d'enregistrement
#define SAMPLE_RATE_IN 32000 // Fréquence d'entrée (32 kHz)
#define SAMPLE_RATE_OUT 8000 // Fréquence de sortie après subsampling (8 kHz)
#define BUFFER_SIZE (RECORD_TIME * SAMPLE_RATE_OUT) // 8000 cases

// --- PARAMÈTRES ET5 ---
#define FRAME_SIZE 256
#define HOP_LENGTH 128

#include "arduinoMFCC.h"
#include "neural_speech_model_4mots.h"

// --- SERVOMOTEURS (pins identiques au sketch d'origine) ---
#include <Servo.h>
Servo brasDroit;   // pour le mot "droite"
Servo brasGauche;  // pour le mot "gauche"
Servo tete;        // pour le mot "tete"

const int brocheBrasDroit  = 11;
const int brocheBrasGauche = 10;
const int brocheTete       = 9;

// Offset pour centrer la tete (a regler si besoin)
const int correctionTete = 30;

// Paramètres : 26 filtres Mel (standard), 13 coefficients (requis ET6), frames de 256, à 8000 Hz
arduinoMFCC mfcc(26, 13, FRAME_SIZE, SAMPLE_RATE_OUT);

// Tableau pour récupérer le résultat (les 13 MFCC de la frame)
float mfcc_result[13];

volatile int16_t audioBuffer[BUFFER_SIZE];
volatile int bufferIndex = 0;
volatile bool isRecording = false;
volatile bool recordFinished = false;

// --- VARIABLES POUR L'IA ---
// 61 frames = (8000 - 256) / 128 + 1 (cf NS_N_FRAMES dans le .h)
float matrice_mfcc[61][13];
int frame_count = 0;

// --- FILTRE RIF ---
#define FILTER_TAPS 25

const int16_t filterCoeffs[FILTER_TAPS] = {
  451, -223, -618, -1139, -1583, -1706, -1298, -261,
  1325, 3200, 4978, 6253, 6716, 6253, 4978, 3200,
  1325, -261, -1298, -1706, -1583, -1139, -618, -223, 451
};

volatile int16_t circularBuffer[FILTER_TAPS];
volatile int circIndex = 0;
volatile int downsampleCounter = 0;

void setup() {
  Serial.begin(250000);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  setupADC();
  setupTimer32kHz();

  // --- INITIALISATION MFCC ---
  mfcc.create_hamming_window();
  mfcc.create_mel_filter_bank();
  mfcc.create_dct_matrix();

  // --- INITIALISATION SERVOS ---
  brasDroit.attach(brocheBrasDroit);
  brasGauche.attach(brocheBrasGauche);
  tete.attach(brocheTete);
  bougerLesTroisServos(0, 0, 0);   // position de repos
  delay(500);
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW && !isRecording && !recordFinished) {
    delay(50);
    bufferIndex = 0;
    isRecording = true;
  }

  if (recordFinished) {
    frame_count = 0;
    computeAndExportMFCC();
    faireInference();
    recordFinished = false;
  }
}

// --- CONFIGURATION ADC (12 bits, Channel 7 -> A0) ---
void setupADC() {
  PMC->PMC_PCER1 |= PMC_PCER1_PID37; // Active l'horloge de l'ADC (PID 37)
  ADC->ADC_CR = ADC_CR_SWRST;        // Reset de l'ADC
  ADC->ADC_MR = ADC_MR_LOWRES_BITS_12 | // Résolution 12 bits
                ADC_MR_PRESCAL(3) |     // Prescaler
                ADC_MR_STARTUP_SUT64;   // Temps de démarrage
  ADC->ADC_CHER = ADC_CHER_CH7;      // Active le Channel 7 (Broche A0)
}

// --- CONFIGURATION TIMER (TC2, Channel 2 à 32 kHz) ---
// NB : la lib Servo (Due) reserve TC0..TC5 _Handler (tout le TC block 0 et 1).
//      On utilise donc le canal 2 du TC block 2 -> handler TC8_Handler (libre).
void setupTimer32kHz() {
  PMC->PMC_PCER1 |= (1 << (35 - 32)); // Active l'horloge de TC2 canal 2 (PID 35, dans PCER1)

  // Calcul de RC pour 32 kHz avec MCK/2 (42 MHz) -> 42000000 / 32000 = 1312.5
  uint32_t rc = 1312;

  TC2->TC_CHANNEL[2].TC_CMR = TC_CMR_TCCLKS_TIMER_CLOCK1 | // Clock = MCK/2
                              TC_CMR_WAVE |                // Mode Waveform
                              TC_CMR_WAVSEL_UP_RC;         // Reset on RC match

  TC2->TC_CHANNEL[2].TC_RC = rc;
  TC2->TC_CHANNEL[2].TC_IER = TC_IER_CPCS; // Interruption sur RC Compare

  NVIC_EnableIRQ(TC8_IRQn); // Active l'interruption (TC block 2 ch2 = TC8_IRQn)

  TC2->TC_CHANNEL[2].TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG; // Démarre le Timer
}

// --- ROUTINE D'INTERRUPTION TIMER (Exécutée 32000 fois par seconde) ---
void TC8_Handler() {
  long dummy = TC2->TC_CHANNEL[2].TC_SR; // Clear le flag d'interruption

  if (isRecording) {
    // 1. Déclenchement manuel de l'ADC et attente de la fin de conversion
    ADC->ADC_CR = ADC_CR_START;
    while (!(ADC->ADC_ISR & ADC_ISR_EOC7));
    int16_t sample = ADC->ADC_CDR[7]; // Lecture de la valeur (0 - 4095)

    // Retrait de la composante continue (centrage autour de 0)
    sample = sample - 2048;

    // 2. Insertion dans le Buffer Circulaire (ET3)
    circularBuffer[circIndex] = sample;

    // 3. Application du Filtre RIF (ET2)
    int32_t sum = 0;
    int tempIndex = circIndex;
    for (int i = 0; i < FILTER_TAPS; i++) {
      sum += (int32_t)circularBuffer[tempIndex] * filterCoeffs[i];
      tempIndex--;
      if (tempIndex < 0) {
        tempIndex = FILTER_TAPS - 1;
      }
    }

    // Avance l'index circulaire
    circIndex++;
    if (circIndex >= FILTER_TAPS) circIndex = 0;

    // Ajustement du gain final
    int16_t filteredSample = (int16_t)(sum >> 15);

    // 4. Sous-échantillonnage par 4 (32 kHz -> 8 kHz)
    downsampleCounter++;
    if (downsampleCounter >= 4) {
      downsampleCounter = 0;
      audioBuffer[bufferIndex] = filteredSample;
      bufferIndex++;

      // Fin de l'enregistrement après 8000 échantillons
      if (bufferIndex >= BUFFER_SIZE) {
        isRecording = false;
        recordFinished = true;
      }
    }
  }
}

// --- FONCTION DE DÉCOUPAGE EN FRAMES ---
void computeAndExportMFCC() {
  int numFrames = (BUFFER_SIZE - FRAME_SIZE) / HOP_LENGTH + 1;
  float currentFrame[FRAME_SIZE];

  for (int i = 0; i < numFrames; i++) {
    int startIndex = i * HOP_LENGTH;
    for (int j = 0; j < FRAME_SIZE; j++) {
      currentFrame[j] = (float)audioBuffer[startIndex + j];
    }

    mfcc.computeWithDCT(currentFrame, mfcc_result);

    if (i < 61) {
      for (int k = 0; k < 13; k++) {
        matrice_mfcc[i][k] = mfcc_result[k];
      }
      frame_count = i + 1;
    }
  }
}

// =====================================================================
//  Inference CNN : Conv1D(8,3) -> Pool -> Conv1D(16,3) -> Pool -> GAP
//                  -> Dense(16,relu) -> Dense(4,softmax)
//  Poids dans neural_speech_model_4mots.h (genere par train_neural_speech.py)
//  Classes (indices) : 0=bruit, 1=droite, 2=gauche, 3=tete
// =====================================================================

// Buffers globaux pour eviter de manger la stack (Arduino Due = 96 KB SRAM)
static float mfcc_norm[NS_N_FRAMES][NS_N_MFCC];   // 61 x 13
static float conv1_out[61][8];
static float pool1_out[30][8];
static float conv2_out[30][16];
static float pool2_out[15][16];
static float gap_out[16];
static float dense1_out[16];
static float dense2_out[NS_N_CLASSES];            // = 4

void faireInference() {
  Serial.println("Analyse IA en cours...");

  // -------- 1. NORMALISATION par coefficient (stats du training) --------
  for (int t = 0; t < NS_N_FRAMES; t++) {
    for (int c = 0; c < NS_N_MFCC; c++) {
      float v = (t < frame_count) ? matrice_mfcc[t][c] : 0.0f;
      mfcc_norm[t][c] = (v - ns_mfcc_mean[c]) / ns_mfcc_std[c];
    }
  }

  // -------- 2. Conv1D #1 (kernel=3, padding=same) + ReLU --------
  for (int t = 0; t < 61; t++) {
    for (int o = 0; o < 8; o++) {
      float s = ns_conv1_b[o];
      for (int k = 0; k < 3; k++) {
        int tt = t + k - 1;
        if (tt < 0 || tt >= 61) continue;
        for (int i = 0; i < 13; i++) {
          s += mfcc_norm[tt][i] * ns_conv1_w[((k * 13) + i) * 8 + o];
        }
      }
      conv1_out[t][o] = (s > 0.0f) ? s : 0.0f;
    }
  }

  // -------- 3. MaxPool1D (pool=2, stride=2) -- (61,8) -> (30,8) --
  for (int t = 0; t < 30; t++) {
    for (int o = 0; o < 8; o++) {
      float a = conv1_out[2 * t][o];
      float b = conv1_out[2 * t + 1][o];
      pool1_out[t][o] = (a > b) ? a : b;
    }
  }

  // -------- 4. Conv1D #2 (kernel=3, padding=same) + ReLU --------
  for (int t = 0; t < 30; t++) {
    for (int o = 0; o < 16; o++) {
      float s = ns_conv2_b[o];
      for (int k = 0; k < 3; k++) {
        int tt = t + k - 1;
        if (tt < 0 || tt >= 30) continue;
        for (int i = 0; i < 8; i++) {
          s += pool1_out[tt][i] * ns_conv2_w[((k * 8) + i) * 16 + o];
        }
      }
      conv2_out[t][o] = (s > 0.0f) ? s : 0.0f;
    }
  }

  // -------- 5. MaxPool1D (pool=2, stride=2) -- (30,16) -> (15,16) --
  for (int t = 0; t < 15; t++) {
    for (int o = 0; o < 16; o++) {
      float a = conv2_out[2 * t][o];
      float b = conv2_out[2 * t + 1][o];
      pool2_out[t][o] = (a > b) ? a : b;
    }
  }

  // -------- 6. GlobalAveragePooling1D : moyenne sur les 15 frames -----
  for (int o = 0; o < 16; o++) {
    float s = 0.0f;
    for (int t = 0; t < 15; t++) s += pool2_out[t][o];
    gap_out[o] = s / 15.0f;
  }

  // -------- 7. Dense (16) + ReLU --------
  for (int o = 0; o < 16; o++) {
    float s = ns_dense1_b[o];
    for (int i = 0; i < 16; i++) {
      s += gap_out[i] * ns_dense1_w[i * 16 + o];
    }
    dense1_out[o] = (s > 0.0f) ? s : 0.0f;
  }

  // -------- 8. Dense (4) + Softmax --------
  float max_logit = -1.0e30f;
  for (int o = 0; o < NS_N_CLASSES; o++) {
    float s = ns_dense2_b[o];
    for (int i = 0; i < 16; i++) {
      s += dense1_out[i] * ns_dense2_w[i * NS_N_CLASSES + o];
    }
    dense2_out[o] = s;
    if (s > max_logit) max_logit = s;
  }
  float exp_sum = 0.0f;
  for (int o = 0; o < NS_N_CLASSES; o++) {
    dense2_out[o] = expf(dense2_out[o] - max_logit);
    exp_sum += dense2_out[o];
  }
  for (int o = 0; o < NS_N_CLASSES; o++) {
    dense2_out[o] = (dense2_out[o] / exp_sum) * 100.0f;
  }

  // -------- 9. AFFICHAGE --------
  Serial.println("===========================");
  Serial.print("Bruit  : "); Serial.print(dense2_out[0]); Serial.println(" %");
  Serial.print("Droite : "); Serial.print(dense2_out[1]); Serial.println(" %");
  Serial.print("Gauche : "); Serial.print(dense2_out[2]); Serial.println(" %");
  Serial.print("Tete   : "); Serial.print(dense2_out[3]); Serial.println(" %");
  Serial.println("===========================");

  // -------- 10. DECISION (seuil 85 %) --------
  int best = 0;
  for (int o = 1; o < NS_N_CLASSES; o++) {
    if (dense2_out[o] > dense2_out[best]) best = o;
  }
  if (dense2_out[best] < 85.0f) {
    Serial.println(">>> MOT NON RECONNU (Incertain) <<<");
    return;
  }
  switch (best) {
    case 0:
      Serial.println(">>> IGNORE : BRUIT DE FOND <<<");
      break;
    case 1:
      Serial.println(">>> MOT DETECTE : DROITE <<<");
      bougerLesTroisServos(90, 0, 0);   // bras droit leve
      delay(100);
      bougerLesTroisServos(0, 0, 0);
      delay(100);
      bougerLesTroisServos(90, 0, 0);
      delay(100);
      bougerLesTroisServos(0, 0, 0);
      delay(100);
      bougerLesTroisServos(90, 0, 0);
      delay(100);
      bougerLesTroisServos(0, 0, 0);
      delay(100);
      bougerLesTroisServos(90, 0, 0);
      delay(100);
      bougerLesTroisServos(0, 0, 0);
      break;
    case 2:
      Serial.println(">>> MOT DETECTE : GAUCHE <<<");
      bougerLesTroisServos(0, 90, 0);   // bras gauche leve
      delay(100);
      bougerLesTroisServos(0, 0, 0);
      break;
    case 3:
      Serial.println(">>> MOT DETECTE : TETE <<<");
      bougerLesTroisServos(0, 0, 45);   // tete a gauche
      delay(100);
      bougerLesTroisServos(0, 0, -45);  // tete a droite
      delay(100);
      bougerLesTroisServos(0, 0, 0);    // retour centre
      break;
  }
}

// =====================================================================
//  Pilotage simultane des 3 servos (repris du sketch d'origine)
//  - bras  : 0 = centre, +/-90 = mouvement
//  - tete  : 0 = centre, + correctionTete pour aligner
// =====================================================================
void bougerLesTroisServos(int angleDroit, int angleGauche, int angleTete) {
  brasDroit.write(angleDroit + 90);
  brasGauche.write(angleGauche + 90);
  tete.write(angleTete + 90 + correctionTete);
}

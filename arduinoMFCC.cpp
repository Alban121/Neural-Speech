/*
	MFCC library
	Copyright (C) 2023 Foued DERRAZ

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

  updated from https://github.com/FouedDrz/arduinoMFCC by Raphaël Jeantet

*/
#include<Arduino.h>
#include<arduinoFFT.h>
#include "arduinoMFCC.h"
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#if !defined(__SAM3X8E__)
#error "Ce code est conçu pour être utilisé uniquement avec Arduino Due. Veuillez utiliser un Arduino Due pour le projet Neurospeech."
#endif

arduinoMFCC::arduinoMFCC(uint8_t mfcc_size,uint8_t dct_mfcc_size, uint16_t frame_size, float samplerate) {
    _mfcc_size = mfcc_size;
    _frame_size = frame_size;
    _dct_mfcc_size = dct_mfcc_size;
    _samplerate = samplerate;

    _frame = (float*)malloc(_frame_size * sizeof(float));

    _hamming_window = (float*)malloc(_frame_size * sizeof(float));

    _mel_filter_bank  = (float **)malloc(_mfcc_size * sizeof(float *));
      for (uint8_t i = 0; i < _mfcc_size; i++) {
        _mel_filter_bank[i] = (float *)malloc((_frame_size/2) * sizeof(float));
    }

    _dct_matrix  = (float **)malloc(_dct_mfcc_size * sizeof(float *));
        for (uint8_t i = 0; i < _dct_mfcc_size; i++) {
             _dct_matrix[i] = (float *)malloc(_mfcc_size * sizeof(float));//
             }      

    _dct_mfcc_coeffs = (float*)malloc(_dct_mfcc_size * sizeof(float)); 
    _mfcc_coeffs = (float*)malloc(_mfcc_size* sizeof(float)); 	
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

arduinoMFCC::~arduinoMFCC() {
    free(_frame);
    free(_hamming_window);
    for (int i = 0; i < _mfcc_size; i++) {
          free(_mel_filter_bank[i]);
          }
    free(_mel_filter_bank);
    //
    for (int i = 0; i < _dct_mfcc_size; i++) {
          free(_dct_matrix[i]);
          }
    free(_dct_matrix);
    //
    free(_dct_mfcc_coeffs);
    free(_mfcc_coeffs);

}
/////////////////////////////////////////////////////////////////////////////
void arduinoMFCC::pre_emphasis()
{
// pre-emphasis (FIX : 0.97 standard, etait 0.95)
// NOTE : appliquer en sens INVERSE car y[j] depend de x[j-1] (sample d'origine)
// pour ne pas reinjecter la sortie deja modifiee.
    for (uint16_t j = _frame_size - 1; j >= 1; j--)
          _frame[j] = _frame[j] - 0.97f * _frame[j-1];
}
/////////////////////////////////////////////////////////////////////////
void arduinoMFCC::compute(float* frame, float* mfcc_coeffs) {
    for(int i = 0; i < _frame_size; i++) {
        _frame[i] = frame[i];
    }
    
    pre_emphasis();
    apply_hamming_window();
    apply_fft();
    apply_mel_filter_bank();
    for(int i = 0; i < _mfcc_size; i++) {
        mfcc_coeffs[i] = _mfcc_coeffs[i];
    }
}

void arduinoMFCC::computeWithDCT(float* frame, float* mfcc_coeffs) {
    for(int i = 0; i < _frame_size; i++) {
        _frame[i] = frame[i];
    }
    
    pre_emphasis();
    apply_hamming_window();
    apply_fft();
    apply_mel_filter_bank();
    apply_dct();

    for(int i = 0; i < _dct_mfcc_size; i++) {
        mfcc_coeffs[i] = _dct_mfcc_coeffs[i];
    }
}
///////////////////////////////////////////////////////////////////////////
// Fonction publique pour créer la fenêtre de Hamming
void arduinoMFCC::create_hamming_window() {
    // ... code pour créer la fenêtre de Hamming ...
	// Fonction publique pour créer la fenêtre de Hamming
    for (uint16_t  i = 0; i < _frame_size; i++) {
        _hamming_window[i] = 0.54 - 0.46 * cos(2 * PI * i / (_frame_size - 1));
    }
}


///////////////////////////////////////////////////////////////////////////
// Fonction publique pour créer la fenêtre de Hanning
void arduinoMFCC::create_hanning_window() {
    // ... code pour créer la fenêtre de Hanning ...
	// Fonction publique pour créer la fenêtre de Hanning
    for (uint16_t  i = 0; i < _frame_size; i++) {
        _hanning_window[i] = 0.5 - 0.5 * cos(2 * PI * i / (_frame_size - 1));
    }
}
///////////////////////////////////////////////////////////////////////////
// Fonction publique pour créer les filtres de Mel
void arduinoMFCC::create_mel_filter_bank()
 {
    // FIX : f_low = 0 (etait 300, ce qui coupait les basses freq vocales).
    // Et calcul mel direct sur la freq, sans le /2. parasite (qui marchait
    // par hasard parce que f_high = samplerate au lieu de samplerate/2).
    float f_low          = 0.f;
    float f_high         = _samplerate / 2.f; // VRAIE freq de Nyquist
    float mel_low_freq   = 2595.f * log10f(1.f + f_low  / 700.f);
    float mel_high_freq  = 2595.f * log10f(1.f + f_high / 700.f);
    float* mel_f         = (float*)malloc((_mfcc_size + 2) * sizeof(float));
    float* hzPoints      = (float*)malloc((_mfcc_size + 2) * sizeof(float)); // Corresponding Hz scale points
  // Calculate Mel and Hz scale points
    float mel_freq_delta = (mel_high_freq - mel_low_freq) / (_mfcc_size + 1);
  for (uint8_t i = 0; i < _mfcc_size + 2; i++) {
    mel_f[i] = mel_low_freq + i * mel_freq_delta;
    hzPoints[i] = 700.0 * (powf(10, mel_f[i] / 2595.0) - 1);
  }
  // Create the filter bank
  for (uint16_t i = 0; i < _mfcc_size; i++) {
    for (uint16_t j = 0; j < _frame_size/2; j++) {
      float freq = (float)j * (_samplerate / 2) / (_frame_size / 2);
      if (freq  < hzPoints[i])
        _mel_filter_bank[i][j] = 0;
      else if ( freq >= hzPoints[i] && freq < hzPoints[i+1])
        _mel_filter_bank[i][j] = (freq - hzPoints[i]) / (hzPoints[i+1] - hzPoints[i]);
      else if ( freq >= hzPoints[i+1] && freq <= hzPoints[i+2])
        _mel_filter_bank[i][j] = (hzPoints[i+2] - freq) / (hzPoints[i+2] - hzPoints[i+1]);
      else
        _mel_filter_bank[i][j] = 0;
    
    }
    
  }
  free(mel_f);
  free(hzPoints);
}
//////////////////////////////////////////////////////////////////////////////////
// Fonction publique pour creer la matrice de transformee de cosinus discrete (DCT)
// FIX MAJEUR : DCT-II orthonormee (HTK/librosa). Avant : denominateur du cos
// etait _dct_mfcc_size (= 13, taille sortie) au lieu de _mfcc_size (= 26,
// taille entree), et la normalisation sqrt_2_over_n etait calculee mais
// jamais appliquee -> les coefficients C1..C12 etaient mathematiquement
// faux et non orthogonaux.
void arduinoMFCC::create_dct_matrix() {
    float norm0 = sqrtf(1.0f / (float)_mfcc_size);
    float normk = sqrtf(2.0f / (float)_mfcc_size);
    for (uint8_t  i = 0; i < _dct_mfcc_size; i++) {
        float c = (i == 0) ? norm0 : normk;
        for (uint8_t  j = 0; j < _mfcc_size; j++) {
            _dct_matrix[i][j] = c * cosf((PI * (float)i * ((float)j + 0.5f)) / (float)_mfcc_size);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////
// Fonction publique pour appliquer la fenêtre de Hamming au signal audio
void arduinoMFCC::apply_hamming_window() {
    for (uint16_t  n = 0; n < _frame_size; n++) {
        _frame[n] = _frame[n] * _hamming_window[n];
    }
}
/////////////////////////////////////////////////////////////////////////////
// Fonction publique pour appliquer la fenêtre de Hamming au signal audio
void arduinoMFCC::apply_hanning_window() {
    for (uint16_t  n = 0; n < _frame_size; n++) {
        _frame[n] = _frame[n] * _hanning_window[n];
    }
}
/////////////////////////////////////////////////////////////////////////////
// Fonction publique pour appliquer les filtres de Mel au signal audio
void arduinoMFCC::apply_mel_filter_bank() {
    // FIX : on doit envoyer le SPECTRE DE PUISSANCE (|FFT|^2) au banc Mel,
    // pas la magnitude. Et le log doit etre NATUREL (logf), pas log10f.
    for (uint16_t  i = 0; i < _mfcc_size; i++) {
        float output = 0.0f;
        for (uint16_t  j = 0; j < _frame_size/2; j++) {
            float power = _frame[j] * _frame[j];          // FIX : |FFT|^2
            output += power * _mel_filter_bank[i][j];
        }
        // plancher anti-log(0) puis log naturel
        if (output < 1e-10f) output = 1e-10f;
        _mfcc_coeffs[i] = logf(output);                   // FIX : ln au lieu de log10
    }
}
/////////////////////////////////////////////////////////////////////////////

void arduinoMFCC::apply_fft() {

  double *_vframe= (double*)malloc(_frame_size * sizeof(double));
   double *_rframe= (double*)malloc(_frame_size * sizeof(double));
  for(uint16_t i=0;i<_frame_size;i++){
    _rframe[i]=_frame[i];
    _vframe[i]=0.0;
  }
  ArduinoFFT<double> myFFTframe= ArduinoFFT<double>(_rframe, _vframe,  _frame_size, (double)_samplerate);
  myFFTframe.dcRemoval();
  myFFTframe.compute(FFT_FORWARD);
  myFFTframe.complexToMagnitude();
  for(uint16_t i=0;i<_frame_size;i++)  _frame[i]=(float)_rframe[i];

  free(_rframe);
  free(_vframe);
}

/////////////////////////////////////////////////////////////////////////////
// Fonction publique pour appliquer la transformée de cosinus discrète (DCT) au signal audio
void arduinoMFCC::apply_dct() {
      for (uint16_t  i = 0; i < _dct_mfcc_size; i++) {
        _dct_mfcc_coeffs[i] = 0.0;
        for (uint8_t  j = 0; j < _mfcc_size; j++) {
            _dct_mfcc_coeffs[i] += _mfcc_coeffs[j] *_dct_matrix[i][j];
        }
    }
}



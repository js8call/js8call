#ifndef COMMONS_H
#define COMMONS_H

// NSPS, the number of samples per second (at a sample rate of 1200
// samples per second) is a constant, chosen so as to be a number
// with no prime factor greater than 7.

#define NSPS  6192
#define NSMAX 6827
#define NTMAX 60

#define RX_SAMPLE_RATE 12000

#define JS8_USE_IHSYM      0       // compute ihsym manually instead of from symspec
#define JS8_RING_BUFFER    1       // use a ring buffer instead of clearing the decode frames
#define JS8_DECODE_THREAD  1       // use a separate thread for decode process handling
#define JS8_ALLOW_EXTENDED 1       // allow extended latin-1 capital charset
#define JS8_AUTO_SYNC      1       // enable the experimental auto sync feature

#ifdef QT_DEBUG
#define JS8_DEBUG_DECODE   0       // emit debug statements for the decode pipeline
#else
#define JS8_DEBUG_DECODE   0
#endif

#define JS8_NUM_SYMBOLS    79
#define JS8_ENABLE_JS8A    1
#define JS8_ENABLE_JS8B    1
#define JS8_ENABLE_JS8C    1
#define JS8_ENABLE_JS8E    1
#define JS8_ENABLE_JS8I    0

#define JS8A_SYMBOL_SAMPLES 1920
#define JS8A_TX_SECONDS     15
#define JS8A_START_DELAY_MS 500

#define JS8B_SYMBOL_SAMPLES 1200
#define JS8B_TX_SECONDS     10
#define JS8B_START_DELAY_MS 200

#define JS8C_SYMBOL_SAMPLES 600
#define JS8C_TX_SECONDS     6
#define JS8C_START_DELAY_MS 100

#define JS8E_SYMBOL_SAMPLES 3840
#define JS8E_TX_SECONDS     30
#define JS8E_START_DELAY_MS 500

#define JS8I_SYMBOL_SAMPLES 384
#define JS8I_TX_SECONDS     4
#define JS8I_START_DELAY_MS 100

#ifdef __cplusplus
#include <cstdbool>
extern "C" {
#else
#include <stdbool.h>
#endif

  /*
   * This structure is shared with Fortran code, it MUST be kept in
   * sync with lib/jt9com.f90
   */
extern struct dec_data {
  float ss[184*NSMAX]; // symbol spectra
  float savg[NSMAX];
  short int d2[NTMAX*RX_SAMPLE_RATE]; // sample frame buffer for sample collection
  struct
  {
    int nutc;                   // UTC as integer, HHMM
    bool ndiskdat;              // true ==> data read from *.wav file
    int nfqso;                  // User-selected QSO freq (kHz)
    bool newdat;                // true ==> new data, must do long FFT
    int npts8;                  // npts for c0() array
    int nfa;                    // Low decode limit (Hz) (filter min)
    int nfb;                    // High decode limit (Hz) (filter max)
    bool syncStats;              // only compute sync candidates
    int kin;                    // number of frames written to d2
    int kposA;                  // starting position of decode for submode A
    int kposB;                  // starting position of decode for submode B
    int kposC;                  // starting position of decode for submode C
    int kposE;                  // starting position of decode for submode E
    int kposI;                  // starting position of decode for submode I
    int kszA;                   // number of frames for decode for submode A
    int kszB;                   // number of frames for decode for submode B
    int kszC;                   // number of frames for decode for submode C
    int kszE;                   // number of frames for decode for submode E
    int kszI;                   // number of frames for decode for submode I
    int nsubmode;               // which submode to decode (-1 if using nsubmodes)
    int nsubmodes;              // which submodes to decode
    bool nagain;
    int ndepth;
    int napwid;
    int nmode;
    int nranera;
    char datetime[20];
    char mycall[12];
  } params;
} dec_data;

extern struct {
  float syellow[NSMAX];
  float ref[3457];
  float filter[3457];
} spectra_;

#ifdef __cplusplus
}
#endif

#endif // COMMONS_H

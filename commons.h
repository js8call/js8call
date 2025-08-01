#ifndef COMMONS_H
#define COMMONS_H

#include <cstdbool>
#include <cstdint>
#include <mutex>

// NSPS, the number of samples per second (at a sample rate of 1200
// samples per second) is a constant, chosen so as to be a number
// with no prime factor greater than 7.

#define JS8_NSPS           6192
#define JS8_NSMAX          6827
#define JS8_NTMAX          60
#define JS8_RX_SAMPLE_RATE 12000
#define JS8_RX_SAMPLE_SIZE (JS8_NTMAX * JS8_RX_SAMPLE_RATE)

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

extern struct dec_data
{
  std::int16_t d2[JS8_RX_SAMPLE_SIZE]; // sample frame buffer for sample collection
  struct
  {
    int nutc;                   // UTC as integer. See code_time() below for details.
    int nfqso;                  // User-selected QSO freq (kHz)
    bool newdat;                // true ==> new data, must do long FFT
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
    int nsubmodes;              // which submodes to decode
  } params;
} dec_data;

extern struct
specData
{
  float savg[JS8_NSMAX];
  float slin[JS8_NSMAX];
}
specData;

extern std::mutex fftw_mutex;

// The way we squeeze a timestamp into an int.
// See also decode_time() below.
inline int code_time(int hour, int minute, int second){
  return hour * 10000 + minute * 100 + second;
}

struct hour_minute_second {
  int hour;
  int minute;
  int second;
};

// Undo code_time().
inline hour_minute_second decode_time(int nutc){
  struct hour_minute_second result;
  result.hour = nutc / 10000;
  result.minute = nutc % 10000 / 100;
  result.second = nutc % 100;
  return result;
}

#endif // COMMONS_H

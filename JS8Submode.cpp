#include "JS8Submode.hpp"
#include "commons.h"
#include "varicode.h"
#include <type_traits>

/******************************************************************************/
// Private Implementation
/******************************************************************************/

namespace JS8::Submode
{
  namespace
  {
    // std::floor doesn't become constexpr until C++23; until then, our own
    // implementation. We only use this for computation of the number of
    // frames needed for a submode, so it doesn't need to be complicated.
    //
    // If we move to a compiler requirement later than C++17, then concepts
    // would make for a nicer syntax than what we've used here.

    template <typename T,
              typename = std::enable_if_t<std::is_floating_point_v<T>>>
    constexpr int floor(T const v)
    {
      auto const i = static_cast<int>(v);
      return v < i ? i - 1 : i;
    }

    // Ensure that our implementation works as expected.

    static_assert(floor(0.0)       == 0);
    static_assert(floor(0.499999)  == 0);
    static_assert(floor(0.5)       == 0);
    static_assert(floor(0.999999)  == 0);
    static_assert(floor(1.0)       == 1);
    static_assert(floor(123.0)     == 123);
    static_assert(floor(123.4)     == 123);
    static_assert(floor(-0.499999) == -1);
    static_assert(floor(-0.5)      == -1);
    static_assert(floor(-0.999999) == -1);
    static_assert(floor(-1.0)      == -1);
    static_assert(floor(-123.0)    == -123);
    static_assert(floor(-123.4)    == -124);

    // Data that describes a JS8 submode. Anything here should be able to be
    // completely determined at compile time, i.e., any instance of this is
    // just constant data.

    class Data
    {
    public:

      // Constructor; in addition to the basics provided by the constructor
      // parameters, we'll determine various convenience constants in order
      // to simplify calling code. These derived values depend only on the
      // JS8_NUM_SYMBOLS and RX_SAMPLE_RATE definitions, and therefore can
      // be entirely computed at compile time.

      constexpr
      Data(const char * const name,
           unsigned int const symbolSamples,
           unsigned int const startDelayMS,
           unsigned int const txSeconds,
           int          const costas,
           int          const rxSNRThreshold,
           int          const rxThreshold = 10)
      : m_name            (name),
        m_symbolSamples   (symbolSamples),
        m_startDelayMS    (startDelayMS),
        m_period          (txSeconds),
        m_costas          (costas),
        m_rxSNRThreshold  (rxSNRThreshold),
        m_rxThreshold     (rxThreshold),
        m_framesForSymbols(   JS8_NUM_SYMBOLS * symbolSamples),
        m_bandwidth       (8 * RX_SAMPLE_RATE / symbolSamples),
        m_framesPerCycle  (    RX_SAMPLE_RATE * txSeconds),
        m_toneSpacing     (    RX_SAMPLE_RATE / (double)symbolSamples),
        m_framesNeeded    (floor(m_framesForSymbols + (0.5 + startDelayMS / 1000.0) * RX_SAMPLE_RATE)),
        m_ratio           (      m_framesForSymbols / (double)RX_SAMPLE_RATE),
        m_txDuration      (      m_ratio                   + startDelayMS / 1000.0)
      {}

      // Inline accessors

      constexpr auto name()             const { return m_name;             }
      constexpr auto symbolSamples()    const { return m_symbolSamples;    }
      constexpr auto startDelayMS()     const { return m_startDelayMS;     }
      constexpr auto period()           const { return m_period;           }
      constexpr auto costas()           const { return m_costas;           }
      constexpr auto rxSNRThreshold()   const { return m_rxSNRThreshold;   }
      constexpr auto rxThreshold()      const { return m_rxThreshold;      }
      constexpr auto framesForSymbols() const { return m_framesForSymbols; }
      constexpr auto bandwidth()        const { return m_bandwidth;        }
      constexpr auto framesPerCycle()   const { return m_framesPerCycle;   }
      constexpr auto framesNeeded()     const { return m_framesNeeded;     }
      constexpr auto ratio()            const { return m_ratio;            }
      constexpr auto toneSpacing()      const { return m_toneSpacing;      }
      constexpr auto txDuration()       const { return m_txDuration;       }

    private:

      // Data members ** ORDER DEPENDENCY **

      const char * m_name;
      unsigned int m_symbolSamples;
      unsigned int m_startDelayMS;
      unsigned int m_period;
      int          m_costas;
      int          m_rxSNRThreshold;
      int          m_rxThreshold;
      int          m_framesForSymbols;
      int          m_bandwidth;
      int          m_framesPerCycle;
      double       m_toneSpacing;
      int          m_framesNeeded;
      double       m_ratio;
      double       m_txDuration;
    };

    // Data for known submodes. Normal mode uses the old Costas Array
    // definition; all other modes use the new one. Note that as of this
    // writing, Ultra is a known, but unused, submode; we handle it here
    // nevertheless, but it's in general disabled in the calling code.

    constexpr Data Normal = {"NORMAL", JS8A_SYMBOL_SAMPLES, JS8A_START_DELAY_MS, JS8A_TX_SECONDS, 1, -24};
    constexpr Data Fast   = {"FAST",   JS8B_SYMBOL_SAMPLES, JS8B_START_DELAY_MS, JS8B_TX_SECONDS, 2, -22, 16};
    constexpr Data Turbo  = {"TURBO",  JS8C_SYMBOL_SAMPLES, JS8C_START_DELAY_MS, JS8C_TX_SECONDS, 2, -20, 32};
    constexpr Data Slow   = {"SLOW",   JS8E_SYMBOL_SAMPLES, JS8E_START_DELAY_MS, JS8E_TX_SECONDS, 2, -28};
    constexpr Data Ultra  = {"ULTRA",  JS8I_SYMBOL_SAMPLES, JS8I_START_DELAY_MS, JS8I_TX_SECONDS, 2, -18, 50};

    // Given a submode, return data for it, or, if we don't have any idea
    // what the caller is talking about, throw.
    //
    // Note that the original code in all cases did its best to just carry
    // on in the event of an invalid submode, e.g., by returning 0, etc.,
    // but that approach will in general lead to things like division by
    // zero in computeCycleForDecode(), below, so either way we're going
    // to end up with a runtime error, and it seems preferable that it's
    // an informative one.
    //
    // Note that the Varicode::SubModeType enum is not dense, so we can't
    // just do direct indexed access here.

    constexpr Data const &
    data(int const submode)
    {
      switch (submode)
      {
        case Varicode::JS8CallNormal: return Normal;
        case Varicode::JS8CallFast:   return Fast;
        case Varicode::JS8CallTurbo:  return Turbo;
        case Varicode::JS8CallSlow:   return Slow;
        case Varicode::JS8CallUltra:  return Ultra;
        default:
        {
          throw error {QObject::tr("Invalid JS8 submode %1").arg(submode)};
        }
      }
    }
  }
}

/******************************************************************************/
// Public Implementation
/******************************************************************************/

namespace JS8::Submode
{
  // Submode name inquiry function; return a translated value, if there is
  // a translated value, otherwise, the untranslated mode name.

  QString
  name(int const submode)
  {
    return QObject::tr(data(submode).name());
  }

  // Basic submode numeric inquiry functions, i.e., parameterized only by
  // the submode, returning constant data.

  unsigned int bandwidth       (int const submode) { return data(submode).bandwidth();        }
  int          costas          (int const submode) { return data(submode).costas();           }
  unsigned int framesPerCycle  (int const submode) { return data(submode).framesPerCycle();   }
  unsigned int framesForSymbols(int const submode) { return data(submode).framesForSymbols(); }
  unsigned int framesNeeded    (int const submode) { return data(submode).framesNeeded();     }
  unsigned int period          (int const submode) { return data(submode).period();           }
  int          rxSNRThreshold  (int const submode) { return data(submode).rxSNRThreshold();   }
  int          rxThreshold     (int const submode) { return data(submode).rxThreshold();      }
  unsigned int startDelayMS    (int const submode) { return data(submode).startDelayMS();     }
  unsigned int symbolSamples   (int const submode) { return data(submode).symbolSamples();    }
  double       toneSpacing     (int const submode) { return data(submode).toneSpacing();      }
  double       txDuration      (int const submode) { return data(submode).txDuration();       }

  // Compute which cycle we are currently in based on submode frames per cycle
  // and current k position.

  int
  computeCycleForDecode(int const submode,
                        int const k)
  {
    int const maxFrames   = NTMAX * RX_SAMPLE_RATE;
    int const cycleFrames = framesPerCycle(submode);

    return (k         / cycleFrames) %  // we mod here so we loop
           (maxFrames / cycleFrames);   // back to zero correctly
  }

  // Compute an alternate cycle offset by a specific number of frames e.g.,
  // if we want the 0 cycle to start at second 5, we'd provide an offset of
  // (5 * RX_SAMPLE_RATE).

  int
  computeAltCycleForDecode(int const submode,
                           int const k,
                           int const offsetFrames)
  {
    int const altK = k - offsetFrames;

    return computeCycleForDecode(submode, altK < 0
                                        ? altK + NTMAX * RX_SAMPLE_RATE
                                        : altK);
  }

  double
  computeRatio(int    const submode,
               double const period)
  {
    return (period - data(submode).ratio()) / period;
  }
}

/******************************************************************************/

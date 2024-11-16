#ifndef JS8_SUBMODE_HPP_
#define JS8_SUBMODE_HPP_

#include <QString>
#include <stdexcept>

namespace JS8::Submode
{
  // Exception thrown on unexpected errors, principally, handing
  // us a submode that we don't understand, which seems like your
  // problem, not ours.

  struct error : public std::runtime_error
  {
    explicit error(QString const & what)
    : std::runtime_error(what.toStdString())
    {}
  };

  // Functions that, when provided with a valid submode, return
  // constant data specific to the submode. Each of those functions
  // will throw if provided with an invalid JS8 submode.

  QString      name(int);
  unsigned int bandwidth(int);
  int          costas(int);
  unsigned int framesPerCycle(int);
  unsigned int framesForSymbols(int);
  unsigned int framesNeeded(int);
  unsigned int period(int);
  int          rxSNRThreshold(int);
  int          rxThreshold(int);
  unsigned int startDelayMS(int);
  unsigned int symbolSamples(int);
  double       toneSpacing(int);
  double       txDuration(int);

  // Functions that, when provided with a valid submode and addtional
  // parametric data, compute and return results specific to the submode.
  // Each of these functions will throw if provided with an invalid JS8
  // submode.

  int    computeCycleForDecode(int, int);
  int    computeAltCycleForDecode(int, int, int);
  double computeRatio(int, double);
}

#endif // JS8_SUBMODE_HPP_

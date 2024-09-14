#include "getfile.h"
#include <random>
#include <QRandomGenerator>

/* Generate gaussian random float with mean=0 and std_dev=1 */
float gran()
{
  static std::normal_distribution<float> d;
  return d (*QRandomGenerator::global ());
}

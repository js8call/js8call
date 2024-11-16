#ifndef W_F_HPP__
#define W_F_HPP__

#include <array>
#include <QMetaType>
#include <QList>
#include <QVector>
#include <QColor>
#include "commons.h"

class QString;

namespace WF
{
  Q_NAMESPACE

  // Spectrum type, defines the types of waterfall spectrum displays.

  enum class Spectrum
  {
    Current,
    Cumulative,
    LinearAvg
  }; Q_ENUM_NS(Spectrum)

  // Maximum width of the screen in pixels.

  static constexpr std::size_t MaxScreenWidth = 2048;

  // Waterfall data storage types.

  using SPlot = std::array<float, NSMAX>;
  using SWide = std::array<float, MaxScreenWidth>;

  //
  // Class Palette
  //
  //	Encapulates  a waterfall palette  description.  A  colour gradient
  //	over 256 intervals is described  by a list of RGB colour triplets.
  //	The list of  colours are use to interpolate  the full 256 interval
  //	waterfall colour gradient.
  //
  // Responsibilities
  //
  //	Construction from  a string which is  a path to  a file containing
  //	colour  descriptions  in  the   form  rrr;ggg;bbb  on  up  to  256
  //	consecutive lines, where rrr, ggg and, bbb are integral numbers in
  //	the range 0<=n<256.
  //
  //	Construction from a list of QColor instances.  Up to the first 256
  //	list elements are used.
  //
  //	Includes a design GUI to create or adjust a Palette.
  //
  class Palette
  {
  public:
    using Colours = QList<QColor>;

    Palette () = default;
    explicit Palette (Colours const&);
    explicit Palette (QString const& file_path);
    Palette (Palette const&) = default;
    Palette& operator = (Palette const&) = default;

    Colours colours () const {return colours_;}

    // interpolate a gradient over 256 steps
    QVector<QColor> interpolate () const;

    // returns true if colours have been modified
    bool design ();

  private:
    Colours colours_;
  };
}

Q_DECLARE_METATYPE (WF::Spectrum);
Q_DECLARE_METATYPE (WF::Palette::Colours);

#endif

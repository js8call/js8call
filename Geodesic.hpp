#include <cmath>
#include <utility>
#include <QString>
#include <QStringView>

namespace Geodesic
{
  // Azimuth class, describes an azimuth in degrees. Created via
  // interpolation of Maidenhead grid coordinates, and as such
  // will be invalid if interpolation failed, typically due to
  // bad coordinates.

  class Azimuth
  {
    // Data members

    float m_value = NAN;

    // Constructors

    Azimuth() = default;
    Azimuth(float const value)
    : m_value {value}
    {}

    // Allow construction only by Vector.

    friend class Vector;

  public:

    // Allow copying, moving, and assignment by anyone.

    Azimuth            (Azimuth const & )          = default;
    Azimuth & operator=(Azimuth const & )          = default;
    Azimuth            (Azimuth       &&) noexcept = default;
    Azimuth & operator=(Azimuth       &&) noexcept = default;

    // Inline Accessors

    auto isValid() const { return !std::isnan(m_value); }

    // Conversion operators; return validity and value. These
    // are all we need to implement an ordering relation, but
    // we do so elsewhere.

    explicit operator  bool () const noexcept { return isValid(); }
             operator float () const noexcept { return m_value;   }

    // Return as a directional arrow and cardinal direction, if
    // valid, an empty string if invalid.

    QStringView compass() const;

    // String conversion, to the nearest whole degree; always
    // succeeds, returning an empty string if invalid. Caller
    // must specify if they want units or just a bare value.

    QString toString(bool units) const;
  };

  // Distance class, describes a distance in kilometers. Created via
  // interpolation of Maidenhead grid coordinates, and as such will
  // be invalid if interpolation failed, typically due to bad coordinates.
  //
  // May additionally be defined as 'close', meaning that either of the
  // grids provided was only 4 characters and the computed distance was
  // short, so we know it's close, but not just how close. In this case,
  // the value will be a non-zero minimum constant, and string conversion
  // will prepend a '<'.
  //
  // While distances are stored internally only in kilometers, caller may
  // request string conversion in terms of statute miles.

  class Distance
  {
    // Value, in kilometers, that we consider to be the close limit, so
    // if we're informed that we should consider the value for closeness,
    // i.e., one of the grids that gave rise to use is of at best square
    // magnitude, we'd consider any value at or under the limit to be of
    // only the limit in terms of precision.

    static constexpr float CLOSE = 120.0f;

    // Data members

    float m_value = NAN;

    // Constructors

    Distance() = default;
    Distance(float const value,
             bool  const close)
    : m_value {close && CLOSE > value ? INFINITY : value}
    {}

    // Allow construction only by Vector.

    friend class Vector;

  public:

    // Allow copying, moving, and assignment by anyone.

    Distance            (Distance const & )          = default;
    Distance & operator=(Distance const & )          = default;
    Distance            (Distance       &&) noexcept = default;
    Distance & operator=(Distance       &&) noexcept = default;

    // Inline Accessors

    auto isValid() const { return !std::isnan(m_value); }
    auto isClose() const { return  std::isinf(m_value); }

    // Conversion operators; return validity and value. These
    // are all we need to implement an ordering relation, but
    // we do so elsewhere.

    explicit operator  bool () const noexcept { return isValid(); }
             operator float () const noexcept { return isClose() ? CLOSE
                                                                 : m_value; }

    // String conversion, to the nearest whole kilometer or mile,
    // always succeeds, returning an empty string if invalid. Caller
    // must specify if they want units or just the bare value.

    QString toString(bool miles,
                     bool units) const;
  };

  // Vector class, aggregate of azimuth and distance from an
  // origin grid to a remote grid.

  class Vector
  {
    // Data members

    Azimuth  m_azimuth;
    Distance m_distance;

    // Constructors; disallow creation without going through
    // the vector() function.

    Vector() = default;
    Vector(std::pair<float, float> const & azdist,
           bool                    const   square)
    : m_azimuth  {azdist.first}
    , m_distance {azdist.second, square}
    {}

    friend Vector vector(QStringView,
                         QStringView);

  public:

    // Allow copying, moving, and assignment by anyone.

    Vector            (Vector const & )          = default;
    Vector & operator=(Vector const & )          = default;
    Vector            (Vector       &&) noexcept = default;
    Vector & operator=(Vector       &&) noexcept = default;

    // Inline accessors

    Azimuth const  & azimuth()  const { return m_azimuth;  }
    Distance const & distance() const { return m_distance; }
  };

  // Creation method; manages a cache, returning cached data
  // if possible. This gets called just a lot; performing the
  // calculations needed to make a vector are not cheap, so
  // we want to reuse results as much as possible.

  Vector
  vector(QStringView origin,
         QStringView remote);
}
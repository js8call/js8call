#include "plotter.h"
#include <concepts>
#include <iterator>
#include <numeric>
#include <type_traits>
#include <utility>
#include <QDebug>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QToolTip>
#include <QWheelEvent>
#include "commons.h"
#include "moc_plotter.cpp"
#include "DriftingDateTime.h"
#include "JS8Submode.hpp"

/******************************************************************************/
// Constants
/******************************************************************************/

namespace
{
  // The Qt Raster engine seems to have terrible performance when
  // drawing large polylines; the size at which we should split
  // drawing into smaller lines.

  constexpr qsizetype POLYLINE_SIZE = 6;

  // Debounce interval, in milliseconds; adjust to taste.

  constexpr auto DEBOUNCE_INTERVAL = 100;

  // Vertical divisions in the spectrum display.

  constexpr std::size_t VERT_DIVS = 7;

  // FFT bin width, as with NSPS, a constant; see the JT9 documentation
  // for the reasoning behind the values used here, but in short, since
  // NSPS is always 6912, 1500 for nsps2 and 2048 for nfft3 are optimal.

  constexpr float FFT_BIN_WIDTH = 1500.0 / 2048.0;

  // 30 meter band: 10.130-10.140 RTTY
  //                10.140-10.150 Packet

  constexpr float BAND_30M_START = 10.13f;
  constexpr float BAND_30M_END   = 10.15f;

  // The WSPR range starts at 10.1401 MHz and runs for 200 Hz.

  constexpr float WSPR_START = 10.1401f;
  constexpr int   WSPR_RANGE = 200;

  // Band colors, always drawn with a 3-pixel pen.

  constexpr auto BAND_EDGE = QColor{149, 165, 166};  // Gray
  constexpr auto BAND_GOOD = QColor{ 46, 204, 113};  // Green
  constexpr auto BAND_WARN = QColor{241, 196,  15};  // Yellow
  constexpr auto BAND_WSPR = QColor{230, 126,  34};  // Orange
}

/******************************************************************************/
// Local Utilities
/******************************************************************************/

namespace
{
  // Given a floating point value, return the fractional portion of the
  // value e.g., 42.7 -> 0.7.

  template <std::floating_point T>
  constexpr auto
  fractionalPart(T const v)
  {
    T                    integralPart;
    return std::modf(v, &integralPart);
  }

  // Given the frequency span of the entire viewable plot region, return
  // the frequency span that each division should occupy.

  auto
  freqPerDiv(float const fSpan)
  {
    if (fSpan > 2500) { return 500; }
    if (fSpan > 1000) { return 200; }
    if (fSpan >  500) { return 100; }
    if (fSpan >  250) { return  50; }
    if (fSpan >  100) { return  20; }
                        return  10;
  }
}

/******************************************************************************/
// Implementation
/******************************************************************************/

CPlotter::CPlotter(QWidget * parent)
  : QWidget        {parent}
  , m_freqPerPixel {m_binsPerPixel * FFT_BIN_WIDTH}
  , m_scaler1D     {m_waterfallAvg, m_binsPerPixel}
  , m_scaler2D     {m_h2}
  , m_replotTimer  {new QTimer(this)}
  , m_resizeTimer  {new QTimer(this)}
{
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);

  // Debounce resize events such that resize() doesn't actually get called
  // until the debounce time has elapsed without any further resize events.
  // Likewise, for control-initiated changes that would cause a replot.

  m_replotTimer->setSingleShot(true);
  m_resizeTimer->setSingleShot(true);

  m_replotTimer->setInterval(DEBOUNCE_INTERVAL);
  m_resizeTimer->setInterval(DEBOUNCE_INTERVAL);

  connect(m_replotTimer, &QTimer::timeout, this, &CPlotter::replot);
  connect(m_resizeTimer, &QTimer::timeout, this, &CPlotter::resize);
}

CPlotter::~CPlotter() = default;

QSize
CPlotter::minimumSizeHint() const
{
  return QSize(50, 50);
}

QSize
CPlotter::sizeHint() const
{
  return QSize(180, 180);
}

void
CPlotter::paintEvent(QPaintEvent *)
{
  QPainter p(this);

  p.drawPixmap(0, 0,    m_ScalePixmap);
  p.drawPixmap(0, 30,   m_WaterfallPixmap);
  p.drawPixmap(0, m_h1, m_SpectrumPixmap);

  p.drawPixmap(xFromFreq(m_freq), 30, m_DialPixmap[0]);

  if (m_lastMouseX >= 0)
  {
    p.drawPixmap(m_lastMouseX, 30, m_DialPixmap[1]);
  }

  if (m_filterEnabled && m_filterWidth > 0)
  {
    p.drawPixmap(                                                      0, 0, m_FilterPixmap[0]);
    p.drawPixmap(m_w - m_FilterPixmap[1].deviceIndependentSize().width(), 0, m_FilterPixmap[1]);
  }
}

void
CPlotter::resizeEvent(QResizeEvent *)
{
  m_resizeTimer->start();
}

void
CPlotter::drawLine(QString const & text)
{
  m_WaterfallPixmap.scroll(0, 1, m_WaterfallPixmap.rect());

  QPainter p(&m_WaterfallPixmap);

  // Draw a green line across the complete span.

  p.setPen(Qt::green);
  p.drawLine(0, 0, m_w, 0);

  // Compute the number of lines required before we need to draw the
  // text, and note the text to draw, saving it against a potential
  // replot request.

  m_text = text;
  m_line = p.fontMetrics().height() * devicePixelRatio();
  m_replot.push_front(m_text);

  update();
}

void
CPlotter::drawData(WF::SWide       swide,
                   WF::State const state)
{
  m_WaterfallPixmap.scroll(0, 1, m_WaterfallPixmap.rect());

  // Flattening, we just process the visible width; tends to be the best
  // approach in terms of what happens when resizing to a larger size.

  m_flatten(swide.data(), m_w);

  // Display the data in the waterfall, drawing only the displayed range.

  QPainter p(&m_WaterfallPixmap);

  for (auto x = 0; x < m_w; ++x)
  {
    p.setPen(m_colors[m_scaler1D(swide[x])]);
    p.drawPoint(x, 0);
  }

  // See if we've reached the point where we should draw previously computed
  // line text.

  if (--m_line == 0)
  {
    m_line = std::numeric_limits<int>::max();

    p.setPen(Qt::white);
    p.drawText(5, p.fontMetrics().ascent(), m_text);
  }

  // A number of factors determine whether or not we should draw the spectrum.

  if (shouldDrawSpectrum(state))
  {
    // We draw the spectrum by copying the overlay prototype and drawing our
    // points into it.

    m_SpectrumPixmap = m_OverlayPixmap.copy();

    QPainter p(&m_SpectrumPixmap);

    // Add a point to the polyline.

    auto const addPoint = [this](int   const x,
                                 float const y)
    {
      m_points.emplace_back(x, m_scaler2D(y));
    };

    // Add points from one of the ranges of adjunct data instead of the
    // spectrum data.

    auto const addPoints = [this, &addPoint](auto const begin,
                                             auto const value)

    {
      // Determine the starting bin offset of the adjunct data.

      auto const start = begin + static_cast<std::size_t>(m_startFreq / FFT_BIN_WIDTH + 0.5f);

      // Average the values in each range of adjunct data bins
      // and convert to points, passing the average through the
      // supplied value function.

      for (auto x = 0; x < m_w; ++x)
      {
        auto const first = start + x * m_binsPerPixel;

        addPoint(x, value(std::reduce(first,
                                      first + m_binsPerPixel) /
                                              m_binsPerPixel));
      }
    };

    // Clear the current points and ensure space exists to add all the
    // points we require without reallocation.

    m_points.clear();
    m_points.reserve(m_w);

    switch (m_spectrum)
    {
      // Current spectrum is displayed as a green line. Find the minimum
      // value within the displayed spectrum, then display each point as
      // the delta above that value.

      case Spectrum::Current:
      {
        p.setPen(Qt::green);

        auto const min = *std::min_element(swide.begin(),
                                           swide.begin() + m_w);

        for (auto x = 0; x < m_w; ++x) addPoint(x, swide[x] - min);
      }
      break;

      // Cumulative spectrum is displayed as a cyan line; use the average
      // data, which is power scaled and must be converted to dB scale.

      case Spectrum::Cumulative:
      {
        p.setPen(Qt::cyan);
        addPoints(std::begin(specData.savg), [](auto const value)
        {
          return 30.0f + 10.0f * std::log10(value);
        });
      }
      break;

      // Linear Average spectrum is displayed as a yellow line; use the
      // the precomputed linear average data.

      case Spectrum::LinearAvg:
      {
        p.setPen(Qt::yellow);
        addPoints(std::begin(specData.slin), [](auto const value)
        {
          return value;
        });
      }
      break;
    }

    // Draw the spectrum line, reducing the resulting points prior to
    // drawing them, but keeping the collection capacity. We also work
    // around what seems to be a performance bug in all versions of Qt
    // up to and including 6.8, when drawing large polylines; this was
    // culled from the Qwt library's workaround for the issue. Doubles
    // overall program performance, pretty much.

    m_points.erase(m_rdp(m_points), m_points.end());
    p.setRenderHint(QPainter::Antialiasing);

    for (qsizetype i  = 0;
                   i  < m_points.size();
                   i += POLYLINE_SIZE)
    {
      p.drawPolyline(m_points.data() + i, qMin(POLYLINE_SIZE   + 1,
                                               m_points.size() - i));
    }
  }

  // Save the data against a potential replot requirement.

  m_replot.push_front(std::move(swide));

  update();
}

void
CPlotter::drawDecodeLine(QColor const & color,
                         int    const   ia,
                         int    const   ib)
{
  auto const x1 = xFromFreq(ia);
  auto const x2 = xFromFreq(ib);

  QPainter p(&m_WaterfallPixmap);

  p.setPen(color);
  p.drawLine(qMin(x1, x2), 4, qMax(x1, x2), 4);
  p.drawLine(qMin(x1, x2), 0, qMin(x1, x2), 9);
  p.drawLine(qMax(x1, x2), 0, qMax(x1, x2), 9);
}

void
CPlotter::drawHorizontalLine(QColor const & color,
                             int    const   x,
                             int    const   width)
{
  QPainter p(&m_WaterfallPixmap);

  p.setPen(color);
  p.drawLine(x, 0, width <= 0 ? m_w : x + width, 0);
}

void
CPlotter::drawMetrics()
{
  if (m_ScalePixmap.isNull()) return;

  m_ScalePixmap.fill(Qt::white);

  QPainter p(&m_ScalePixmap);

  p.setPen(Qt::black);
  p.drawRect(0, 0, m_w, 30);

  auto        const fSpan   = m_w * m_freqPerPixel;
  auto        const fpd     = freqPerDiv(fSpan);
  float       const ppdV    = fpd / m_freqPerPixel;
  std::size_t const hdivs   = fSpan / fpd + 1.9999f;
  int         const fOffset = ((m_startFreq + fpd - 1) / fpd) * fpd;
  auto        const xOffset = float(fOffset - m_startFreq) / fpd;
  std::size_t const nMajor  = hdivs - 1;
  std::size_t const nMinor  = fpd == 200 ? 4: 5;
  float       const ppdVM   = ppdV / nMinor;
  float       const ppdVL   = ppdV / 2;

  // Draw ticks and labels.

  for (std::size_t iMajor = 0; iMajor < nMajor; iMajor++)
  {
    auto const rMajor = (xOffset + iMajor) * ppdV;
    auto const xMajor = static_cast<int>(rMajor);
    p.drawLine(xMajor, 18, xMajor, 30);

    for (std::size_t iMinor = 1; iMinor < nMinor; iMinor++)
    {
      auto const xMinor = static_cast<int>(rMajor + iMinor * ppdVM);
      p.drawLine(xMinor, 22, xMinor, 30);
    }

    if (xMajor > 70)
    {
       p.drawText(QRect(xMajor - static_cast<int>(ppdVL), 0, static_cast<int>(ppdV), 20),
                  Qt::AlignCenter,
                  QString::number(fOffset + iMajor * fpd));
    }
  }

  // Given a starting frequency and range to cover, return corresponding
  // X values for the sub-band.

  auto const bandX = [this](float const start,
                            int   const range)
  {
    return std::make_pair(xFromFreq(start),
                          xFromFreq(start + range));
  };

  // Given a pair of X values, draw a band line, if visible.

  auto const drawBand = [this, &p](auto const & bandX)
  {
    auto const [x1, x2] = bandX;

    if (x1 <= m_w && x2 > 0)
    {
      p.drawLine(x1 + 1, 26, x2 - 2, 26);
      p.drawLine(x1 + 1, 28, x2 - 2, 28);
    }
  };

  // Colorize the JS8 sub-bands.

  p.setPen(QPen(BAND_EDGE, 3)); drawBand(bandX(   0.0f, 4000));
  p.setPen(QPen(BAND_WARN, 3)); drawBand(bandX( 500.0f, 2500));
  p.setPen(QPen(BAND_GOOD, 3)); drawBand(bandX(1000.0f, 1500));

  // If we're in the 30 meter band, we'd rather that the WSPR sub-band not
  // get stomped on; draw an orange indicator in the scale to denote the
  // WSPR portion of the band.
  //
  // Note that given the way xfromFreq() works, we're always going to see
  // clamped X values here, either 0 or m_w, if the frequency is outside
  // of the range, so we're always going to draw. If the WSPR range is not
  // in the displayed range, the effect will be, given the pen size, that
  // an orange indicator will indicate in which direction the WSPR range
  // lies.

  if (in30MBand())
  {
    auto const wspr = bandX(1.0e6f * (WSPR_START - m_dialFreq), WSPR_RANGE);
    auto       font = QFont();

    font.setBold(true);
    font.setPointSize(10);

    p.setFont(font);
    p.setPen(QPen(BAND_WSPR, 3));
    drawBand(wspr);
    p.drawText(QRect(wspr.first, 0, wspr.second - wspr.first, 25),
               Qt::AlignHCenter|Qt::AlignBottom,
               "WSPR");
  }

  // Our spectrum might be of zero height, in which case our overlay pixmap
  // isn't going to be usable; proceed only if it's usable.

  if (!m_OverlayPixmap.isNull())
  {
    QLinearGradient gradient(0, 0, 0, m_h2);

    gradient.setColorAt(1, Qt::black);
    gradient.setColorAt(0, Qt::darkBlue);

    QPainter p(&m_OverlayPixmap);

    p.setBrush(gradient);
    p.drawRect(0, 0, m_w, m_h2);
    p.setBrush(Qt::SolidPattern);
    p.setPen(QPen(Qt::darkGray, 1, Qt::DotLine));

    // Draw vertical grids.

    auto const x0 = static_cast<int>(fractionalPart((float)m_startFreq / fpd) * ppdV + 0.5f);

    for (std::size_t i = 1; i < hdivs; i++)
    {
      if (auto const x  = static_cast<int>(i * ppdV) - x0;
                    x >= 0 &&
                    x <= m_w)
      {
        p.drawLine(x, 0, x , m_h2);
      }
    }

    // Draw horizontal grids.

    float const ppdH = (float)m_h2 / VERT_DIVS;

    for (std::size_t i = 1; i < VERT_DIVS; i++)
    {
      auto const y = static_cast<int>(i * ppdH);
      p.drawLine(0, y, m_w, y);
    }
  }
}

// Draw the filter overlay pixmaps, if the filter is enabled and has a width
// greater than zero. Note that we could be more clever here and ensure the
// filter is actually visible prior to painting, but what we're doing here
// is reasonably trivial, so probably not worth the effort.

void
CPlotter::drawFilter()
{
  if (m_filterEnabled && m_filterWidth > 0 && !size().isEmpty())
  {
    auto const filterPixmap = [height = size().height(),
                               fill   = QColor(0, 0, 0, std::clamp(m_filterOpacity, 0, 255)),
                               dpr    = devicePixelRatio()](int const width,
                                                            int const lineX)
    {
      // Ending up with an unusable size here is expected, as in the case
      // where the combination of the filter center and width shifts one
      // or both ends of the filter out of the displayed range. Thus, no
      // matter what, we're going to return a pixmap here, though it may
      // be an empty one.

      if (auto const size = QSize(width, height);
                     size.isEmpty())
      {
        return QPixmap();
      }
      else
      {
        QPixmap pixmap = QPixmap(size * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(fill);

        QPainter p(&pixmap);

        p.setPen(Qt::yellow);
        p.drawLine(lineX, 1, lineX, height);

        return pixmap;
      }
    };

    auto const width = m_filterWidth / 2.0f;
    auto const start = xFromFreq(m_filterCenter - width);
    auto const end   = xFromFreq(m_filterCenter + width);

    m_FilterPixmap = {
      filterPixmap(start, start),
      filterPixmap(size().width() - end, 0)
    };
  }
}

// Draw the two dials, the first of which will be used to display the selected
// offset and bandwith, the second prospective offset and bandwidth. These are
// not reliant on anything but height, submode, and bins per pixel.

void
CPlotter::drawDials()
{
  if (auto const height = size().height() - 30;
                 height > 0)
  {
    auto const width      = static_cast<int>(JS8::Submode::bandwidth(m_nSubMode) / m_freqPerPixel + 0.5f);
    auto const dialPixmap = [size = QSize(width, height),
                             rect = QRect(1, 1, width - 2, height - 2),
                             dpr  = devicePixelRatio()](QColor const & color,
                                                        QBrush const & brush)
    {
      QPixmap pixmap = QPixmap(size * dpr);
      pixmap.setDevicePixelRatio(dpr);
      pixmap.fill(Qt::transparent);

      QPainter p(&pixmap);

      p.setBrush(brush);
      p.setPen(QPen(QBrush(color), 2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
      p.drawRect(rect);

      return pixmap;
    };

    m_DialPixmap = {
      dialPixmap(Qt::red,   QBrush(QColor(255, 255, 255, 75), Qt::Dense4Pattern)),
      dialPixmap(Qt::white, Qt::transparent)
    };
  }
}

// Replot the waterfall display, using the data present in the replot
// buffer, if any.

void
CPlotter::replot()
{
  if (m_WaterfallPixmap.isNull()) return;

  // Whack anything currently in the waterfall pixmap; we must do this
  // before attaching a painter.

  m_WaterfallPixmap.fill(Qt::black);

  // We need to consider that entries have been added to the replot
  // buffer at a rate proportional to the display pixel ratio, i.e.,
  // it deals in device pixels, not logical pixels, so we must deal
  // with scaling in the y dimension for this to work out.

  QPainter p(&m_WaterfallPixmap);

  p.scale(1, 1 / m_WaterfallPixmap.devicePixelRatio());

  // Our draw routine pushed entries to the front of the buffer, so we
  // can iterate in forward order here, the Qt coordinate system having
  // (0, 0) as the upper-left point.

  auto y = 0;

  for (auto && v : m_replot)
  {
    std::visit([ratio   = m_WaterfallPixmap.devicePixelRatio(),
                width   = m_WaterfallPixmap.size().width(),
                extra   = p.fontMetrics().descent(),
                &y      = std::as_const(y),
                &colors = std::as_const(m_colors),
                &scaler = std::as_const(m_scaler1D),
                &p](auto const & v)
    {
      // Note that a monostate is constructed as the default when we
      // resize but have no backing data. There is nothing to in that
      // case; just data that we didn't have when we were resized.

      using T = std::decay_t<decltype(v)>;

      // Line drawing; draw the usual green line across the width of the
      // pixmap, annotated by the text provided.

      if constexpr (std::is_same_v<T, QString>)
      {
        p.setPen(Qt::white);
        p.save();
        p.scale(1, ratio);
        p.drawText(5, y / ratio - extra, v);
        p.restore();
        p.setPen(Qt::green);
        p.drawLine(0, y, width, y);
      }

      // Standard waterfall data display; run through the vector of data
      // and color each corresponding point in the pixmap appropriately.

      else if constexpr (std::is_same_v<T, WF::SWide>)
      {
        auto const end = std::min(width, static_cast<int>(v.size()));

        for (auto x = 0; x < end; ++x)
        {
          p.setPen(colors[scaler(v[x])]);
          p.drawPoint(x, y);
        }
      }
    }, v);

    y++;
  }

  // The waterfall pixmap should now look as it did before, but with the
  // current zero, gain, and color palette applied; schedule a repaint.

  update();
}

// Called (indirectly, debounced) from our resize event handler and from
// setPercent2DScreen() after a change to the 2D screen percentage.

void
CPlotter::resize()
{
  if (size().isValid())
  {
    auto const makePixmap = [dpr = devicePixelRatio()](QSize  const & size,
                                                       QColor const & fill)
    {
      auto pixmap = QPixmap(size * dpr);

      pixmap.setDevicePixelRatio(dpr);
      pixmap.fill(fill);

      return pixmap;
    };

    m_w  = size().width();
    m_h2 = m_percent2D * (size().height() - 30) / 100.0;
    m_h1 =                size().height() - m_h2;

    // We want our 3 main pixmaps sized to occupy our entire height,
    // and to be completely filled with an opaque color, since we're
    // going to take the opaque paint even optimization path. If this
    // is a high-DPI display, scale the pixmaps to avoid text looking
    // pixelated.

    m_ScalePixmap     = makePixmap({m_w,   30}, Qt::white);
    m_WaterfallPixmap = makePixmap({m_w, m_h1}, Qt::black);
    m_OverlayPixmap   = makePixmap({m_w, m_h2}, Qt::black);

    // The replot circular buffer should have capacity to hold the full
    // height of the waterfall pixmap, in device, not logical, pixels.
    // Since our variant lists std::monostate as the first alternative,
    // if we get larger here, the added items will be constructed using
    // std::monostate as the alternative.

    m_replot.resize(m_WaterfallPixmap.size().height());

    // Ensure the 2D scaler is working with the current spectrum height.

    m_scaler2D.rescale();

    // The dials, filter, scale and overlay pixmaps don't depend on
    // inbound data, so we can draw them now.

    drawDials();
    drawFilter();
    drawMetrics();

    // The overlay pixmap acts as a prototype for the spectrum pixmap;
    // each time we draw the spectrum, we do so by first making a copy
    // of the overlay, then drawing the spectrum line into it.

    m_SpectrumPixmap = m_OverlayPixmap.copy();

    replot();
  }
}

// If the overlay pixmap is null, then we definitely are not going to
// draw the spectrum. If it's non-null, then our need to draw depends
// on what the spectrum is displaying and the state.

bool
CPlotter::shouldDrawSpectrum(WF::State const state) const
{
  if (m_OverlayPixmap.isNull()) return false;

  return m_spectrum == Spectrum::Current
       ? state.testFlag(WF::Sink::Current)
       : state.testFlag(WF::Sink::Summary);
}

bool
CPlotter::in30MBand() const
{
  return (m_dialFreq >= BAND_30M_START &&
          m_dialFreq <= BAND_30M_END);
}

int
CPlotter::xFromFreq(float const f) const
{
  return std::clamp(static_cast<int>((f - m_startFreq) / m_freqPerPixel + 0.5f), 0, m_w);
}

float
CPlotter::freqFromX(int const x) const
{
  return m_startFreq + x * m_freqPerPixel;
}

void
CPlotter::leaveEvent(QEvent * event)
{
  m_lastMouseX = -1;
  event->ignore();
}

void
CPlotter::wheelEvent(QWheelEvent * event)
{
    auto const y = event->angleDelta().y();

    if (auto const d = ((y > 0) - (y < 0)))
    {
      Q_EMIT changeFreq(event->modifiers() & Qt::ControlModifier
                      ? freq()           + d
                      : freq() / 10 * 10 + d * 10);
    }
    else
    {
      event->ignore();
    }
}

void
CPlotter::mouseMoveEvent(QMouseEvent * event)
{
  m_lastMouseX = std::clamp(static_cast<int>(event->position().x()), 0, m_w);

  update();
  event->ignore();

  QToolTip::showText(event->globalPosition().toPoint(),
                     QString::number(static_cast<int>(freqFromX(m_lastMouseX))));
}

void
CPlotter::mouseReleaseEvent(QMouseEvent * event)
{
  if (Qt::LeftButton == event->button())
  {
    Q_EMIT changeFreq(static_cast<int>(freqFromX(m_lastMouseX)));
  }
  else
  {
    event->ignore();
  }
}

void
CPlotter::setBinsPerPixel(int const binsPerPixel)
{
  if (m_binsPerPixel != binsPerPixel)
  {
    m_binsPerPixel = std::max(1, binsPerPixel);
    m_freqPerPixel = m_binsPerPixel * FFT_BIN_WIDTH;
    m_scaler1D.rescale();
    drawMetrics();
    drawFilter();
    drawDials();
    update();
  }
}

void
CPlotter::setColors(Colors const & colors)
{
  if (m_colors != colors)
  {
    m_colors = colors;
    replot();
  }
}

void
CPlotter::setDialFreq(float const dialFreq)
{
  if (m_dialFreq != dialFreq)
  {
    m_dialFreq = dialFreq;
    drawMetrics();
    update();
  }
}

void
CPlotter::setFilter(int const filterCenter,
                    int const filterWidth)
{
  if (m_filterCenter != filterCenter ||
      m_filterWidth  != filterWidth)
  {
    m_filterCenter = filterCenter;
    m_filterWidth  = filterWidth;
    drawFilter();
    update();
  }
}

void
CPlotter::setFilterEnabled(bool const filterEnabled)
{
  if (m_filterEnabled != filterEnabled)
  {
    m_filterEnabled = filterEnabled;
    drawFilter();
    update();
  }
}

void
CPlotter::setFilterOpacity(int const filterOpacity)
{
  if (m_filterOpacity != filterOpacity)
  {
    m_filterOpacity = filterOpacity;
    drawFilter();
    update();
  }
}

void
CPlotter::setFreq(int const freq)
{
  if (m_freq != freq)
  {
    m_freq = freq;
    drawMetrics();
    update();
  }
}

void
CPlotter::setPercent2D(int percent2D)
{
  if (m_percent2D != percent2D)
  {
    m_percent2D = percent2D;
    resize();
    update();
  }
}

void
CPlotter::setPlotGain(int const plotGain)
{
  if (m_scaler1D.gain() != plotGain)
  {
    m_scaler1D.setGain(plotGain);
    m_replotTimer->start();
  }
}

void
CPlotter::setPlotZero(int const plotZero)
{
  if (m_scaler1D.zero() != plotZero)
  {
    m_scaler1D.setZero(plotZero);
    m_replotTimer->start();
  }
}

void
CPlotter::setStartFreq(int const startFreq)
{
  if (m_startFreq != startFreq)
  {
    m_startFreq = startFreq;
    drawMetrics();
    drawFilter();
    update();
  }
}

void
CPlotter::setSubMode(int const nSubMode)
{
  if (m_nSubMode != nSubMode)
  {
    m_nSubMode = nSubMode;
    drawDials();
    update();
  }
}

void
CPlotter::setWaterfallAvg(int const waterfallAvg)
{
  if (m_waterfallAvg != waterfallAvg)
  {
    m_waterfallAvg = waterfallAvg;
    m_scaler1D.rescale();
  }
}

/******************************************************************************/

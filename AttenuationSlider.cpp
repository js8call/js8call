#include "AttenuationSlider.hpp"
#include <QApplication>
#include <QPainter>
#include <QPixmapCache>
#include <QStyle>

/******************************************************************************/
// Private Implementation
/******************************************************************************/

namespace
{
  // Tunable dimensions. Note that we're going to fill the handle with
  // strings of the format -##.#, e.g., 0, -22, -16.7, using the default
  // system font, which will generally be 12 point; define accordingly.

  constexpr auto grooveWidth = 10;
  constexpr auto tickLength  = 8;
  constexpr auto handleSize  = QSize(40, 20);

  // Colors; the overall flavor of the app is like that of the fusion
  // style in terms of color choices, etc.; these are colors that should
  // feel at home there. Note that we're not addressing dark mode here,
  // since that's work that overall we've not addressed yet for the app.

  constexpr auto grooveColor      = QColor(192, 192, 192);
  constexpr auto activeColor      = QColor( 10, 129, 254);
  constexpr auto handleStartColor = QColor(  0, 255,   0);
  constexpr auto handleStopColor  = QColor( 39, 174,  96);
  constexpr auto outlineColor     = QColor(  0,   0,   0, 160);
  constexpr auto contrastColor    = QColor(255, 255, 255,  30);

  // Given a size, return a transparently-filled pixmap, with a pixel
  // ratio appropriate to the device in play; if the size is martian,
  // return a null pixmap.

  auto
  makePixmap(QSize const size)
  {
    if (size.isEmpty()) return QPixmap();

    auto const pixelRatio = qApp->devicePixelRatio();
    auto       pixmap     = QPixmap(size * pixelRatio);

    pixmap.setDevicePixelRatio(pixelRatio);
    pixmap.fill(Qt::transparent);

    return pixmap;
  }

  // Create and return a pixmap for the groove, using the provided size.

  auto
  makeGroovePixmap(QSize const size)
  {
    if (auto pixmap = makePixmap(size);
             pixmap.isNull())
    {
      return pixmap;
    }
    else
    {
      auto const rect     = QRect(QPoint(), size);
      auto       gradient = QLinearGradient(rect.left(),
                                            rect.center().y(),
                                            rect.right(),
                                            rect.center().y());

      gradient.setColorAt(0, grooveColor.darker(110));
      gradient.setColorAt(1, grooveColor.lighter(110));

      QPainter p(&pixmap);

      p.setRenderHint(QPainter::Antialiasing, true);
      p.translate(0.5, 0.5);
      p.setPen(outlineColor);
      p.setBrush(gradient);
      p.drawRoundedRect(rect.adjusted(1, 1, -2, -2), 1, 1);

      return pixmap;
    }
  }

  // Create and return a pixmap for the groove active highlight, using
  // the provided size.

  auto
  makeActivePixmap(QSize const size)
  {
    if (auto pixmap = makePixmap(size);
             pixmap.isNull())
    {
      return pixmap;
    }
    else
    {
      auto const rect     = QRect(QPoint(), size);
      auto       gradient = QLinearGradient(rect.left(),
                                            rect.center().y(),
                                            rect.right(),
                                            rect.center().y());

      gradient.setColorAt(0, activeColor);
      gradient.setColorAt(1, activeColor.lighter(130));

      QPainter p(&pixmap);

      p.setRenderHint(QPainter::Antialiasing, true);
      p.translate(0.5, 0.5);
      p.setPen(outlineColor);
      p.setBrush(gradient);
      p.drawRoundedRect(rect.adjusted(1, 1, -2, -2), 1, 1);
      p.setPen(Qt::darkGray);
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(rect.adjusted(2, 2, -3, -3), 1, 1);

      return pixmap;
    }
  }

  // Create and return a slider handle, using the provided size.

  auto
  makeHandlePixmap(QSize const size)
  {
    if (auto pixmap = makePixmap(size);
             pixmap.isNull())
    {
      return pixmap;
    }
    else
    {
      auto const rect     = QRect(QPoint(), size);
      auto const r        = rect.adjusted(1, 1, -2, -2);
      auto const gradRect = rect.adjusted(2, 2, -2, -2);
      auto       gradient = QLinearGradient(gradRect.center().x(),
                                            gradRect.top(),
                                            gradRect.center().x(),
                                            gradRect.bottom());
                                    
      gradient.setColorAt(0, handleStartColor);
      gradient.setColorAt(1, handleStopColor);
      
      QPainter p(&pixmap);

      p.setRenderHint(QPainter::Antialiasing, true);
      p.translate(0.5, 0.5);
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(0, 0, 0, 40));
      p.drawRect(r.adjusted(-1, 2, 1, -2));
      p.setPen(outlineColor);
      p.setBrush(gradient);
      p.drawRoundedRect(r, 2, 2);
      p.setBrush(Qt::NoBrush);
      p.setPen(contrastColor);
      p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 2, 2);
      p.setPen(QColor(0, 0, 0, 10));
      p.drawLine(QPoint(r.left()  + 2, r.bottom() + 1), QPoint(r.right() - 2, r.bottom() + 1));
      p.drawLine(QPoint(r.right() + 1, r.bottom() - 3), QPoint(r.right() + 1, r.top()    + 4));
      p.drawLine(QPoint(r.right() - 1, r.bottom()    ), QPoint(r.right() + 1, r.bottom() - 2));

      return pixmap;
    }
  }

  // Convenience type definition for the three element-specific pixmap
  // creation functions above.

  using MakePixmap = QPixmap(*)(QSize);

  // Look for a matching pixmap in the global pixmap cache, returning it
  // if found, creating and caching it if it wasn't present in the cache.
  // Note that the cache is of limited size, so a pixmap not being present
  // doesn't mean we've never created one; it could have been purged since
  // the last time we did so.

  auto
  cachedPixmap(QSize        const size,
               const char * const name,
               MakePixmap   const make)
  {
    QPixmap pixmap;

    if (auto const key = QString("attenuation_slider_%1(%2,%3)")
                                 .arg(name)
                                 .arg(size.width())
                                 .arg(size.height());
        !QPixmapCache::find(key, &pixmap))
    {
      pixmap = make(size);
      QPixmapCache::insert(key, pixmap);
    }

    return pixmap;
  }
}

/******************************************************************************/
// Public Implementation
/******************************************************************************/

// The default QSlider implementation is platform style specific, and is
// unfortunately very inconsistent in application of custom styling, which
// makes it difficult to achieve our goal of making it look like an audio
// fader control.
//
// The platform implementations work by delegating to a platform style in
// their paintEvent(). Here, we instead just draw everything in a custom
// manner, regardless of the platform style, so it should look the same
// on every platform.
//
// Note that as opposed to the standard QSlider, we ignore horizontal
// orientation here; a fader control is always vertical in orientation.

void
AttenuationSlider::paintEvent(QPaintEvent *)
{
  auto const handle = QRect(QPoint((rect().width() - handleSize.width()) / 2,
                                   yValue(sliderPosition())),
                            handleSize);
  auto const groove = QRect((rect().width() - grooveWidth) / 2,
                            rect().y()      + handleSize.height() / 2,
                            grooveWidth,
                            rect().height() - handleSize.height());
  QPainter p(this);

  // Set color for tick marks and attenuation text.

  p.setPen(Qt::black);

  // Draw groove.

  p.drawPixmap(groove.topLeft(), cachedPixmap(groove.size(), "groove", &makeGroovePixmap));

  // Draw groove active highlight, clipping it to the active portion;
  // we want to draw the full size and clip here so that we can take
  // advantage of pixmap caching. Note that the standard control puts
  // this above the handle, not below, but we're attenuating here, so
  // below is what makes sense for us.

  auto const clipRect = QRect(QPoint(groove.left(), handle.bottom()), groove.bottomRight());

  p.save();
  p.setClipRect(clipRect.adjusted(0, 0, 1, 1), Qt::IntersectClip);
  p.drawPixmap(groove.topLeft(), cachedPixmap(groove.size(), "active", &makeActivePixmap));
  p.restore();

  // Draw tick marks, if any are specified. Typically, both sides.

  if (auto const position  = tickPosition();
                 position != NoTicks)
  {
    auto const left  = position & TicksLeft;
    auto const right = position & TicksRight;

    for (auto value  = minimum();
              value <= maximum();
              value += tickInterval())
    {
      auto const y = yValue(value) + handleSize.height() / 2;

      if (left)  p.drawLine(rect().left(),  y, rect().left()  + tickLength, y);
      if (right) p.drawLine(rect().right(), y, rect().right() - tickLength, y);
    }
  }

  // Draw slider handle and attenuation level text; our value is 10x
  // that of the attenuation level in dB. Note that we don't do anything
  // special here for keyboard focused state, the computation for which is:
  //
  //   hasFocus() && window()->testAttribute(Qt::WA_KeyboardFocusChange)
  //
  // However, if we wanted to do so, one option would be to do something
  // like inverting the pixmap, tinting it, etc., or we could use a
  // different pen color for the text.

  p.drawPixmap(handle.topLeft(), cachedPixmap(handle.size(), "handle", &makeHandlePixmap));
  p.drawText(handle, Qt::AlignCenter, QString::number(-(value() / 10.0)));
}

// Given an attenuation value, compute and return the corresponding Y value.

int
AttenuationSlider::yValue(int const value) const
{
  return QStyle::sliderPositionFromValue(minimum(),
                                         maximum(),
                                         value,
                                         rect().height() - handleSize.height(),
                                        !invertedAppearance());
}

/******************************************************************************/

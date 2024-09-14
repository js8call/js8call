// Simple bargraph dB meter
// Originally implemented by Edson Pereira PY2SDR
//

#include "signalmeter.h"
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPolygon>
#include <QVBoxLayout>
#include <boost/circular_buffer.hpp>
#include "moc_signalmeter.cpp"

// Meter component, which displays to the right of the scale, as a
// level gauge with a peak hold indicator. Displays green when the
// level is good, yellow when it's too low, red when it's too high.

class SignalMeter::Meter final: public QWidget
{
public:

  static constexpr int MAX = 100;
  static constexpr int LO  =  15;
  static constexpr int HI  =  85;

  // We handle the peak hold calculation using a circular buffer that
  // always contains the last 10 values; initially, it'll contain 10
  // zeroes. The current meter reading is whatever the last value we
  // put into the buffer was, and the peak hold level is the largest
  // value in the buffer at any moment.

  explicit Meter(QWidget * parent)
  : QWidget {parent}
  {}

  QSize
  sizeHint() const override
  {
    return {10, 100};
  }

  int last() const { return m_values.back(); }
  int peak() const { return m_peak;          }
  int max()  const { return m_max;           }

  // Caller has provided us with exciting new information. Since GUI
  // components are in general not thread-safe, we don't need to be
  // concerned about locking here; this function can only be called
  // by the setValue() function of the SignalMeter that created us,
  // which is defined as a slot, should that need to be done by a
  // non-GUI thread.
  //
  // This will get called very frequently, often sequentially with
  // identical values, so to avoid needless repaints, we do need to
  // take some care here to ensure that something actually did change
  // such that we'd need to update.

  void
  setValue(const int value,
           const int valueMax)
  {
    auto const oldLast = last();
    auto const oldPeak = peak();
    auto const oldMax  = max();

    m_values.push_back(std::clamp(value, 0, MAX));
    m_peak = *std::max_element(m_values.begin(),
                               m_values.end());
    m_max  = valueMax;

    if (last() != oldLast ||
        peak() != oldPeak ||
        max()  != oldMax) update();
  }
 
protected:

  // Draw the level bar, which might be of zero height, coloring it
  // appropriately if we're above or below a warning threshold. If
  // our peak level is non-zero, also draw the peak hold indicator.

  void
  paintEvent(QPaintEvent *) override
  {
    QPainter p {this};
    p.setPen(Qt::NoPen);

    if      (m_max  > HI) { p.setBrush(Qt::red);    }
    else if (m_peak < LO) { p.setBrush(Qt::yellow); }
    else                  { p.setBrush(Qt::green);  }

    auto const target = contentsRect();
    auto const scaled = [&target](auto const value)
    {
      return QPoint{
        target.left(),
        static_cast<int>(target.top() + target.height() - value / (double)MAX * target.height())
      };
    };

    p.drawRect(QRect{scaled(last()), target.bottomRight()});

    if (peak())
    {
      p.setBrush(Qt::white);
      p.setRenderHint(QPainter::Antialiasing);
      p.translate(scaled(peak()));
      p.drawPolygon(QPolygon{
        {target.width(), -4},
        {target.width(),  4},
        {0,               0}
      });
    }
  }

private:

  boost::circular_buffer<int> m_values{10};
  int                         m_peak;
  int                         m_max;
};

// Scale component, which displays to the left of the meter.

class SignalMeter::Scale final: public QWidget
{
private:

  static constexpr int         text_indent = 2;
  static constexpr int         tick_length = 4;
  static constexpr std::size_t tick_range  = 10;
  static constexpr std::size_t tick_count  = Meter::MAX / tick_range;

public:

  explicit Scale(QWidget * parent)
    : QWidget {parent}
  {
    setSizePolicy(QSizePolicy::Minimum,
                  QSizePolicy::MinimumExpanding);
  }

  QSize
  sizeHint() const override
  {
    return minimumSizeHint();
  }

  QSize
  minimumSizeHint() const override
  {
    QFontMetrics metrics{font(), this};
    return {metrics.horizontalAdvance("00+") + text_indent + tick_length,
            static_cast<int>(metrics.height() * tick_count)};
  }

protected:

  void
  paintEvent (QPaintEvent *) override
  {
    auto const target  = contentsRect();
    auto const metrics = QFontMetrics(font(), this);
    auto const margin  = metrics.height() / 2;
    auto const offset  = metrics.height() / 4;
    auto const span    = target.height() - metrics.height();

    QPainter p {this};
    p.setPen(Qt::white);

    p.drawLine(target.right(), target.top()    + margin,
               target.right(), target.bottom() - margin);

    for (std::size_t tick  = 0;
                     tick <= tick_count;
                   ++tick)
    {
      p.save();
      p.translate(target.right() - tick_length,
                  target.top()   + margin + tick * span / tick_count);
      p.drawLine(0, 0, tick_length, 0);
      if (tick & 1) {
        auto const text = QString::number(Meter::MAX - tick * tick_range);
        p.drawText(-(text_indent + metrics.horizontalAdvance(text)), offset, text);
      }
      p.restore();
    }
  }
};

// Signal meter implementation; displays as a scaled level meter above
// a level value display.

SignalMeter::SignalMeter(QWidget * parent)
  : QFrame  {parent}
  , m_scale {new Scale {this}}
  , m_meter {new Meter {this}}
  , m_value {new QLabel{this}}
{
  auto outer_layout = new QVBoxLayout;
  outer_layout->setSpacing(8);

  auto inner_layout = new QHBoxLayout;
  inner_layout->setContentsMargins(9, 0, 9, 0);
  inner_layout->setSpacing(0);

  auto label_layout = new QHBoxLayout;
  label_layout->setSpacing(4);

  auto const margin = QFontMetrics(m_scale->font(),
                                   m_scale).height() / 2;

  m_meter->setContentsMargins(0, margin, 0, margin);
  m_meter->setSizePolicy(QSizePolicy::Minimum,
                         QSizePolicy::Minimum);

  m_value->setAlignment(Qt::AlignRight);

  inner_layout->addWidget(m_scale);
  inner_layout->addWidget(m_meter);

  label_layout->addWidget(m_value);
  label_layout->addWidget(new QLabel("dB", this));

  outer_layout->addLayout(inner_layout);
  outer_layout->addLayout(label_layout);

  setLayout (outer_layout);
}

void
SignalMeter::setValue(const float value,
                      const float valueMax)
{
  m_meter->setValue(value, valueMax);
  m_value->setText(QString::number(value, 'f', 0));
}

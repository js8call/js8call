// -*- Mode: C++ -*-
///////////////////////////////////////////////////////////////////////////
// Some code in this file and accompanying files is based on work by
// Moe Wheatley, AE4Y, released under the "Simplified BSD License".
// For more details see the accompanying file LICENSE_WHEATLEY.TXT
///////////////////////////////////////////////////////////////////////////

#ifndef PLOTTER_H
#define PLOTTER_H

#include <limits>
#include <variant>
#include <QColor>
#include <QPixmap>
#include <QPolygonF>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <boost/circular_buffer.hpp>
#include "WF.hpp"

class CPlotter final : public QWidget
{
  Q_OBJECT

public:

  using Colors   = QVector<QColor>;
  using Spectrum = WF::Spectrum;

  explicit CPlotter(QWidget *parent = nullptr);

  // Sizing

  QSize minimumSizeHint() const override;
  QSize sizeHint()        const override;

  // Inline accessors

  int      binsPerPixel() const { return m_binsPerPixel; }
  int      freq()         const { return m_freq;         }
  int      plot2dGain()   const { return m_plot2dGain;   }
  int      plot2dZero()   const { return m_plot2dZero;   }
  int      plotGain()     const { return m_plotGain;     }
  int      plotZero()     const { return m_plotZero;     }
  Spectrum spectrum()     const { return m_spectrum;     }
  int      startFreq()    const { return m_startFreq;    }

  int
  frequencyAt(int const x) const
  {
    return static_cast<int>(freqFromX(x));
  }

  // Inline manipulators

  void setFlatten     (bool     const flatten     ) { m_flatten      = flatten;      }
  void setPlot2dGain  (int      const plot2dGain  ) { m_plot2dGain   = plot2dGain;   }
  void setPlot2dZero  (int      const plot2dZero  ) { m_plot2dZero   = plot2dZero;   }
  void setSpectrum    (Spectrum const spectrum    ) { m_spectrum     = spectrum;     }
  void setWaterfallAvg(int      const waterfallAvg) { m_waterfallAvg = waterfallAvg; }

  // Manipulators

  void drawLine(QString const &);
  void drawData(WF::SWide);
  void drawDecodeLine    (const QColor &, int, int);
  void drawHorizontalLine(const QColor &, int, int);
  void setBinsPerPixel(int);
  void setColors(Colors const &);
  void setDialFreq(float);
  void setFilter(int, int);
  void setFilterEnabled(bool);
  void setFilterOpacity(int);
  void setFreq(int);
  void setPercent2DScreen(int);
  void setPeriod(int);
  void setPlotGain(int);
  void setPlotZero(int);
  void setStartFreq(int);
  void setSubMode(int nSubMode);

signals:

  void changeFreq(int);

protected:

  // Event Handlers

  void paintEvent       (QPaintEvent  *) override;
  void resizeEvent      (QResizeEvent *) override;
  void leaveEvent       (QEvent       *) override;
  void wheelEvent       (QWheelEvent  *) override;
  void mouseMoveEvent   (QMouseEvent  *) override;
  void mouseReleaseEvent(QMouseEvent  *) override;

private:

  // Replot data storage; alternatives of nothing at all, a
  // string denoting the label of a transmit period interval
  // start, and waterfall display data, flattened. Important
  // that the monostate alternative is first in the list.

  using Replot = boost::circular_buffer<std::variant<
    std::monostate,
    QString,
    WF::SWide
  >>;

  // Accessors

  bool  in30MBand()        const;
  int   xFromFreq(float f) const;
  float freqFromX(int   x) const;
  float gainFactor()       const;

  // Manipulators

  void drawMetrics();
  void drawFilter();
  void drawDials();
  void replot();
  void resize();

  QTimer  * m_resize;
  Replot    m_replot;
  QPolygonF m_points;
  Colors    m_colors;
  Spectrum  m_spectrum = Spectrum::Current;

  QPixmap m_ScalePixmap;
  QPixmap m_WaterfallPixmap;
  QPixmap m_OverlayPixmap;
  QPixmap m_SpectrumPixmap;
  
  std::array<QPixmap, 2> m_FilterPixmap = {};
  std::array<QPixmap, 2> m_DialPixmap   = {};

  QString m_text;

  float  m_dialFreq        =  0.0f;
  int    m_nSubMode        =  0;
  int    m_filterCenter    =  0;
  int    m_filterWidth     =  0;
  int    m_filterOpacity   =  127;
  int    m_percent2DScreen =  0;
  int    m_plotZero        =  0;
  int    m_plotGain        =  0;
  int    m_plot2dGain      =  0;
  int    m_plot2dZero      =  0;
  int    m_binsPerPixel    =  2;
  int    m_waterfallAvg    =  1;
  int    m_lastMouseX      = -1;
  int    m_line            =  std::numeric_limits<int>::max();
  int    m_startFreq       =  0;
  int    m_freq            =  0;
  int    m_w               =  0;
  int    m_h1              =  0;
  int    m_h2              =  0;
  bool   m_filterEnabled   = false;
  bool   m_flatten         = false;
  float  m_freqPerPixel;
};

#endif // PLOTTER_H

// -*- Mode: C++ -*-
///////////////////////////////////////////////////////////////////////////
// Some code in this file and accompanying files is based on work by
// Moe Wheatley, AE4Y, released under the "Simplified BSD License".
// For more details see the accompanying file LICENSE_WHEATLEY.TXT
///////////////////////////////////////////////////////////////////////////

#ifndef PLOTTER_H
#define PLOTTER_H

#include <QColor>
#include <QFrame>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QVector>
#include "WF.hpp"

extern QVector<QColor> g_ColorTbl;  // XXX

class QAction;

class CPlotter : public QFrame
{
  Q_OBJECT

public:

  using Spectrum = WF::Spectrum;

  explicit CPlotter(QWidget *parent = nullptr);
  ~CPlotter();

  // Sizing

  QSize minimumSizeHint() const override;
  QSize sizeHint()        const override;

  // Inline accessors

  int      binsPerPixel() const { return m_binsPerPixel; }
  int      Fmax()         const { return m_fMax;         }
  float    fSpan()        const { return m_fSpan;        }
  int      plot2dGain()   const { return m_plot2dGain;   }
  int      plot2dZero()   const { return m_plot2dZero;   }
  int      plotGain()     const { return m_plotGain;     }
  int      plotWidth()    const { return m_w;            }
  int      plotZero()     const { return m_plotZero;     }
  int      rxFreq()       const { return m_rxFreq;       }
  bool     scaleOK()      const { return m_bScaleOK;     }
  Spectrum spectrum()     const { return m_spectrum;     }
  int      startFreq()    const { return m_startFreq;    }

  // Inline manipulators

  void setDataFromDisk(bool     const   dataFromDisk) { m_dataFromDisk = dataFromDisk; }
  void setPlot2dZero  (int      const   plot2dZero  ) { m_plot2dZero   = plot2dZero;   }
  void setPlotGain    (int      const   plotGain    ) { m_plotGain     = plotGain;     }
  void setPlotWidth   (int      const   w           ) { m_w            = w;            }
  void setPlotZero    (int      const   plotZero    ) { m_plotZero     = plotZero;     }
  void setRedFile     (QString  const   redFile     ) { m_redFile      = redFile;      }
  void SetRunningState(bool     const   running     ) { m_Running      = running;      }
  void setSpectrum    (Spectrum const   spectrum    ) { m_spectrum     = spectrum;     }
  void setVHF         (bool     const   bVHF        ) { m_bVHF         = bVHF;         }
  void setWaterfallAvg(int      const   waterfallAvg) { m_waterfallAvg = waterfallAvg; }

  // Statics

  static QVector<QColor>  const & colors()                               { return g_ColorTbl;      }
  static void                     setColours(QVector<QColor> const & cl) {        g_ColorTbl = cl; }

  // Manipulators

  void draw(float swide[], bool bScroll, bool bRed);		//Update the waterfall
  void replot();
  void setStartFreq(int f);
  void setPlot2dGain(int n);
  void UpdateOverlay();
  void setBinsPerPixel(int n);
  void setRxFreq(int n);
  void setNsps(int ntrperiod, int nsps);
  void setTxFreq(int n);
  void SetPercent2DScreen(int percent);
  void setDialFreq(double d);
  void setFlatten(bool b1, bool b2);
  void setTol(int n);
  void setRxBand(QString const & band);
  void setTurbo(bool turbo);
  void setFilterCenter(int center);
  void setFilterWidth(int width);
  void setFilterEnabled(bool enabled);
  void setFilterOpacity(int alpha);
  void setRxRange(int             fMin);
  void setMode   (QString const & mode);
  void setModeTx (QString const & modeTx);
  void setSubMode(int             nSubMode);
  void drawDecodeLine    (const QColor & color, int ia, int ib   );
  void drawHorizontalLine(const QColor & color, int x,  int width);

  int frequencyAt(int const x) const { return int(FreqfromX(x)); }

signals:
  void freezeDecode1(int n);
  void setFreq1(int rxFreq, int txFreq);
  void qsy(int hzDelta);

protected:

  // Event Handlers

  void paintEvent           (QPaintEvent  *) override;
  void resizeEvent          (QResizeEvent *) override;
  void leaveEvent           (QEvent       *) override;
  void wheelEvent           (QWheelEvent  *) override;
  void mouseMoveEvent       (QMouseEvent  *) override;
  void mouseReleaseEvent    (QMouseEvent  *) override;
  void mouseDoubleClickEvent(QMouseEvent  *) override;

private:

  static constexpr std::size_t MaxScreenSize = 2048;

  void  DrawOverlay();
  void  DrawOverlayScale(double, float);
  void  DrawOverlayDial(int);
  void  DrawOverlayHover(int);
  void  DrawOverlayFilter();
  int   XfromFreq(float f) const;
  float FreqfromX(int   x) const;

  QAction * m_set_freq_action;
  Spectrum  m_spectrum = Spectrum::Current;

  bool    m_bScaleOK;
  bool    m_bVHF;

  float   m_fSpan;

  qint32  m_plotZero;
  qint32  m_plotGain;
  qint32  m_plot2dGain;
  qint32  m_plot2dZero;
  qint32  m_binsPerPixel;
  qint32  m_waterfallAvg;
  qint32  m_w;
  qint32  m_Flatten;
  qint32  m_nSubMode;

  QPixmap m_FilterOverlayPixmap;
  QPixmap m_DialOverlayPixmap;
  QPixmap m_HoverOverlayPixmap;
  QPixmap m_WaterfallPixmap;
  QPixmap m_2DPixmap;
  QPixmap m_ScalePixmap;
  QPixmap m_OverlayPixmap;

  QSize   m_Size;
  QString m_Str;
  QString m_mode;
  QString m_modeTx;
  QString m_rxBand;
  QString m_redFile;

  bool    m_filterEnabled;
  int     m_filterCenter;
  int     m_filterWidth;
  bool    m_turbo;
  bool    m_Running;
  bool    m_paintEventBusy;
  bool    m_dataFromDisk;
  bool    m_bReplot;

  double  m_fftBinWidth;
  double  m_dialFreq;

  float   m_sum[MaxScreenSize];
  QPoint  m_points[MaxScreenSize];

  qint32  m_filterOpacity;
  qint32  m_line;
  qint32  m_freqPerDiv;
  qint32  m_nsps;
  qint32  m_Percent2DScreen;
  qint32  m_Percent2DScreen0;
  qint32  m_h;
  qint32  m_h1;
  qint32  m_h2;
  qint32  m_TRperiod;
  qint32  m_rxFreq;
  qint32  m_txFreq;
  qint32  m_fMin;
  qint32  m_fMax;
  qint32  m_startFreq;
  qint32  m_tol;
  qint32  m_j;
  qint32  m_lastMouseX;
};

#endif // PLOTTER_H

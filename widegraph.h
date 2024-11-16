// -*- Mode: C++ -*-
#ifndef WIDEGRAPH_H
#define WIDEGRAPH_H

#include <array>
#include <QColor>
#include <QDir>
#include <QEvent>
#include <QMutex>
#include <QObject>
#include <QScopedPointer>
#include <QString>
#include <QStringView>
#include <QVector>
#include <QWidget>
#include "commons.h"
#include "WF.hpp"

namespace Ui {
  class WideGraph;
}

class Configuration;
class QSettings;
class QTimer;

class WideGraph : public QWidget
{
  Q_OBJECT

public:
  explicit WideGraph(QSettings *, QWidget * = nullptr);
  ~WideGraph ();

  // Accessors

  int  centerFreq() const;
  int  filterMinimum() const;
  int  filterMaximum() const;
  bool filterEnabled() const;
  int  freq() const;
  bool isAutoSyncEnabled() const;
  int  nStartFreq() const;
  bool shouldDisplayDecodeAttempts() const;
  bool shouldAutoSyncSubmode(int) const;
  int  smoothYellow() const;

  // Manipulators

  void dataSink(WF::SPlot const &, float);
  void drawDecodeLine(QColor const &, int, int);
  void drawHorizontalLine(QColor const &, int, int);
  void saveSettings();
  void setBand(QString const &);
  void setFilterCenter(int);
  void setFilter(int, int);
  void setFilterMinimumBandwidth(int);
  void setFilterEnabled(bool);
  void setFilterOpacityPercent(int);
  void setFreq(int);
  void setPeriod(int);
  void setSubMode(int);

signals:
  void changeFreq(int);
  void f11f12(int n);
  void setXIT(int n);
  void qsy(int);
  void drifted(int, int);

public slots:
  void setDialFreq(float);
  void setTimeControlsVisible(bool);
  bool timeControlsVisible() const;
  void setControlsVisible(bool, bool = true);
  bool controlsVisible() const;
  void setDrift(int);
  int  drift() const;
  void setPaused(bool paused){ m_paused = paused; }
  void notifyDriftedSignalsDecoded(int);

protected:
  void keyPressEvent (QKeyEvent *e) override;
  void closeEvent (QCloseEvent *) override;

private slots:

  void on_qsyPushButton_clicked();
  void on_offsetSpinBox_valueChanged(int n);
  void on_waterfallAvgSpinBox_valueChanged(int arg1);
  void on_bppSpinBox_valueChanged(int arg1);
  void on_spec2dComboBox_currentIndexChanged(int);
  void on_fStartSpinBox_valueChanged(int n);
  void on_paletteComboBox_activated(int);
  void on_cbFlatten_toggled(bool b);
  void on_adjust_palette_push_button_clicked (bool);
  void on_gainSlider_valueChanged(int value);
  void on_zeroSlider_valueChanged(int value);
  void on_gain2dSlider_valueChanged(int value);
  void on_zero2dSlider_valueChanged(int value);
  void on_smoSpinBox_valueChanged(int n);  
  void on_sbPercent2dPlot_valueChanged(int n);
  void on_filterMinSpinBox_valueChanged(int n);
  void on_filterMaxSpinBox_valueChanged(int n);
  void on_filterCenterSpinBox_valueChanged(int n);
  void on_filterWidthSpinBox_valueChanged(int n);
  void on_filterCenterSyncButton_clicked();
  void on_filterCheckBox_toggled(bool b);
  void on_filterOpacitySpinBox_valueChanged(int n);

  void on_autoDriftButton_toggled(bool checked);
  void on_driftSpinBox_valueChanged(int n);
  void on_driftSyncButton_clicked();
  void on_driftSyncEndButton_clicked();
  void on_driftSyncMinuteButton_clicked();
  void on_driftSyncResetButton_clicked();


private:

  void readPalette();

  QScopedPointer<Ui::WideGraph> ui;

  int  m_percent2DScreen     = 0;
  int  m_waterfallAvg        = 1;
  int  m_waterfallNow        = 0;
  int  m_filterMinimum       = 0;
  int  m_filterMaximum       = 5000;
  int  m_filterCenter        = 1500;
  int  m_filterMinWidth      = 0;
  int  m_nsmo                = 1;
  int  m_TRperiod            = 15;
  int  m_lastSecondInPeriod  = 0;
  int  m_autoSyncTimeLeft    = 0;
  int  m_autoSyncDecodesLeft = 0;
  bool m_paused              = false;
  bool m_filterEnabled       = false;
  bool m_flatten             = true;
  bool m_autoSyncConnected   = false;

  QSettings * m_settings;
  QTimer    * m_drawTimer;
  QTimer    * m_autoSyncTimer;
  QDir        m_palettes_path;
  WF::Palette m_userPalette;
  WF::SWide   m_swide = {};
  WF::SPlot   m_splot = {};
  QMutex      m_drawLock;
  QStringView m_timeFormat;
  QString     m_waterfallPalette;
  QString     m_band;
  QList<int>  m_sizes;
};

#endif // WIDEGRAPH_H

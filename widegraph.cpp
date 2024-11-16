#include "widegraph.h"
#include <algorithm>
#include <QElapsedTimer>
#include <QMenu>
#include <QMutexLocker>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include "ui_widegraph.h"
#include "Configuration.hpp"
#include "DriftingDateTime.h"
#include "EventFilter.hpp"
#include "MessageBox.hpp"
#include "SettingsGroup.hpp"
#include "varicode.h"

#include "moc_widegraph.cpp"

namespace
{
  auto const user_defined = QObject::tr ("User Defined");

  // Time formats; we're likely only ever to use the second.

  constexpr QStringView TIME_FORMAT_MINS = u"hh:mm";
  constexpr QStringView TIME_FORMAT_SECS = u"hh:mm:ss";

  constexpr auto
  timeFormat(int const period)
  {
    return period < 60
         ? TIME_FORMAT_SECS
         : TIME_FORMAT_MINS;
  }

  // Set the spinbox to the value, ensuring that signals are
  // blocked during the set operation and restoring the prior
  // blocked state afterward.

  void
  setValueBlocked(int const  value,
                  QSpinBox * block)
  {
    QSignalBlocker blocker(block);
    block->setValue(value);
  };

  // Set the checkbox to the value, ensuring that signals are
  // blocked during the set operation and restoring the prior
  // blocked state afterward.

  void
  setValueBlocked(bool  const value,
                  QCheckBox * block)
  {
    QSignalBlocker blocker(block);
    block->setChecked(value);
  };
}

WideGraph::WideGraph(QSettings * settings,
                     QWidget   * parent)
: QWidget         {parent}
, ui              {new Ui::WideGraph}
, m_settings      {settings}
, m_drawTimer     {new QTimer(this)}
, m_autoSyncTimer {new QTimer(this)}
, m_palettes_path {":/Palettes"}
, m_timeFormat    {timeFormat(m_TRperiod)}
{
  ui->setupUi(this);

  setMaximumHeight (880);

  ui->splitter->setChildrenCollapsible(false);
  ui->splitter->setCollapsible(ui->splitter->indexOf(ui->controls_widget), false);
  ui->splitter->updateGeometry();

  auto const eventFilterFocusOut = new EventFilter::FocusOut([this]
  {
    setFilter(filterMinimum(),
              filterMaximum());
  }, this);
  ui->filterMinSpinBox->installEventFilter(eventFilterFocusOut);
  ui->filterMaxSpinBox->installEventFilter(eventFilterFocusOut);

  auto const eventFilterEscape = new EventFilter::EscapeKeyPress([this](QKeyEvent *)
  {
    setFilter(0, 5000);
    return true;
  }, this);
  ui->filterMinSpinBox->installEventFilter(eventFilterEscape);
  ui->filterMaxSpinBox->installEventFilter(eventFilterEscape);

  ui->widePlot->setCursor(Qt::CrossCursor);
  ui->widePlot->setMaximumWidth(WF::MaxScreenWidth);
  ui->widePlot->setMaximumHeight(800);

  ui->widePlot->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->widePlot, &CPlotter::customContextMenuRequested, this, [this](const QPoint &pos){
      auto menu = new QMenu(this);

      auto const f = ui->widePlot->frequencyAt(pos.x());

      auto offsetAction = menu->addAction(QString("Set &Offset to %1 Hz").arg(f));
      connect(offsetAction, &QAction::triggered, this, [this, f](){
        ui->offsetSpinBox->setValue(f);
      });

      menu->addSeparator();

      if(m_filterEnabled){
          auto disableAction = menu->addAction(QString("&Disable Filter"));
          connect(disableAction, &QAction::triggered, this, [this](){
            ui->filterCheckBox->setChecked(false);
          });
      }

      auto centerAction = menu->addAction(QString("Set Filter &Center to %1 Hz").arg(f));
      connect(centerAction, &QAction::triggered, this, [this, f](){
        ui->filterCenterSpinBox->setValue(f);
        ui->filterCheckBox->setChecked(true);
      });

      auto widthMenu = menu->addMenu("Set Filter &Width to...");
      auto widths = QList<int>{ 25, 50, 75, 100, 250, 500, 750, 1000, 1500, 2000 };
      foreach(auto width, widths){
        if(width < m_filterMinWidth){ continue; }
        auto widthAction = widthMenu->addAction(QString("%1 Hz").arg(width));
        connect(widthAction, &QAction::triggered, this, [this, width](){
            ui->filterWidthSpinBox->setValue(width);
            ui->filterCheckBox->setChecked(true);
        });
      }

      auto minAction = menu->addAction(QString("Set Filter &Minimum to %1 Hz").arg(f));
      connect(minAction, &QAction::triggered, this, [this, f](){
        ui->filterMinSpinBox->setValue(f);
        ui->filterCheckBox->setChecked(true);
      });

      auto maxAction = menu->addAction(QString("Set Filter Ma&ximum to %1 Hz").arg(f));
      connect(maxAction, &QAction::triggered, this, [this, f](){
        ui->filterMaxSpinBox->setValue(f);
        ui->filterCheckBox->setChecked(true);
      });

      menu->popup(ui->widePlot->mapToGlobal(pos));
  });

  connect(ui->widePlot, &CPlotter::changeFreq, this, &WideGraph::changeFreq);

  {

    //Restore user's settings
    SettingsGroup g {m_settings, "WideGraph"};
    restoreGeometry (m_settings->value ("geometry", saveGeometry ()).toByteArray ());
    ui->widePlot->setPlotZero(m_settings->value("PlotZero", 0).toInt());
    ui->widePlot->setPlotGain(m_settings->value("PlotGain", 0).toInt());
    ui->widePlot->setPlot2dGain(m_settings->value("Plot2dGain", 0).toInt());
    ui->widePlot->setPlot2dZero(m_settings->value("Plot2dZero", 0).toInt());
    ui->zeroSlider->setValue(ui->widePlot->plotZero());
    ui->gainSlider->setValue(ui->widePlot->plotGain());
    ui->gain2dSlider->setValue(ui->widePlot->plot2dGain());
    ui->zero2dSlider->setValue(ui->widePlot->plot2dZero());
    auto const n = m_settings->value("BinsPerPixel", 2).toInt();
    m_flatten=m_settings->value("Flatten", true).toBool();
    ui->cbFlatten->setChecked(m_flatten);
    ui->widePlot->setFlatten(m_flatten);
    ui->bppSpinBox->setValue(n);
    m_nsmo=m_settings->value("SmoothYellow",1).toInt();
    ui->smoSpinBox->setValue(m_nsmo);
    m_percent2DScreen=m_settings->value("Percent2D", 0).toInt();
    m_waterfallAvg = m_settings->value("WaterfallAvg", 1).toInt();
    ui->waterfallAvgSpinBox->setValue(m_waterfallAvg);
    ui->widePlot->setWaterfallAvg(m_waterfallAvg);
    ui->widePlot->setSpectrum(m_settings->value("WaterfallSpectrum", QVariant::fromValue(WF::Spectrum::Current)).value<WF::Spectrum>());
    if(ui->widePlot->spectrum() == WF::Spectrum::Current)    ui->spec2dComboBox->setCurrentIndex(0);
    if(ui->widePlot->spectrum() == WF::Spectrum::Cumulative) ui->spec2dComboBox->setCurrentIndex(1);
    if(ui->widePlot->spectrum() == WF::Spectrum::LinearAvg)  ui->spec2dComboBox->setCurrentIndex(2);
    int nbpp=m_settings->value("BinsPerPixel", 2).toInt();
    ui->widePlot->setBinsPerPixel(nbpp);
    ui->sbPercent2dPlot->setValue(m_percent2DScreen);
    ui->widePlot->setPercent2DScreen(m_percent2DScreen);
    ui->widePlot->setStartFreq(m_settings->value("StartFreq", 500).toInt());
    ui->centerSpinBox->setValue(m_settings->value("CenterOffset", 1500).toInt());
    ui->fStartSpinBox->setValue(ui->widePlot->startFreq());
    m_waterfallPalette=m_settings->value("WaterfallPalette","Default").toString();
    m_userPalette = WF::Palette {m_settings->value("UserPalette").value<WF::Palette::Colours> ()};
    ui->controls_widget->setVisible(!m_settings->value("HideControls", false).toBool());
    ui->fpsSpinBox->setValue(m_settings->value ("WaterfallFPS", 4).toInt());
    ui->decodeAttemptCheckBox->setChecked(m_settings->value("DisplayDecodeAttempts", false).toBool());
    ui->autoDriftAutoStopCheckBox->setChecked(m_settings->value ("StopAutoSyncOnDecode", true).toBool());
    ui->autoDriftStopSpinBox->setValue(m_settings->value ("StopAutoSyncAfter", 1).toInt());

    auto splitState = m_settings->value("SplitState").toByteArray();
    if(!splitState.isEmpty()){
        ui->splitter->restoreState(splitState);
    }

    setFilter(m_settings->value("FilterMinimum", 500).toInt(), m_settings->value("FilterMaximum", 2500).toInt());
    setFilterOpacityPercent(m_settings->value("FilterOpacityPercent", 50).toInt());
    setFilterEnabled(m_settings->value("FilterEnabled", false).toBool());
  }

  int index = 0;
  for (QString const & file :m_palettes_path.entryList(QDir::NoDotAndDotDot |
                                                       QDir::System         |
                                                       QDir::Hidden         |
                                                       QDir::AllDirs        |
                                                       QDir::Files,
                                                       QDir::DirsFirst))
  {
    QString t = file.mid(0, file.length() - 4);
    ui->paletteComboBox->addItem(t);
    if (t == m_waterfallPalette) ui->paletteComboBox->setCurrentIndex(index);
    index++;
  }
  ui->paletteComboBox->addItem(user_defined);
  if (user_defined == m_waterfallPalette) ui->paletteComboBox->setCurrentIndex(index);
  readPalette();

  connect(m_drawTimer, &QTimer::timeout, this, [this]
  {
    auto   const  fps    = std::clamp(ui->fpsSpinBox->value(), 1, 100);
    qint64 const  loopMs = 1000 / (fps * devicePixelRatio()) * m_waterfallAvg;
    QElapsedTimer timer;

    // Start the elapsed timer and do the drawing, unless we're paused.

    timer.start();

    if (!m_paused)
    {
      QMutexLocker lock(&m_drawLock);

      // Draw the tr cycle horizontal lines if needed.

      auto const now            = DriftingDateTime::currentDateTimeUtc();
      auto const secondInToday  = now.time().msecsSinceStartOfDay() / 1000;
      int  const secondInPeriod = secondInToday % m_TRperiod;

      if (secondInPeriod < m_lastSecondInPeriod)
      {
        ui->widePlot->drawLine(now.toString(m_timeFormat).append(m_band));
      }
      m_lastSecondInPeriod = secondInPeriod;

      // Draw the data, handing the plotter a copy.

      ui->widePlot->drawData(m_swide);
    }

    // Compute the processing time and adjust loop to hit the next frame.

    m_drawTimer->start(std::max(std::chrono::milliseconds(loopMs - timer.elapsed()),
                                std::chrono::milliseconds::zero()));
  });

  m_drawTimer->setTimerType(Qt::PreciseTimer);
  m_drawTimer->setSingleShot(true);
  m_drawTimer->start(100);   //### Don't change the 100 ms! ###
}

WideGraph::~WideGraph() = default;

void
WideGraph::closeEvent (QCloseEvent * event)
{
  saveSettings();
  QWidget::closeEvent(event);
}

void WideGraph::saveSettings()                                           //saveSettings
{
  SettingsGroup g {m_settings, "WideGraph"};
  m_settings->setValue ("geometry", saveGeometry ());
  m_settings->setValue ("PlotZero", ui->widePlot->plotZero());
  m_settings->setValue ("PlotGain", ui->widePlot->plotGain());
  m_settings->setValue ("Plot2dGain", ui->widePlot->plot2dGain());
  m_settings->setValue ("Plot2dZero", ui->widePlot->plot2dZero());
  m_settings->setValue ("BinsPerPixel", ui->bppSpinBox->value ());
  m_settings->setValue ("SmoothYellow", ui->smoSpinBox->value ());
  m_settings->setValue ("Percent2D",m_percent2DScreen);
  m_settings->setValue ("WaterfallAvg", ui->waterfallAvgSpinBox->value ());
  m_settings->setValue ("WaterfallSpectrum", QVariant::fromValue(ui->widePlot->spectrum()));
  m_settings->setValue ("BinsPerPixel", ui->widePlot->binsPerPixel ());
  m_settings->setValue ("StartFreq", ui->widePlot->startFreq ());
  m_settings->setValue ("WaterfallPalette", m_waterfallPalette);
  m_settings->setValue ("UserPalette", QVariant::fromValue (m_userPalette.colours ()));
  m_settings->setValue ("Flatten", m_flatten);
  m_settings->setValue ("HideControls", ui->controls_widget->isHidden ());
  m_settings->setValue ("CenterOffset", ui->centerSpinBox->value());
  m_settings->setValue ("FilterMinimum", m_filterMinimum);
  m_settings->setValue ("FilterMaximum", m_filterMaximum);
  m_settings->setValue ("FilterEnabled", m_filterEnabled);
  m_settings->setValue ("FilterOpacityPercent", ui->filterOpacitySpinBox->value());
  m_settings->setValue ("SplitState", ui->splitter->saveState());
  m_settings->setValue ("WaterfallFPS", ui->fpsSpinBox->value());
  m_settings->setValue ("DisplayDecodeAttempts", ui->decodeAttemptCheckBox->isChecked());
  m_settings->setValue ("StopAutoSyncOnDecode", ui->autoDriftAutoStopCheckBox->isChecked());
  m_settings->setValue ("StopAutoSyncAfter", ui->autoDriftStopSpinBox->value());
}

bool
WideGraph::shouldDisplayDecodeAttempts() const
{
  return ui->decodeAttemptCheckBox->isChecked();
}

bool
WideGraph::isAutoSyncEnabled() const
{
  // enabled if we're auto drifting
  // and we are not auto stopping
  // or if we are auto stopping,
  // we have auto sync decodes left
  return ui->autoDriftButton->isChecked() && (
      !ui->autoDriftAutoStopCheckBox->isChecked() ||
      m_autoSyncDecodesLeft > 0
  );
}

bool
WideGraph::shouldAutoSyncSubmode(int const submode) const
{
  return isAutoSyncEnabled() && (
          submode == Varicode::JS8CallSlow
      || submode == Varicode::JS8CallNormal
  //  || submode == Varicode::JS8CallFast
  //  || submode == Varicode::JS8CallTurbo
  //  || submode == Varicode::JS8CallUltra
  );
}

void
WideGraph::notifyDriftedSignalsDecoded(int const signalsDecoded)
{
  //qDebug() << "decoded" << signalsDecoded << "with" << m_autoSyncDecodesLeft << "left";

  m_autoSyncDecodesLeft -= signalsDecoded;

  if(ui->autoDriftAutoStopCheckBox->isChecked() && m_autoSyncDecodesLeft <= 0)
  {
    ui->autoDriftButton->setChecked(false);
  }
}

void
WideGraph::on_autoDriftButton_toggled(bool const checked)
{
  if (!m_autoSyncConnected)
  {
    connect(m_autoSyncTimer, &QTimer::timeout, this, [this]()
    {
      // if auto drift isn't checked, don't worry about this...
      if (!ui->autoDriftButton->isChecked()) return;

      // uncheck after timeout
      if (m_autoSyncTimeLeft == 0)
      {
        ui->autoDriftButton->setChecked(false);
        return;
      }

      // set new text and decrement timeleft
      auto const text    = ui->autoDriftButton->text();
      auto const newText = QString("%1 (%2)")
                                  .arg(text.left(text.indexOf("(")).trimmed())
                                  .arg(m_autoSyncTimeLeft--);

      ui->autoDriftButton->setText(newText);
    });

    m_autoSyncConnected = true;
  }

  // if in the future we want to auto sync timeout after a time period
  auto const autoSyncTimeout = false;
  auto       text            = ui->autoDriftButton->text();

  if (autoSyncTimeout)
  {
    if (checked)
    {
      m_autoSyncTimeLeft = 120;
      m_autoSyncTimer->setInterval(1000);
      m_autoSyncTimer->start();
      ui->autoDriftButton->setText(QString("%1 (%2)")
                                          .arg(text.replace("Start", "Stop"))
                                          .arg(m_autoSyncTimeLeft--));
    }
    else
    {
      m_autoSyncTimeLeft = 0;
      m_autoSyncTimer->stop();
      ui->autoDriftButton->setText(text.left(text.indexOf("(")).trimmed().replace("Stop", "Start"));
    }
  }
  else
  {
    if (checked)
    {
      m_autoSyncDecodesLeft = ui->autoDriftStopSpinBox->value();
      ui->autoDriftButton->setText(text.left(text.indexOf("(")).trimmed().replace("Start", "Stop"));
      ui->autoDriftStopSpinBox->setEnabled(false);
    }
    else
    {
      m_autoSyncDecodesLeft = 0;
      ui->autoDriftButton->setText(text.left(text.indexOf("(")).trimmed().replace("Stop", "Start"));
      ui->autoDriftStopSpinBox->setEnabled(true);
    }
  }
}

void
WideGraph::drawDecodeLine(QColor const & color,
                          int    const   ia,
                          int    const   ib)
{
  ui->widePlot->drawDecodeLine(color, ia, ib);
}

void
WideGraph::drawHorizontalLine(QColor const & color,
                              int    const   x,
                              int    const   width)
{
    ui->widePlot->drawHorizontalLine(color, x, width);
}

void
WideGraph::dataSink(WF::SPlot const & s,
                    float     const   df3)
{
  QMutexLocker lock(&m_drawLock);

  // If we need a fresh picture, just copy the entirety of the inbound
  // data. Otherwise, we're somewhere in the process of averaging data,
  // so add to what we've already accumulated.

  if (m_waterfallNow == 0)
  {
    m_splot = s;
  }
  else
  {
    std::transform(s.begin(),
                   s.end(),
                   m_splot.begin(),
                   m_splot.begin(),
                   std::plus<>{});
  }

  // Either way, that was another round; see if we've hit the point at
  // which we should normalize the average. Note that m_waterfallAvg
  // can change at any time, so we must be defensive here; it could
  // have been a high value last round, and is now a low one.

  if (++m_waterfallNow >= m_waterfallAvg)
  {
    // Normalize the average, unless there's just one round present.

    if (m_waterfallNow != 1) std::transform(m_splot.begin(),
                                            m_splot.end(),
                                            m_splot.begin(),
                                            [now = m_waterfallNow](auto const value)
                                            {
                                              return value / now;
                                            });

    // Next round, we'll need a fresh picture.

    m_waterfallNow = 0;

    auto const nbpp = ui->widePlot->binsPerPixel();
    auto       sit  = m_splot.begin() + static_cast<int>(ui->widePlot->startFreq() / df3 + 0.5f);
    auto       it   = m_swide.begin();
    auto const end  = it + std::min(m_swide.size(),
                                    static_cast<std::size_t>(5000.0f / (nbpp * df3)));

    for (; it != end; ++it, sit += nbpp) *it = nbpp * std::reduce(sit, sit + nbpp);
  }
}

void
WideGraph::on_bppSpinBox_valueChanged(int const n)
{
  ui->widePlot->setBinsPerPixel(n);
}

void
WideGraph::on_qsyPushButton_clicked()
{
  emit qsy(freq() - centerFreq());
}

void
WideGraph::on_offsetSpinBox_valueChanged(int const n)
{
  if (n == freq()) return;

  // TODO: jsherer - here's where we'd set minimum frequency again (later?)
  auto const newFreq = qMax(0, n);

  setFreq(newFreq);
  emit changeFreq(newFreq);
}

void
WideGraph::on_waterfallAvgSpinBox_valueChanged(int const n)
{
  m_waterfallAvg = n;
  ui->widePlot->setWaterfallAvg(n);
}

void
WideGraph::keyPressEvent(QKeyEvent * event)
{  
  switch (event->key())
  {
  case Qt::Key_F11:
    emit f11f12(11);
    break;
  case Qt::Key_F12:
    emit f11f12(12);
    break;
  default:
    event->ignore();
  }
}

int
WideGraph::freq() const
{
  return ui->widePlot->freq();
}

int
WideGraph::centerFreq() const
{
  return ui->centerSpinBox->value();
}

int
WideGraph::nStartFreq() const
{
  return ui->widePlot->startFreq();
}

int
WideGraph::filterMinimum() const
{   
  return std::max(0, std::min(m_filterMinimum, m_filterMaximum));
}

int
WideGraph::filterMaximum() const
{
  return std::min(std::max(m_filterMinimum, m_filterMaximum), 5000);
}

bool
WideGraph::filterEnabled() const
{
  return m_filterEnabled;
}

void
WideGraph::setFilterCenter(int const n)
{
  auto const delta = n - m_filterCenter;
  setFilter(filterMinimum() + delta,
            filterMaximum() + delta);
}

void
WideGraph::setFilter(int const a,
                     int const b)
{
  auto const low    = std::min(a, b);
  auto const high   = std::max(a, b);
  auto const width  = high - low;
  auto const center = low + width / 2;

  // update the filter history
  m_filterMinimum = a;
  m_filterMaximum = b;
  m_filterCenter  = center;

    // update the spinner UI
  setValueBlocked(a,      ui->filterMinSpinBox);
  setValueBlocked(b,      ui->filterMaxSpinBox);
  setValueBlocked(center, ui->filterCenterSpinBox);
  setValueBlocked(width,  ui->filterWidthSpinBox);

  // update the wide plot UI
  ui->widePlot->setFilter(center, width);
}

void
WideGraph::setFilterMinimumBandwidth(int const width)
{
  m_filterMinWidth = width;

  ui->filterWidthSpinBox->setMinimum(width);

  auto const low  = filterMinimum();
  auto const high = filterMaximum();

  setFilter(low, std::max(low + width, high));
}

void
WideGraph::setFilterEnabled(bool enabled)
{
  m_filterEnabled = enabled;

  // update the filter ui
  ui->filterCenterSpinBox->setEnabled(enabled);
  ui->filterCenterSyncButton->setEnabled(enabled);
  ui->filterWidthSpinBox->setEnabled(enabled);
  ui->filterMinSpinBox->setEnabled(enabled);
  ui->filterMaxSpinBox->setEnabled(enabled);

  // update the checkbox ui
  setValueBlocked(enabled, ui->filterCheckBox);

  // update the wideplot
  ui->widePlot->setFilterEnabled(enabled);
}

void
WideGraph::setFilterOpacityPercent(int const n)
{
  // update the spinbox
  setValueBlocked(n, ui->filterOpacitySpinBox);

  // update the wide plot
  ui->widePlot->setFilterOpacity(int((float(n)/100.0)*255));
}

void
WideGraph::setPeriod(int const ntrperiod)
{
  m_TRperiod   = ntrperiod;
  m_timeFormat = timeFormat(m_TRperiod);
}

void
WideGraph::setFreq(int const n)
{
  emit setXIT(n);
  ui->widePlot->setFreq(n);
  ui->offsetSpinBox->setValue(n);
}

void
WideGraph::setSubMode(int const n)
{
  ui->widePlot->setSubMode(n);
}

void
WideGraph::on_spec2dComboBox_currentIndexChanged(int const index)
{
  ui->smoSpinBox->setEnabled(false);
  switch (index)
  {
    case 0:
      ui->widePlot->setSpectrum(WF::Spectrum::Current);
      break;
    case 1:
      ui->widePlot->setSpectrum(WF::Spectrum::Cumulative);
      break;
    case 2:
      ui->widePlot->setSpectrum(WF::Spectrum::LinearAvg);
      ui->smoSpinBox->setEnabled(true);
      break;
  }
}

void
WideGraph::setDialFreq(float const dialFreq)
{
  ui->widePlot->setDialFreq(dialFreq);
}

void
WideGraph::setTimeControlsVisible(bool const visible)
{
  setControlsVisible(visible, false);
  ui->tabWidget->setCurrentWidget(ui->timingTab);
}

bool
WideGraph::timeControlsVisible() const
{
  return controlsVisible() && ui->tabWidget->currentWidget() == ui->timingTab;
}

void
WideGraph::setControlsVisible(bool const visible,
                              bool const controlTab)
{
  if (ui->controls_widget->isVisible() != visible)
  {
    if (visible)
    {
      if (m_sizes.isEmpty())
      {
        auto const width = ui->splitter->width();
        m_sizes = {width,
                   width / 4};
      }
      ui->splitter->setSizes(m_sizes);
      if (controlTab)
      {
        ui->tabWidget->setCurrentWidget(ui->controlTab);
      }
    }
    else
    {
      m_sizes = ui->splitter->sizes();
    }
    ui->controls_widget->setVisible(visible);
  }
}

bool
WideGraph::controlsVisible() const
{
  return ui->controls_widget->isVisible();
}

void
WideGraph::setBand(QString const & band)
{
  m_band = QString(4, ' ').append(band);
}

void
WideGraph::on_fStartSpinBox_valueChanged(int const n)
{
  ui->widePlot->setStartFreq(n);
}

void
WideGraph::readPalette()
{
  try
  {
    ui->widePlot->setColors(user_defined == m_waterfallPalette
                          ? WF::Palette{m_userPalette}.interpolate()
                          : WF::Palette{m_palettes_path.absoluteFilePath(m_waterfallPalette + ".pal")}.interpolate());
  }
  catch (std::exception const & e)
  {
    MessageBox::warning_message(this, tr("Read Palette"), e.what());
  }
}

void
WideGraph::on_paletteComboBox_activated(int const palette_index)
{
  m_waterfallPalette = ui->paletteComboBox->itemText(palette_index);
  readPalette();
}

void
WideGraph::on_cbFlatten_toggled(bool const flatten)
{
  m_flatten = flatten;
  ui->widePlot->setFlatten(flatten);
}

void
WideGraph::on_adjust_palette_push_button_clicked(bool)
{
  try
  {
    if (m_userPalette.design ())
    {
      m_waterfallPalette = user_defined;
      ui->paletteComboBox->setCurrentText(m_waterfallPalette);
      readPalette();
    }
  }
  catch (std::exception const & e)
  {
    MessageBox::warning_message(this, tr("Read Palette"), e.what());
  }
}

void
WideGraph::on_gainSlider_valueChanged(int const value)
{
  ui->widePlot->setPlotGain(value);
}

void
WideGraph::on_zeroSlider_valueChanged(int const value)
{
  ui->widePlot->setPlotZero(value);
}

void
WideGraph::on_gain2dSlider_valueChanged(int const value) 
{
  ui->widePlot->setPlot2dGain(value);
}

void
WideGraph::on_zero2dSlider_valueChanged(int const value)
{
  ui->widePlot->setPlot2dZero(value);
}

void
WideGraph::on_smoSpinBox_valueChanged(int const n)
{
  m_nsmo = n;
}

int
WideGraph::smoothYellow() const
{
  return m_nsmo;
}

void
WideGraph::on_sbPercent2dPlot_valueChanged(int const n)
{
  m_percent2DScreen = n;
  ui->widePlot->setPercent2DScreen(n);
}

void
WideGraph::on_filterMinSpinBox_valueChanged(int const n)
{
  setFilter(n, m_filterMaximum);
}

void
WideGraph::on_filterMaxSpinBox_valueChanged(int const n)
{
  setFilter(m_filterMinimum, n);
}

void
WideGraph::on_filterCenterSpinBox_valueChanged(int const n)
{
  setFilterCenter(n);
}

void
WideGraph::on_filterWidthSpinBox_valueChanged(int const n)
{
  setFilter(m_filterCenter - n/2,
            m_filterCenter - n/2 + n);
}

void
WideGraph::on_filterCenterSyncButton_clicked()
{
  setFilterCenter(ui->offsetSpinBox->value());
}

void
WideGraph::on_filterCheckBox_toggled(bool const b)
{
  setFilterEnabled(b);
}

void
WideGraph::on_filterOpacitySpinBox_valueChanged(int const n)
{
  setFilterOpacityPercent(n);
}

void
WideGraph::on_driftSpinBox_valueChanged(int const n)
{
  if (n != DriftingDateTime::drift()) setDrift(n);
}

void
WideGraph::on_driftSyncButton_clicked()
{
  auto const now = QDateTime::currentDateTimeUtc();
  int  const pos = m_TRperiod - (now.time().second() % m_TRperiod);
  int  const neg = (now.time().second() % m_TRperiod) - m_TRperiod;
  auto const sec = abs(neg) < pos ? neg : pos;

  setDrift(sec * 1000);
}

void
WideGraph::on_driftSyncEndButton_clicked()
{
  auto const now = QDateTime::currentDateTimeUtc();
  int  const pos = m_TRperiod - (now.time().second() % m_TRperiod);
  int  const neg = (now.time().second() % m_TRperiod) - m_TRperiod;
  auto const sec = abs(neg) < pos ? neg + 2 : pos - 2;

  setDrift(sec * 1000);
}

void
WideGraph::on_driftSyncMinuteButton_clicked()
{
  auto const now = QDateTime::currentDateTimeUtc();
  auto const val = now.time().second();
  auto const sec = val < 30 ? -val : 60 - val;

  setDrift(sec * 1000);
}

void
WideGraph::on_driftSyncResetButton_clicked()
{
  setDrift(0);
}

void
WideGraph::setDrift(int const n)
{
  auto const prev = drift();

  DriftingDateTime::setDrift(n);

  qDebug() << qSetRealNumberPrecision(12) << "Drift milliseconds:" << n;
  qDebug() << qSetRealNumberPrecision(12) << "Clock time:" << QDateTime::currentDateTimeUtc();
  qDebug() << qSetRealNumberPrecision(12) << "Drifted time:" << DriftingDateTime::currentDateTimeUtc();

  if (ui->driftSpinBox->value() != n) ui->driftSpinBox->setValue(n);

  emit drifted(prev, n);
}

int
WideGraph::drift() const
{
  return DriftingDateTime::drift();
}

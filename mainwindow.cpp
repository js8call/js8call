//---------------------------------------------------------- MainWindow
#include "mainwindow.h"
#include <algorithm>
#include <cmath>
#include <cinttypes>
#include <cstring>
#include <limits>
#include <functional>
#include <fstream>
#include <iterator>
#include <fftw3.h>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QtConcurrent/QtConcurrentRun>
#include <QProgressDialog>
#include <QHostInfo>
#include <QVector>
#include <QCursor>
#include <QToolTip>
#include <QAction>
#include <QActionGroup>
#include <QSoundEffect>
#include <QUdpSocket>
#include <QVariant>
#include <QPixmap>
#include <QMdiSubWindow>
#include <QFileDialog>
#include <QInputDialog>
#include <QScrollBar>
#include <QVersionNumber>
#include <QTimeZone>
#include <QByteArrayView>
#include <QElapsedTimer>

#include "revision_utils.hpp"
#include "qt_helpers.hpp"
#include "NetworkAccessManager.hpp"
#include "soundout.h"
#include "soundin.h"
#include "Modulator.hpp"
#include "Detector.hpp"
#include "plotter.h"
#include "about.h"
#include "widegraph.h"
#include "sleep.h"
#include "logqso.h"
#include "decodedtext.h"
#include "Radio.hpp"
#include "Bands.hpp"
#include "TransceiverFactory.hpp"
#include "StationList.hpp"
#include "MessageClient.hpp"
#include "SignalMeter.hpp"
#include "HelpTextWindow.hpp"
#include "MultiSettings.hpp"
#include "CallsignValidator.hpp"
#include "SelfDestructMessageBox.h"
#include "messagereplydialog.h"
#include "DriftingDateTime.h"
#include "jsc.h"
#include "jsc_checker.h"
#include "Inbox.h"
#include "messagewindow.h"
#include "NotificationAudio.h"
#include "JS8Submode.hpp"
#include "EventFilter.hpp"

#include "ui_mainwindow.h"
#include "moc_mainwindow.cpp"

extern "C" {
  //----------------------------------------------------- C and Fortran routines
  void symspec_(struct dec_data *, int* k, int* k0, int *ja, float* ssum, int* ntrperiod, int* nsps, int* ingain,
                int* minw, float* px, float* s, float* df3, int* nhsym, int* npts8,
                float *m_pxmax);

  void genjs8_(char* msg, int* icos, int* i3bit, char* msgsent,
               char ft8msgbits[], int itone[], fortran_charlen_t,
               fortran_charlen_t);

  void azdist_(char* MyGrid, char* HisGrid, double* utch, int* nAz, int* nEl,
               int* nDmiles, int* nDkm, int* nHotAz, int* nHotABetter,
               fortran_charlen_t, fortran_charlen_t);
}

int volatile    itone[NUM_ISCAT_SYMBOLS];  // Audio tones for all Tx symbols
struct dec_data dec_data;                  // for sharing with Fortran

namespace
{
  namespace Default
  {
    constexpr Radio::Frequency DIAL_FREQUENCY = 14078000;
    constexpr auto             FREQUENCY      = 1500;
    constexpr auto             DEPTH          = 2;
    constexpr auto             SUBMODE        = Varicode::JS8CallNormal;
  }

  namespace State
  {
    constexpr auto RX = 1;
    constexpr auto TX = 2;
  }

  int ms_minute_error ()
  {
    auto const now    = DriftingDateTime::currentDateTime();
    auto const time   = now.time();
    auto const second = time.second();

    return now.msecsTo (now.addSecs (second > 30 ? 60 - second : -second)) - time.msec ();
  }

  QString
  since(QDateTime const & time)
  {
      auto const delta = time.secsTo(DriftingDateTime::currentDateTimeUtc());

      if      (delta >= 60 * 60 * 24) return QString("%1d").arg(delta / (60 * 60 * 24));
      else if (delta >= 60 * 60     ) return QString("%1h").arg(delta / (60 * 60     ));
      else if (delta >= 60          ) return QString("%1m").arg(delta / (60          ));
      else if (delta >= 15          ) return QString("%1s").arg(delta - (delta % 15  ));
      else                            return QString("now");
  }

  namespace State
  {
    constexpr QStringView Ready   = u"Ready";
    constexpr QStringView Send    = u"Send";
    constexpr QStringView Sending = u"Sending";
    constexpr QStringView Tuning  = u"Tuning";

    QString
    timed(QStringView const state,
          int         const delay)
    {
        auto time = std::div(delay, 60);

        if      (time.quot && time.rem) return QString("%1 (%2m %3s)").arg(state).arg(time.quot).arg(time.rem);
        else if (time.quot)             return QString("%1 (%2m)"    ).arg(state).arg(time.quot);
        else                            return QString("%1 (%2s)"    ).arg(state).arg(time.rem);
    }
  }

#if 0
  int round(int numToRound, int multiple)
  {
   if(multiple == 0)
   {
    return numToRound;
   }

   int roundDown = ( (int) (numToRound) / multiple) * multiple;

   if(numToRound - roundDown > multiple/2){
    return roundDown + multiple;
   }

   return roundDown;
  }
#endif

  int roundUp(int numToRound, int multiple)
  {
   if(multiple == 0)
   {
    return numToRound;
   }

   int roundDown = ( numToRound / multiple) * multiple;
   return roundDown + multiple;
  }

  auto
  replaceMacros(QString                const & text,
                QMap<QString, QString> const & values,
                bool                   const   prune)
  {
    QString output = text;

    for (auto const [key, value] : values.asKeyValueRange())
    {
      output = output.replace(key, value.toUpper());
    }

    return prune ? output.replace(QRegularExpression("[<](?:[^>]+)[>]"), "")
                 : output;
  }

  // Copy at most size bytes into the array, filling any unused size
  // with spaces if less than size bytes were available to copy. For
  // convenience, return a one past the end iterator, i.e., equal to
  // array + size.

  auto
  copyByteData(QByteArrayView const bytes,
               char         * const array,
               qsizetype      const size)
  {
    return std::fill_n(std::copy_n(bytes.begin(),
                                   std::min(size, bytes.size()),
                                   array), size - bytes.size(), ' ');
  }

  // Copy at most size bytes from the string into the array, filling
  // the array with spaces at the end if we didn't use up the size.

  void
  copyStringData(QStringView const string,
                 char      * const array,
                 qsizetype   const size)
  {
    copyByteData(string.toLatin1(),
                 array,
                 size);
  }

  // Copy at most size bytes into the array, padding out the message
  // with spaces if less than size bytes were available to copy, and
  // null-terminate it. Caller is responsbile for ensuring that at
  // least (size + 1) bytes of space are available.

  void
  copyMessage(QStringView const string,
              char      * const array,
              qsizetype   const size = 28)
  {
    *copyByteData(string.toLocal8Bit(),
                  array,
                  size) = '\0';
  }

  // Distance class, encapsulates determination of distance and
  // azimuth from an origin grid to a remote grid.

  class Distance
  {
  public:

    // Constructor

    Distance(QStringView const originGrid,
             QStringView const remoteGrid,
             bool        const inMiles)
    : m_inMiles{inMiles}
    {
      auto const originGridTrimmed = originGrid.trimmed();
      auto const remoteGridTrimmed = remoteGrid.trimmed();

      if (originGridTrimmed.length() >= 4 &&
          remoteGridTrimmed.length() >= 4)
      {
        m_valid = true;

        auto const nsec = DriftingDateTime::currentSecsSinceEpoch() % 86400;
        auto       utch = nsec / 3600.0;
        int        el;
        int        miles;
        int        km;
        int        hotAz;
        int        hotABetter;
        char       originGridData[6];
        char       remoteGridData[6];

        copyStringData(originGridTrimmed, originGridData, sizeof(originGridData));
        copyStringData(remoteGridTrimmed, remoteGridData, sizeof(remoteGridData));

        azdist_(originGridData,
                remoteGridData,
                &utch,
                &m_azimuth,
                &el,
                &miles,
                &km,
                &hotAz,
                &hotABetter,
                sizeof(originGridData),
                sizeof(remoteGridData));

        auto distance = inMiles ? miles : km;

        if (originGridTrimmed.length() < 6 ||
            remoteGridTrimmed.length() < 6)
        {
          if (auto const close = inMiles ? CloseMiles : CloseKM;
                         close > distance)
          {
            m_close  = true;
            distance = close;
          }
        }

        m_distance = distance;
      }
    }

    // Conversion operators; return validity and distance. These
    // are all we need to implement an ordering relation, but we
    // do so elsewhere.

    explicit operator bool () const noexcept { return m_valid;    }
             operator  int () const noexcept { return m_distance; }

    // String conversion; if valid, return computed information;
    // if invalid, an empty string.

    QString
    toString() const
    {
      if (m_valid)
      {
        auto string = QString("%1 %2 / %3°")
                             .arg(m_distance)
                             .arg(m_inMiles ? "mi" : "km")
                             .arg(m_azimuth);
        return m_close ? string.prepend('<') : string;
      }

      return QString();
    }

  private:

    // Distances that we consider to be 'close'.
 
    static constexpr auto CloseMiles = 75;
    static constexpr auto CloseKM    = 120;

    // Data members

    int  m_azimuth  = 0;
    int  m_distance = 0;
    bool m_valid    = false;
    bool m_close    = false;
    bool m_inMiles;
  };
}

//--------------------------------------------------- MainWindow constructor
MainWindow::MainWindow(QString  const & program_info,
                       QDir     const & temp_directory,
                       bool     const   multiple,
                       MultiSettings  * multi_settings,
                       QSharedMemory  * shdmem,
                       unsigned const   downSampleFactor,
                       QWidget        * parent) :
  QMainWindow(parent),
  m_network_manager {this},
  m_valid {true},
  m_multiple {multiple},
  m_multi_settings {multi_settings},
  m_configurations_button {0},
  m_settings {multi_settings->settings ()},
  m_settings_read {false},
  ui(new Ui::MainWindow),
  m_config {temp_directory, m_settings, this},
  m_rigErrorMessageBox {MessageBox::Critical, tr ("Rig Control Error")
      , MessageBox::Cancel | MessageBox::Ok | MessageBox::Retry},
  m_wideGraph (new WideGraph(m_settings)),
  // no parent so that it has a taskbar icon
  m_logDlg (new LogQSO (program_title (), m_settings, &m_config, nullptr)),
  m_lastDialFreq {0},
  m_detector {new Detector {RX_SAMPLE_RATE, NTMAX, downSampleFactor}},
  m_FFTSize {6912 / 2},         // conservative value to avoid buffer overruns
  m_soundInput {new SoundInput},
  m_modulator {new Modulator},
  m_soundOutput {new SoundOutput},
  m_notification {new NotificationAudio},
  m_decoder {this},
  m_secBandChanged {0},
  m_freqNominal {0},
  m_freqTxNominal {0},
  m_XIT {0},
  m_sec0 {-1},
  m_RxLog {1},      //Write Date and Time to RxLog
  m_nutc0 {999999},
  m_TRperiod {60},
  m_inGain {0},
  m_idleMinutes {0},
  m_nSubMode {Default::SUBMODE},
  m_frequency_list_fcal_iter {m_config.frequencies ()->begin ()},
  m_i3bit {0},
  m_btxok {false},
  m_auto {false},
  m_restart {false},
  m_currentMessageType {-1},
  m_lastMessageType {-1},
  m_tuneup {false},
  m_bTxTime {false},
  m_ihsym {0},
  m_px {0.0},
  m_iptt0 {0},
  m_btxok0 {false},
  m_onAirFreq0 {0.0},
  m_first_error {true},
  tx_status_label {"Receiving"},
  m_appDir {QApplication::applicationDirPath ()},
  m_palette {"Linrad"},
  m_txFrameCountEstimate {0},
  m_txFrameCount {0},
  m_txFrameCountSent {0},
  m_txTextDirty {false},
  m_driftMsMMA { 0 },
  m_driftMsMMA_N { 0 },
  m_previousFreq {0},
  m_hbInterval {0},
  m_cqInterval {0},
  m_hbPaused { false },
  mem_js8 {shdmem},
  m_msAudioOutputBuffered (0u),
  m_framesAudioInputBuffered (RX_SAMPLE_RATE / 10),
  m_downSampleFactor (downSampleFactor),
  m_audioThreadPriority (QThread::HighPriority),
  m_notificationAudioThreadPriority (QThread::LowPriority),
  m_decoderThreadPriority (QThread::HighPriority),
  m_splitMode {false},
  m_monitoring {false},
  m_tx_when_ready {false},
  m_transmitting {false},
  m_tune {false},
  m_tx_watchdog {false},
  m_block_pwr_tooltip {false},
  m_PwrBandSetOK {true},
  m_lastMonitoredFrequency {Default::DIAL_FREQUENCY},
  m_messageClient {new MessageClient {m_config.udp_server_name(), m_config.udp_server_port(), this}},
  m_messageServer {new MessageServer()},
  m_n3fjpClient {new TCPClient{this}},
  m_pskReporter {new PSKReporter {&m_config, program_info}},     // UR
  m_spotClient {new SpotClient   {"spot.js8call.com", 50000, program_info}},
  m_aprsClient {new APRSISClient {"rotate.aprs2.net", 14580}},
  m_manual {&m_network_manager}
{
  ui->setupUi(this);

  createStatusBar();
  add_child_to_event_filter (this);

  m_baseCall = Radio::base_callsign (m_config.my_callsign ());
  m_opCall = m_config.opCall();

  // Closedown.
  connect (ui->actionExit, &QAction::triggered, this, &QMainWindow::close);

  // parts of the rig error message box that are fixed
  m_rigErrorMessageBox.setInformativeText (tr ("Do you want to reconfigure the radio interface?"));
  m_rigErrorMessageBox.setDefaultButton (MessageBox::Ok);

  // start audio thread and hook up slots & signals for shutdown management
  // these objects need to be in the audio thread so that invoking
  // their slots is done in a thread safe way
  m_soundOutput->moveToThread (&m_audioThread);
  m_modulator->moveToThread (&m_audioThread);
  m_soundInput->moveToThread (&m_audioThread);
  m_detector->moveToThread (&m_audioThread);

  // notification audio operates in its own thread at a lower priority
  m_notification->moveToThread(&m_notificationAudioThread);

  // Move the aprs client message server, psk reporter, and spot client
  // to the network thread at a lower priority.

  m_aprsClient->moveToThread(&m_networkThread);
  m_messageServer->moveToThread(&m_networkThread);
  m_pskReporter->moveToThread(&m_networkThread);
  m_spotClient->moveToThread(&m_networkThread);

  // hook up the message server slots and signals and disposal
  connect (m_messageServer, &MessageServer::message, this, &MainWindow::tcpNetworkMessage);
  connect (this, &MainWindow::apiSetMaxConnections, m_messageServer, &MessageServer::setMaxConnections);
  connect (this, &MainWindow::apiSetServer,         m_messageServer, &MessageServer::setServer);
  connect (this, &MainWindow::apiStartServer,       m_messageServer, &MessageServer::start);
  connect (this, &MainWindow::apiStopServer,        m_messageServer, &MessageServer::stop);
  connect (&m_config, &Configuration::tcp_server_changed,          m_messageServer, &MessageServer::setServerHost);
  connect (&m_config, &Configuration::tcp_server_port_changed,     m_messageServer, &MessageServer::setServerPort);
  connect (&m_config, &Configuration::tcp_max_connections_changed, m_messageServer, &MessageServer::setMaxConnections);
  connect (&m_networkThread, &QThread::finished, m_messageServer, &QObject::deleteLater);

  // hook up the aprs client slots and signals and disposal
  connect (this, &MainWindow::aprsClientEnqueueSpot,       m_aprsClient, &APRSISClient::enqueueSpot);
  connect (this, &MainWindow::aprsClientEnqueueThirdParty, m_aprsClient, &APRSISClient::enqueueThirdParty);
  connect (this, &MainWindow::aprsClientSendReports,       m_aprsClient, &APRSISClient::sendReports);
  connect (this, &MainWindow::aprsClientSetLocalStation,   m_aprsClient, &APRSISClient::setLocalStation);
  connect (this, &MainWindow::aprsClientSetPaused,         m_aprsClient, &APRSISClient::setPaused);
  connect (this, &MainWindow::aprsClientSetServer,         m_aprsClient, &APRSISClient::setServer);
  connect (this, &MainWindow::aprsClientSetSkipPercent,    m_aprsClient, &APRSISClient::setSkipPercent);
  connect (&m_networkThread, &QThread::finished, m_aprsClient, &QObject::deleteLater);

  // hook up the psk reporter slots and signals and disposal
  connect (m_pskReporter, &PSKReporter::errorOccurred, this, &MainWindow::pskReporterError);
  connect (this, &MainWindow::pskReporterSendReport,       m_pskReporter, &PSKReporter::sendReport);
  connect (this, &MainWindow::pskReporterAddRemoteStation, m_pskReporter, &PSKReporter::addRemoteStation);
  connect (this, &MainWindow::pskReporterSetLocalStation,  m_pskReporter, &PSKReporter::setLocalStation);
  connect (&m_networkThread, &QThread::started,  m_pskReporter, &PSKReporter::start);
  connect (&m_networkThread, &QThread::finished, m_pskReporter, &QObject::deleteLater);

  // hook up the spot client signals and disposal
  connect (this, &MainWindow::spotClientEnqueueCmd,       m_spotClient, &SpotClient::enqueueCmd);
  connect (this, &MainWindow::spotClientEnqueueSpot,      m_spotClient, &SpotClient::enqueueSpot);
  connect (this, &MainWindow::spotClientSetLocalStation,  m_spotClient, &SpotClient::setLocalStation);
  connect (&m_networkThread, &QThread::started,  m_spotClient, &SpotClient::start);
  connect (&m_networkThread, &QThread::finished, m_spotClient, &QObject::deleteLater);

  // hook up sound output stream slots & signals and disposal
  connect (this, &MainWindow::initializeAudioOutputStream, m_soundOutput, &SoundOutput::setFormat);
  connect (m_soundOutput, &SoundOutput::error, this, &MainWindow::showSoundOutError);
  connect (m_soundOutput, &SoundOutput::error, &m_config, &Configuration::invalidate_audio_output_device);
  connect (this, &MainWindow::outAttenuationChanged, m_soundOutput, &SoundOutput::setAttenuation);
  connect (&m_audioThread, &QThread::finished, m_soundOutput, &QObject::deleteLater);

  connect (this, &MainWindow::initializeNotificationAudioOutputStream, m_notification, &NotificationAudio::setDevice);
  connect (&m_config, &Configuration::test_notify, this, &MainWindow::tryNotify);
  connect (this, &MainWindow::playNotification, m_notification, &NotificationAudio::play);
  connect (&m_notificationAudioThread, &QThread::finished, m_notification, &QObject::deleteLater);

  // hook up Modulator slots and disposal
  connect (this, &MainWindow::transmitFrequency, m_modulator, &Modulator::setFrequency);
  connect (this, &MainWindow::endTransmitMessage, m_modulator, &Modulator::stop);
  connect (this, &MainWindow::tune, m_modulator, &Modulator::tune);
  connect (this, &MainWindow::sendMessage, m_modulator, &Modulator::start);
  connect (&m_audioThread, &QThread::finished, m_modulator, &QObject::deleteLater);

  // hook up the audio input stream signals, slots and disposal
  connect (this, &MainWindow::startAudioInputStream, m_soundInput, &SoundInput::start);
  connect (this, &MainWindow::suspendAudioInputStream, m_soundInput, &SoundInput::suspend);
  connect (this, &MainWindow::resumeAudioInputStream, m_soundInput, &SoundInput::resume);
  connect (this, &MainWindow::finished, m_soundInput, &SoundInput::stop);
  connect(m_soundInput, &SoundInput::error, this, &MainWindow::showSoundInError);
  connect(m_soundInput, &SoundInput::error, &m_config, &Configuration::invalidate_audio_input_device);
  // connect(m_soundInput, &SoundInput::status, this, &MainWindow::showStatusMessage);
  connect (&m_audioThread, &QThread::finished, m_soundInput, &QObject::deleteLater);

  connect (this, &MainWindow::finished, this, &MainWindow::close);

  // hook up the detector signals, slots and disposal
  connect (this, &MainWindow::FFTSize, m_detector, &Detector::setBlockSize);
  connect(m_detector, &Detector::framesWritten, this, &MainWindow::dataSink);
  connect (&m_audioThread, &QThread::finished, m_detector, &QObject::deleteLater);

  // setup the waterfall
  connect(m_wideGraph.data(), &WideGraph::f11f12, this, &MainWindow::f11f12);
  connect(m_wideGraph.data(), &WideGraph::setXIT, this, &MainWindow::setXIT);

  connect (this, &MainWindow::finished, m_wideGraph.data (), &WideGraph::close);

  // setup the log QSO dialog
  connect (m_logDlg.data (), &LogQSO::acceptQSO, this, &MainWindow::acceptQSO);
  connect (this, &MainWindow::finished, m_logDlg.data (), &LogQSO::close);

  // Network message handling
  connect (m_messageClient, &MessageClient::message, this, &MainWindow::udpNetworkMessage);

  // decoder queue handler
  //connect (&m_decodeThread, &QThread::finished, m_notification, &QObject::deleteLater);
  //connect(this, &MainWindow::decodedLineReady, this, &MainWindow::processDecodedLine);
  connect(&m_decoder, &Decoder::ready, this, &MainWindow::processDecodedLine);
  connect(&m_decoder, &Decoder::error, this, [this](int errorCode, QString errorString){
    subProcessError(m_decoder.program(), m_decoder.arguments(), errorCode, errorString);
  });
  connect(&m_decoder, &Decoder::finished, this, [this](int exitCode, int statusCode, QString errorString){
    subProcessFailed(m_decoder.program(), m_decoder.arguments(), exitCode, statusCode, errorString);
  });

  QActionGroup* depthGroup = new QActionGroup(this);
  ui->actionQuickDecode->setActionGroup(depthGroup);
  ui->actionMediumDecode->setActionGroup(depthGroup);
  ui->actionDeepDecode->setActionGroup(depthGroup);
  ui->actionDeepestDecode->setActionGroup(depthGroup);

   m_dateTimeQSOOn = QDateTime{};

  // initialize decoded text font and hook up font change signals
  // defer initialization until after construction otherwise menu
  // fonts do not get set
  QTimer::singleShot (0, this, &MainWindow::initialize_fonts);
  connect (&m_config, &Configuration::gui_text_font_changed, [this] (QFont const& font) {
      set_application_font (font);
  });
  connect (&m_config, &Configuration::table_font_changed, [this] (QFont const&) {
      ui->tableWidgetRXAll->setFont(m_config.table_font());
      ui->tableWidgetCalls->setFont(m_config.table_font());
  });
  connect (&m_config, &Configuration::rx_text_font_changed, [this] (QFont const&) {
      setTextEditFont(ui->textEditRX, m_config.rx_text_font());
  });
  connect (&m_config, &Configuration::compose_text_font_changed, [this] (QFont const&) {
      setTextEditFont(ui->extFreeTextMsgEdit, m_config.compose_text_font());
  });
  connect (&m_config, &Configuration::colors_changed, [this](){
     setTextEditStyle(ui->textEditRX, m_config.color_rx_foreground(), m_config.color_rx_background(), m_config.rx_text_font());
     setTextEditStyle(ui->extFreeTextMsgEdit, m_config.color_compose_foreground(), m_config.color_compose_background(), m_config.compose_text_font());
     ui->extFreeTextMsgEdit->setFont(m_config.compose_text_font(), m_config.color_compose_foreground(), m_config.color_compose_background());

     // rehighlight
     auto d = ui->textEditRX->document();
     if(d){
         for(int i = 0; i < d->lineCount(); i++){
             auto b = d->findBlockByLineNumber(i);

             switch (b.userState())
             {
             case State::RX:
                 highlightBlock(b, m_config.rx_text_font(), m_config.color_rx_foreground(), QColor(Qt::transparent));
                 break;
             case State::TX:
                 highlightBlock(b, m_config.tx_text_font(), m_config.color_tx_foreground(), QColor(Qt::transparent));
                 break;
             }
         }
     }


  });

  setWindowTitle (program_title ());

  // Hook up working frequencies.

  ui->currentFreq->setCursor(QCursor(Qt::PointingHandCursor));
  ui->currentFreq->display("14.078 000");
  ui->currentFreq->installEventFilter(new EventFilter::MouseButtonPress([this](QMouseEvent * event)
  {
    QMenu * menu = new QMenu(ui->currentFreq);
    buildFrequencyMenu(menu);
    menu->popup(event->globalPosition().toPoint());
    return true;
  }, this));

  ui->labDialFreqOffset->setCursor(QCursor(Qt::PointingHandCursor));
  ui->labDialFreqOffset->installEventFilter(new EventFilter::MouseButtonPress([this](QMouseEvent *)
  {
    on_actionSetOffset_triggered();
    return true;
  }, this));

  // Hook up callsign label click to open preferences

  ui->labCallsign->setCursor(QCursor(Qt::PointingHandCursor));
  ui->labCallsign->installEventFilter(new EventFilter::MouseButtonPress([this](QMouseEvent *)
  {
    openSettings(0);
    return true;
  }, this));

  // hook up configuration signals
  connect (&m_config, &Configuration::transceiver_update, this, &MainWindow::handle_transceiver_update);
  connect (&m_config, &Configuration::transceiver_failure, this, &MainWindow::handle_transceiver_failure);
  connect (&m_config, &Configuration::udp_server_name_changed, m_messageClient, &MessageClient::set_server_name);
  connect (&m_config, &Configuration::udp_server_port_changed, m_messageClient, &MessageClient::set_server_port);
  connect (&m_config, &Configuration::band_schedule_changed, this, [this](){
    this->m_bandHopped = true;
  });
  connect (&m_config, &Configuration::enumerating_audio_devices, [this]()
  {
    showStatusMessage (tr ("Enumerating audio devices"));
  });

  // set up configurations menu
  connect (m_multi_settings, &MultiSettings::configurationNameChanged, [this] (QString const& name) {
      if ("Default" != name) {
        config_label.setText (name);
        config_label.show ();
      }
      else {
        config_label.hide ();
      }
    });
  m_multi_settings->create_menu_actions (this, ui->menuConfig);
  m_configurations_button = m_rigErrorMessageBox.addButton (tr ("Configurations...")
                                                            , QMessageBox::ActionRole);
  connect(ui->extFreeTextMsgEdit, &QTextEdit::textChanged, [this]()
  {
    currentTextChanged();
  });

  m_guiTimer.setTimerType(Qt::PreciseTimer);
  m_guiTimer.setSingleShot(true);
  connect(&m_guiTimer, &QTimer::timeout, this, &MainWindow::guiUpdate);
  m_guiTimer.start(100);   //### Don't change the 100 ms! ###

  ptt0Timer.setSingleShot(true);
  connect(&ptt0Timer, &QTimer::timeout, this, &MainWindow::stopTx2);

  ptt1Timer.setSingleShot(true);
  connect(&ptt1Timer, &QTimer::timeout, this, &MainWindow::startTx2);

  logQSOTimer.setSingleShot(true);
  connect(&logQSOTimer, &QTimer::timeout, this, &MainWindow::on_logQSOButton_clicked);

  tuneButtonTimer.setSingleShot(true);
  connect(&tuneButtonTimer, &QTimer::timeout, this, &MainWindow::end_tuning);

  tuneATU_Timer.setSingleShot(true);
  connect(&tuneATU_Timer, &QTimer::timeout, this, &MainWindow::stopTuneATU);

  TxAgainTimer.setSingleShot(true);
  connect(&TxAgainTimer, &QTimer::timeout, this, &MainWindow::TxAgain);

  repeatTimer.setSingleShot(false);
  repeatTimer.setInterval(1000);
  connect(&repeatTimer, &QTimer::timeout, this, &MainWindow::checkRepeat);

  connect(m_wideGraph.data(), &WideGraph::changeFreq, this, &MainWindow::changeFreq);
  connect(m_wideGraph.data(), &WideGraph::qsy,        this, &MainWindow::qsy);
  connect(m_wideGraph.data(), &WideGraph::drifted,    this, &MainWindow::drifted);

  decodeBusy(false);

  m_msg[0][0]=0;

  displayDialFrequency();
  readSettings();            //Restore user's setup params

  m_networkThread.start(m_networkThreadPriority);
  m_audioThread.start (m_audioThreadPriority);
  m_notificationAudioThread.start(m_notificationAudioThreadPriority);
  m_decoder.start(m_decoderThreadPriority);

#ifdef WIN32
  if (!m_multiple)
    {
      while(true)
        {
          int iret=killbyname("js8.exe");
          if(iret == 603) break;
          if(iret != 0)
            MessageBox::warning_message (this, tr ("Error Killing js8.exe Process")
                                         , tr ("KillByName return code: %1")
                                         .arg (iret));
        }
    }
#endif

  initDecoderSubprocess();

  QString fname {QDir::toNativeSeparators(m_config.writeable_data_dir ().absoluteFilePath ("wsjtx_wisdom.dat"))};
  QByteArray cfname=fname.toLocal8Bit();
  fftwf_import_wisdom_from_filename(cfname);

//  Q_EMIT startAudioInputStream (m_config.audio_input_device (), m_framesAudioInputBuffered, &m_detector, m_downSampleFactor, m_config.audio_input_channel ());
  Q_EMIT startAudioInputStream (m_config.audio_input_device (), m_framesAudioInputBuffered, m_detector, m_downSampleFactor, m_config.audio_input_channel ());
  Q_EMIT initializeAudioOutputStream (m_config.audio_output_device (), AudioDevice::Mono == m_config.audio_output_channel () ? 1 : 2, m_msAudioOutputBuffered);
  Q_EMIT initializeNotificationAudioOutputStream(m_config.notification_audio_output_device(), m_msAudioOutputBuffered);
  Q_EMIT transmitFrequency (freq() - m_XIT);

  enable_DXCC_entity (m_config.DXCC ());  // sets text window proportions and (re)inits the logbook

  // this must be done before initializing the mode as some modes need
  // to turn off split on the rig e.g. WSPR
  m_config.transceiver_online ();

  setupJS8();

  Q_EMIT transmitFrequency (freq() - m_XIT);

  if((m_ndepth&7)==1) ui->actionQuickDecode->setChecked(true);
  if((m_ndepth&7)==2) ui->actionMediumDecode->setChecked(true);
  if((m_ndepth&7)==3) ui->actionDeepDecode->setChecked(true);
  if((m_ndepth&7)==4) ui->actionDeepestDecode->setChecked(true);

  statusChanged();

  connect (&minuteTimer, &QTimer::timeout, this, &MainWindow::on_the_minute);
  minuteTimer.setSingleShot (true);
  minuteTimer.start (ms_minute_error () + 60 * 1000);

  QTimer::singleShot (0, this, &MainWindow::checkStartupWarnings);

  // UI Customizations & Tweaks
  ui->horizontalLayoutBand->insertSpacing(1, 6);
  ui->horizontalLayoutBand->insertWidget (2, m_wideGraph.data(), 1);
  ui->horizontalLayoutBand->insertSpacing(3, 8);

  // remove disabled menus from the menu bar
  foreach(auto action, ui->menuBar->actions()){
      if(action->isEnabled()){
          continue;
      }
      ui->menuBar->removeAction(action);
  }

  //auto f = findFreeFreqOffset(1000, 2000, 50);
  //setFreqOffsetForRestore(f, false);

  ui->actionModeAutoreply->setChecked(m_config.autoreply_on_at_startup());
  ui->spotButton->setChecked(m_config.spot_to_reporting_networks());

  QActionGroup * modeActionGroup = new QActionGroup(this);
  ui->actionModeJS8Normal->setActionGroup(modeActionGroup);
  ui->actionModeJS8Fast->setActionGroup(modeActionGroup);
  ui->actionModeJS8Turbo->setActionGroup(modeActionGroup);
  ui->actionModeJS8Slow->setActionGroup(modeActionGroup);
  ui->actionModeJS8Ultra->setActionGroup(modeActionGroup);

  ui->modeButton->installEventFilter(new EventFilter::MouseButtonPress([this](QMouseEvent * event)
  {
    ui->menuModeJS8->popup(event->globalPosition().toPoint());
    return true;
  }, this));

  if (!JS8_ENABLE_JS8A) ui->actionModeJS8Normal->setVisible(false);
  if (!JS8_ENABLE_JS8B) ui->actionModeJS8Fast->setVisible(false);
  if (!JS8_ENABLE_JS8C) ui->actionModeJS8Turbo->setVisible(false);
  if (!JS8_ENABLE_JS8E) ui->actionModeJS8Slow->setVisible(false);
  if (!JS8_ENABLE_JS8I) ui->actionModeJS8Ultra->setVisible(false);

  // prep
  prepareMonitorControls();
  prepareHeartbeatMode(canCurrentModeSendHeartbeat() && ui->actionModeJS8HB->isChecked());

  ui->extFreeTextMsgEdit->installEventFilter(new EventFilter::EnterKeyPress([this](QKeyEvent * const event)
  {
    if (event->modifiers() & Qt::ShiftModifier) return false;
    if (ui->extFreeTextMsgEdit->isReadOnly())   return false;

    if (ui->extFreeTextMsgEdit->toPlainText().trimmed().isEmpty()) return true;
    if (!ensureCanTransmit())                                      return true;
    if (!ensureCallsignSet(true))                                  return true;

    toggleTx(true);
    return true;
  }, this));

  ui->textEditRX->viewport()->installEventFilter(new EventFilter::MouseButtonDblClick([this](QMouseEvent *)
  {
    QTimer::singleShot(150, this, [this]()
    {
      // When we double click the rx window, we send the selected text to
      // the log dialog when the text could be an snr value prefixed with
      // a - or +, we extend the selection to include it.

      auto textCursor = ui->textEditRX->textCursor();
      auto text       = textCursor.selectedText();

      if (text.isEmpty()) return;

      auto const start = textCursor.selectionStart();
      auto const end   = textCursor.selectionEnd();

      textCursor.clearSelection();
      textCursor.setPosition(start);
      textCursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor);
      textCursor.movePosition(QTextCursor::NextCharacter,     QTextCursor::KeepAnchor, 1 + end - start);

      if (auto const prev = textCursor.selectedText();
                     prev.startsWith("-") ||
                     prev.startsWith("+"))
      {
          ui->textEditRX->setTextCursor(textCursor);
          text = prev;
      }

      m_logDlg->acceptText(text);
    });
    return false;
  }, this));

  auto clearActionSep = new QAction(nullptr);
  clearActionSep->setSeparator(true);

  auto clearActionAll = new QAction(QString("Clear All"), nullptr);
  connect(clearActionAll, &QAction::triggered, this, [this](){
      if (QMessageBox::Yes != QMessageBox::question(this, "Clear All Activity", "Are you sure you would like to clear all activity?", QMessageBox::Yes|QMessageBox::No)){
          return;
      }

      clearActivity();
  });

  // setup tablewidget context menus
  auto clearAction1 = new QAction(QString("Clear"), ui->textEditRX);
  connect(clearAction1, &QAction::triggered, this, [this]()
  {
    clearRXActivity();
  });

  auto saveAction = new QAction(QString("Save As..."), ui->textEditRX);
  connect(saveAction, &QAction::triggered, this, [this](){
    auto writePath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    auto writeDir = QDir(writePath);
    auto defaultFilename = writeDir.absoluteFilePath(QString("js8call-%1.txt").arg(DriftingDateTime::currentDateTimeUtc().toString("yyyyMMdd")));

    QString selectedFilter = "*.txt";

    auto filename = QFileDialog::getSaveFileName(this,
        "Save As...",
        defaultFilename,
        "Text files (*.txt);; All files (*)",
        &selectedFilter
    );
    if(filename.isEmpty()){
        return;
    }

    auto text = ui->textEditRX->toPlainText();
    QFile f(filename);
    if (f.open(QIODevice::Truncate | QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&f);
        stream << text;
    }
  });

  ui->textEditRX->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->textEditRX, &QTableWidget::customContextMenuRequested, this, [this, clearAction1, clearActionAll, saveAction](QPoint const &point){
      QMenu * menu = new QMenu(ui->textEditRX);

      buildEditMenu(menu, ui->textEditRX);

      menu->addSeparator();

      menu->addAction(clearAction1);
      menu->addAction(clearActionAll);

      menu->addSeparator();
      menu->addAction(saveAction);

      menu->popup(ui->textEditRX->mapToGlobal(point));

  });

  auto clearAction2 = new QAction(QString("Clear"), ui->extFreeTextMsgEdit);
  connect(clearAction2, &QAction::triggered, this, [this]()
  {
    resetMessage();
    m_lastTxMessage.clear();
  });

  auto restoreAction = new QAction(QString("Restore Previous Message"), ui->extFreeTextMsgEdit);
  connect(restoreAction, &QAction::triggered, this, [this](){ this->restoreMessage(); });

  ui->extFreeTextMsgEdit->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->extFreeTextMsgEdit, &QTableWidget::customContextMenuRequested, this, [this, clearAction2, clearActionAll, restoreAction](QPoint const &point){
    QMenu * menu = new QMenu(ui->extFreeTextMsgEdit);

    auto selectedCall = callsignSelected();
    bool missingCallsign = selectedCall.isEmpty();

    buildSuggestionsMenu(menu, ui->extFreeTextMsgEdit, point);

    restoreAction->setDisabled(m_lastTxMessage.isEmpty());
    menu->addAction(restoreAction);

    auto savedMenu = menu->addMenu("Saved Messages...");
    buildSavedMessagesMenu(savedMenu);

    auto directedMenu = menu->addMenu(QString("Directed to %1...").arg(selectedCall));
    directedMenu->setDisabled(missingCallsign);
    buildQueryMenu(directedMenu, selectedCall);

    auto relayMenu = menu->addMenu("Relay via...");
    relayMenu->setDisabled(ui->extFreeTextMsgEdit->toPlainText().isEmpty() || m_callActivity.isEmpty());
    buildRelayMenu(relayMenu);

    menu->addSeparator();

    buildEditMenu(menu, ui->extFreeTextMsgEdit);

    menu->addSeparator();

    menu->addAction(clearAction2);
    menu->addAction(clearActionAll);

    menu->popup(ui->extFreeTextMsgEdit->mapToGlobal(point));

    displayActivity(true);
  });



  auto clearAction3 = new QAction(QString("Clear"), ui->tableWidgetRXAll);
  connect(clearAction3, &QAction::triggered, this, [this]()
  {
    clearBandActivity();
  });

  auto removeActivity = new QAction(QString("Remove Activity"), ui->tableWidgetRXAll);
  connect(removeActivity, &QAction::triggered, this, [this](){
      if(ui->tableWidgetRXAll->selectedItems().isEmpty()){
          return;
      }

      auto selectedItems = ui->tableWidgetRXAll->selectedItems();
      int selectedOffset = selectedItems.first()->data(Qt::UserRole).toInt();

      m_bandActivity.remove(selectedOffset);
      displayActivity(true);
  });

  auto logAction = new QAction(QString("Log..."), ui->tableWidgetCalls);
  connect(logAction, &QAction::triggered, this, &MainWindow::on_logQSOButton_clicked);


  ui->tableWidgetRXAll->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->tableWidgetRXAll->horizontalHeader(), &QHeaderView::customContextMenuRequested, this, [this](QPoint const &point){
      QMenu * menu = new QMenu(ui->tableWidgetRXAll);

      QMenu * sortByMenu = menu->addMenu("Sort By...");
      buildBandActivitySortByMenu(sortByMenu);

      QMenu * showColumnsMenu = menu->addMenu("Show Columns...");
      buildShowColumnsMenu(showColumnsMenu, "band");

      menu->popup(ui->tableWidgetRXAll->horizontalHeader()->mapToGlobal(point));
  });


  ui->tableWidgetRXAll->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->tableWidgetRXAll, &QTableWidget::customContextMenuRequested, this, [this, clearAction3, clearActionAll, removeActivity, logAction](QPoint const &point){
    QMenu * menu = new QMenu(ui->tableWidgetRXAll);

    // clear the selection of the call widget on right click
    // but only if the table has rows.
    if(ui->tableWidgetRXAll->rowAt(point.y()) != -1){
        ui->tableWidgetCalls->selectionModel()->clearSelection();
    }

    QString selectedCall = callsignSelected();
    bool missingCallsign = selectedCall.isEmpty();
    bool isAllCall = isAllCallIncluded(selectedCall);

    int selectedOffset = -1;
    if(!ui->tableWidgetRXAll->selectedItems().isEmpty()){
        auto selectedItems = ui->tableWidgetRXAll->selectedItems();
        selectedOffset = selectedItems.first()->data(Qt::UserRole).toInt();
    }

    if(selectedOffset != -1){
        auto qsyAction = menu->addAction(QString("Jump to %1Hz").arg(selectedOffset));
        connect(qsyAction, &QAction::triggered, this, [this, selectedOffset](){
            setFreqOffsetForRestore(selectedOffset, false);
        });

        if(m_wideGraph->filterEnabled()){
            auto filterQsyAction = menu->addAction(QString("Center filter at %1Hz").arg(selectedOffset));
            connect(filterQsyAction, &QAction::triggered, this, [this, selectedOffset](){
                m_wideGraph->setFilterCenter(selectedOffset);
            });
        }

        auto items = m_bandActivity.value(selectedOffset);
        if(!items.isEmpty()){
            int submode = items.last().submode;
            auto speed = JS8::Submode::name(submode);
            if(submode != m_nSubMode){
                auto qrqAction = menu->addAction(QString("Jump to %1%2 speed").arg(speed.left(1)).arg(speed.mid(1).toLower()));
                connect(qrqAction, &QAction::triggered, this, [this, submode](){
                    setSubmode(submode);
                });
            }

            int tdrift = -int(items.last().tdrift * 1000);
            auto qtrAction = menu->addAction(QString("Jump to %1 ms time drift").arg(tdrift));
            connect(qtrAction, &QAction::triggered, this, [this, tdrift](){
                setDrift(tdrift);
            });
        }

        menu->addSeparator();
    }

    menu->addAction(logAction);
    logAction->setDisabled(missingCallsign || isAllCall);

    menu->addSeparator();

    auto savedMenu = menu->addMenu("Saved Messages...");
    buildSavedMessagesMenu(savedMenu);

    auto directedMenu = menu->addMenu(QString("Directed to %1...").arg(selectedCall));
    directedMenu->setDisabled(missingCallsign);
    buildQueryMenu(directedMenu, selectedCall);

    auto relayAction = buildRelayAction(selectedCall);
    relayAction->setText(QString("Relay via %1...").arg(selectedCall));
    relayAction->setDisabled(missingCallsign);
    menu->addActions({ relayAction });

    auto deselectAction = menu->addAction(QString("Deselect %1").arg(selectedCall));
    deselectAction->setDisabled(missingCallsign);
    connect(deselectAction, &QAction::triggered, this, [this](){
        ui->tableWidgetRXAll->clearSelection();
        ui->tableWidgetCalls->clearSelection();
    });

    menu->addSeparator();

    removeActivity->setDisabled(selectedOffset == -1);
    menu->addAction(removeActivity);

    menu->addSeparator();
    menu->addAction(clearAction3);
    menu->addAction(clearActionAll);

    menu->popup(ui->tableWidgetRXAll->mapToGlobal(point));

    displayActivity(true);
  });




  auto clearAction4 = new QAction(QString("Clear"), ui->tableWidgetCalls);
  connect(clearAction4, &QAction::triggered, this, [this]()
  {
    clearCallActivity();
  });

  auto addStation = new QAction(QString("Add New Station or Group..."), ui->tableWidgetCalls);
  connect(addStation, &QAction::triggered, this, [this](){
      bool ok = false;
      QString callsign = QInputDialog::getText(this, tr("Add New Station or Group"),
                                               tr("Station or Group Callsign:"), QLineEdit::Normal,
                                               "", &ok).toUpper().trimmed();
      if(!ok || callsign.trimmed().isEmpty()){
         return;
      }

      // if we're adding allcall, turn off allcall avoidance
      if(callsign == "@ALLCALL"){
          m_config.set_avoid_allcall(false);
      }
      else if(callsign.startsWith("@")){
          if(Varicode::isCompoundCallsign(callsign)){
              m_config.addGroup(callsign);
          } else {
              MessageBox::critical_message (this, QString("%1 is not a valid group").arg(callsign));
          }

      } else {
          if(Varicode::isValidCallsign(callsign, nullptr)){
              CallDetail cd = {};
              cd.call = callsign;
              m_callActivity[callsign] = cd;
          } else {
              MessageBox::critical_message (this, QString("%1 is not a valid callsign or group").arg(callsign));
          }
      }

      displayActivity(true);
  });

  auto removeStation = new QAction(QString("Remove Station"), ui->tableWidgetCalls);
  connect(removeStation, &QAction::triggered, this, [this](){
      QString selectedCall = callsignSelected();
      if(selectedCall.isEmpty()){
          return;
      }

      if (selectedCall == "@ALLCALL"){
          m_config.set_avoid_allcall(true);
      }
      else if (selectedCall.startsWith("@")){
          m_config.removeGroup(selectedCall);
      }
      else if(m_callActivity.contains(selectedCall)){
          m_callActivity.remove(selectedCall);
      }

      displayActivity(true);
  });

  connect(ui->actionShow_Message_Inbox, &QAction::triggered, this, [this](){
      QString selectedCall = callsignSelected();
      if(selectedCall.isEmpty()){
          selectedCall = "%";
      }

      Inbox inbox(inboxPath());
      if(!inbox.open()){
          return;
      }

      QList<QPair<int, Message> > msgs;

      msgs.append(inbox.values("STORE", "$.params.TO", selectedCall, 0, 1000));

      msgs.append(inbox.values("READ", "$.params.FROM", selectedCall, 0, 1000));

      foreach(auto pair, inbox.values("UNREAD", "$.params.FROM", selectedCall, 0, 1000)){
          msgs.append(pair);

          // mark as read
          auto msg = pair.second;
          msg.setType("READ");
          inbox.set(pair.first, msg);
      }

      std::stable_sort(msgs.begin(), msgs.end(), [](QPair<int, Message> const &a, QPair<int, Message> const &b){
          return QVariant::compare(a.second.params().value("UTC"),
                                   b.second.params().value("UTC")) == QPartialOrdering::Greater;
      });

      auto mw = new MessageWindow(this);
      connect(mw, &MessageWindow::finished, this, [this](int){
          refreshInboxCounts();
          displayCallActivity();
      });
      connect(mw, &MessageWindow::deleteMessage, this, [this](int id){
          Inbox inbox(inboxPath());
          if(!inbox.open()){
              return;
          }

          inbox.del(id);
      });
      connect(mw, &MessageWindow::replyMessage, this, [this, mw](const QString &text){
          addMessageText(text, true, true);
          refreshInboxCounts();
          displayCallActivity();
          mw->close();
      });
      mw->setCall(selectedCall);
      mw->populateMessages(msgs);
      mw->show();
  });

  auto historyAction = new QAction(QString("Show Message Inbox..."), ui->tableWidgetCalls);
  connect(historyAction, &QAction::triggered, ui->actionShow_Message_Inbox, &QAction::trigger);

  auto localMessageAction = new QAction(QString("Store Message..."), ui->tableWidgetCalls);
  connect(localMessageAction, &QAction::triggered, this, [this](){
      QString selectedCall = callsignSelected();
      if(selectedCall.isEmpty()){
          return;
      }

      auto m = new MessageReplyDialog(this);
      m->setWindowTitle("Message");
      m->setLabel(QString("Store this message locally for %1:").arg(selectedCall));
      if(m->exec() != QMessageBox::Accepted){
          return;
      }

      CommandDetail d = {};
      d.cmd = " MSG ";
      d.to = selectedCall;
      d.from = m_config.my_callsign();
      d.relayPath = d.from;
      d.text = m->textValue();
      d.utcTimestamp = DriftingDateTime::currentDateTimeUtc();
      d.submode = m_nSubMode;

      addCommandToStorage("STORE", d);
  });

  ui->tableWidgetCalls->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->tableWidgetCalls->horizontalHeader(), &QHeaderView::customContextMenuRequested, this, [this](QPoint const &point){
      QMenu * menu = new QMenu(ui->tableWidgetCalls);

      QMenu * sortByMenu = menu->addMenu("Sort By...");
      buildCallActivitySortByMenu(sortByMenu);

      QMenu * showColumnsMenu = menu->addMenu("Show Columns...");
      buildShowColumnsMenu(showColumnsMenu, "call");

      menu->popup(ui->tableWidgetCalls->horizontalHeader()->mapToGlobal(point));
  });

  ui->tableWidgetCalls->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->tableWidgetCalls, &QTableWidget::customContextMenuRequested, this, [this, logAction, historyAction, localMessageAction, clearAction4, clearActionAll, addStation, removeStation](QPoint const &point){
    QMenu * menu = new QMenu(ui->tableWidgetCalls);

    // clear the selection of the call widget on right click
    // but only if the table has rows.
    if(ui->tableWidgetCalls->rowAt(point.y()) != -1){
        ui->tableWidgetRXAll->selectionModel()->clearSelection();
    }

    QString selectedCall = callsignSelected();
    bool isAllCall = isAllCallIncluded(selectedCall);
    bool isGroupCall = isGroupCallIncluded(selectedCall);
    bool missingCallsign = selectedCall.isEmpty();

    if(!missingCallsign && !isAllCall){
        int selectedOffset = m_callActivity[selectedCall].offset;
        if(selectedOffset != -1){
            auto qsyAction = menu->addAction(QString("Jump to %1Hz").arg(selectedOffset));
            connect(qsyAction, &QAction::triggered, this, [this, selectedOffset](){
                setFreqOffsetForRestore(selectedOffset, false);
            });

            if(m_wideGraph->filterEnabled()){
                auto filterQsyAction = menu->addAction(QString("Center filter at %1Hz").arg(selectedOffset));
                connect(filterQsyAction, &QAction::triggered, this, [this, selectedOffset](){
                    m_wideGraph->setFilterCenter(selectedOffset);
                });
            }

            int submode = m_callActivity[selectedCall].submode;
            auto speed  = JS8::Submode::name(submode);
            if(submode != m_nSubMode){
                auto qrqAction = menu->addAction(QString("Jump to %1%2 speed").arg(speed.left(1)).arg(speed.mid(1).toLower()));
                connect(qrqAction, &QAction::triggered, this, [this, submode](){
                    setSubmode(submode);
                });
            }

            int tdrift = -int(m_callActivity[selectedCall].tdrift * 1000);
            auto qtrAction = menu->addAction(QString("Jump to %1 ms time drift").arg(tdrift));
            connect(qtrAction, &QAction::triggered, this, [this, tdrift](){
                setDrift(tdrift);
            });

            menu->addSeparator();
        }
    }

    menu->addAction(logAction);
    logAction->setDisabled(missingCallsign || isAllCall);

    menu->addAction(historyAction);
    historyAction->setDisabled(missingCallsign || isAllCall || isGroupCall || !hasMessageHistory(selectedCall));

    menu->addAction(localMessageAction);
    localMessageAction->setDisabled(missingCallsign || isAllCall || isGroupCall);

    menu->addSeparator();

    auto savedMenu = menu->addMenu("Saved Messages...");
    buildSavedMessagesMenu(savedMenu);

    auto directedMenu = menu->addMenu(QString("Directed to %1...").arg(selectedCall));
    directedMenu->setDisabled(missingCallsign);
    buildQueryMenu(directedMenu, selectedCall);

    auto relayAction = buildRelayAction(selectedCall);
    relayAction->setText(QString("Relay via %1...").arg(selectedCall));
    relayAction->setDisabled(missingCallsign || isAllCall);
    menu->addActions({ relayAction });

    auto deselect = menu->addAction(QString("Deselect %1").arg(selectedCall));
    deselect->setDisabled(missingCallsign);
    connect(deselect, &QAction::triggered, this, [this](){
        ui->tableWidgetRXAll->clearSelection();
        ui->tableWidgetCalls->clearSelection();
    });

    menu->addSeparator();

    menu->addAction(addStation);
    removeStation->setDisabled(missingCallsign);
    removeStation->setText(selectedCall.startsWith("@") ? "Remove Group" : "Remove Station");
    menu->addAction(removeStation);

    menu->addSeparator();
    menu->addAction(clearAction4);
    menu->addAction(clearActionAll);

    menu->popup(ui->tableWidgetCalls->mapToGlobal(point));
  });

  connect(ui->tableWidgetRXAll->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::tableSelectionChanged);
  connect(ui->tableWidgetCalls->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::tableSelectionChanged);

  auto p = ui->tableWidgetRXAll->palette();
  p.setColor(QPalette::Inactive, QPalette::Highlight, p.color(QPalette::Active, QPalette::Highlight));
  ui->tableWidgetRXAll->setPalette(p);

  p = ui->tableWidgetCalls->palette();
  p.setColor(QPalette::Inactive, QPalette::Highlight, p.color(QPalette::Active, QPalette::Highlight));
  ui->tableWidgetCalls->setPalette(p);

  ui->hbMacroButton->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->hbMacroButton, &QPushButton::customContextMenuRequested, this, [this](QPoint const &point){
      QMenu * menu = new QMenu(ui->hbMacroButton);

      buildHeartbeatMenu(menu);

      menu->popup(ui->hbMacroButton->mapToGlobal(point));
  });

  ui->cqMacroButton->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->cqMacroButton, &QPushButton::customContextMenuRequested, this, [this](QPoint const &point){
      QMenu * menu = new QMenu(ui->cqMacroButton);

      buildCQMenu(menu);

      menu->popup(ui->cqMacroButton->mapToGlobal(point));
  });

  // Don't block heartbeat's first run...
  m_lastTxStartTime = DriftingDateTime::currentDateTimeUtc().addSecs(-300);

  // But do block the decoder's first run until 50% through next transmit period
  m_lastTxStopTime = nextTransmitCycle().addSecs(-m_TRperiod/2);

  int width = 75;
  /*
  QList<QPushButton*> btns;
  foreach(auto child, ui->buttonGrid->children()){
      if(!child->isWidgetType()){
          continue;
      }

      if(!child->objectName().contains("Button")){
          continue;
      }

      auto b = qobject_cast<QPushButton*>(child);
      width = qMax(width, b->geometry().width());
      btns.append(b);
  }
  */
  foreach(auto child, ui->buttonGrid->children()){
      if(!child->isWidgetType()){
          continue;
      }

      if(!child->objectName().contains("Button")){
          continue;
      }

      auto b = qobject_cast<QPushButton*>(child);
      b->setCursor(QCursor(Qt::PointingHandCursor));
  }
  auto buttonLayout = ui->buttonGrid->layout();
  auto gridButtonLayout = qobject_cast<QGridLayout*>(buttonLayout);
  gridButtonLayout->setColumnMinimumWidth(0, width);
  gridButtonLayout->setColumnMinimumWidth(1, width);
  gridButtonLayout->setColumnMinimumWidth(2, width);
  gridButtonLayout->setColumnStretch(0, 1);
  gridButtonLayout->setColumnStretch(1, 1);
  gridButtonLayout->setColumnStretch(2, 1);

  // dial up and down buttons sizes
  ui->dialFreqUpButton->setFixedSize(30, 24);
  ui->dialFreqDownButton->setFixedSize(30, 24);

  // Prepare spotting configuration...
  prepareApi();
  prepareSpotting();

  displayActivity(true);

  m_txTextDirtyDebounce.setSingleShot(true);
  connect(&m_txTextDirtyDebounce, &QTimer::timeout, this, &MainWindow::refreshTextDisplay);

  QTimer::singleShot(500, this, &MainWindow::initializeDummyData);

  // this must be the last statement of constructor
  if (!m_valid) throw std::runtime_error {"Fatal initialization exception"};
}

void MainWindow::initDecoderSubprocess(){
    //delete any .quit file that might have been left lying around
    //since its presence will cause jt9 to exit a soon as we start it
    //and decodes will hang
    {
        QFile quitFile {m_config.temp_dir ().absoluteFilePath (".quit")};
        while (quitFile.exists ())
        {
            if (!quitFile.remove ())
            {
                MessageBox::query_message (this, tr ("Error removing \"%1\"").arg (quitFile.fileName ())
                                 , tr ("Click OK to retry"));
            }
        }
    }

    //Create .lock so jt9 will wait
    if(JS8_DEBUG_DECODE) qDebug() << "decoder lock create";
    QFile {m_config.temp_dir ().absoluteFilePath (".lock")}.open(QIODevice::ReadWrite);

    // create path
    QString path = QDir::toNativeSeparators(m_appDir) + QDir::separator() + "js8";

    // create args
    QStringList args {
      "-s", QApplication::applicationName () // shared memory key,
                                             // includes rig
  #ifdef NDEBUG
        , "-w", "1"               //FFTW patience - release
  #else
        , "-w", "1"               //FFTW patience - debug builds for speed
  #endif
        // The number  of threads for  FFTW specified here is  chosen as
        // three because  that gives  the best  throughput of  the large
        // FFTs used  in jt9.  The count  is the minimum of  (the number
        // available CPU threads less one) and three.  This ensures that
        // there is always at least one free CPU thread to run the other
        // mode decoder in parallel.
        , "-m", QString::number (qMin (qMax (QThread::idealThreadCount () - 1, 1), 3)) //FFTW threads

        , "-e", QDir::toNativeSeparators (m_appDir)
        , "-a", QDir::toNativeSeparators (m_config.writeable_data_dir ().absolutePath ())
        , "-t", QDir::toNativeSeparators (m_config.temp_dir ().absolutePath ())
    };

    // initialize
    m_decoder.processStart(path, args);

    // reset decode busy
    if(m_decoderBusy){
        decodeBusy(false);
    }

    if(!m_valid){
        m_valid = true;
    }
}

void
MainWindow::checkVersion(bool const alertOnUpToDate)
{
  auto m = new QNetworkAccessManager(this);
  connect(m, &QNetworkAccessManager::finished, this, [this, alertOnUpToDate](QNetworkReply * reply){
    if(reply->error()){
        qDebug() << "Checking for Updates Error:" << reply->errorString();
        return;
    }

    QString content = reply->readAll().trimmed();

    auto const currentVersion = QVersionNumber::fromString(version());
    auto const networkVersion = QVersionNumber::fromString(content);

    qDebug() << "Checking Version" << currentVersion << "with" << networkVersion;

    if(currentVersion < networkVersion){

        SelfDestructMessageBox * m = new SelfDestructMessageBox(60,
          "New Updates Available",
          QString("A new version (%1) of JS8Call is now available. Please see js8call.com for more details.").arg(content),
          QMessageBox::Information,
          QMessageBox::Ok,
          QMessageBox::Ok,
          false,
          this);

        m->show();

    } else if(alertOnUpToDate){

        SelfDestructMessageBox * m = new SelfDestructMessageBox(60,
          "No Updates Available",
          QString("Your version (%1) of JS8Call is up-to-date.").arg(version()),
          QMessageBox::Information,
          QMessageBox::Ok,
          QMessageBox::Ok,
          false,
          this);

        m->show();
    }
  });

  qDebug() << "Checking for Updates...";
  QUrl url("http://files.js8call.com/version.txt");
  QNetworkRequest r(url);
  m->get(r);
}

void MainWindow::checkStartupWarnings ()
{
  if(m_config.check_for_updates()){
      checkVersion(false);
  }
  ensureCallsignSet(false);
}

void MainWindow::initializeDummyData(){
    if(!QApplication::applicationName().contains("dummy")){
        return;
    }

#if 0
    Codeword all;
    foreach(CodewordPair p, JSC::compress("")){
        all.append(p.first);
    }
    qDebug() << all;
    qDebug() << JSC::decompress(all) << (JSC::decompress(all) == "HELLO WORLD ");
    exit(-1);
#endif

#if 0
    auto t = new QTimer(this);
    t->setInterval(150);
    connect(t, &QTimer::timeout, this, [this](){
        if(!ui->extFreeTextMsgEdit->hasFocus()){
            return;
        }

        auto c = ui->extFreeTextMsgEdit->textCursor();
        int pos = qMin(c.selectionStart(), c.selectionEnd());

        if(pos <= 5 && c.selectionStart() != c.selectionEnd()){
            c.clearSelection();
            ui->extFreeTextMsgEdit->setTextCursor(c);
        }
    });
    t->start();

    auto kpe = new KeyPressEater();
    connect(kpe, &KeyPressEater::keyPressed, this, [this](QObject *obj, QKeyEvent * e, bool *pProcessed){
        auto t = e->text();
        auto c = ui->extFreeTextMsgEdit->textCursor();
        int pos = qMin(c.selectionStart(), c.selectionEnd());

        if(e->key() == Qt::Key_Escape){
            *pProcessed = false;
            return;
        }

        QTextCursor cc(c);
        cc.setPosition(5);
        cc.movePosition(QTextCursor::NextWord);
        int cpos = qMax(cc.selectionStart(), cc.selectionEnd());
        if(e->key() == Qt::Key_Backspace && e->modifiers().testFlag(Qt::ControlModifier) && pos < cpos){
            *pProcessed = true;
            return;
        }

        if((e->key() == Qt::Key_Backspace && pos <= 5) ||
           (e->key() == Qt::Key_Delete && pos < 5)){
            *pProcessed = true;
            return;
        }

        if(e->key() == Qt::Key_V && e->modifiers().testFlag(Qt::ControlModifier) && pos <= 5){
            *pProcessed = true;
            return;
        }

        if(!t.isEmpty() && pos < 5){
            *pProcessed = true;
            return;
        }
    });
    ui->extFreeTextMsgEdit->installEventFilter(kpe);

    connect(ui->extFreeTextMsgEdit, &QTextEdit::copyAvailable, this, [this](bool available){
        if(!available){
            return;
        }
        auto c = ui->extFreeTextMsgEdit->textCursor();

        qDebug() << "select" << c.selectionStart() << c.selectionEnd();

        int pos = qMin(c.selectionStart(), c.selectionEnd());
        if(pos <= 5){
            auto text = c.selectedText();
            if(!text.isEmpty()){
                c.clearSelection();
                ui->extFreeTextMsgEdit->setTextCursor(c);
            }
        }
    });

    /*
    connect(ui->extFreeTextMsgEdit->document(), &QTextDocument::cursorPositionChanged, this, [this](const QTextCursor &){
        auto c = ui->extFreeTextMsgEdit->textCursor();
        int pos = qMin(c.selectionStart(), c.selectionEnd());
        if(pos <= 5){
            auto text = c.selectedText();
            if(!text.isEmpty()){
                c.clearSelection();
                ui->extFreeTextMsgEdit->setTextCursor(c);
            }
        }
    });

    connect(ui->extFreeTextMsgEdit, &QTextEdit::cursorPositionChanged, this, [this](){
        auto c = ui->extFreeTextMsgEdit->textCursor();
        int pos = qMin(c.selectionStart(), c.selectionEnd());

        if(pos <= 5){
            auto text = c.selectedText();
            if(!text.isEmpty()){
                c.clearSelection();
                ui->extFreeTextMsgEdit->setTextCursor(c);
            }
        }
    });
    */

    /*connect(ui->extFreeTextMsgEdit->document(), &QTextDocument::contentsChange, this, [this](int from, int removed, int added){
        if(from < 5 && removed == 1){
            ui->extFreeTextMsgEdit->document()->blockSignals(true);
            ui->extFreeTextMsgEdit->document()->undo();
            ui->extFreeTextMsgEdit->document()->clearUndoRedoStacks(QTextDocument::RedoStack);
            ui->extFreeTextMsgEdit->document()->blockSignals(false);
        }
    });*/

    ui->extFreeTextMsgEdit->setText("HELLO BRAVE NEW WORLD");
    auto c = ui->extFreeTextMsgEdit->textCursor();
    c.setPosition(0);
    c.setPosition(5, QTextCursor::KeepAnchor);
    auto f = c.charFormat();
    f.setFontStrikeOut(true);
    c.setCharFormat(f);
#endif

    // this causes a segfault!
    processDecodedLine("223000 -15 -0.3 1681 B  6t++yk+aJbaE         6   \n");

    ui->extFreeTextMsgEdit->setPlainText("HELLOBRAVE NEW WORLD");
    ui->extFreeTextMsgEdit->setCharsSent(6);

    logHeardGraph("KN4CRD", "OH8STN");
    logHeardGraph("KN4CRD", "K0OG");
    logHeardGraph("K0OG", "KN4CRD");

    auto path = QDir::toNativeSeparators(m_config.writeable_data_dir ().absoluteFilePath(QString("test.db3")));
    auto inbox = Inbox(path);
    if(inbox.open()){
        qDebug() << "test inbox opened" << inbox.count("test", "$", "%") << "messages";

        // int i = inbox.append(Message("test", "booya1"));

        int i = inbox.append(Message("test", "booya2"));
        qDebug() << "i" << i;

        qDebug() << inbox.set(i, Message("test", "booya3"));

        auto m = inbox.value(i);
        qDebug() << QString(m.toJson());

        qDebug() << inbox.del(i);

        foreach(auto pair, inbox.values("test", "$", "%", 0, 5)){
            qDebug() << pair.first << QString(pair.second.toJson());
        }
    }

    auto d = DecodedText("SN5-lUuJkby0", Varicode::JS8CallFirst, 1);
    qDebug () << "KN4CRD: K0OG ===>" << d.message();

    // qDebug() << Varicode::isValidCallsign("@GROUP1", nullptr);
    // qDebug() << Varicode::packAlphaNumeric50("VE7/KN4CRD");
    // qDebug() << Varicode::unpackAlphaNumeric50(Varicode::packAlphaNumeric50("VE7/KN4CRD"));
    // qDebug() << Varicode::unpackAlphaNumeric50(Varicode::packAlphaNumeric50("@GROUP/42"));
    // qDebug() << Varicode::unpackAlphaNumeric50(Varicode::packAlphaNumeric50("SP1ATOM"));

    if(!m_config.my_groups().contains("@GROUP42")){
        m_config.addGroup("@GROUP42");
    }

    QList<QString> calls = {
        "KN4CRD",
        "VE7/KN4CRD",
        "KN4CRD/P",
        "KC9QNE",
        "KI6SSI",
        "K0OG",
        "LB9YH",
        "VE7/LB9YH",
        "M0IAX",
        "N0JDS",
        "OH8STN",
        "VA3OSO",
        "VK1MIC",
        "W0FW"
    };

    auto dt = DriftingDateTime::currentDateTimeUtc().addSecs(-300);

    int i = 0;
    foreach(auto call, calls){
        CallDetail cd = {};
        cd.call = call;
        cd.through = i == 2 ? "KN4CRD" : "";
        cd.dial = 7078000;
        cd.offset = 500 + 100*i;
        cd.snr = i == 3 ? -100 : i;
        cd.ackTimestamp = i == 1 ? dt.addSecs(-900) : QDateTime{};
        cd.utcTimestamp = dt;
        cd.grid = i == 5 ? "J042" : i == 6 ? " FN42FN42FN" : "";
        cd.tdrift = 0.1*i;
        cd.submode = i % 3;
        logCallActivity(cd, false);

        ActivityDetail ad = {};
        ad.bits = Varicode::JS8CallFirst | Varicode::JS8CallLast;
        ad.snr = i == 3 ? -100 : i;
        ad.dial = 7078000;
        ad.offset = 500 + 100*i;
        ad.text = QString("%1: %2 TEST MESSAGE").arg(call).arg(m_config.my_callsign());
        ad.utcTimestamp = dt;
        ad.submode = cd.submode;
        m_bandActivity[500+100*i] = { ad };

        markOffsetDirected(500+100*i, false);

        i++;
    }

    ActivityDetail adHB1 = {};
    adHB1.bits = Varicode::JS8CallFirst;
    adHB1.snr = 0;
    adHB1.dial = 7078000;
    adHB1.offset = 750;
    adHB1.text = QString("KN4CRD: HB AUTO EM73");
    adHB1.utcTimestamp = DriftingDateTime::currentDateTimeUtc();
    adHB1.submode = Varicode::JS8CallNormal;
    m_bandActivity[750].append(adHB1);

    ActivityDetail adHB2 = {};
    adHB2.bits = Varicode::JS8CallLast;
    adHB2.snr = 0;
    adHB2.dial = 7078000;
    adHB2.offset = 750;
    adHB2.text = QString(" MSG ID 1");
    adHB2.utcTimestamp = DriftingDateTime::currentDateTimeUtc();
    adHB2.submode = Varicode::JS8CallNormal;
    m_bandActivity[750].append(adHB2);

    CommandDetail cmd = {};
    cmd.cmd = ">";
    cmd.to = m_config.my_callsign();
    cmd.from = "N0JDS";
    cmd.relayPath = "N0JDS>OH8STN";
    cmd.text = "HELLO BRAVE SOUL";
    cmd.utcTimestamp = dt;
    cmd.submode = Varicode::JS8CallNormal;
    addCommandToMyInbox(cmd);

    QString eot = m_config.eot();

    displayTextForFreq(QString("KN4CRD: @ALLCALL? %1 ").arg(eot), 42, DriftingDateTime::currentDateTimeUtc().addSecs(-315), true, true, true);
    displayTextForFreq(QString("J1Y: KN4CRD SNR -05 %1 ").arg(eot), 42, DriftingDateTime::currentDateTimeUtc().addSecs(-300), false, true, true);
    displayTextForFreq(QString("HELLO BRAVE  NEW   WORLD    %1 ").arg(eot), 42, DriftingDateTime::currentDateTimeUtc().addSecs(-300), false, true, true);

    auto now = DriftingDateTime::currentDateTimeUtc();
    displayTextForFreq(QString("KN4CRD: JY1 ACK -12 %1 ").arg(eot), 780, now, false, true, true);
    displayTextForFreq(QString("KN4CRD: JY1 ACK -12 %1 ").arg(eot), 780, now, false, true, true); // should be hidden (duplicate)
    displayTextForFreq(QString("OH8STN: JY1 ACK -12 %1 ").arg(eot), 780, now, false, true, true);

    displayTextForFreq(QString("KN4CRD: JY1 ACK -10 %1 ").arg(eot), 800, now, false, true, true);
    displayTextForFreq(QString("KN4CRD: JY1 ACK -12 %1 ").arg(eot), 780, now.addSecs(120), false, true, true);

    displayTextForFreq(QString("HELLO\\nBRAVE\\nNEW\\nWORLD %1 ").arg(eot), 1500, now, false, true, true);

    displayActivity(true);
}

void MainWindow::initialize_fonts ()
{
  set_application_font (m_config.text_font ());

  setTextEditFont(ui->textEditRX, m_config.rx_text_font());
  setTextEditFont(ui->extFreeTextMsgEdit, m_config.tx_text_font());

  displayActivity(true);
}

void MainWindow::on_the_minute ()
{
  if (minuteTimer.isSingleShot ())
    {
      minuteTimer.setSingleShot (false);
      minuteTimer.start (60 * 1000); // run free
    }
  else
    {
        auto const& ms_error = ms_minute_error ();
        if (qAbs (ms_error) > 1000) // keep drift within +-1s
        {
          minuteTimer.setSingleShot (true);
          minuteTimer.start (ms_error + 60 * 1000);
        }
    }

  if (m_config.watchdog ())
    {
      incrementIdleTimer();
    }
  else
    {
      tx_watchdog (false);
    }
}

void MainWindow::tryBandHop(){
  // see if we need to hop bands...
  if(!m_config.auto_switch_bands()){
      return;
  }

  // make sure we're not transmitting
  if(isMessageQueuedForTransmit()){
      return;
  }

  // get the current band
  auto dialFreq = dialFrequency();

  auto currentBand = m_config.bands()->find(dialFreq);

  // get the stations list
  auto stations = m_config.stations()->station_list();

  // order stations by (switch_at, switch_until) time tuple
  std::stable_sort(stations.begin(), stations.end(), [](StationList::Station const &a, StationList::Station const &b){
    return (a.switch_at_ < b.switch_at_) || (a.switch_at_ == b.switch_at_ && a.switch_until_ < b.switch_until_);
  });

  // we just set the date to a known y/m/d to make the comparisons easier
  QDateTime d = DriftingDateTime::currentDateTimeUtc();
  d.setDate(QDate(2000, 1, 1));

  QDateTime startOfDay = QDateTime(QDate(2000, 1, 1), QTime(0, 0));
  QDateTime endOfDay = QDateTime(QDate(2000, 1, 1), QTime(23, 59));

  // see if we can find a needed band switch...
  foreach(auto station, stations){
      // we can switch to this frequency if we're in the time range, inclusive of switch_at, exclusive of switch_until
      // and if we are switching to a different frequency than the last hop. this allows us to switch bands at that time,
      // but then later we can later switch to a different band if needed without the automatic band switching to take over
      bool inTimeRange = (
        (station.switch_at_ <= d && d <= station.switch_until_) ||          // <- normal range, 12-16 && 6-8, evalued as 12 <= d <= 16 || 6 <= d <= 8

        (station.switch_until_ < station.switch_at_ && (                    // <- say for a range of 12->2 & 2->12;  12->2,
             (station.switch_at_ <= d && d <= endOfDay)         ||          //    should be evaluated as 12 <= d <= 23:59 || 00:00 <= d <= 2
             (startOfDay <= d && d <= station.switch_until_)
        ))
      );

      bool noOverride = (
        m_bandHopped || (!m_bandHopped && station.frequency_ != m_bandHoppedFreq)
      );

      bool freqIsDifferent = (station.frequency_ != dialFreq);

      bool canSwitch = (
        inTimeRange     &&
        noOverride      &&
        freqIsDifferent
      );

      // switch, if we can and the band is different than our current band
      if(canSwitch){
          Frequency frequency = station.frequency_;

          m_bandHopped = false;
          m_bandHoppedFreq = frequency;

          SelfDestructMessageBox * m = new SelfDestructMessageBox(30,
            "Scheduled Frequency Change",
            QString("A scheduled frequency change has arrived. The rig frequency will be changed to %1 MHz in %2 second(s).").arg(Radio::frequency_MHz_string(station.frequency_)),
            QMessageBox::Information,
            QMessageBox::Ok | QMessageBox::Cancel,
            QMessageBox::Ok,
            true,
            this);

          connect(m, &SelfDestructMessageBox::accepted, this, [this, frequency](){
              m_bandHopped = true;
              setRig(frequency);
          });

          m->show();

#if 0
          // TODO: jsherer - this is totally a hack because of the signal that gets emitted to clearActivity on band change...
          QTimer *t = new QTimer(this);
          t->setInterval(250);
          t->setSingleShot(true);
          connect(t, &QTimer::timeout, this, [this, station, dialFreq](){
              auto message = QString("Scheduled frequency switch from %1 MHz to %2 MHz");
              message = message.arg(Radio::frequency_MHz_string(dialFreq));
              message = message.arg(Radio::frequency_MHz_string(station.frequency_));
              writeNoticeTextToUI(DriftingDateTime::currentDateTimeUtc(), message);
          });
          t->start();
#endif

          return;
      }
  }
}

//--------------------------------------------------- MainWindow destructor
MainWindow::~MainWindow()
{
  QString fname {QDir::toNativeSeparators(m_config.writeable_data_dir ().absoluteFilePath ("wsjtx_wisdom.dat"))};
  QByteArray cfname=fname.toLocal8Bit();

  fftwf_export_wisdom_to_filename(cfname);

  m_networkThread.quit();
  m_networkThread.wait();

  m_audioThread.quit ();
  m_audioThread.wait ();

  m_notificationAudioThread.quit();
  m_notificationAudioThread.wait();

  m_decoder.quit();
  m_decoder.wait();

  remove_child_from_event_filter (this);
}

//-------------------------------------------------------- writeSettings()
void MainWindow::writeSettings()
{
  m_settings->beginGroup("MainWindow");
  m_settings->setValue ("geometry", saveGeometry ());
  m_settings->setValue ("geometryNoControls", m_geometryNoControls);
  m_settings->setValue ("state", saveState ());

  m_settings->setValue("MainSplitter", ui->mainSplitter->saveState());
  m_settings->setValue("TextHorizontalSplitter", ui->textHorizontalSplitter->saveState());
  m_settings->setValue("BandActivityVisible", ui->tableWidgetRXAll->isVisible());
  m_settings->setValue("BandHBActivityVisible", ui->actionShow_Band_Heartbeats_and_ACKs->isChecked());
  m_settings->setValue("TextVerticalSplitter", ui->textVerticalSplitter->saveState());
  m_settings->setValue("TimeDrift", DriftingDateTime::drift());
  m_settings->setValue("ShowTooltips", ui->actionShow_Tooltips->isChecked());
  m_settings->setValue("ShowStatusbar", ui->statusBar->isVisible());
  m_settings->setValue("RXActivity", ui->textEditRX->toHtml());

  m_settings->endGroup();

  m_settings->beginGroup("Common");
  m_settings->setValue("NDepth",m_ndepth);
  m_settings->setValue("Freq", freq());
  m_settings->setValue("SubMode",m_nSubMode);
  m_settings->setValue("SubModeHB", ui->actionModeJS8HB->isChecked());
  m_settings->setValue("SubModeHBAck", ui->actionHeartbeatAcknowledgements->isChecked());
  m_settings->setValue("SubModeMultiDecode", ui->actionModeMultiDecoder->isChecked());
  m_settings->setValue("DialFreq", QVariant::fromValue(m_lastMonitoredFrequency));
  m_settings->setValue("OutAttenuation", ui->outAttenuation->value ());
  m_settings->setValue("pwrBandTxMemory",m_pwrBandTxMemory);
  m_settings->setValue("pwrBandTuneMemory",m_pwrBandTuneMemory);
  m_settings->setValue("SortBy", QVariant(m_sortCache));
  m_settings->setValue("ShowColumns", QVariant(m_showColumnsCache));
  m_settings->setValue("HBInterval", m_hbInterval);
  m_settings->setValue("CQInterval", m_cqInterval);





  // TODO: jsherer - need any other customizations?
  /*m_settings->setValue("PanelLeftGeometry", ui->tableWidgetRXAll->geometry());
  m_settings->setValue("PanelRightGeometry", ui->tableWidgetCalls->geometry());
  m_settings->setValue("PanelTopGeometry", ui->extFreeTextMsg->geometry());
  m_settings->setValue("PanelBottomGeometry", ui->extFreeTextMsgEdit->geometry());
  m_settings->setValue("PanelWaterfallGeometry", ui->bandHorizontalWidget->geometry());*/
  //m_settings->setValue("MainSplitter", QVariant::fromValue(ui->mainSplitter->sizes()));

  m_settings->endGroup();


  auto now = DriftingDateTime::currentDateTimeUtc();
  int callsignAging = m_config.callsign_aging();

  m_settings->beginGroup("CallActivity");
  m_settings->remove(""); // remove all keys in current group
  foreach(auto cd, m_callActivity.values()){
      if (cd.call.trimmed().isEmpty()){
          continue;
      }
      if (callsignAging && cd.utcTimestamp.secsTo(now) / 60 >= callsignAging) {
          continue;
      }
      m_settings->setValue(cd.call.trimmed(), QVariantMap{
        {"snr", QVariant(cd.snr)},
        {"grid", QVariant(cd.grid)},
        {"dial", QVariant(cd.dial)},
        {"freq", QVariant(cd.offset)},
        {"tdrift", QVariant(cd.tdrift)},
#if CACHE_CALL_DATETIME_AS_STRINGS
        {"ackTimestamp", QVariant(cd.ackTimestamp.toString("yyyy-MM-dd hh:mm:ss"))},
        {"utcTimestamp", QVariant(cd.utcTimestamp.toString("yyyy-MM-dd hh:mm:ss"))},
#else
        {"ackTimestamp", QVariant(cd.ackTimestamp)},
        {"utcTimestamp", QVariant(cd.utcTimestamp)},
#endif
        {"submode", QVariant(cd.submode)},
      });
  }
  m_settings->endGroup();
}

//---------------------------------------------------------- readSettings()
void MainWindow::readSettings()
{
  m_settings->beginGroup("MainWindow");
  setMinimumSize(800, 400);
  restoreGeometry (m_settings->value ("geometry", saveGeometry ()).toByteArray ());
  setMinimumSize(800, 400);

  m_geometryNoControls = m_settings->value ("geometryNoControls",saveGeometry()).toByteArray();
  restoreState (m_settings->value ("state", saveState ()).toByteArray ());

  auto mainSplitterState = m_settings->value("MainSplitter").toByteArray();
  if(!mainSplitterState.isEmpty()){
    ui->mainSplitter->restoreState(mainSplitterState);
  }
  auto horizontalState = m_settings->value("TextHorizontalSplitter").toByteArray();
  if(!horizontalState.isEmpty()){
    ui->textHorizontalSplitter->restoreState(horizontalState);
    auto hsizes = ui->textHorizontalSplitter->sizes();

    ui->tableWidgetRXAll->setVisible(hsizes.at(0) > 0);
    ui->tableWidgetCalls->setVisible(hsizes.at(2) > 0);
  }

  m_bandActivityWasVisible = m_settings->value("BandActivityVisible", true).toBool();
  ui->tableWidgetRXAll->setVisible(m_bandActivityWasVisible);

  auto verticalState = m_settings->value("TextVerticalSplitter").toByteArray();
  if(!verticalState.isEmpty()){
    ui->textVerticalSplitter->restoreState(verticalState);
  }
  setDrift(m_settings->value("TimeDrift", 0).toInt());
  ui->actionShow_Waterfall_Controls->setChecked(m_wideGraph->controlsVisible());
  ui->actionShow_Waterfall_Time_Drift_Controls->setChecked(m_wideGraph->timeControlsVisible());
  ui->actionShow_Tooltips->setChecked(m_settings->value("ShowTooltips", true).toBool());
  ui->actionShow_Statusbar->setChecked(m_settings->value("ShowStatusbar",true).toBool());
  ui->statusBar->setVisible(ui->actionShow_Statusbar->isChecked());
  ui->textEditRX->setHtml(m_config.reset_activity() ? "" : m_settings->value("RXActivity", "").toString());
  ui->actionShow_Band_Heartbeats_and_ACKs->setChecked(m_settings->value("BandHBActivityVisible", true).toBool());
  m_settings->endGroup();

  m_settings->beginGroup("Common");

  // set the frequency offset
  setFreqOffsetForRestore(m_settings->value("Freq", Default::FREQUENCY).toInt(), false); // XXX

  setSubmode(m_settings->value("SubMode", Default::SUBMODE).toInt());
  ui->actionModeJS8HB->setChecked(m_settings->value("SubModeHB", false).toBool());
  ui->actionHeartbeatAcknowledgements->setChecked(m_settings->value("SubModeHBAck", false).toBool());
  ui->actionModeMultiDecoder->setChecked(m_settings->value("SubModeMultiDecode", true).toBool());

  m_lastMonitoredFrequency = m_settings->value ("DialFreq",
    QVariant::fromValue<Frequency> (Default::DIAL_FREQUENCY)).value<Frequency> ();
  setFreq(0); // ensure a change is signaled
  setFreq(m_settings->value("Freq", Default::FREQUENCY).toInt());
  m_ndepth=m_settings->value("NDepth", Default::DEPTH).toInt();
  // setup initial value of tx attenuator
  m_block_pwr_tooltip = true;
  ui->outAttenuation->setValue (m_settings->value ("OutAttenuation", 0).toInt ());
  m_block_pwr_tooltip = false;
  m_pwrBandTxMemory=m_settings->value("pwrBandTxMemory").toHash();
  m_pwrBandTuneMemory=m_settings->value("pwrBandTuneMemory").toHash();

  m_sortCache = m_settings->value("SortBy").toMap();
  m_showColumnsCache = m_settings->value("ShowColumns").toMap();
  m_hbInterval = m_settings->value("HBInterval", 0).toInt();
  m_cqInterval = m_settings->value("CQInterval", 0).toInt();

  // TODO: jsherer - any other customizations?
  //ui->mainSplitter->setSizes(m_settings->value("MainSplitter", QVariant::fromValue(ui->mainSplitter->sizes())).value<QList<int> >());
  //ui->tableWidgetRXAll->restoreGeometry(m_settings->value("PanelLeftGeometry", ui->tableWidgetRXAll->saveGeometry()).toByteArray());
  //ui->tableWidgetCalls->restoreGeometry(m_settings->value("PanelRightGeometry", ui->tableWidgetCalls->saveGeometry()).toByteArray());
  //ui->extFreeTextMsg->setGeometry( m_settings->value("PanelTopGeometry", ui->extFreeTextMsg->geometry()).toRect());
  //ui->extFreeTextMsgEdit->setGeometry( m_settings->value("PanelBottomGeometry", ui->extFreeTextMsgEdit->geometry()).toRect());
  //ui->bandHorizontalWidget->setGeometry( m_settings->value("PanelWaterfallGeometry", ui->bandHorizontalWidget->geometry()).toRect());
  //qDebug() << m_settings->value("PanelTopGeometry") << ui->extFreeTextMsg;

  setTextEditStyle(ui->textEditRX, m_config.color_rx_foreground(), m_config.color_rx_background(), m_config.rx_text_font());
  setTextEditStyle(ui->extFreeTextMsgEdit, m_config.color_compose_foreground(), m_config.color_compose_background(), m_config.compose_text_font());
  ui->extFreeTextMsgEdit->setFont(m_config.compose_text_font(), m_config.color_compose_foreground(), m_config.color_compose_background());

  m_settings->endGroup();

  // use these initialisation settings to tune the audio o/p buffer
  // size and audio thread priority
  m_settings->beginGroup ("Tune");
  m_msAudioOutputBuffered = m_settings->value ("Audio/OutputBufferMs").toInt ();
  m_framesAudioInputBuffered = m_settings->value ("Audio/InputBufferFrames", RX_SAMPLE_RATE / 10).toInt ();
  m_audioThreadPriority = static_cast<QThread::Priority> (m_settings->value ("Audio/ThreadPriority", QThread::TimeCriticalPriority).toInt () % 8);
  m_notificationAudioThreadPriority = static_cast<QThread::Priority> (m_settings->value ("Audio/NotificationThreadPriority", QThread::LowPriority).toInt () % 8);
  m_decoderThreadPriority = static_cast<QThread::Priority> (m_settings->value ("Audio/DecoderThreadPriority", QThread::HighPriority).toInt () % 8);
  m_networkThreadPriority = static_cast<QThread::Priority> (m_settings->value ("Network/NetworkThreadPriority", QThread::LowPriority).toInt () % 8);
  m_settings->endGroup ();

  if(m_config.reset_activity()){
      // NOOP
  } else {
      m_settings->beginGroup("CallActivity");
      foreach(auto call, m_settings->allKeys()){

          auto values = m_settings->value(call).toMap();

          auto snr = values.value("snr", -64).toInt();
          auto grid = values.value("grid", "").toString();
          auto dial = values.value("dial", 0).toInt();
          auto freq = values.value("freq", 0).toInt();
          auto tdrift = values.value("tdrift", 0).toFloat();


#if CACHE_CALL_DATETIME_AS_STRINGS
          auto ackTimestampStr = values.value("ackTimestamp", "").toString();
          auto ackTimestamp = QDateTime::fromString(ackTimestampStr, "yyyy-MM-dd hh:mm:ss");
          ackTimestamp.setUtcOffset(0);

          auto utcTimestampStr = values.value("utcTimestamp", "").toString();
          auto utcTimestamp = QDateTime::fromString(utcTimestampStr, "yyyy-MM-dd hh:mm:ss");
          utcTimestamp.setUtcOffset(0);
#else
          auto ackTimestamp = values.value("ackTimestamp").toDateTime();
          auto utcTimestamp = values.value("utcTimestamp").toDateTime();
#endif
          auto submode = values.value("submode", Varicode::JS8CallNormal).toInt();

          CallDetail cd = {};
          cd.call = call;
          cd.snr = snr;
          cd.grid = grid;
          cd.dial = dial;
          cd.offset = freq;
          cd.tdrift = tdrift;
          cd.ackTimestamp = ackTimestamp;
          cd.utcTimestamp = utcTimestamp;
          cd.submode = submode;

          logCallActivity(cd, false);
      }
      m_settings->endGroup();
  }

  m_settings_read = true;
}

void MainWindow::set_application_font (QFont const& font)
{
  qApp->setFont (font);
  // set font in the application style sheet as well in case it has
  // been modified in the style sheet which has priority
  qApp->setStyleSheet (qApp->styleSheet () + "* {" + font_as_stylesheet (font) + '}');
  for (auto& widget : qApp->topLevelWidgets ())
    {
      widget->updateGeometry ();
    }
}


//-------------------------------------------------------------- dataSink()
void MainWindow::dataSink(qint64 frames)
{
    // symspec global vars
    static int ja = 0;
    static int k0 = 999999999;
    static WF::SPlot ssum = {};
    static WF::SPlot s    = {};

    int k (frames);
    if(k0 == 999999999){
        m_ihsym = int((float)frames/(float)NSPS)*2;
        ja = k;
        k0 = k;
    }

    //qDebug() << "k" << k << "k0" << k0 << "delta" << k-k0;

    // Get power, spectrum, and ihsym
    int trmin=m_TRperiod/60;
    int nsps=NSPS;
    int nsmo=m_wideGraph->smoothYellow()-1;

    /// START IHSYM
#if JS8_USE_IHSYM
    // moving ihsym computation to here from symspec.f90
    // 1) set the initial ihsym
    if(m_ihsym == 0){
        m_ihsym = int((float)k/NSPS)*2;
    }
    // 2) reset the ihsym when loop around
    if(k < k0){
        m_ihsym = 0;
    }
    k0 = k;
    int ihs = m_ihsym;
    dec_data.params.kpos = computeCycleStartForDecode(computeCurrentCycle(m_TRperiod), m_TRperiod);
    symspec_(&dec_data,&k,&k0,&trmin,&nsps,&m_inGain,&nsmo,&m_px,s,&m_df3,&ihs,&m_npts8,&m_pxmax);
    // 3) if symspec wants ihs to be 0, set it.
    if(ihs == 0){
        m_ihsym = ihs;
    } else {
        m_ihsym += 1;
    }

    // make ihsym similar to how it was...relative to the tr period
    m_ihsym = m_ihsym % (m_TRperiod*RX_SAMPLE_RATE/NSPS*2);

    /// qDebug() << "dataSink" << k << "ihsym" << m_ihsym << "ihs" << ihs;
    /// QVector<float> sss;
    /// for(int i = 0; i < 10; i++){
    ///     sss << s[i];
    /// }
    /// qDebug() << "-->" << sss;

    /// END IHSYM
#else
    // make sure the ssum global is reset every period cycle
    static int lastCycle = -1;
    int const cycle = JS8::Submode::computeCycleForDecode(m_nSubMode, k);
    if(cycle != lastCycle){
        if(JS8_DEBUG_DECODE) qDebug() << "period loop, resetting ssum";
        ssum.fill(0.0f);
    }
    lastCycle = cycle;

    // cap ihsym based on the period max
    m_ihsym = m_ihsym%(m_TRperiod*RX_SAMPLE_RATE/NSPS*2);

    // compute the symbol spectra for the waterfall display
    symspec_(&dec_data,
             &k,&k0,
             &ja,
             ssum.data(),
             &trmin,
             &nsps,
             &m_inGain,
             &nsmo,
             &m_px,
             s.data(),
             &m_df3,
             &m_ihsym,
             &m_npts8,
             &m_pxmax);

    // make sure ja is equal to k so if we jump ahead in the buffer, everything resolves correctly
    ja = k;
#endif

    if(m_ihsym <= 0) return;

    if(ui) ui->signal_meter_widget->setValue(m_px,m_pxmax); // Update thermometer

    if(m_monitoring) {
      m_wideGraph->dataSink(s, m_df3);
    }

    m_dateTime = DriftingDateTime::currentDateTimeUtc().toString ("yyyy-MMM-dd hh:mm");

    decode(k);
}

void MainWindow::showSoundInError(const QString& errorMsg)
{
  MessageBox::critical_message (this, tr ("Error in Sound Input"), errorMsg);
}

void MainWindow::showSoundOutError(const QString& errorMsg)
{
  MessageBox::critical_message (this, tr ("Error in Sound Output"), errorMsg);
}

void MainWindow::showStatusMessage(const QString& statusMsg)
{
  statusBar()->showMessage(statusMsg, 5000);
}

void MainWindow::on_menuModeJS8_aboutToShow(){
    bool canChangeMode = !m_transmitting && m_txFrameCount == 0 && m_txFrameQueue.isEmpty();
    ui->actionModeJS8Normal->setEnabled(canChangeMode);
    ui->actionModeJS8Fast->setEnabled(canChangeMode);
    ui->actionModeJS8Turbo->setEnabled(canChangeMode);
    ui->actionModeJS8Slow->setEnabled(canChangeMode);
    ui->actionModeJS8Ultra->setEnabled(canChangeMode);

    // dynamically replace the autoreply menu item text
    auto autoreplyText = ui->actionModeAutoreply->text();
    if(m_config.autoreply_confirmation() && !autoreplyText.contains(" with Confirmation")){
        autoreplyText.replace("Autoreply", "Autoreply with Confirmation");
        autoreplyText.replace("&AUTO", "&AUTO+CONF");
        ui->actionModeAutoreply->setText(autoreplyText);
    }
    else if(!m_config.autoreply_confirmation() && autoreplyText.contains(" with Confirmation")){
        autoreplyText.replace(" with Confirmation", "");
        autoreplyText.replace("+CONF", "");
        ui->actionModeAutoreply->setText(autoreplyText);
    }
}

void
MainWindow::on_menuControl_aboutToShow()
{
    auto freqMenu = new QMenu(this->menuBar());
    buildFrequencyMenu(freqMenu);
    ui->actionSetFrequency->setMenu(freqMenu);

    auto heartbeatMenu = new QMenu(this->menuBar());
    buildHeartbeatMenu(heartbeatMenu);
    ui->actionHeartbeat->setMenu(heartbeatMenu);

    auto cqMenu = new QMenu(this->menuBar());
    buildCQMenu(cqMenu);
    ui->actionCQ->setMenu(cqMenu);

    ui->actionEnable_Monitor_RX->setChecked(ui->monitorButton->isChecked());
    ui->actionEnable_Transmitter_TX->setChecked(ui->monitorTxButton->isChecked());
    ui->actionEnable_Reporting_SPOT->setChecked(ui->spotButton->isChecked());
    ui->actionEnable_Tuning_Tone_TUNE->setChecked(ui->tuneButton->isChecked());
}

void MainWindow::on_actionCheck_for_Updates_triggered(){
    checkVersion(true);
}

void MainWindow::on_actionEnable_Monitor_RX_toggled(bool checked){
    ui->monitorButton->setChecked(checked);
}

void MainWindow::on_actionEnable_Transmitter_TX_toggled(bool checked){
    ui->monitorTxButton->setChecked(checked);
}


void MainWindow::on_actionEnable_Reporting_SPOT_toggled(bool checked){
    ui->spotButton->setChecked(checked);
}

void MainWindow::on_actionEnable_Tuning_Tone_TUNE_toggled(bool checked){
    ui->tuneButton->setChecked(checked);
    on_tuneButton_clicked(checked);
}

void MainWindow::on_menuWindow_aboutToShow(){
    ui->actionShow_Fullscreen->setChecked((windowState() & Qt::WindowFullScreen) == Qt::WindowFullScreen);

    ui->actionShow_Statusbar->setChecked(ui->statusBar && ui->statusBar->isVisible());

    auto hsizes = ui->textHorizontalSplitter->sizes();
    ui->actionShow_Band_Activity->setChecked(hsizes.at(0) > 0);
    ui->actionShow_Call_Activity->setChecked(hsizes.at(2) > 0);

    auto vsizes = ui->mainSplitter->sizes();
    ui->actionShow_Frequency_Clock->setChecked(vsizes.first() > 0);
    ui->actionShow_Waterfall->setChecked(vsizes.last() > 0);
    ui->actionShow_Waterfall_Controls->setChecked(ui->actionShow_Waterfall->isChecked() && m_wideGraph->controlsVisible());
    ui->actionShow_Waterfall_Time_Drift_Controls->setChecked(ui->actionShow_Waterfall->isChecked() && m_wideGraph->timeControlsVisible());

    QMenu * sortBandMenu = new QMenu(this->menuBar()); //ui->menuWindow);
    buildBandActivitySortByMenu(sortBandMenu);
    ui->actionSort_Band_Activity->setMenu(sortBandMenu);
    ui->actionSort_Band_Activity->setEnabled(ui->actionShow_Band_Activity->isChecked());

    QMenu * sortCallMenu = new QMenu(this->menuBar()); //ui->menuWindow);
    buildCallActivitySortByMenu(sortCallMenu);
    ui->actionSort_Call_Activity->setMenu(sortCallMenu);
    ui->actionSort_Call_Activity->setEnabled(ui->actionShow_Call_Activity->isChecked());

    QMenu * showBandMenu = new QMenu(this->menuBar()); //ui->menuWindow);
    buildShowColumnsMenu(showBandMenu, "band");
    ui->actionShow_Band_Activity_Columns->setMenu(showBandMenu);
    ui->actionShow_Band_Activity_Columns->setEnabled(ui->actionShow_Band_Activity->isChecked());

    QMenu * showCallMenu = new QMenu(this->menuBar()); //ui->menuWindow);
    buildShowColumnsMenu(showCallMenu, "call");
    ui->actionShow_Call_Activity_Columns->setMenu(showCallMenu);
    ui->actionShow_Call_Activity_Columns->setEnabled(ui->actionShow_Call_Activity->isChecked());

    ui->actionShow_Band_Heartbeats_and_ACKs->setEnabled(ui->actionShow_Band_Activity->isChecked());
}

void MainWindow::on_actionFocus_Message_Receive_Area_triggered(){
    ui->textEditRX->setFocus();
}

void MainWindow::on_actionFocus_Message_Reply_Area_triggered(){
    ui->extFreeTextMsgEdit->setFocus();
}

void MainWindow::on_actionFocus_Band_Activity_Table_triggered(){
    ui->tableWidgetRXAll->setFocus();
}

void MainWindow::on_actionFocus_Call_Activity_Table_triggered(){
    ui->tableWidgetCalls->setFocus();
}

void MainWindow::on_actionClear_All_Activity_triggered(){
    clearActivity();
}

void MainWindow::on_actionClear_Band_Activity_triggered(){
    clearBandActivity();
}

void MainWindow::on_actionClear_RX_Activity_triggered(){
    clearRXActivity();
}

void MainWindow::on_actionClear_Call_Activity_triggered(){
    clearCallActivity();
}

void MainWindow::on_actionSetOffset_triggered(){
    bool       ok          = false;
    auto const currentFreq = freq();
    QString newFreq = QInputDialog::getText(this, tr("Set Frequency Offset"),
                                             tr("Offset in Hz:"), QLineEdit::Normal,
                                             QString("%1").arg(currentFreq), &ok).toUpper().trimmed();
    int offset = newFreq.toInt(&ok);
    if(!ok){
       return;
    }

    setFreqOffsetForRestore(offset, false);
}

void MainWindow::on_actionShow_Fullscreen_triggered(bool checked){
    auto state = windowState();
    if(checked){
        state |= Qt::WindowFullScreen;
    } else {
        state &= ~Qt::WindowFullScreen;
    }
    setWindowState(state);
}

void MainWindow::on_actionShow_Statusbar_triggered(bool checked){
    if(!ui->statusBar){
        return;
    }

    ui->statusBar->setVisible(checked);
}

void MainWindow::on_actionShow_Frequency_Clock_triggered(bool checked){
    auto vsizes = ui->mainSplitter->sizes();
    vsizes[0] = checked ? ui->logHorizontalWidget->minimumHeight() : 0;
    ui->logHorizontalWidget->setVisible(checked);
    ui->mainSplitter->setSizes(vsizes);
}

void MainWindow::on_actionShow_Band_Activity_triggered(bool checked){
    auto hsizes = ui->textHorizontalSplitter->sizes();

    if(m_bandActivityWidth == 0){
        m_bandActivityWidth = ui->textHorizontalSplitter->width()/4;
    }

    if(m_callActivityWidth == 0){
        m_callActivityWidth = ui->textHorizontalSplitter->width()/4;
    }

    if(m_textActivityWidth == 0){
        m_textActivityWidth = ui->textHorizontalSplitter->width()/2;
    }

    if(checked){
        hsizes[0] = m_bandActivityWidth;
        hsizes[1] = m_textActivityWidth;
        if(hsizes[2]) hsizes[2] = m_callActivityWidth;

    } else {
        if(hsizes[0]) m_bandActivityWidth = hsizes[0];
        if(hsizes[1]) m_textActivityWidth = hsizes[1];
        if(hsizes[2]) m_callActivityWidth = hsizes[2];
        hsizes[0] = 0;
    }

    ui->textHorizontalSplitter->setSizes(hsizes);
    ui->tableWidgetRXAll->setVisible(checked);
    m_bandActivityWasVisible = checked;

}

void MainWindow::on_actionShow_Band_Heartbeats_and_ACKs_triggered(bool){
    displayBandActivity();
}

void MainWindow::on_actionShow_Call_Activity_triggered(bool checked){
    auto hsizes = ui->textHorizontalSplitter->sizes();

    if(m_bandActivityWidth == 0){
        m_bandActivityWidth = ui->textHorizontalSplitter->width()/4;
    }

    if(m_callActivityWidth == 0){
        m_callActivityWidth = ui->textHorizontalSplitter->width()/4;
    }

    if(m_textActivityWidth == 0){
        m_textActivityWidth = ui->textHorizontalSplitter->width()/2;
    }

    if(checked){
        if(hsizes[0]) hsizes[0] = m_bandActivityWidth;
        hsizes[1] = m_textActivityWidth;
        hsizes[2] = m_callActivityWidth;

    } else {
        if(hsizes[0]) m_bandActivityWidth = hsizes[0];
        if(hsizes[1]) m_textActivityWidth = hsizes[1];
        if(hsizes[2]) m_callActivityWidth = hsizes[2];
        hsizes[2] = 0;
    }

    ui->textHorizontalSplitter->setSizes(hsizes);
    ui->tableWidgetCalls->setVisible(checked);
}

void MainWindow::on_actionShow_Waterfall_triggered(bool checked){
    auto vsizes = ui->mainSplitter->sizes();

    if(m_waterfallHeight == 0){
        m_waterfallHeight = ui->mainSplitter->height()/4;
    }

    if(checked){
        vsizes[vsizes.length() - 1] = m_waterfallHeight;

    } else {
        m_waterfallHeight = vsizes[vsizes.length() - 1];
        vsizes[1] += m_waterfallHeight;
        vsizes[vsizes.length() - 1] = 0;
    }

    ui->mainSplitter->setSizes(vsizes);
    ui->bandHorizontalWidget->setVisible(checked);
}

void MainWindow::on_actionShow_Waterfall_Controls_triggered(bool checked){
    m_wideGraph->setControlsVisible(checked);
    if(checked && !ui->bandHorizontalWidget->isVisible()){
        on_actionShow_Waterfall_triggered(checked);
    }
}

void MainWindow::on_actionShow_Waterfall_Time_Drift_Controls_triggered(bool checked){
    m_wideGraph->setTimeControlsVisible(checked);
    if(checked && !ui->bandHorizontalWidget->isVisible()){
        on_actionShow_Waterfall_triggered(checked);
    }
}

void MainWindow::on_actionReset_Window_Sizes_triggered(){
   // auto size = this->centralWidget()->size();

    ui->mainSplitter->setSizes({
        ui->logHorizontalWidget->minimumHeight(),
        ui->mainSplitter->height()/2,
        ui->macroHorizonalWidget->minimumHeight(),
        ui->mainSplitter->height()/4
    });

    ui->textHorizontalSplitter->setSizes({
        ui->textHorizontalSplitter->width()/4,
        ui->textHorizontalSplitter->width()/2,
        ui->textHorizontalSplitter->width()/4
    });

    ui->textVerticalSplitter->setSizes({
        ui->textVerticalSplitter->height()/2,
        ui->textVerticalSplitter->height()/2
    });
}

void MainWindow::on_actionSettings_triggered(){
    openSettings();
}

void MainWindow::openSettings(int tab){
    m_config.select_tab(tab);

    // things that might change that we need know about
    auto callsign = m_config.my_callsign ();
    auto my_grid = m_config.my_grid ();
    auto spot_on = m_config.spot_to_reporting_networks ();
    if (QDialog::Accepted == m_config.exec ()) {
        if (m_config.my_callsign () != callsign) {
            m_baseCall = Radio::base_callsign (m_config.my_callsign ());
        }
        if (m_config.my_callsign () != callsign || m_config.my_grid () != my_grid) {
            statusUpdate ();
        }

        enable_DXCC_entity (m_config.DXCC ());  // sets text window proportions and (re)inits the logbook

        prepareApi();
        prepareSpotting();

        // this will close the connection to PSKReporter if it has been
        // disabled
        if (spot_on && !m_config.spot_to_reporting_networks ())
        {
          Q_EMIT pskReporterSendReport (true);
        }

        if(m_config.restart_audio_input () && !m_config.audio_input_device ().isNull ()) {
            Q_EMIT startAudioInputStream (m_config.audio_input_device (),
                m_framesAudioInputBuffered, m_detector, m_downSampleFactor,
                m_config.audio_input_channel ());
        }

        if(m_config.restart_audio_output () && !m_config.audio_output_device ().isNull ()) {
            Q_EMIT initializeAudioOutputStream (m_config.audio_output_device (),
                AudioDevice::Mono == m_config.audio_output_channel () ? 1 : 2,
                m_msAudioOutputBuffered);
        }

        if(m_config.restart_notification_audio_output () && !m_config.notification_audio_output_device ().isNull ()) {
            Q_EMIT initializeNotificationAudioOutputStream(m_config.notification_audio_output_device(),
                m_msAudioOutputBuffered);
        }

        displayDialFrequency ();
        displayActivity(true);

        setup_status_bar ();
        setupJS8();

        m_config.transceiver_online ();

        setXIT(freq());

        m_opCall=m_config.opCall();
    }
}

void MainWindow::prepareApi(){
    // the udp api is prepared by default (always listening)

    // so, we just need to prepare the tcp api
    bool enabled = m_config.tcpEnabled();
    if(enabled){
        emit apiSetMaxConnections(m_config.tcp_max_connections());
        emit apiSetServer(m_config.tcp_server_name(), m_config.tcp_server_port());
        emit apiStartServer();
    } else {
        emit apiStopServer();
    }
}

void MainWindow::prepareSpotting(){
    if(m_config.spot_to_reporting_networks ()){
        spotSetLocal();
        pskSetLocal();
        aprsSetLocal();
        emit aprsClientSetSkipPercent(0.25);
        emit aprsClientSetServer(m_config.aprs_server_name(), m_config.aprs_server_port());
        emit aprsClientSetPaused(false);
        ui->spotButton->setChecked(true);
    } else {
        emit aprsClientSetPaused(true);
        ui->spotButton->setChecked(false);
    }
}

void MainWindow::on_spotButton_clicked(bool checked){
    // 1. save setting
    m_config.set_spot_to_reporting_networks(checked);

    // 2. prepare
    prepareApi();
    prepareSpotting();
}

void MainWindow::on_monitorButton_clicked (bool checked)
{
  if (!m_transmitting) {
    auto prior = m_monitoring;
    monitor (checked);
    if (checked && !prior) {
      if (m_config.monitor_last_used ()) {
              // put rig back where it was when last in control
        setRig (m_lastMonitoredFrequency);
        setXIT (freq());
      }
      setFreq(freq()); // ensure FreqCal triggers
    }
      //Get Configuration in/out of strict split and mode checking
    Q_EMIT m_config.sync_transceiver (true, checked);
  } else {
    ui->monitorButton->setChecked (false); // disallow
  }
}

void MainWindow::monitor (bool state)
{
  ui->monitorButton->setChecked (state);

  // make sure widegraph is running if we are monitoring, otherwise pause it.
  m_wideGraph->setPaused(!state);

  if (state) {
    if (!m_monitoring) Q_EMIT resumeAudioInputStream ();
  } else {
    Q_EMIT suspendAudioInputStream ();
  }
  m_monitoring = state;
}

void MainWindow::on_actionAbout_triggered()                  //Display "About"
{
  CAboutDlg {this}.exec ();
}

void MainWindow::on_monitorButton_toggled(bool){
    resetPushButtonToggleText(ui->monitorButton);
}

void MainWindow::on_monitorTxButton_toggled(bool checked){
    resetPushButtonToggleText(ui->monitorTxButton);

    if(!checked){
        on_stopTxButton_clicked();
    }
}

void MainWindow::on_tuneButton_toggled(bool){
    resetPushButtonToggleText(ui->tuneButton);
}

void MainWindow::on_spotButton_toggled(bool){
    resetPushButtonToggleText(ui->spotButton);
}

void MainWindow::auto_tx_mode (bool state)
{
  m_auto = state;
  statusUpdate();
  if (!state) on_stopTxButton_clicked();
}

void MainWindow::keyPressEvent (QKeyEvent * e)
{
    switch (e->key()) {
        case Qt::Key_Escape:
            on_stopTxButton_clicked();
            stopTx();
            return;
        case Qt::Key_F5:
            on_logQSOButton_clicked();
            return;
    }

    QMainWindow::keyPressEvent (e);
}

void
MainWindow::f11f12(int const n)
{
  if (n == 11) setFreq(freq() - 1);
  if (n == 12) setFreq(freq() + 1);
}

Radio::Frequency MainWindow::dialFrequency() {
    return Frequency {m_rigState.ptt () && m_rigState.split () ?
        m_rigState.tx_frequency () : m_rigState.frequency ()};
}

void MainWindow::setSubmode(int submode){
    m_nSubMode = submode;
    ui->actionModeJS8Normal->setChecked(submode == Varicode::JS8CallNormal);
    ui->actionModeJS8Fast->setChecked(submode == Varicode::JS8CallFast);
    ui->actionModeJS8Turbo->setChecked(submode == Varicode::JS8CallTurbo);
    ui->actionModeJS8Slow->setChecked(submode == Varicode::JS8CallSlow);
    ui->actionModeJS8Ultra->setChecked(submode == Varicode::JS8CallUltra);
    setupJS8();
}

void MainWindow::updateCurrentBand(){
    QVariant state = ui->readFreq->property("state");
    if(!state.isValid()){
        return;
    }

    auto dial_frequency = dialFrequency();
    auto const& band_name = m_config.bands ()->find(dial_frequency);

    if (m_lastBand == band_name){
        return;
    }

    cacheActivity(m_lastBand);

    // clear activity on startup if asked or on when the previous band is not empty
    if(m_config.reset_activity() || !m_lastBand.isEmpty()){
        clearActivity();
    }

    m_wideGraph->setBand (band_name);

    qDebug() << "setting band" << band_name;
    sendNetworkMessage("RIG.FREQ", "", {
        {"_ID", QVariant(-1)},
        {"BAND", QVariant(band_name)},
        {"FREQ", QVariant((quint64)dialFrequency() + freq())},
        {"DIAL", QVariant((quint64)dialFrequency())},
        {"OFFSET", QVariant((quint64)freq())}
    });
    m_lastBand = band_name;

    band_changed();
    restoreActivity(m_lastBand);
}

void MainWindow::displayDialFrequency (){
#if 0
    qDebug() << "rx nominal" << m_freqNominal;
    qDebug() << "tx nominal" << m_freqTxNominal;
    qDebug() << "offset set to" << freq() << freq();
#endif

    auto dial_frequency  = dialFrequency();
    auto audio_frequency = freq();

    // lookup band
    auto const& band_name = m_config.bands ()->find (dial_frequency);

    auto sFreq = Radio::pretty_frequency_MHz_string (dial_frequency);
    ui->currentFreq->setDigitCount(sFreq.length());
    ui->currentFreq->display(sFreq);

    if(m_splitMode && m_transmitting){
        audio_frequency -= m_XIT;
    }
    ui->labDialFreqOffset->setText(QString("%1 Hz").arg(audio_frequency));
}

void MainWindow::statusChanged()
{
  statusUpdate ();
}

bool MainWindow::eventFilter (QObject * object, QEvent * event)
{
  switch (event->type())
    {
    case QEvent::KeyPress:
      // fall through
    case QEvent::MouseButtonPress:
      // reset the Tx watchdog
      resetIdleTimer();
      tx_watchdog (false);
      break;

    case QEvent::ChildAdded:
      // ensure our child widgets get added to our event filter
      add_child_to_event_filter (static_cast<QChildEvent *> (event)->child ());
      break;

    case QEvent::ChildRemoved:
      // ensure our child widgets get d=removed from our event filter
      remove_child_from_event_filter (static_cast<QChildEvent *> (event)->child ());
      break;

    case QEvent::ToolTip:
      if(!ui->actionShow_Tooltips->isChecked()){
          return true;
      }

      break;

    default: break;
    }
  return QObject::eventFilter(object, event);
}

void MainWindow::createStatusBar()                           //createStatusBar
{
  tx_status_label.setAlignment (Qt::AlignCenter);
  tx_status_label.setMinimumSize (QSize  {150, 18});
  tx_status_label.setStyleSheet ("QLabel{background-color: #22ff22}");
  tx_status_label.setFrameStyle (QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget (&tx_status_label);

  config_label.setAlignment (Qt::AlignCenter);
  config_label.setMinimumSize (QSize {80, 18});
  config_label.setFrameStyle (QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget (&config_label);
  config_label.hide ();         // only shown for non-default configuration

  mode_label.setAlignment (Qt::AlignCenter);
  mode_label.setMinimumSize (QSize {80, 18});
  mode_label.setStyleSheet ("QLabel{background-color: #6699ff}");
  mode_label.setFrameStyle (QFrame::Panel | QFrame::Sunken);
  mode_label.setText("JS8");
  statusBar()->addWidget (&mode_label);

  last_tx_label.setAlignment (Qt::AlignCenter);
  last_tx_label.setMinimumSize (QSize {150, 18});
  last_tx_label.setFrameStyle (QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget (&last_tx_label);

  statusBar()->addPermanentWidget(&progressBar);
  progressBar.setMinimumSize (QSize {100, 18});
  progressBar.setFormat ("%v/%m");

  statusBar()->addPermanentWidget(&wpm_label);
  wpm_label.setMinimumSize (QSize {120, 18});
  wpm_label.setFrameStyle (QFrame::Panel | QFrame::Sunken);
  wpm_label.setAlignment(Qt::AlignCenter);
}

void
MainWindow::setup_status_bar()
{
  last_tx_label.clear();
}

void MainWindow::subProcessFailed (QString program, QStringList args, int exitCode, int status, QString errorString){
    if(!m_valid){
        return;
    }

    if(!exitCode || QProcess::NormalExit == QProcess::ExitStatus(status)){
        return;
    }

    // surpress any other process notifications until restart
    m_valid = false;

    QStringList arguments;
    for (auto argument: args){
      if (argument.contains (' ')) argument = '"' + argument + '"';
      arguments << argument;
    }

    MessageBox::critical_message (this, tr ("Subprocess Error")
                                , tr ("Subprocess failed with exit code %1 and will restart.")
                                .arg (exitCode)
                                , tr ("Running: %1\n%2")
                                .arg (program + ' ' + arguments.join (' '))
                                .arg (errorString));

    initDecoderSubprocess();
}

void MainWindow::subProcessError (QString program, QStringList args, int errorCode, QString errorString){
    if(!m_valid){
        return;
    }

    // surpress any other process notifications until process restart
    m_valid = false;

    QStringList arguments;
    for(auto argument: args){
        if (argument.contains (' ')) argument = '"' + argument + '"';
        arguments << argument;
    }

    MessageBox::critical_message (this, tr ("Subprocess error")
                                    , tr ("Subprocess errored with code %1 and will restart.").arg(errorCode)
                                    , tr ("Running: %1\n%2")
                                    .arg (program + ' ' + arguments.join (' '))
                                    .arg (errorString));

    initDecoderSubprocess();
}

void MainWindow::closeEvent(QCloseEvent * e)
{
  m_valid = false;              // suppresses subprocess errors
  m_config.transceiver_offline ();
  writeSettings ();
  m_guiTimer.stop ();
  m_prefixes.reset ();
  m_shortcuts.reset ();
  m_mouseCmnds.reset ();
  mem_js8->detach();
  QFile quitFile {m_config.temp_dir ().absoluteFilePath (".quit")};
  quitFile.open(QIODevice::ReadWrite);
  {
      if(JS8_DEBUG_DECODE) qDebug() << "decoder lock remove";
      QFile {m_config.temp_dir ().absoluteFilePath (".lock")}.remove(); // Allow jt9 to terminate
      m_decoder.processQuit();
  }
  quitFile.remove();
  Q_EMIT finished ();

  QMainWindow::closeEvent (e);
}

void MainWindow::on_dialFreqUpButton_clicked(){
    setRig(m_freqNominal + 250);
}

void MainWindow::on_dialFreqDownButton_clicked(){
    setRig(m_freqNominal - 250);
}

void MainWindow::on_actionAdd_Log_Entry_triggered(){
  on_logQSOButton_clicked();
}

void MainWindow::on_actionCopyright_Notice_triggered()
{
  auto const& message = tr("If you make fair use of any part of this program under terms of the GNU "
                           "General Public License, you must display the following copyright "
                           "notice prominently in your derivative work:\n\n"
                           "\"The algorithms, source code, look-and-feel of WSJT-X and related "
                           "programs, and protocol specifications for the modes FSK441, FT8, JT4, "
                           "JT6M, JT9, JT65, JTMS, QRA64, ISCAT, MSK144 are Copyright (C) "
                           "2001-2018 by one or more of the following authors: Joseph Taylor, "
                           "K1JT; Bill Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, "
                           "IV3NWV; Greg Beam, KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; "
                           "Philip Karn, KA9Q; and other members of the WSJT Development Group.\n\n"
                           "Further, the source code of JS8Call contains material Copyright (C) "
                           "2018-2019 by Jordan Sherer, KN4CRD.\"");
  MessageBox::warning_message(this, message);
}

/**
 * @brief MainWindow::isDecodeReady
 *        determine if decoding is ready for a given submode
 * @param submode - submode to test
 * @param k - current frame count
 * @param k0 - previous frame count
 * @param pCurrentDecodeStart - input pointer to a static integer with the current decode start position
 * @param pNextDecodeStart - input pointer to a static integer with the next decode start position
 * @param pStart - output pointer to the next start position when decode is ready
 * @param pSz - output pointer to the next size when decode is ready
 * @param pCycle - output pointer to the next cycle when decode is ready
 * @return true if decode is ready for this submode, false otherwise
 */
bool
MainWindow::isDecodeReady(int    const submode,
                          qint32 const k,
                          qint32 const k0,
                          qint32     * pCurrentDecodeStart,
                          qint32     * pNextDecodeStart,
                          qint32     * pStart,
                          qint32     * pSz,
                          qint32     * pCycle)
{
    if (pCurrentDecodeStart == nullptr ||
        pNextDecodeStart    == nullptr)
    {
        return false;
    }

    qint32 const cycleFrames  = JS8::Submode::framesPerCycle(submode);
    qint32 const framesNeeded = JS8::Submode::framesNeeded(submode);
    qint32 const currentCycle = JS8::Submode::computeCycleForDecode(submode, k);
    qint32 const delta        = qAbs(k - k0);

    if(delta > cycleFrames){
        if(JS8_DEBUG_DECODE) qDebug() << "-->" << JS8::Submode::name(submode) << "buffer advance delta" << delta;
    }

    // say, current decode start is 360000 and the next is 540000 (right before we loop)
    // frames needed are 150000
    // and then we turn off rx until k is 110000 and the cycle frames are 180000 and k0 is a proper 100000
    // we need to still reset...
    // so, if k is less than the last decode start - cycle frames (in this case 360000-180000, or 180000),
    // then we should reset. but, what if k is now 182000??

    // k=182000
    // k < current (182000 < 360000) true
    // k < max(0, current-cycleframes+framesNeeded) (k < 180000+150000) true

    // k=6000
    // k < current (6000<360000) true
    // k < max(0, 360000-180000+150000) true

    // k=350000
    // k < current (350000<360000) true
    // k < max(0, 360000-350000+150000) false

    // are we in the space between the end of the last decode and the start of the next decode?
    bool const deadAir = (k < *pCurrentDecodeStart &&
                          k < qMax(0, *pCurrentDecodeStart-cycleFrames+framesNeeded));

    // on buffer loop or init, prepare proper next decode start
    if(
        (deadAir)                    ||
        (k < k0)                     ||
        (delta > cycleFrames)        ||
        (*pCurrentDecodeStart == -1) ||
        (*pNextDecodeStart    == -1)
    ){
        *pCurrentDecodeStart = currentCycle * cycleFrames;
        *pNextDecodeStart    = *pCurrentDecodeStart + cycleFrames;
    }

    bool const ready = *pCurrentDecodeStart + framesNeeded <= k;

    if(ready){
        if(JS8_DEBUG_DECODE) qDebug() << "-->" << JS8::Submode::name(submode) << "from" << *pCurrentDecodeStart << "to" << *pCurrentDecodeStart+framesNeeded << "k" << k << "k0" << k0;

        if(pCycle) *pCycle = currentCycle;
        if(pStart) *pStart = *pCurrentDecodeStart;
        if(pSz)    *pSz    = qMax(framesNeeded, k-(*pCurrentDecodeStart));

        *pCurrentDecodeStart = *pNextDecodeStart;
        *pNextDecodeStart    = *pCurrentDecodeStart + cycleFrames;
    }

    return ready;
}


/**
 * @brief MainWindow::decode
 *        try decoding
 * @return true if the decoder was activated, false otherwise
 */
bool MainWindow::decode(qint32 k){
    static int k0 = 9999999;
    int kZero = k0;
    k0 = k;

    if(JS8_DEBUG_DECODE) qDebug() << "decoder checking if ready..." << "k" << k << "k0" << kZero << "busy?" << m_decoderBusy << "lock exists?" << ( QFile{m_config.temp_dir ().absoluteFilePath (".lock")}.exists());

    if(k == kZero){
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder stream has not advanced";
        return false;
    }

    if(!m_monitoring){
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder stream is not active";
        return false;
    }

    bool ready = false;

#if JS8_USE_EXPERIMENTAL_DECODE_TIMING
    ready = decodeEnqueueReady(k, kZero);
    if(ready || !m_decoderQueue.isEmpty()){
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder is ready to be run with" << m_decoderQueue.count() << "decode periods";
    }
#else
    ready = decodeEnqueueReadyExperiment(k, kZero);
    if(ready || !m_decoderQueue.isEmpty()){
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder is ready to be run with" << m_decoderQueue.count() << "decode periods";
    }
#endif

    //
    // TODO: what follows can likely be pulled out to an async process
    //

    // pause decoder if we are currently transmitting
    if(m_transmitting){
        // we used to use isMessageQueuedForTransmit, and some form of checking for queued messages
        // but, that just caused problems with missing decodes, so we only pause if we are actually
        // actively transmitting.
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder paused during transmit";
        return false;
    }

    if(m_decoderBusyStartTime.isValid() && m_decoderBusyStartTime.msecsTo(QDateTime::currentDateTimeUtc()) < 1000){
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder paused for 1000 ms after last decode start";
        return false;
    }

    int threshold = m_nSubMode == Varicode::JS8CallSlow ? 4000 : 2000; // two seconds
    if(isInDecodeDelayThreshold(threshold)){
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder paused for" << threshold << "ms after transmit stop";
        return false;
    }

    // critical section (modifying dec_data)

    qint32 submode = -1;
    if(!decodeProcessQueue(&submode)){
        return false;
    }

    decodeStart();

    return true;
}

/**
 * @brief MainWindow::decodeEnqueueReady
 *        compute the available decoder ranges that can be processed and
 *        place them in the decode queue
 * @param k - the current frame count
 * @param k0 - the previous frame count
 * @return true if decoder ranges were queued, false otherwise
 */
bool MainWindow::decodeEnqueueReady(qint32 k, qint32 k0){
    // compute the next decode for each submode
    // enqueue those decodes that are "ready"
    // on an interval, issue a decode
    int decodes = 0;

    bool couldDecodeA = false;
    qint32 startA = -1;
    qint32 szA = -1;
    qint32 cycleA = -1;

    bool couldDecodeB = false;
    qint32 startB = -1;
    qint32 szB = -1;
    qint32 cycleB = -1;

    bool couldDecodeC = false;
    qint32 startC = -1;
    qint32 szC = -1;
    qint32 cycleC = -1;

    bool couldDecodeE = false;
    qint32 startE = -1;
    qint32 szE = -1;
    qint32 cycleE = -1;

#if JS8_ENABLE_JS8I
    bool couldDecodeI = false;
    qint32 startI = -1;
    qint32 szI = -1;
    qint32 cycleI = -1;
#endif

    static qint32 currentDecodeStartA = -1;
    static qint32 nextDecodeStartA = -1;
    if(JS8_DEBUG_DECODE) qDebug() << "? NORMAL   " << currentDecodeStartA << nextDecodeStartA;
    couldDecodeA = isDecodeReady(Varicode::JS8CallNormal, k, k0, &currentDecodeStartA, &nextDecodeStartA, &startA, &szA, &cycleA);

    static qint32 currentDecodeStartB = -1;
    static qint32 nextDecodeStartB = -1;
    if(JS8_DEBUG_DECODE) qDebug() << "? FAST     " << currentDecodeStartB << nextDecodeStartB;
    couldDecodeB = isDecodeReady(Varicode::JS8CallFast, k, k0, &currentDecodeStartB, &nextDecodeStartB, &startB, &szB, &cycleB);

    static qint32 currentDecodeStartC = -1;
    static qint32 nextDecodeStartC = -1;
    if(JS8_DEBUG_DECODE) qDebug() << "? TURBO    " << currentDecodeStartC << nextDecodeStartC;
    couldDecodeC = isDecodeReady(Varicode::JS8CallTurbo, k, k0, &currentDecodeStartC, &nextDecodeStartC, &startC, &szC, &cycleC);

    static qint32 currentDecodeStartE = -1;
    static qint32 nextDecodeStartE = -1;
    if(JS8_DEBUG_DECODE) qDebug() << "? SLOW     " << currentDecodeStartE << nextDecodeStartE;
    couldDecodeE = isDecodeReady(Varicode::JS8CallSlow, k, k0, &currentDecodeStartE, &nextDecodeStartE, &startE, &szE, &cycleE);

#if JS8_ENABLE_JS8I
    static qint32 currentDecodeStartI = -1;
    static qint32 nextDecodeStartI = -1;
    if(JS8_DEBUG_DECODE) qDebug() << "? ULTRA    " << currentDecodeStartI << nextDecodeStartI;
    couldDecodeI = isDecodeReady(Varicode::JS8CallUltra, k, k0, &currentDecodeStartI, &nextDecodeStartI, &startI, &szI, &cycleI);
#endif

    if(couldDecodeA){
        DecodeParams d;
        d.submode = Varicode::JS8CallNormal;
        d.start = startA;
        d.sz = szA;
        m_decoderQueue.append(d);
        decodes++;
    }

    if(couldDecodeB){
        DecodeParams d;
        d.submode = Varicode::JS8CallFast;
        d.start = startB;
        d.sz = szB;
        m_decoderQueue.append(d);
        decodes++;
    }

    if(couldDecodeC){
        DecodeParams d;
        d.submode = Varicode::JS8CallTurbo;
        d.start = startC;
        d.sz = szC;
        m_decoderQueue.append(d);
        decodes++;
    }

    if(couldDecodeE){
        DecodeParams d;
        d.submode = Varicode::JS8CallSlow;
        d.start = startE;
        d.sz = szE;
        m_decoderQueue.append(d);
        decodes++;
    }

#if JS8_ENABLE_JS8I
    if(couldDecodeI){
        DecodeParams d;
        d.submode = Varicode::JS8CallUltra;
        d.start = startI;
        d.sz = szI;
        m_decoderQueue.append(d);
        decodes++;
    }
#endif

    return decodes > 0;
}

/**
 * @brief MainWindow::decodeEnqueueReadyExperiment
 *        compute the available decoder ranges that can be processed and
 *        place them in the decode queue
 *
 *        experiment with decoding on a much shorter interval than usual
 *
 * @param k - the current frame count
 * @param k0 - the previous frame count
 * @return true if decoder ranges were queued, false otherwise
 */
bool MainWindow::decodeEnqueueReadyExperiment(qint32 k, qint32 /*k0*/){
    // TODO: make this non-static field of MainWindow?
    // map of last decode positions for each submode
    // static QMap<qint32, qint32> m_lastDecodeStartMap;

    // TODO: make this non-static field of MainWindow?
    // map of submodes to decode + optional alternate decode positions
    static QMap<qint32, QList<qint32>> submodes = {
        {Varicode::JS8CallSlow,   {0}},
        {Varicode::JS8CallNormal, {0}},
        {Varicode::JS8CallFast,   {0}}, // NORMAL: 0, 10, 20    --- ALT: 15, 25
        {Varicode::JS8CallTurbo,  {0}}, // NORMAL: 0, 6, 12, 18 --- ALT: 15, 21, 27
#if JS8_ENABLE_JS8I
        {Varicode::JS8CallUltra,  {0}},
#endif
    };

    static qint32 maxSamples = NTMAX*RX_SAMPLE_RATE;
    static qint32 oneSecondSamples = RX_SAMPLE_RATE;

    int decodes = 0;

    // do we have a better way to check this?
    bool multi = ui->actionModeMultiDecoder->isChecked();

    // do we need to process alternate positions?
    bool skipAlt = true;

    foreach(auto submode, submodes.keys()){
        // do we have a better way to check this?
        bool everySecond = m_wideGraph->shouldAutoSyncSubmode(submode);

        // skip if multi is disabled and this mode is not the current submode and we're not autosyncing this mode
        if(!everySecond && !multi && submode != m_nSubMode){
            continue;
        }

        // check all alternate decode positions
        foreach(auto alt, submodes.value(submode)){
            // skip alt decode positions if needed
            if(skipAlt && alt != 0){
                continue;
            }

            // skip alts if we are decoding every second
            if(everySecond && alt != 0){
                continue;
            }

            qint32 const cycle             = JS8::Submode::computeAltCycleForDecode(submode, k, alt*oneSecondSamples);
            qint32 const cycleFrames       = JS8::Submode::framesPerCycle(submode);
            qint32 const cycleFramesNeeded = JS8::Submode::framesForSymbols(submode); //computeFramesNeededForDecode(submode) - oneSecondSamples;
            qint32       cycleFramesReady  = k - (cycle * cycleFrames);
            if(cycleFramesReady < 0){
                cycleFramesReady = k + (maxSamples - (cycle * cycleFrames));
            }

            if(!m_lastDecodeStartMap.contains(submode)){
                m_lastDecodeStartMap[submode] = cycle * cycleFrames;
            }

            qint32 lastDecodeStart = m_lastDecodeStartMap[submode];
            qint32 incrementedBy = k - lastDecodeStart;
            if(k < lastDecodeStart){
                incrementedBy = maxSamples - lastDecodeStart + k;
            }

            if(JS8_DEBUG_DECODE) qDebug() << JS8::Submode::name(submode) << "alt" << alt << "cycle" << cycle << "cycle frames" << cycleFrames << "cycle start" << cycle*cycleFrames << "cycle end" << (cycle+1)*cycleFrames << "k" << k << "frames ready" << cycleFramesReady << "incremeted by" << incrementedBy;

            if(everySecond && incrementedBy >= oneSecondSamples){
                DecodeParams d;
                d.submode = submode;
                d.sz = cycleFrames;
                d.start = k - d.sz;
                if(d.start < 0){
                    d.start += maxSamples;
                }
                m_decoderQueue.append(d);
                decodes++;

                // keep track of last decode position
                m_lastDecodeStartMap[submode] = k;
            }
            else if(
                (incrementedBy >= 1.5*oneSecondSamples && cycleFramesReady >= cycleFramesNeeded)                        || // within every 3/2 seconds for normal positions
                (incrementedBy >= oneSecondSamples     && cycleFramesReady >= cycleFramesNeeded - 1.5*oneSecondSamples) || // within the last 3/2 seconds of a new cycle
                (incrementedBy >= oneSecondSamples     && cycleFramesReady < 1.5*oneSecondSamples)                         // within the first 3/2 seconds of a new cycle
            ){
                DecodeParams d;
                d.submode = submode;
                d.start = cycle*cycleFrames;
                d.sz = cycleFramesReady;
                m_decoderQueue.append(d);
                decodes++;

                // keep track of last decode position
                m_lastDecodeStartMap[submode] = k;
            }
        }
    }

    return decodes > 0;
}

/**
 * @brief MainWindow::decodeProcessQueue
 *        process the decode queue by merging available decode ranges
 *        into the dec_data shared structure for the decoder to process
 * @param pSubmode - the lowest speed submode in this iteration
 * @return true if the decoder is ready to be run, false otherwise
 */
bool MainWindow::decodeProcessQueue(qint32 *pSubmode){
    // critical section
    QMutexLocker mutex(m_detector->getMutex());

    if(m_decoderBusy){
        int seconds = m_decoderBusyStartTime.secsTo(QDateTime::currentDateTimeUtc());
        if(seconds > 60){
            if(JS8_DEBUG_DECODE) qDebug() << "--> decoder should be killed!" << QString("(%1 seconds)").arg(seconds);
        } else if(seconds > 30){
            if(JS8_DEBUG_DECODE) qDebug() << "--> decoder is hanging!" << QString("(%1 seconds)").arg(seconds);
        } else {
            if(JS8_DEBUG_DECODE) qDebug() << "--> decoder is busy!";
        }

        return false;
    }

    if(m_decoderQueue.isEmpty()){
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder has nothing to process!";
        return false;
    }

    int submode = -1;
    int maxDecodes = 1;

    bool multi = ui->actionModeMultiDecoder->isChecked();
    if(multi){
        maxDecodes = JS8_ENABLE_JS8I ? 5 : 4;
    }

    int count = m_decoderQueue.count();
    if(count > maxDecodes){
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder skipping at least 1 decode cycle" << "count" << count << "max" << maxDecodes;
    }

    // default to no submodes being decoded, then bitwise OR the modes together to decode them all at once
    dec_data.params.nsubmodes = 0;

    while(!m_decoderQueue.isEmpty()){
        auto params = m_decoderQueue.front();
        m_decoderQueue.removeFirst();

        // skip if we are not in multi mode and the submode doesn't equal the global submode
        if(!multi && params.submode != m_nSubMode){
            continue;
        }

        if(submode == -1 || params.submode < submode){
            submode = params.submode;
        }

        switch(params.submode){
        case Varicode::JS8CallNormal:
            dec_data.params.kposA = params.start;
            dec_data.params.kszA = params.sz;
            dec_data.params.nsubmodes |= (params.submode + 1);
            break;
        case Varicode::JS8CallFast:
            dec_data.params.kposB = params.start;
            dec_data.params.kszB = params.sz;
            dec_data.params.nsubmodes |= (params.submode << 1);
            break;
        case Varicode::JS8CallTurbo:
            dec_data.params.kposC = params.start;
            dec_data.params.kszC = params.sz;
            dec_data.params.nsubmodes |= (params.submode << 1);
            break;
        case Varicode::JS8CallSlow:
            dec_data.params.kposE = params.start;
            dec_data.params.kszE = params.sz;
            dec_data.params.nsubmodes |= (params.submode << 1);
            break;
#if JS8_ENABLE_JS8I
        case Varicode::JS8CallUltra:
            dec_data.params.kposI = params.start;
            dec_data.params.kszI = params.sz;
            dec_data.params.nsubmodes |= (params.submode << 1);
            break;
#endif
        }
    }

    if(submode == -1){
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder has no segments to decode!";
        return false;
    }

    dec_data.params.syncStats = (m_wideGraph->shouldDisplayDecodeAttempts() || m_wideGraph->isAutoSyncEnabled());
    dec_data.params.npts8     = (m_ihsym*NSPS)/16;
    dec_data.params.newdat    = 1;
    dec_data.params.nagain    = 0;

    auto const period = JS8::Submode::period(submode);
    auto const t      = DriftingDateTime::currentDateTimeUtc().addSecs(2 - period);
    auto const ihr    = t.toString("hh").toInt();
    auto const imin   = t.toString("mm").toInt();
    auto const isec   = t.toString("ss").toInt();

    dec_data.params.nutc = ihr  * 10000 +
                           imin *   100 +
                           isec - isec % period;

    dec_data.params.nfqso    = freq();
    dec_data.params.ndepth   = m_ndepth;
    dec_data.params.nranera  = 6;
    dec_data.params.ndiskdat = 0;
    dec_data.params.nfa      = 0;
    dec_data.params.nfb      = 5000;

    if (m_wideGraph->filterEnabled())
    {
      auto const low  = max(0, m_wideGraph->filterMinimum());
      auto const high = min(m_wideGraph->filterMaximum(), 5000);

      dec_data.params.nfa = min(low, high);
      dec_data.params.nfb = max(low, high);
    }

    if (dec_data.params.nutc   <  m_nutc0) m_RxLog = 1;       //Date and Time to ALL.TXT
    if (dec_data.params.newdat == 1)       m_nutc0 = dec_data.params.nutc;

    dec_data.params.nmode    =  8;
    dec_data.params.napwid   =  50;
    dec_data.params.nsubmode = -1;  // not needed

    copyStringData(m_dateTime,             dec_data.params.datetime, sizeof(dec_data.params.datetime));
    copyStringData(m_config.my_callsign(), dec_data.params.mycall,   sizeof(dec_data.params.mycall));

    // keep track of the minimum submode
    if(pSubmode) *pSubmode = submode;

    return true;
}

/**
 * @brief MainWindow::decodeStart
 *        copy the dec_data structure to shared memory and
 *        remove the lock file to start the decoding process
 */
void MainWindow::decodeStart(){
    // critical section
    QMutexLocker mutex(m_detector->getMutex());

    if(m_decoderBusy){
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder cannot start...busy (busy flag)";
        return;
    }

    QFile lock {m_config.temp_dir ().absoluteFilePath (".lock")};
    if(!lock.exists()){
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder cannot start...busy (lock missing)";
        return;
    }

    // mark the decoder busy early while we prep the memory copy
    // decodeDone is responsible for marking the decode _not_ busy
    decodeBusy(true);
    {
        if(JS8_DEBUG_DECODE) qDebug() << "--> decoder starting";
        if(JS8_DEBUG_DECODE) qDebug() << " --> kin:" << dec_data.params.kin;
        if(JS8_DEBUG_DECODE) qDebug() << " --> newdat:" << dec_data.params.newdat;
        if(JS8_DEBUG_DECODE) qDebug() << " --> nsubmodes:" << dec_data.params.nsubmodes;
        if(JS8_DEBUG_DECODE) qDebug() << " --> A:" << dec_data.params.kposA << dec_data.params.kposA + dec_data.params.kszA << QString("(%1)").arg(dec_data.params.kszA);
        if(JS8_DEBUG_DECODE) qDebug() << " --> B:" << dec_data.params.kposB << dec_data.params.kposB + dec_data.params.kszB << QString("(%1)").arg(dec_data.params.kszB);
        if(JS8_DEBUG_DECODE) qDebug() << " --> C:" << dec_data.params.kposC << dec_data.params.kposC + dec_data.params.kszC << QString("(%1)").arg(dec_data.params.kszC);
        if(JS8_DEBUG_DECODE) qDebug() << " --> E:" << dec_data.params.kposE << dec_data.params.kposE + dec_data.params.kszE << QString("(%1)").arg(dec_data.params.kszE);
        if(JS8_DEBUG_DECODE) qDebug() << " --> I:" << dec_data.params.kposI << dec_data.params.kposI + dec_data.params.kszI << QString("(%1)").arg(dec_data.params.kszI);

        //newdat=1  ==> this is new data, must do the big FFT
        //nagain=1  ==> decode only at fQSO +/- Tol

        char *to = (char*)mem_js8->data();
        char *from = (char*) dec_data.ss;
        int size=sizeof(struct dec_data);

        // only copy the params
        if(dec_data.params.newdat==0) {
            int noffset {offsetof (struct dec_data, params.nutc)};
            to += noffset;
            from += noffset;
            size -= noffset;
        }

        memcpy(to, from, qMin(mem_js8->size(), size));
    }
    if(JS8_DEBUG_DECODE) qDebug() << "decoder lock remove";
    lock.remove(); // Allow decoder to start
}

/**
 * @brief MainWindow::decodeBusy
 *        mark the decoder as currently busy (to prevent overlapping decodes)
 * @param b - true if busy, false otherwise
 */
void MainWindow::decodeBusy(bool b)                             //decodeBusy()
{
  m_decoderBusy=b;
  if(m_decoderBusy){
    tx_status_label.setText("Decoding");
    m_decoderBusyStartTime = QDateTime::currentDateTimeUtc(); //DriftingDateTime::currentDateTimeUtc();
    m_decoderBusyFreq = dialFrequency();
    m_decoderBusyBand = m_config.bands()->find (m_decoderBusyFreq);
  }
}

/**
 * @brief MainWindow::decodeDone
 *        clean up after a decode is finished
 */
void MainWindow::decodeDone ()
{
  // critical section
  QMutexLocker mutex(m_detector->getMutex());

  if(JS8_DEBUG_DECODE) qDebug() << "decoder lock create";
  QFile {m_config.temp_dir ().absoluteFilePath (".lock")}.open(QIODevice::ReadWrite);
  dec_data.params.newdat   = false;
  dec_data.params.nagain   = false;
  dec_data.params.ndiskdat = false;
  m_RxLog                  = 0;

  // cleanup old cached messages (messages > submode period old)

  erase_if(m_messageDupeCache, [](auto const & it)
  {
    auto const & cached = it.value();
    return cached.date.secsTo(QDateTime::currentDateTimeUtc()) > JS8::Submode::period(cached.submode);
  });

  decodeBusy(false);
}

/**
 * @brief MainWindow::decodeCheckHangingDecoder
 *        check if decoder is hanging and reset if it is
 */
void MainWindow::decodeCheckHangingDecoder(){
    if(!m_decoderBusy){
        return;
    }

    if(!m_decoderBusyStartTime.isValid() || m_decoderBusyStartTime.secsTo(QDateTime::currentDateTimeUtc()) < 60){
        return;
    }

    m_decoderBusyStartTime = QDateTime();

    SelfDestructMessageBox * m = new SelfDestructMessageBox(30,
      "Decoder Restart",
      "The JS8 decoder is restarting.",
      QMessageBox::Warning,
      QMessageBox::Ok,
      QMessageBox::Ok,
      false,
      this);

    m->show();

    initDecoderSubprocess();
}

QDateTime MainWindow::nextTransmitCycle(){
    auto timestamp = DriftingDateTime::currentDateTimeUtc();

    // remove milliseconds
    auto t = timestamp.time();
    t.setHMS(t.hour(), t.minute(), t.second());
    timestamp.setTime(t);

    // round to 15 second increment
    int secondsSinceEpoch = (timestamp.toMSecsSinceEpoch()/1000);
    int delta = roundUp(secondsSinceEpoch, m_TRperiod) + 1 - secondsSinceEpoch;
    timestamp = timestamp.addSecs(delta);

    return timestamp;
}

void MainWindow::resetAutomaticIntervalTransmissions(bool stopCQ, bool stopHB){
    resetCQTimer(stopCQ);
    resetHeartbeatTimer(stopHB);
}

void MainWindow::resetCQTimer(bool stop){
    if(ui->cqMacroButton->isChecked() && m_cqInterval > 0){
        ui->cqMacroButton->setChecked(false);
        if(!stop){
            ui->cqMacroButton->setChecked(true);
        }
    }
}

void MainWindow::resetHeartbeatTimer(bool stop){
    // toggle the heartbeat timer if we have a repeating heartbeat
    if(ui->hbMacroButton->isChecked() && m_hbInterval > 0){
        ui->hbMacroButton->setChecked(false);
        if(!stop){
            ui->hbMacroButton->setChecked(true);
        }
    }
}

QList<int> generateOffsets(int minOffset, int maxOffset){
    QList<int> offsets;

    // TODO: these offsets aren't ordered correctly...

    for(int i = minOffset; i <= maxOffset; i++){
        offsets.append(i);
    }
    return offsets;
}

void MainWindow::readFromStdout(QProcess * proc)                             //readFromStdout
{
  if(!proc || proc->state() == QProcess::NotRunning){
    qDebug() << "proc not running";
    return;
  }

  while(proc->canReadLine()) {
    processDecodedLine(proc->readLine());
  }

  // See MainWindow::postDecode for displaying the latest decodes
}

void MainWindow::processDecodedLine(QByteArray t){
  if(JS8_DEBUG_DECODE) qDebug() << "JS8: " << QString(t);

  //int navg=0;

  static QList<qint32> driftQueue;

  static qint32 syncStart = -1;
  if(t.indexOf("<DecodeSyncMeta> sync start") >= 0){
      auto segs =  QString(t.trimmed()).split(QRegularExpression("[\\s\\t]+"), Qt::SkipEmptyParts);
      if(segs.isEmpty()){
          return;
      }

      syncStart = segs.at(3).toInt();
      return;
  }

  if(t.indexOf("<DecodeSyncStat>") >= 0) {
      auto segs =  QString(t.trimmed()).split(QRegularExpression("[\\s\\t]+"), Qt::SkipEmptyParts);
      if(segs.isEmpty()){
          return;
      }

      // only continue if we should either display decode attempts
      if(!m_wideGraph->shouldDisplayDecodeAttempts()){
          return;
      }

      auto m1 = QString(segs.at(2));
      auto m = int(m1.toInt());

      auto f1 = QString(segs.at(4));
      auto f = int(f1.toFloat());

      auto s1 = QString(segs.at(6));
      auto s = int(s1.toFloat());

      auto xdt1 = QString(segs.at(8));
      auto xdt = xdt1.toFloat();
      auto xdtMs = int(xdt*1000);

      // draw candidates
      if(abs(xdtMs) <= 2000){
          if(s < 10){
            m_wideGraph->drawDecodeLine(QColor(Qt::darkCyan), f, f + JS8::Submode::bandwidth(m));
          } else if (s <= 15){
            m_wideGraph->drawDecodeLine(QColor(Qt::cyan), f, f + JS8::Submode::bandwidth(m));
          } else if (s <= 21){
            m_wideGraph->drawDecodeLine(QColor(Qt::white), f, f + JS8::Submode::bandwidth(m));
          }
      }

      if(!t.contains("decode")){
          return;
      }

      // draw decodes
      m_wideGraph->drawDecodeLine(QColor(Qt::red), f, f + JS8::Submode::bandwidth(m));

      if(JS8_DEBUG_DECODE) qDebug() << "--> busy?" << m_decoderBusy << "lock exists?" << ( QFile{m_config.temp_dir ().absoluteFilePath (".lock")}.exists());

      return;
  }

  if(t.indexOf("<DecodeStarted>") >= 0) {
      if(m_wideGraph->shouldDisplayDecodeAttempts()){
          m_wideGraph->drawHorizontalLine(QColor(Qt::yellow), 0, 5);
      }

      if(JS8_DEBUG_DECODE) qDebug() << "--> busy?" << m_decoderBusy << "lock exists?" << ( QFile{m_config.temp_dir ().absoluteFilePath (".lock")}.exists());
      return;
  }

  if(t.indexOf("<DecodeDebug>") >= 0) {
      return;
  }

  if(t.indexOf("<DecodeFinished>") >= 0) {
    if(JS8_DEBUG_DECODE) qDebug() << "decode duration" << m_decoderBusyStartTime.msecsTo(QDateTime::currentDateTimeUtc()) << "ms";

    // TODO: move this into a function
    if(!driftQueue.isEmpty()){
        if(m_driftMsMMA_N == 0){
            m_driftMsMMA_N = 1;
            m_driftMsMMA = DriftingDateTime::drift();
        }

        // let the widegraph know for timing control
        m_wideGraph->notifyDriftedSignalsDecoded(driftQueue.count());

        while(!driftQueue.isEmpty()){
            qint32 newDrift = driftQueue.first();
            driftQueue.removeFirst();

            m_driftMsMMA = (((m_driftMsMMA_N-1) * m_driftMsMMA) + newDrift) / m_driftMsMMA_N;
            if(m_driftMsMMA_N < 60) m_driftMsMMA_N++; // cap it to 60 observations
        }

        // XXX The following lines do nothing; it's a completely dead store. For
        //     now, just #ifdefing them out, but they were in the 2.2.1-devel code,
        //     and presumably they were important; need to see what the intent was
        //     here.
#if 0
        qint32 driftLimitMs = JS8::Submode::period(Varicode::JS8CallNormal) * 1000;
        qint32 newDriftMs   = m_driftMsMMA;
        if(newDriftMs < 0){
            newDriftMs = -((-newDriftMs) % driftLimitMs);
        } else {
            newDriftMs = ((newDriftMs) % driftLimitMs);
        }
#endif

        setDrift(m_driftMsMMA);
        //writeNoticeTextToUI(QDateTime::currentDateTimeUtc(), QString("Automatic Drift: %1").arg(driftAvg));
    }

    m_bDecoded = t.mid(16).trimmed().toInt() > 0;
    decodeDone();
    return;
  }

  auto rawText = QString::fromUtf8 (t.constData ()).remove (QRegularExpression {"\r|\n"});

  DecodedText decodedtext {rawText};

  // TODO: move this into a function
  // frames are valid if they pass our dupe check (haven't seen the same frame in the past 1/2 decode period)
  auto frameOffset = decodedtext.frequencyOffset();
  auto frameDedupeKey = QString("%1:%2").arg(decodedtext.submode()).arg(decodedtext.frame());
  if(m_messageDupeCache.contains(frameDedupeKey)){
      auto cached = m_messageDupeCache.value(frameDedupeKey);

      // check to see if the time since last seen is > 1/2 decode period
      auto cachedDate = cached.date;
      if(cachedDate.secsTo(QDateTime::currentDateTimeUtc()) < 0.5 * JS8::Submode::period(decodedtext.submode())){
          qDebug() << "duplicate frame at" << cachedDate << "using key" << frameDedupeKey;
          return;
      }

      // check to see if the frequency is near our previous frame
      auto cachedFreq = cached.freq;
      if(qAbs(cachedFreq - frameOffset) <= JS8::Submode::rxThreshold(decodedtext.submode())){
        qDebug() << "duplicate frame from" << cachedFreq << "and" << frameOffset << "using key" << frameDedupeKey;
        return;
      }

      // huzzah!
      // if we make it here, the cache is invalid and will be bumped when we cache the new frame below
  }

  // frames are valid if they meet our minimum rx threshold for the submode
  bool bValidFrame = decodedtext.snr() >= JS8::Submode::rxSNRThreshold(decodedtext.submode());

  qDebug() << "valid" << bValidFrame << JS8::Submode::name(decodedtext.submode()) << "decoded text" << decodedtext.message();

  // skip if invalid
  if(!bValidFrame) {
      return;
  }

  // TODO: move this into a function
  // compute time drift for non-dupe messages
  if(m_wideGraph->shouldAutoSyncSubmode(decodedtext.submode())){
      int m = decodedtext.submode();
      float xdt = decodedtext.dt();

      // if we're here at this point, we _should_ be operating a decode every second
      //
      // so we need to figure out where:
      //
      //   1) this current decode started
      //   2) when that cycle _should_ have started
      //   3) compute the delta
      //   4) apply the drift

      qint32 periodMs = 1000 * JS8::Submode::period(m);

      //writeNoticeTextToUI(now, QString("Decode at %1 (kin: %2, lastDecoded: %3)").arg(syncStart).arg(dec_data.params.kin).arg(m_lastDecodeStartMap.value(m)));

      float expectedStartDelay = JS8::Submode::startDelayMS(m) / 1000.0;

      float decodedSignalTime = (float)syncStart/(float)RX_SAMPLE_RATE;

      //writeNoticeTextToUI(now, QString("--> started at %1 seconds into the start of my drifted minute").arg(decodedSignalTime));

      //writeNoticeTextToUI(now, QString("--> we add a time delta of %1 seconds into the start of the cycle").arg(xdt));

      // adjust for expected start delay
      decodedSignalTime -= expectedStartDelay;

      // adjust for time delta
      decodedSignalTime += xdt;

      // ensure that we are within a 60 second minute
      if(decodedSignalTime < 0){
          decodedSignalTime += 60.0f;
      } else if(decodedSignalTime > 60){
          decodedSignalTime -= 60.0f;
      }

      //writeNoticeTextToUI(now, QString("--> so signal adjusted started at %1 seconds into the start of my drifted minute").arg(decodedSignalTime));

      qint32 decodedSignalTimeMs = 1000 * decodedSignalTime;
      qint32 cycleStartTimeMs = (decodedSignalTimeMs / periodMs) * periodMs;
      qint32 driftMs = cycleStartTimeMs - decodedSignalTimeMs;

      //writeNoticeTextToUI(now, QString("--> which is a drift adjustment of %1 milliseconds").arg(driftMs));

      // if we have a large negative offset (say -14000), use the positive inverse of +1000
      if(driftMs + periodMs < qAbs(driftMs)){
          driftMs += periodMs;
      }
      // if we have a large positive offset (say 14000, use the negative inverse of -1000)
      else if(qAbs(driftMs - periodMs) < driftMs){
          driftMs -= periodMs;
      }

      //writeNoticeTextToUI(now, QString("--> which is a corrected drift adjustment of %1 milliseconds").arg(driftMs));

      qint32 newDrift = DriftingDateTime::drift() + driftMs;
      if(newDrift < 0){
          newDrift %= -periodMs;
      } else {
          newDrift %= periodMs;
      }

      //writeNoticeTextToUI(now, QString("--> which is rounded to a total drift of %1 milliseconds for this period").arg(newDrift));

      driftQueue.append(newDrift);
  }

  // if the frame is valid, cache it!
  m_messageDupeCache[frameDedupeKey] = {QDateTime::currentDateTimeUtc(), decodedtext.submode(), frameOffset};

  // log valid frames to ALL.txt (and correct their timestamp format)
  auto freq = dialFrequency();

  // if we changed frequencies, use the old frequency that we started the decode with
  if(m_decoderBusyFreq != freq){
      freq = m_decoderBusyFreq;
  }

  auto date = DriftingDateTime::currentDateTimeUtc().toString("yyyy-MM-dd");
  auto time = rawText.left(2) + ":" + rawText.mid(2, 2) + ":" + rawText.mid(4, 2);
  writeAllTxt(date + " " + time + rawText.mid(7) + " " + decodedtext.message(), decodedtext.bits());

  ActivityDetail d = {};
  CallDetail cd = {};
  CommandDetail cmd = {};
  CallDetail td = {};

  // Parse General Activity
#if 1
  bool shouldParseGeneralActivity = true;
  if(shouldParseGeneralActivity && !decodedtext.messageWords().isEmpty()){
    int offset = decodedtext.frequencyOffset();

    if(!m_bandActivity.contains(offset)){
        int const range = JS8::Submode::rxThreshold(decodedtext.submode());

        QList<int> offsets = generateOffsets(offset-range, offset+range);

        foreach(int prevOffset, offsets){
            if(!m_bandActivity.contains(prevOffset)){ continue; }
            m_bandActivity[offset] = m_bandActivity[prevOffset];
            m_bandActivity.remove(prevOffset);
            break;
        }
    }

    //ActivityDetail d = {};
    d.isLowConfidence = decodedtext.isLowConfidence();
    d.isFree = !decodedtext.isStandardMessage();
    d.isCompound = decodedtext.isCompound();
    d.isDirected = decodedtext.isDirectedMessage();
    d.bits = decodedtext.bits();
    d.dial = freq;
    d.offset = offset;
    d.text = decodedtext.message();
    d.utcTimestamp = DriftingDateTime::currentDateTimeUtc();
    d.snr = decodedtext.snr();
    d.isBuffered = false;
    d.submode = decodedtext.submode();
    d.tdrift = m_wideGraph->shouldAutoSyncSubmode(d.submode) ? DriftingDateTime::drift()/1000.0 : decodedtext.dt();

    // if we have any "first" frame, and a buffer is already established, clear it...
    int prevBufferOffset = -1;
    if(((d.bits & Varicode::JS8CallFirst) == Varicode::JS8CallFirst) && hasExistingMessageBuffer(decodedtext.submode(), d.offset, true, &prevBufferOffset)){
        qDebug() << "first message encountered, clearing existing buffer" << prevBufferOffset;
        m_messageBuffer.remove(d.offset);
    }

    // if we have a data frame, and a message buffer has been established, buffer it...
    if(hasExistingMessageBuffer(decodedtext.submode(), d.offset, true, &prevBufferOffset) && !decodedtext.isCompound() && !decodedtext.isDirectedMessage()){
        qDebug() << "buffering data" << d.dial << d.offset << d.text;
        d.isBuffered = true;
        m_messageBuffer[d.offset].msgs.append(d);
        // TODO: incremental display if it's "to" me.
    }

    m_rxActivityQueue.append(d);
    m_bandActivity[offset].append(d);
    while(m_bandActivity[offset].count() > 10){
        m_bandActivity[offset].removeFirst();
    }
  }
#endif

  // Process compound callsign commands (put them in cache)"
#if 1
  qDebug() << "decoded" << decodedtext.frameType() << decodedtext.isCompound() << decodedtext.isDirectedMessage() << decodedtext.isHeartbeat();
  bool shouldProcessCompound = true;
  if(shouldProcessCompound && decodedtext.isCompound() && !decodedtext.isDirectedMessage()){
    cd.call = decodedtext.compoundCall();
    cd.grid = decodedtext.extra(); // compound calls via pings may contain grid...
    cd.snr = decodedtext.snr();
    cd.dial = freq;
    cd.offset = decodedtext.frequencyOffset();
    cd.utcTimestamp = DriftingDateTime::currentDateTimeUtc();
    cd.bits = decodedtext.bits();
    cd.submode = decodedtext.submode();
    cd.tdrift = m_wideGraph->shouldAutoSyncSubmode(d.submode) ? DriftingDateTime::drift()/1000.0 : decodedtext.dt();

    // Only respond to HEARTBEATS...remember that CQ messages are "Alt" pings
    if(decodedtext.isHeartbeat()){
        if(decodedtext.isAlt()){
            // this is a cq with a standard or compound call, ala "KN4CRD/P: @ALLCALL CQ CQ CQ"
            cd.cqTimestamp = DriftingDateTime::currentDateTimeUtc();

            // convert CQ to a directed command and process...
            cmd.from = cd.call;
            cmd.to = "@ALLCALL";
            cmd.cmd = " CQ";
            cmd.snr = cd.snr;
            cmd.bits = cd.bits;
            cmd.grid = cd.grid;
            cmd.dial = cd.dial;
            cmd.offset = cd.offset;
            cmd.utcTimestamp = cd.utcTimestamp;
            cmd.tdrift = cd.tdrift;
            cmd.submode = cd.submode;
            cmd.text = decodedtext.message();

            // TODO: check bits so we only auto respond to "finished" cqs
            m_rxCommandQueue.append(cmd);

            // since this is no longer processed here we omit logging it here.
            // if we change this behavior, we'd change this back to logging here.
            // logCallActivity(cd, true);

            // notification for cq
            tryNotify("cq");

        } else {
            // convert HEARTBEAT to a directed command and process...
            cmd.from = cd.call;
            cmd.to = "@HB";
            cmd.cmd = " HEARTBEAT";
            cmd.snr = cd.snr;
            cmd.bits = cd.bits;
            cmd.grid = cd.grid;
            cmd.dial = cd.dial;
            cmd.offset = cd.offset;
            cmd.utcTimestamp = cd.utcTimestamp;
            cmd.tdrift = cd.tdrift;
            cmd.submode = cd.submode;

            // TODO: check bits so we only auto respond to "finished" heartbeats
            m_rxCommandQueue.append(cmd);

            // notification for hb
            tryNotify("hb");
        }

    } else {
        qDebug() << "buffering compound call" << cd.offset << cd.call << cd.bits;

        hasExistingMessageBuffer(cd.submode, cd.offset, true, nullptr);
        m_messageBuffer[cd.offset].compound.append(cd);
    }
  }
#endif

  // Parse commands
  // KN4CRD K1JT ?
#if 1
  bool shouldProcessDirected = true;
  if(shouldProcessDirected && decodedtext.isDirectedMessage()){
      auto parts = decodedtext.directedMessage();

      cmd.from = parts.at(0);
      cmd.to = parts.at(1);
      cmd.cmd = parts.at(2);
      cmd.dial = freq;
      cmd.offset = decodedtext.frequencyOffset();
      cmd.snr = decodedtext.snr();
      cmd.utcTimestamp = DriftingDateTime::currentDateTimeUtc();
      cmd.bits = decodedtext.bits();
      cmd.extra = parts.length() > 2 ? parts.mid(3).join(" ") : "";
      cmd.submode = decodedtext.submode();
      cmd.tdrift = m_wideGraph->shouldAutoSyncSubmode(cmd.submode) ? DriftingDateTime::drift()/1000.0 : decodedtext.dt();

      // if the command is a buffered command and its not the last frame OR we have from or to in a separate message (compound call)
      if((Varicode::isCommandBuffered(cmd.cmd) && (cmd.bits & Varicode::JS8CallLast) != Varicode::JS8CallLast) || cmd.from == "<....>" || cmd.to == "<....>"){
        qDebug() << "buffering cmd" << cmd.dial << cmd.offset << cmd.cmd << cmd.from << cmd.to;

        // log complete buffered callsigns immediately
        if(cmd.from != "<....>" && cmd.to != "<....>"){
            CallDetail cmdcd = {};
            cmdcd.call = cmd.from;
            cmdcd.bits = cmd.bits;
            cmdcd.snr = cmd.snr;
            cmdcd.dial = cmd.dial;
            cmdcd.offset = cmd.offset;
            cmdcd.utcTimestamp = cmd.utcTimestamp;
            cmdcd.ackTimestamp = cmd.to == m_config.my_callsign() ? cmd.utcTimestamp : QDateTime{};
            cmdcd.tdrift = cmd.tdrift;
            cmdcd.submode = cmd.submode;
            logCallActivity(cmdcd, false);
            logHeardGraph(cmd.from, cmd.to);
        }

        // merge any existing buffer to this frequency
        hasExistingMessageBuffer(cmd.submode, cmd.offset, true, nullptr);

        if(cmd.to == m_config.my_callsign()){
            d.shouldDisplay = true;
        }

        m_messageBuffer[cmd.offset].cmd = cmd;
        m_messageBuffer[cmd.offset].msgs.clear();
      } else {
        m_rxCommandQueue.append(cmd);
      }

      // check to see if this is a station we've heard 3rd party
      bool shouldCaptureThirdPartyCallsigns = false;
      if(shouldCaptureThirdPartyCallsigns && Radio::base_callsign(cmd.to) != Radio::base_callsign(m_config.my_callsign())){
          QString relayCall = QString("%1|%2").arg(Radio::base_callsign(cmd.from)).arg(Radio::base_callsign(cmd.to));
          int snr = -100;
          if(parts.length() == 4){
              snr = QString(parts.at(3)).toInt();
          }

          //CallDetail td = {};
          td.through = cmd.from;
          td.call = cmd.to;
          td.grid = "";
          td.snr = snr;
          td.dial = cmd.dial;
          td.offset = cmd.offset;
          td.utcTimestamp = cmd.utcTimestamp;
          td.tdrift = cmd.tdrift;
          td.submode = cmd.submode;
          logCallActivity(td, true);
          logHeardGraph(cmd.from, cmd.to);
      }
  }
#endif




  // Parse CQs
#if 0
  bool shouldParseCQs = true;
  if(shouldParseCQs && decodedtext.isStandardMessage()){
    QString theircall;
    QString theirgrid;
    decodedtext.deCallAndGrid(theircall, theirgrid);

    QStringList calls = Varicode::parseCallsigns(theircall);
    if(!calls.isEmpty() && !calls.first().isEmpty()){
        theircall = calls.first();

        CallDetail d = {};
        d.bits = decodedtext.bits();
        d.call = theircall;
        d.grid = theirgrid;
        d.snr = decodedtext.snr();
        d.freq = decodedtext.frequencyOffset();
        d.utcTimestamp = DriftingDateTime::currentDateTimeUtc();
        m_callActivity[d.call] = d;
      }
  }
#endif

  // Parse standard message callsigns
  // K1JT KN4CRD EM73
  // KN4CRD K1JT -21
  // K1JT KN4CRD R-12
  // DE KN4CRD
  // KN4CRD
#if 0
  bool shouldParseCallsigns = false;
  if(shouldParseCallsigns){
      QStringList callsigns = Varicode::parseCallsigns(decodedtext.message());
      if(!callsigns.isEmpty()){
          // one callsign
          // de [from]
          // cq [from]

          // two callsigns
          // [from]: [to] ...
          // [to] [from] [grid|signal]

          QStringList grids = Varicode::parseGrids(decodedtext.message());

          // one callsigns are handled above... so we only need to handle two callsigns if it's a standard message
          if(decodedtext.isStandardMessage()){
              if(callsigns.length() == 2){
                  auto de_callsign = callsigns.last();

                  // TODO: jsherer - put this in a function to record a callsign...
                  CallDetail d;
                  d.call = de_callsign;
                  d.grid = !grids.empty() ? grids.first() : "";
                  d.snr = decodedtext.snr();
                  d.freq = decodedtext.frequencyOffset();
                  d.utcTimestamp = DriftingDateTime::currentDateTimeUtc();
                  m_callActivity[Radio::base_callsign(de_callsign)] = d;
              }
          }
      }
  }
#endif
}

bool
MainWindow::hasExistingMessageBufferToMe(int * const pOffset)
{
  for (auto const [offset, buffer] : m_messageBuffer.asKeyValueRange())
  {
    // if this is a valid buffer and it's to me...
    if (buffer.cmd.utcTimestamp.isValid() && (buffer.cmd.to == m_config.my_callsign() ||
                                              buffer.cmd.to == Radio::base_callsign(m_config.my_callsign())))
    {
      if (pOffset) *pOffset = offset;
      return true;
    }
  }

  return false;
}

bool MainWindow::hasExistingMessageBuffer(int submode, int offset, bool drift, int *pPrevOffset){
    if(m_messageBuffer.contains(offset)){
        if(pPrevOffset) *pPrevOffset = offset;
        return true;
    }

    int const range = JS8::Submode::rxThreshold(submode);

    QList<int> offsets = generateOffsets(offset-range, offset+range);

    foreach(int prevOffset, offsets){
        if(!m_messageBuffer.contains(prevOffset)){ continue; }

        if(drift){
            m_messageBuffer[offset] = m_messageBuffer[prevOffset];
            m_messageBuffer.remove(prevOffset);
        }

        if(pPrevOffset) *pPrevOffset = prevOffset;
        return true;
    }

    return false;
}

bool MainWindow::hasClosedExistingMessageBuffer(int offset){
#if 0
    int range = 10;
    if(m_nSubMode == Varicode::JS8CallFast){ range = 16; }
    if(m_nSubMode == Varicode::JS8CallTurbo){ range = 32; }

    return offset - range <= m_lastClosedMessageBufferOffset && m_lastClosedMessageBufferOffset <= offset + range;
#elif 0
    int range = 10;
    if(m_nSubMode == Varicode::JS8CallFast){ range = 16; }
    if(m_nSubMode == Varicode::JS8CallTurbo){ range = 32; }

    return m_lastClosedMessageBufferOffset - range <= offset && offset <= m_lastClosedMessageBufferOffset + range;
#else
    Q_UNUSED(offset);
#endif
    return false;
}

void MainWindow::logCallActivity(CallDetail d, bool spot){
    // don't log empty calls
    if(d.call.trimmed().isEmpty()){
        return;
    }

    // don't log relay calls
    if(d.call.contains(">")){
        return;
    }

    if(m_callActivity.contains(d.call)){
        // update (keep grid)
        CallDetail old = m_callActivity[d.call];
        if(d.grid.isEmpty() && !old.grid.isEmpty()){
            d.grid = old.grid;
        }
        if(!d.ackTimestamp.isValid() && old.ackTimestamp.isValid()){
            d.ackTimestamp = old.ackTimestamp;
        }
        if(!d.cqTimestamp.isValid() && old.cqTimestamp.isValid()){
            d.cqTimestamp = old.cqTimestamp;
        }
        m_callActivity[d.call] = d;
    } else {
        // create
        m_callActivity[d.call] = d;

        // notification of old and new callsigns
        if(m_logBook.hasWorkedBefore(d.call, "")){
            tryNotify("call_old");
        } else {
            tryNotify("call_new");
        }
    }

    // enqueue for spotting to psk reporter
    if(spot){
        m_rxCallQueue.append(d);
    }
}

void MainWindow::logHeardGraph(QString from, QString to){
    auto my_callsign = m_config.my_callsign();

    // hearing
    if(m_heardGraphOutgoing.contains(my_callsign)){
        m_heardGraphOutgoing[my_callsign].insert(from);
    } else {
        m_heardGraphOutgoing[my_callsign].insert(from);
    }

    // heard by
    if(m_heardGraphIncoming.contains(from)){
        m_heardGraphIncoming[from].insert(my_callsign);
    } else {
        m_heardGraphIncoming[from] = { my_callsign };
    }

    if(to == "@ALLCALL"){
        return;
    }

    // hearing
    if(m_heardGraphOutgoing.contains(from)){
        m_heardGraphOutgoing[from].insert(to);
    } else {
        m_heardGraphOutgoing[from] = { to };
    }

    // heard by
    if(m_heardGraphIncoming.contains(to)){
        m_heardGraphIncoming[to].insert(from);
    } else {
        m_heardGraphIncoming[to] = { from };
    }
}

QString MainWindow::lookupCallInCompoundCache(QString const &call){
    QString myBaseCall = Radio::base_callsign(m_config.my_callsign());
    if(call == myBaseCall){
        return m_config.my_callsign();
    }
    return m_compoundCallCache.value(call, call);
}

void
MainWindow::spotReport(int     const   submode,
                       int     const   dial,
                       int     const   offset,
                       int     const   snr,
                       QString const & callsign,
                       QString const & grid)
{
  if (!m_config.spot_to_reporting_networks() ||
      (m_config.spot_blacklist().contains(callsign) ||
       m_config.spot_blacklist().contains(Radio::base_callsign(callsign)))) return;

  Q_EMIT spotClientEnqueueSpot (callsign, grid, submode, dial, offset, snr);
}

void
MainWindow::spotCmd(CommandDetail const & cmd)
{
  if (!m_config.spot_to_reporting_networks() ||
      (m_config.spot_blacklist().contains(cmd.from) ||
       m_config.spot_blacklist().contains(Radio::base_callsign(cmd.from)))) return;

  QString cmdStr = cmd.cmd;

  if (!cmdStr.trimmed().isEmpty())
  {
    cmdStr = Varicode::lstrip(cmd.cmd);
  }

  Q_EMIT spotClientEnqueueCmd (cmdStr,
                               cmd.from,
                               cmd.to,
                               cmd.relayPath,
                               cmd.text,
                               cmd.grid,
                               cmd.extra,
                               cmd.submode,
                               cmd.dial,
                               cmd.offset,
                               cmd.snr);
}

// KN4CRD: @APRSIS CMD :EMAIL-2  :email@domain.com booya{1
void MainWindow::spotAprsCmd(CommandDetail const & cmd){
    if(!m_config.spot_to_reporting_networks()) return;
    if(!m_config.spot_to_aprs()) return;
    if(m_config.spot_blacklist().contains(cmd.from) || m_config.spot_blacklist().contains(Radio::base_callsign(cmd.from))) return;

    if(cmd.cmd != " CMD") return;

    qDebug() << "APRSISClient Enqueueing Third Party Text" << cmd.from << cmd.text;

    auto by_call   = APRSISClient::replaceCallsignSuffixWithSSID(m_config.my_callsign(), Radio::base_callsign(m_config.my_callsign()));
    auto from_call = APRSISClient::replaceCallsignSuffixWithSSID(cmd.from,               Radio::base_callsign(cmd.from));

    // we use a queued signal here so we can process these spots in a network thread
    // to prevent blocking the gui/decoder while waiting on TCP
    emit aprsClientEnqueueThirdParty(by_call, from_call, cmd.text);
}

void MainWindow::spotAprsGrid(int dial, int offset, int snr, QString callsign, QString grid){
    if(!m_config.spot_to_reporting_networks()) return;
    if(!m_config.spot_to_aprs()) return;
    if(m_config.spot_blacklist().contains(callsign) || m_config.spot_blacklist().contains(Radio::base_callsign(callsign))) return;
    if(grid.length() < 4) return;

    Frequency frequency = dial + offset;

    auto comment = QString("%1MHz %2dB").arg(Radio::frequency_MHz_string(frequency)).arg(Varicode::formatSNR(snr));
    if(callsign.contains("/")){
        comment = QString("%1 %2").arg(callsign).arg(comment);
    }

    auto by_call = APRSISClient::replaceCallsignSuffixWithSSID(m_config.my_callsign(), Radio::base_callsign(m_config.my_callsign()));
    auto from_call = APRSISClient::replaceCallsignSuffixWithSSID(callsign, Radio::base_callsign(callsign));

    // we use a queued signal here so we can process these spots in a network thread
    // to prevent blocking the gui/decoder while waiting on TCP
    emit aprsClientEnqueueSpot(by_call, from_call, grid, comment);
}

void
MainWindow::pskLogReport(QString const & mode,
                         int     const   dial,
                         int     const   offset,
                         int     const   snr,
                         QString const & callsign,
                         QString const & grid)
{
  if (!m_config.spot_to_reporting_networks() ||
      (m_config.spot_blacklist().contains(callsign) ||
       m_config.spot_blacklist().contains(Radio::base_callsign(callsign)))) return;

  Q_EMIT pskReporterAddRemoteStation (callsign,
                                      grid,
                                      dial + offset,
                                      mode,
                                      snr);
}

//------------------------------------------------------------- //guiUpdate()
void MainWindow::guiUpdate()
{
  static char   message[29];
  static char   msgsent[29];
  static int    msgibits;
  QElapsedTimer timer;

  timer.start();

  if(m_TRperiod==0) m_TRperiod=60;

  double tx1 = 0.0;
  double tx2 = JS8::Submode::txDuration(m_nSubMode);

  if(tx2>m_TRperiod) tx2=m_TRperiod;

  qint64 ms = DriftingDateTime::currentMSecsSinceEpoch() % 86400000;
  int nsec=ms/1000;
  double tsec=0.001*ms;
  double t2p=fmod(tsec, m_TRperiod);

  // how long is the tx?
  m_bTxTime = (t2p >= tx1) and (t2p < tx2);

  if(m_tune) m_bTxTime=true;                 // "Tune" and tones take precedence

  if(m_transmitting or m_auto or m_tune) {
    m_dateTimeLastTX = DriftingDateTime::currentDateTime ();

// Don't transmit another mode in the 30 m WSPR sub-band
    Frequency onAirFreq = m_freqNominal + freq();

    //qDebug() << "transmitting on" << onAirFreq;

    if ((onAirFreq > 10139900 &&
         onAirFreq < 10140320))
    {
      m_bTxTime = false;
      if (m_auto) auto_tx_mode (false);
      if (onAirFreq != m_onAirFreq0)
      {
        m_onAirFreq0 = onAirFreq;
        QTimer::singleShot (0, [this]
        {
          MessageBox::warning_message(this,
                                      tr("WSPR Guard Band"),
                                      tr("Please choose another Tx frequency."
                                         " The app will not knowingly transmit another"
                                         " mode in the WSPR sub-band on 30m."));
        });
      }
    }

    auto const msgLength = QStringView(m_nextFreeTextMsg).trimmed().length();
    auto const fTR       = float((ms % (1000 * m_TRperiod))) /
                                       (1000 * m_TRperiod);

    // TODO: stop
    if (msgLength == 0 && !m_tune) on_stopTxButton_clicked();

    // 15.0 - 12.6
    double const ratio = JS8::Submode::computeRatio(m_nSubMode, m_TRperiod);

    if(fTR > 1.0-ratio && fTR < 1.0){
        if(!m_deadAirTone){
            qDebug() << "should start dead air tone";
            m_deadAirTone = true;
        }
    } else {
        if(m_deadAirTone){
            qDebug() << "should stop dead air tone";
            m_deadAirTone = false;
        }
    }

    // the late threshold is the dead air time minus the tx delay time
    float lateThreshold = ratio - (m_config.txDelay() / m_TRperiod);
    if(m_nSubMode == Varicode::JS8CallFast){
        // for the faster mode, only allow 3/4 late threshold
        lateThreshold *= 0.75;
    }
    else if(m_nSubMode == Varicode::JS8CallTurbo){
        // for the turbo mode, only allow 1/2 late threshold
        lateThreshold *= 0.5;
    }
    else if(m_nSubMode == Varicode::JS8CallUltra){
        // for the ultra mode, only allow 1/2 late threshold
        lateThreshold *= 0.5;
    }
    if(m_iptt == 0 && ((m_bTxTime && fTR < lateThreshold && msgLength > 0) || m_tune))
    {
      //### Allow late starts
      m_iptt = 1;
      setRig ();
      setXIT (freq());
      emitPTT(true);
      m_tx_when_ready = true;

      qDebug() << "start threshold" << fTR << lateThreshold << ms;
    }

    // TODO: stop
    if(!m_bTxTime and !m_tune) m_btxok=false;       //Time to stop transmitting
  }

  // Calculate Tx tones when needed
  if((m_iptt == 1 && m_iptt0 == 0) || m_restart) {
//----------------------------------------------------------------------

    copyMessage(m_nextFreeTextMsg, message);

    if (m_lastMessageSent != m_currentMessage
        || m_lastMessageType != m_currentMessageType)
      {
        m_lastMessageSent = m_currentMessage;
        m_lastMessageType = m_currentMessageType;
      }

    m_currentMessageType = 0;

    if(m_tune) {
      itone[0]=0;
    } else {
      int icos = JS8::Submode::costas(m_nSubMode);

      char ft8msgbits[75 + 12]; //packed 75 bit ft8 message plus 12-bit CRC

      genjs8_(message, &icos, &m_i3bit, msgsent, const_cast<char *> (ft8msgbits),
              const_cast<int *> (itone), 22, 22);

      qDebug() << "-> msg:" << message;
      qDebug() << "-> bit:" << m_i3bit;
      for(int i = 0; i < 7; i++){
        qDebug() << "-> tone" << i << "=" << itone[i];
      }
      for(int i = JS8_NUM_SYMBOLS-7; i < JS8_NUM_SYMBOLS; i++){
        qDebug() << "-> tone" << i << "=" << itone[i];
      }

      msgibits = m_i3bit;
      msgsent[22]=0;

      m_currentMessage = QString::fromLatin1(msgsent).trimmed();
      m_currentMessageBits = msgibits;

      emitTones();
    }

    if (m_tune) {
      m_currentMessage = "TUNE";
      m_currentMessageType = -1;
    }
    if(m_restart) {
      write_transmit_entry ("ALL.TXT");
    }

    auto t2 = DriftingDateTime::currentDateTimeUtc ().toString ("hhmm");
    auto msg_parts = m_currentMessage.split (' ', Qt::SkipEmptyParts);
    if (msg_parts.size () > 2) {
      // clean up short code forms
      msg_parts[0].remove (QChar {'<'});
      msg_parts[1].remove (QChar {'>'});
    }

    if ((m_currentMessageType < 6 || 7 == m_currentMessageType)
        && msg_parts.length() >= 3
        && (msg_parts[1] == m_config.my_callsign () ||
            msg_parts[1] == m_baseCall))
    {
      int i1;
      bool ok;
      i1 = msg_parts[2].toInt(&ok);
      if(ok and i1>=-50 and i1<50)
      {
        m_rptSent = msg_parts[2];
      } else {
        if (msg_parts[2].mid (0, 1) == "R")
        {
          i1 = msg_parts[2].mid (1).toInt (&ok);
          if (ok and i1 >= -50 and i1 < 50)
          {
            m_rptSent = msg_parts[2].mid (1);
          }
        }
      }
    }
    m_restart=false;
//----------------------------------------------------------------------
  }

  if (m_iptt == 1 && m_iptt0 == 0)
    {
      auto const& current_message = QString::fromLatin1 (msgsent);
      if(m_config.watchdog () && current_message != m_msgSent0) {
        // new messages don't reset the idle timer :|
        // tx_watchdog (false);  // in case we are auto sequencing
        m_msgSent0 = current_message;
      }

      if(!m_tune) {
        write_transmit_entry ("ALL.TXT");
      }

      // TODO: jsherer - perhaps an on_transmitting signal?
      m_lastTxStartTime = DriftingDateTime::currentDateTimeUtc();

      m_transmitting = true;
      transmitDisplay (true);
      statusUpdate ();
    }

  // TODO: stop
  if(!m_btxok && m_btxok0 && m_iptt == 1) stopTx();

  //Once per second:
  if(nsec != m_sec0) {

    if(m_monitoring or m_transmitting) {
        progressBar.setMaximum(m_TRperiod);
        int isec=int(fmod(tsec,m_TRperiod));
        progressBar.setValue(isec);
    } else {
        progressBar.setValue(0);
    }

    if(m_transmitting) {
      tx_status_label.setStyleSheet("QLabel{background-color: #ff2222; color:#000}");
      if(m_tune) {
        tx_status_label.setText("Tx: TUNE");
      } else {
        auto message = DecodedText(msgsent, msgibits, m_nSubMode).message();
        tx_status_label.setText(QString("Tx: %1").arg(message).left(40).trimmed());
      }
      transmitDisplay(true);

    } else if(m_monitoring) {
      if (m_tx_watchdog) {
        tx_status_label.setStyleSheet ("QLabel{background-color: #000; color:#fff}");
        tx_status_label.setText ("Idle timeout");
      } else {
        tx_status_label.setStyleSheet("QLabel{background-color: #22ff22}");
        tx_status_label.setText (m_decoderBusy ? "Decoding" : "Receiving");
      }
      transmitDisplay(false);
    } else if (!m_tx_watchdog) {
      tx_status_label.setStyleSheet("");
      tx_status_label.setText("");
    }

    auto drift = DriftingDateTime::drift();
    QDateTime t = DriftingDateTime::currentDateTimeUtc();
    QStringList parts;
    parts << (t.time().toString() + (!drift ? " " : QString(" (%1%2ms)").arg(drift > 0 ? "+" : "").arg(drift)));
    parts << t.date().toString("yyyy MMM dd");
    ui->labUTC->setText(parts.join("\n"));

#if 0
    auto delta = t.secsTo(m_nextHeartbeat);
    QString ping;
    if(heartbeatTimer.isActive()){
        if(delta > 0){
            ping = QString("%1 s").arg(delta);
        } else {
            ping = "queued!";
        }
    } else if (m_nextHeartPaused) {
        ping = "paused";
    } else {
        ping = "on demand";
    }
    ui->labHeartbeat->setText(QString("Next Heartbeat: %1").arg(ping));
#endif

    auto callLabel = m_config.my_callsign();
    if(m_config.use_dynamic_grid() && !m_config.my_grid().isEmpty()){
        callLabel = QString("%1 - %2").arg(callLabel).arg(m_config.my_grid());
    }
    ui->labCallsign->setText(callLabel);

    if(!m_monitoring) {
      ui->signal_meter_widget->setValue(0,0);
    }

    m_sec0=nsec;

    // once per period
    if(m_sec0 % m_TRperiod == 0){
        tryBandHop();
        decodeCheckHangingDecoder();
    }

    // at the end of the period
    bool forceDirty = false;
    if(m_sec0 % (m_TRperiod-2) == 0 ||
       m_sec0 % (m_TRperiod) == 0   ||
       m_sec0 % (m_TRperiod+2) == 0){
        // force rx dirty at the end of the period
        forceDirty = true;
    }
    if(!forceDirty){
        forceDirty = !m_rxActivityQueue.isEmpty();
    }

    // update the dial frequency once per second..
    displayDialFrequency();

    // update repeat button text once per second..
    updateRepeatButtonDisplay();

    // once per second...but not when we're transmitting, unless it's in the first second...
    if(!m_transmitting || (m_sec0 % (m_TRperiod) == 0)){
        // process all received activity...
        processActivity(forceDirty);

        // process outgoing tx queue...
        processTxQueue();

        // once processed, lets update the display...
        displayActivity(forceDirty);
        updateButtonDisplay();
        updateTextDisplay();
    }
  }

  // once per 100ms
  displayTransmit();

  m_iptt0  = m_iptt;
  m_btxok0 = m_btxok;

  // Compute the processing time and adjust loop to hit the next 100ms

  m_guiTimer.start(std::max(std::chrono::milliseconds(100 - timer.elapsed()),
                            std::chrono::milliseconds::zero()));
}               //End of guiUpdate


void MainWindow::startTx()
{
#if IDLE_BLOCKS_TX
  if(m_tx_watchdog){
      return;
  }
#endif

  auto text = ui->extFreeTextMsgEdit->toPlainText();
  if(!ensureCreateMessageReady(text)){
    return;
  }

  if(!prepareNextMessageFrame()){
    return;
  }

  m_dateTimeQSOOn = QDateTime{};
  if (m_transmitting) m_restart=true;

  if (!m_auto) auto_tx_mode(true);

  // disallow editing of the text while transmitting
  // ui->extFreeTextMsgEdit->setReadOnly(true);
  update_dynamic_property(ui->extFreeTextMsgEdit, "transmitting", true);

  // update the tx button display
  updateTxButtonDisplay();
}

void MainWindow::startTx2()
{
  if (m_modulator->isIdle())
  {
    transmit();
    ui->signal_meter_widget->setValue(0, 0);
  }
}

void MainWindow::stopTx()
{
  Q_EMIT endTransmitMessage ();

  auto dt = DecodedText(m_currentMessage.trimmed(), m_currentMessageBits, m_nSubMode);
  last_tx_label.setText("Last Tx: " + dt.message()); //m_currentMessage.trimmed());

  // TODO: uncomment if we want to mark after the frame is sent.
  //// // start message marker
  //// // - keep track of the total message sent so far, and mark it having been sent
  //// m_totalTxMessage.append(dt.message());
  //// ui->extFreeTextMsgEdit->setCharsSent(m_totalTxMessage.length());
  //// qDebug() << "total sent:\n" << m_totalTxMessage;
  //// // end message marker

  m_btxok          = false;
  m_transmitting   = false;
  m_iptt           = 0;
  m_lastTxStopTime = DriftingDateTime::currentDateTimeUtc();
  if (!m_tx_watchdog) {
    tx_status_label.setStyleSheet("");
    tx_status_label.setText("");
  }

#if IDLE_BLOCKS_TX
  bool shouldContinue = !m_tx_watchdog && prepareNextMessageFrame();
#else
  bool shouldContinue = prepareNextMessageFrame();
#endif
  if(!shouldContinue){
      // TODO: jsherer - split this up...
      ui->extFreeTextMsgEdit->clear();
      ui->extFreeTextMsgEdit->setReadOnly(false);
      update_dynamic_property(ui->extFreeTextMsgEdit, "transmitting", false);
      on_stopTxButton_clicked();
      tryRestoreFreqOffset();
  }

  ptt0Timer.start(200);                       //end-of-transmission sequencer delay stopTx2
  monitor (true);
  statusUpdate ();
}

/**
 *  stopTx2 is called from stopTx to open the PTT
 */
void MainWindow::stopTx2(){
    // GM8JCF: m_txFrameCount is set to the number of frames to be transmitted when the send button is pressed
    // and remains at that count until the last frame is transmitted.
    // So, we keep the PTT ON so long as m_txFrameCount is non-zero

    qDebug() << "stopTx2 frames left" << m_txFrameCount;

    // If we're holding the PTT and there are more frames to transmit, do not emit the PTT signal
    if(!m_tune && m_config.hold_ptt() && m_txFrameCount > 0){
        return;
    }

    // Otherwise, emit the PTT signal
    emitPTT(false);
}

void MainWindow::TxAgain()
{
  auto_tx_mode(true);
}

void MainWindow::cacheActivity(QString key){
    m_callActivityBandCache[key] = m_callActivity;
    m_bandActivityBandCache[key] = m_bandActivity;
    m_rxTextBandCache[key] = ui->textEditRX->toHtml();
    m_heardGraphIncomingBandCache[key] = m_heardGraphIncoming;
    m_heardGraphOutgoingBandCache[key] = m_heardGraphOutgoing;
}

void MainWindow::restoreActivity(QString key){
    if(m_callActivityBandCache.contains(key)){
        m_callActivity = m_callActivityBandCache[key];
    }

    if(m_bandActivityBandCache.contains(key)){
        m_bandActivity = m_bandActivityBandCache[key];
    }

    if(m_rxTextBandCache.contains(key)){
        ui->textEditRX->setHtml(m_rxTextBandCache[key]);
    }

    if(m_heardGraphIncomingBandCache.contains(key)){
        m_heardGraphIncoming = m_heardGraphIncomingBandCache[key];
    }

    if(m_heardGraphOutgoingBandCache.contains(key)){
        m_heardGraphOutgoing = m_heardGraphOutgoingBandCache[key];
    }

    displayActivity(true);
}

void MainWindow::clearActivity(){
    qDebug() << "clear activity";

    m_callSeenHeartbeat.clear();
    m_compoundCallCache.clear();
    m_rxCallCache.clear();
    m_rxCallQueue.clear();
    m_rxRecentCache.clear();
    m_rxDirectedCache.clear();
    m_rxCommandQueue.clear();
    m_lastTxMessage.clear();

    refreshInboxCounts();
    resetTimeDeltaAverage();

    clearBandActivity();
    clearRXActivity();
    clearCallActivity();

    displayActivity(true);
}

void MainWindow::clearBandActivity(){
    qDebug() << "clear band activity";
    m_bandActivity.clear();
    ui->tableWidgetRXAll->setRowCount(0);

    resetTimeDeltaAverage();
    displayBandActivity();
}

void MainWindow::clearRXActivity(){
    qDebug() << "clear rx activity";

    m_rxFrameBlockNumbers.clear();
    m_rxActivityQueue.clear();

    ui->textEditRX->clear();

    // make sure to clear the read only and transmitting flags so there's always a "way out"
    ui->extFreeTextMsgEdit->clear();
    ui->extFreeTextMsgEdit->setReadOnly(false);
    update_dynamic_property(ui->extFreeTextMsgEdit, "transmitting", false);
}

void MainWindow::clearCallActivity(){
    qDebug() << "clear call activity";

    m_callActivity.clear();

    m_heardGraphIncoming.clear();
    m_heardGraphOutgoing.clear();

    ui->tableWidgetCalls->setRowCount(0);
    createGroupCallsignTableRows(ui->tableWidgetCalls, "");

    resetTimeDeltaAverage();
    displayCallActivity();
}

void MainWindow::createGroupCallsignTableRows(QTableWidget *table, QString const &selectedCall){
    int count = 0;
    auto now = DriftingDateTime::currentDateTimeUtc();
    int callsignAging = m_config.callsign_aging();

    int startCol = 1;

    foreach(auto cd, m_callActivity.values()){
        if (cd.call.trimmed().isEmpty()){
            continue;
        }
        if (callsignAging && cd.utcTimestamp.secsTo(now) / 60 >= callsignAging) {
            continue;
        }
        count++;
    }

    table->horizontalHeaderItem(startCol)->setText(count == 0 ? "Callsigns" : QString("Callsigns (%1)").arg(count));

    if(!m_config.avoid_allcall()){
        table->insertRow(table->rowCount());

        auto emptyItem = new QTableWidgetItem("");
        emptyItem->setData(Qt::UserRole, QVariant("@ALLCALL"));
        table->setItem(table->rowCount() - 1, 0, emptyItem);

        auto item = new QTableWidgetItem(QString("@ALLCALL"));
        item->setData(Qt::UserRole, QVariant("@ALLCALL"));

        table->setItem(table->rowCount() - 1, startCol, item);
        table->setSpan(table->rowCount() - 1, startCol, 1, table->columnCount());
        if(selectedCall == "@ALLCALL"){
            table->item(table->rowCount()-1, 0)->setSelected(true);
            table->item(table->rowCount()-1, startCol)->setSelected(true);
        }
    }

    auto groups = m_config.my_groups().values();
    std::sort(groups.begin(), groups.end());
    foreach(auto group, groups){
        table->insertRow(table->rowCount());

        auto emptyItem = new QTableWidgetItem("");
        emptyItem->setData(Qt::UserRole, QVariant(group));
        emptyItem->setToolTip(generateCallDetail(group));
        table->setItem(table->rowCount() - 1, 0, emptyItem);

        auto item = new QTableWidgetItem(group);
        item->setData(Qt::UserRole, QVariant(group));
        item->setToolTip(generateCallDetail(group));
        table->setItem(table->rowCount() - 1, startCol, item);
        table->setSpan(table->rowCount() - 1, startCol, 1, table->columnCount());

        if(selectedCall == group){
            table->item(table->rowCount()-1, 0)->setSelected(true);
            table->item(table->rowCount()-1, startCol)->setSelected(true);
        }
    }
}

void MainWindow::displayTextForFreq(QString text, int freq, QDateTime date, bool isTx, bool isNewLine, bool isLast){
    int lowFreq = freq/10*10;
    int highFreq = lowFreq + 10;

    int block = -1;

    if(m_rxFrameBlockNumbers.contains(freq)){
        block = m_rxFrameBlockNumbers[freq];
    } else if(m_rxFrameBlockNumbers.contains(lowFreq)){
        block = m_rxFrameBlockNumbers[lowFreq];
        freq = lowFreq;
    } else if(m_rxFrameBlockNumbers.contains(highFreq)){
        block = m_rxFrameBlockNumbers[highFreq];
        freq = highFreq;
    }

    qDebug() << "existing block?" << block << freq;

    if(isNewLine){
        m_rxFrameBlockNumbers.remove(freq);
        m_rxFrameBlockNumbers.remove(lowFreq);
        m_rxFrameBlockNumbers.remove(highFreq);
        block = -1;
    }

    block = writeMessageTextToUI(date, text, freq, isTx, block);

    // never cache tx or last lines
    if(/*isTx || */isLast) {
        // reset the cache so we're always progressing forward
        m_rxFrameBlockNumbers.clear();
    } else {
        m_rxFrameBlockNumbers.insert(freq, block);
        m_rxFrameBlockNumbers.insert(lowFreq, block);
        m_rxFrameBlockNumbers.insert(highFreq, block);
    }
}

void MainWindow::writeNoticeTextToUI(QDateTime date, QString text){
    auto c = ui->textEditRX->textCursor();
    c.movePosition(QTextCursor::End);
    if(c.block().length() > 1){
        c.insertBlock();
    }

    text = text.toHtmlEscaped();
    c.insertBlock();
    c.insertHtml(QString("<strong>%1 - %2</strong>").arg(date.time().toString()).arg(text));

    c.movePosition(QTextCursor::End);

    ui->textEditRX->ensureCursorVisible();
    ui->textEditRX->verticalScrollBar()->setValue(ui->textEditRX->verticalScrollBar()->maximum());
}

int MainWindow::writeMessageTextToUI(QDateTime date, QString text, int freq, bool isTx, int block){
    auto c = ui->textEditRX->textCursor();

    // find an existing block (that does not contain an EOT marker)
    bool found = false;
    if(block != -1){
        QTextBlock b = c.document()->findBlockByNumber(block);
        c.setPosition(b.position());
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);

        auto blockText = c.selectedText();
        c.clearSelection();
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);

        if(!blockText.contains(m_config.eot())){
            found = true;
        }
    }

    if(!found){
        c.movePosition(QTextCursor::End);
        if(c.block().length() > 1){
            c.insertBlock();
        }
    }

    // fixup duplicate acks
    auto tc = c.document()->find(text);
    if(!tc.isNull() && tc.selectedText() == text && (text.contains(" ACK ") || text.contains(" HEARTBEAT SNR "))){
        tc.select(QTextCursor::BlockUnderCursor);

        if(tc.selectedText().trimmed().startsWith(date.time().toString())){
            qDebug() << "found" << tc.selectedText() << "so not displaying...";
            return tc.blockNumber();
        }
    }

    if(found){
        c.clearSelection();
        c.insertText(text);
    } else {
        text = text.toHtmlEscaped();
        text = text.replace("\n", "<br/>");
        text = text.replace("  ", "&nbsp;&nbsp;");
        c.insertBlock();
        c.insertHtml(QString("%1 - (%2) - %3").arg(date.time().toString()).arg(freq).arg(text));
    }

    if(isTx){
        c.block().setUserState(State::TX);
        highlightBlock(c.block(), m_config.tx_text_font(), m_config.color_tx_foreground(), QColor(Qt::transparent));
    } else {
        c.block().setUserState(State::RX);
        highlightBlock(c.block(), m_config.rx_text_font(), m_config.color_rx_foreground(), QColor(Qt::transparent));
    }

    ui->textEditRX->ensureCursorVisible();
    ui->textEditRX->verticalScrollBar()->setValue(ui->textEditRX->verticalScrollBar()->maximum());

    return c.blockNumber();
}

bool MainWindow::isMessageQueuedForTransmit(){
    return m_transmitting || m_txFrameCount > 0;
}

bool MainWindow::isInDecodeDelayThreshold(int ms){
    if(!m_lastTxStopTime.isValid() || m_lastTxStopTime.isNull()){
        return false;
    }

    return m_lastTxStopTime.msecsTo(DriftingDateTime::currentDateTimeUtc()) < ms;
}

void MainWindow::prependMessageText(QString text){
    // don't add message text if we already have a transmission queued...
    if(isMessageQueuedForTransmit()){
        return;
    }

    auto c = QTextCursor(ui->extFreeTextMsgEdit->textCursor());
    c.movePosition(QTextCursor::Start);
    c.insertText(text);
}

void MainWindow::addMessageText(QString text, bool clear, bool selectFirstPlaceholder){
    // don't add message text if we already have a transmission queued...
    if(isMessageQueuedForTransmit()){
        return;
    }

    if(clear){
        ui->extFreeTextMsgEdit->clear();
    }

    QTextCursor c = ui->extFreeTextMsgEdit->textCursor();
    if(c.hasSelection()){
        c.removeSelectedText();
    }

    int pos = c.position();
    c.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);

    bool isSpace = c.selectedText().isEmpty() || c.selectedText().at(0).isSpace();
    c.clearSelection();

    c.setPosition(pos);

    if(!isSpace){
        c.insertText(" ");
    }

    c.insertText(text);

    if(selectFirstPlaceholder){
        auto match = QRegularExpression("(\\[[^\\]]+\\])").match(ui->extFreeTextMsgEdit->toPlainText());
        if(match.hasMatch()){
            c.setPosition(match.capturedStart());
            c.setPosition(match.capturedEnd(), QTextCursor::KeepAnchor);
            ui->extFreeTextMsgEdit->setTextCursor(c);
        }
    }

    ui->extFreeTextMsgEdit->setFocus();
}

void MainWindow::confirmThenEnqueueMessage(int timeout, int priority, QString message, int offset, Callback c){
    SelfDestructMessageBox * m = new SelfDestructMessageBox(timeout,
      "Autoreply Confirmation Required",
      QString("A transmission is queued for autoreply:\n\n%1\n\nWould you like to send this transmission?").arg(message),
      QMessageBox::Question,
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::No,
      false,
      this);

    connect(m, &SelfDestructMessageBox::finished, this, [this, m, priority, message, offset, c](int){
        // make sure we delete the message box later...
        m->deleteLater();

        if(m->result() == QMessageBox::Yes){
            enqueueMessage(priority, message, offset, c);
        }
    });

    m->setWindowModality(Qt::NonModal);
    m->show();
}

void MainWindow::enqueueMessage(int priority, QString message, int offset, Callback c){
    m_txMessageQueue.enqueue(
        PrioritizedMessage{
            DriftingDateTime::currentDateTimeUtc(), priority, message, offset, c
        }
    );
}

void MainWindow::resetMessage(){
    resetMessageUI();
    resetMessageTransmitQueue();
}

void MainWindow::resetMessageUI(){
    m_nextFreeTextMsg.clear();
    ui->extFreeTextMsgEdit->clear();
    ui->extFreeTextMsgEdit->setReadOnly(false);

    update_dynamic_property (ui->extFreeTextMsgEdit, "transmitting", false);

    if(ui->startTxButton->isChecked()){
        ui->startTxButton->setChecked(false);
    }
}

bool MainWindow::ensureCallsignSet(bool alert){
    if(m_config.my_callsign().trimmed().isEmpty()){
        if(alert) MessageBox::warning_message(this, tr ("Please enter your callsign in the settings."));
        openSettings();
        return false;
    }

    if(m_config.my_grid().trimmed().isEmpty()){
        if(alert) MessageBox::warning_message(this, tr ("Please enter your grid locator in the settings."));
        openSettings();
        return false;
    }

    return true;
}

bool MainWindow::ensureKeyNotStuck(QString const& text){
    // be annoying and drop messages with all the same character to reduce spam...
    if(text.length() > 5 && QString(text).replace(text.at(0), "").trimmed().isEmpty()){
        return false;
    }

    return true;
}

bool MainWindow::ensureNotIdle(){
    if (!m_config.watchdog()){
        return true;
    }

    if(m_idleMinutes < m_config.watchdog ()){
        return true;
    }

    tx_watchdog (true);       // disable transmit and auto replies
    return false;
}

bool MainWindow::ensureCanTransmit(){
    return ui->monitorTxButton->isChecked();
}

bool MainWindow::ensureCreateMessageReady(const QString &text){
    if(text.isEmpty()){
        return false;
    }

    if(!ensureCanTransmit()){
        on_stopTxButton_clicked();
        return false;
    }

    if(!ensureCallsignSet()){
        on_stopTxButton_clicked();
        return false;
    }

    if(!ensureNotIdle()){
        on_stopTxButton_clicked();
        return false;
    }

    if(!ensureKeyNotStuck(text)){
        on_stopTxButton_clicked();

        ui->monitorButton->setChecked(false);
        ui->monitorTxButton->setChecked(false);
        on_monitorButton_clicked(false);
        on_monitorTxButton_toggled(false);

        foreach(auto obj, this->children()){
            if(obj->isWidgetType()){
                auto wid = qobject_cast<QWidget*>(obj);
                wid->setEnabled(false);
            }
        }

        return false;
    }

    return true;
}

QString MainWindow::createMessage(QString const& text, bool *pDisableTypeahead){
    return createMessageTransmitQueue(replaceMacros(text, buildMacroValues(), false), true, false, pDisableTypeahead);
}

QString MainWindow::appendMessage(QString const& text, bool isData, bool *pDisableTypeahead){
    return createMessageTransmitQueue(replaceMacros(text, buildMacroValues(), false), false, isData, pDisableTypeahead);
}

QString MainWindow::createMessageTransmitQueue(QString const& text, bool reset, bool isData, bool *pDisableTypeahead){
  if(reset){
      resetMessageTransmitQueue();
  }

  auto frames = buildMessageFrames(text, isData, pDisableTypeahead);

  QStringList lines;
  foreach(auto frame, frames){
      auto dt = DecodedText(frame.first, frame.second, m_nSubMode);
      lines.append(dt.message());
  }

  m_txFrameQueue.append(frames);
  m_txFrameCount += frames.length();

  // TODO: jsherer - move this outside of create message transmit queue
  // if we're transmitting a message to be displayed, we should bump the repeat buttons...
#if JS8HB_RESET_HB_TIMER_ON_TX
  resetAutomaticIntervalTransmissions(false, false);
#else
  resetCQTimer(false);
#endif

  // return the text
  return lines.join("");
}

void MainWindow::restoreMessage(){
    if(m_lastTxMessage.isEmpty()){
        return;
    }
    addMessageText(Varicode::rstrip(m_lastTxMessage), true);
}

void MainWindow::resetMessageTransmitQueue(){
  m_txFrameCount = 0;
  m_txFrameCountSent = 0;
  m_txFrameQueue.clear();
  m_txMessageQueue.clear();

  // reset the total message sent
  m_totalTxMessage.clear();
}

QPair<QString, int> MainWindow::popMessageFrame(){
  if(m_txFrameQueue.isEmpty()){
      return QPair<QString, int>{};
  }
  return m_txFrameQueue.dequeue();
}

void
MainWindow::currentTextChanged()
{
  auto const text = ui->extFreeTextMsgEdit->toPlainText();

  // keep track of dirty flags
  m_txTextDirty = text != m_txTextDirtyLastText;
  m_txTextDirtyLastText = text;

  // immediately update the display
  updateButtonDisplay();
  updateTextDisplay();
}

void
MainWindow::tableSelectionChanged(QItemSelection const &,
                                  QItemSelection const &)
{
  currentTextChanged();

  auto const selectedCall = callsignSelected();

  if(selectedCall != m_prevSelectedCallsign)
  {
    callsignSelectedChanged(m_prevSelectedCallsign, selectedCall);
  }
}

QList<QPair<QString, int>> MainWindow::buildMessageFrames(const QString &text, bool isData, bool *pDisableTypeahead){
    // prepare selected callsign for directed message
    QString selectedCall = callsignSelected();

    // prepare compound
    QString mycall = m_config.my_callsign();
    QString mygrid = m_config.my_grid().left(4);

    bool forceIdentify = !m_config.avoid_forced_identify();

    // TODO: might want to be more explicit?
    bool forceData = m_txFrameCountSent > 0 && isData;

    Varicode::MessageInfo info;
    auto frames = Varicode::buildMessageFrames(
        mycall,
        mygrid,
        selectedCall,
        text,
        forceIdentify,
        forceData,
        m_nSubMode,
        &info);

    if(pDisableTypeahead){
        // checksummed commands should not allow typeahead
        *pDisableTypeahead = (!info.dirCmd.isEmpty() && Varicode::isCommandChecksumed(info.dirCmd));
    }

#if 0
    qDebug() << "frames:";
    foreach(auto frame, frames){
        auto dt = DecodedText(frame.frame, frame.bits);
        qDebug() << "->" << frame << dt.message() << Varicode::frameTypeString(dt.frameType());
    }
#endif

    return frames;
}

bool MainWindow::prepareNextMessageFrame()
{
  // check to see if the last i3bit was a last bit
  bool i3bitLast = (m_i3bit & Varicode::JS8CallLast) == Varicode::JS8CallLast;

  // TODO: should this be user configurable?
  bool shouldForceDataForTypeahead = !i3bitLast;

  // reset i3
  m_i3bit = Varicode::JS8Call;

  // typeahead
  bool shouldDisableTypeahead = false;
  if(ui->extFreeTextMsgEdit->isDirty() && !ui->extFreeTextMsgEdit->isEmpty()){
      // block edit events while computing next frame
      QString newText;
      ui->extFreeTextMsgEdit->setReadOnly(true);
      {
          auto sent = ui->extFreeTextMsgEdit->sentText();
          auto unsent = ui->extFreeTextMsgEdit->unsentText();
          qDebug() << "text dirty for typeahead\n" << sent << "\n" << unsent;
          m_txFrameQueue.clear();
          m_txFrameCount = 0;

          newText = appendMessage(unsent, shouldForceDataForTypeahead, &shouldDisableTypeahead);

          // if this was the last frame, append a newline
          if(i3bitLast){
              m_totalTxMessage.append("\n");
              newText.prepend("\n");
          }

          qDebug () << "unsent replaced to" << "\n" << newText;
      }
      ui->extFreeTextMsgEdit->setReadOnly(shouldDisableTypeahead);
      ui->extFreeTextMsgEdit->replaceUnsentText(newText, true);
      ui->extFreeTextMsgEdit->setClean();
  }

  QPair<QString, int> f = popMessageFrame();
  auto frame = f.first;
  auto bits = f.second;

  // if not the first frame, ensure first bit is not set
  if(m_txFrameCountSent > 0){
      bits &= ~Varicode::JS8CallFirst;
  }

  // if last frame, ensure the last bit is set
  if(m_txFrameQueue.isEmpty()){
      bits |= Varicode::JS8CallLast;
  }

  if(frame.isEmpty()){
    m_nextFreeTextMsg.clear();
    updateTxButtonDisplay();
    return false;
  }

  // append this frame to the total message sent so far
  auto dt = DecodedText(frame, bits, m_nSubMode);
  m_totalTxMessage.append(dt.message());
  ui->extFreeTextMsgEdit->setCharsSent(m_totalTxMessage.length());
  m_txFrameCountSent += 1;
  m_lastTxMessage = m_totalTxMessage;
  qDebug() << "total sent:" << m_txFrameCountSent << "\n" << m_totalTxMessage;

  // display the frame...
  if (m_txFrameQueue.isEmpty())
  {
    displayTextForFreq(QString("%1 %2 ").arg(dt.message()).arg(m_config.eot()),
                       freq(),
                       DriftingDateTime::currentDateTimeUtc(),
                       true,
                       false,
                       true);
  }
  else
  {
    displayTextForFreq(dt.message(),
                       freq(),
                       DriftingDateTime::currentDateTimeUtc(),
                       true,
                       m_txFrameCountSent == 1,
                       false);
  }

  m_nextFreeTextMsg = frame;
  m_i3bit           = bits;

  updateTxButtonDisplay();

  // TODO: bump heartbeat

  return true;
}

bool
MainWindow::isFreqOffsetFree(int const f,
                             int const bw)
{
  // if this frequency is our current frequency, or it's in our
  // directed cache, it's free.

  if ((freq() == f) || isDirectedOffset(f, nullptr)) return true;

  // Run through the band activity; if there's no activity for a given
  // offset, or we last received on it more than 30 seconds ago, then
  // it's free. If it's an occupied slot within the bandwidth of where
  // we'd like to transmit, then it's not free.

  auto const now = DriftingDateTime::currentDateTimeUtc();

  for (auto [offset, activity] : m_bandActivity.asKeyValueRange())
  {
    if (activity.isEmpty() ||
        activity.last().utcTimestamp.secsTo(now) >= 30) continue;

    if (qAbs(offset - f) < bw) return false;
  }

  return true;
}

int MainWindow::findFreeFreqOffset(int fmin, int fmax, int bw){
    int nslots = (fmax-fmin)/bw;

    int f = fmin;
    for(int i = 0; i < nslots; i++){
        f = fmin + bw * (QRandomGenerator::global()->generate() % nslots);
        if(isFreqOffsetFree(f, bw)){
            return f;
        }
    }

    for(int i = 0; i < nslots; i++){
        f = fmin + (QRandomGenerator::global()->generate() % (fmax-fmin));
        if(isFreqOffsetFree(f, bw)){
            return f;
        }
    }

    // return fmin if there's no free offset
    return fmin;
}

#if 0
// schedulePing
void MainWindow::scheduleHeartbeat(bool first){
    auto timestamp = DriftingDateTime::currentDateTimeUtc();

    // if we have the heartbeat interval disabled, return early, unless this is a "heartbeat now"
    if(!m_config.heartbeat() && !first){
        heartbeatTimer.stop();
        return;
    }

    // remove milliseconds
    auto t = timestamp.time();
    t.setHMS(t.hour(), t.minute(), t.second());
    timestamp.setTime(t);

    // round to 15 second increment
    int secondsSinceEpoch = (timestamp.toMSecsSinceEpoch()/1000);
    int delta = roundUp(secondsSinceEpoch, 15) + 1 + (first ? 0 : qMax(1, m_config.heartbeat()) * 60) - secondsSinceEpoch;
    timestamp = timestamp.addSecs(delta);

    // 25% of the time, switch intervals
    float prob = (float) QRandomGenerator::global()->generate() / (RAND_MAX);
    if(prob < 0.25){
        timestamp = timestamp.addSecs(15);
    }

    m_nextHeartbeat = timestamp;
    m_nextHeartbeatQueued = false;
    m_nextHeartPaused = false;

    if(!heartbeatTimer.isActive()){
        heartbeatTimer.setInterval(1000);
        heartbeatTimer.start();
    }
}

// pausePing
void MainWindow::pauseHeartbeat(){
    m_nextHeartPaused = true;

    if(heartbeatTimer.isActive()){
        heartbeatTimer.stop();
    }
}

// unpausePing
void MainWindow::unpauseHeartbeat(){
    scheduleHeartbeat(false);
}

// checkPing
void MainWindow::checkHeartbeat(){
    if(m_config.heartbeat() <= 0){
        return;
    }
    auto secondsUntilHeartbeat = DriftingDateTime::currentDateTimeUtc().secsTo(m_nextHeartbeat);
    if(secondsUntilHeartbeat > 5 && m_txHeartbeatQueue.isEmpty()){
        return;
    }
    if(m_nextHeartbeatQueued){
        return;
    }
    if(m_tx_watchdog){
        return;
    }

    // idle heartbeat watchdog!
    if (m_config.watchdog() && m_idleMinutes >= m_config.watchdog ()){
      tx_watchdog (true);       // disable transmit
      return;
    }

    prepareHeartbeat();
}

// preparePing
void MainWindow::prepareHeartbeat(){
    QStringList lines;

    QString mycall = m_config.my_callsign();
    QString mygrid = m_config.my_grid().left(4);

    // JS8Call Style
    if(m_txHeartbeatQueue.isEmpty()){
        lines.append(QString("%1: HEARTBEAT %2").arg(mycall).arg(mygrid));
    } else {
        while(!m_txHeartbeatQueue.isEmpty() && lines.length() < 1){
            lines.append(m_txHeartbeatQueue.dequeue());
        }
    }

    // Choose a ping frequency
    auto f = m_config.heartbeat_anywhere() ? -1 : findFreeFreqOffset(500, 1000, 50);

    auto text = lines.join(QChar('\n'));
    if(text.isEmpty()){
        return;
    }

    // Queue the ping
    enqueueMessage(PriorityLow, text, f, [this](){
        m_nextHeartbeatQueued = false;
    });

    m_nextHeartbeatQueued = true;
}
#endif

void MainWindow::checkRepeat(){
    if(ui->hbMacroButton->isChecked() && m_hbInterval > 0 && m_nextHeartbeat.isValid()){
        if(DriftingDateTime::currentDateTimeUtc().secsTo(m_nextHeartbeat) <= 0){
            sendHeartbeat();
        }
    }

    if(ui->cqMacroButton->isChecked() && m_cqInterval > 0 && m_nextCQ.isValid()){
        if(DriftingDateTime::currentDateTimeUtc().secsTo(m_nextCQ) <= 0){
            sendCQ(true);
        }
    }
}

void MainWindow::on_startTxButton_toggled(bool checked)
{
    if(checked){
        startTx();
    } else {
        resetMessage();
        on_stopTxButton_clicked();
        stopTx();
    }
}

void MainWindow::toggleTx(bool start){
    if(start && ui->startTxButton->isChecked()) { return; }
    if(!start && !ui->startTxButton->isChecked()) { return; }
    ui->startTxButton->setChecked(start);
}

void MainWindow::on_logQSOButton_clicked()                 //Log QSO button
{
  QString call = callsignSelected();
  if(m_callSelectedTime.contains(call)){
    m_dateTimeQSOOn = m_callSelectedTime[call];
  }
  if (!m_dateTimeQSOOn.isValid ()) {
    m_dateTimeQSOOn = DriftingDateTime::currentDateTimeUtc();
  }
  auto dateTimeQSOOff = DriftingDateTime::currentDateTimeUtc();
  if (dateTimeQSOOff < m_dateTimeQSOOn) dateTimeQSOOff = m_dateTimeQSOOn;

  if(call.startsWith("@")){
      call = "";
  }
  QString grid="";
  if(m_callActivity.contains(call)){
      grid = m_callActivity[call].grid;
  }
  QString opCall=m_opCall;
  if(opCall.isEmpty()){
      opCall = m_config.my_callsign();
  }

  QString comments = ui->textEditRX->textCursor().selectedText();

  // don't reset the log window if the call hasn't changed.
  if(!m_logDlg->currentCall().isEmpty() && call.trimmed() == m_logDlg->currentCall()){
      m_logDlg->show();
      return;
  }

  m_logDlg->initLogQSO (call.trimmed(), grid.trimmed(), "JS8", m_rptSent, m_rptRcvd,
                        m_dateTimeQSOOn, dateTimeQSOOff, m_freqNominal + freq(),
                        m_config.my_callsign(), m_config.my_grid(),
                        opCall, comments);
}

void MainWindow::acceptQSO (QDateTime const& QSO_date_off, QString const& call, QString const& grid
                            , Frequency dial_freq, QString const& mode, QString const &submode
                            , QString const& rpt_sent, QString const& rpt_received
                            , QString const& comments
                            , QString const& name, QDateTime const& QSO_date_on, QString const& operator_call
                            , QString const& my_call, QString const& my_grid, QByteArray const& ADIF, QVariantMap const &additionalFields)
{
  QString date = QSO_date_on.toString("yyyyMMdd");
  m_logBook.addAsWorked (m_hisCall, m_config.bands ()->find (m_freqNominal), mode, submode, grid, date, name, comments);

  // Log to JS8Call API
  if(canSendNetworkMessage()){
      sendNetworkMessage("LOG.QSO", QString(ADIF), {
          {"_ID", QVariant(-1)},
          {"UTC.ON", QVariant(QSO_date_on.toMSecsSinceEpoch())},
          {"UTC.OFF", QVariant(QSO_date_off.toMSecsSinceEpoch())},
          {"CALL", QVariant(call)},
          {"GRID", QVariant(grid)},
          {"FREQ", QVariant(dial_freq)},
          {"MODE", QVariant(mode)},
          {"SUBMODE", QVariant(submode)},
          {"RPT.SENT", QVariant(rpt_sent)},
          {"RPT.RECV", QVariant(rpt_received)},
          {"NAME", QVariant(name)},
          {"COMMENTS", QVariant(comments)},
          {"STATION.OP", QVariant(operator_call)},
          {"STATION.CALL", QVariant(my_call)},
          {"STATION.GRID", QVariant(my_grid)},
          {"EXTRA", additionalFields}
      });
  }

  // Log to N1MM Logger
  if (m_config.broadcast_to_n1mm() && m_config.valid_n1mm_info())  {
    const QHostAddress n1mmhost = QHostAddress(m_config.n1mm_server_name());
    QUdpSocket _sock;
    auto rzult = _sock.writeDatagram (ADIF + " <eor>", n1mmhost, quint16(m_config.n1mm_server_port()));
    if (rzult == -1) {
      bool hidden = m_logDlg->isHidden();
      m_logDlg->setHidden(true);
      MessageBox::warning_message (this, tr ("Error sending log to N1MM"),
                                   tr ("Write returned \"%1\"").arg (rzult));
      m_logDlg->setHidden(hidden);
    }
  }

  // Log to N3FJP Logger
  if(m_config.broadcast_to_n3fjp() && m_config.valid_n3fjp_info()){
      QString data = QString(
        "<CMD>"
        "<ADDDIRECT>"
        "<EXCLUDEDUPES>TRUE</EXCLUDEDUPES>"
        "<STAYOPEN>FALSE</STAYOPEN>"
        "<fldDateStr>%1</fldDateStr>"
        "<fldTimeOnStr>%2</fldTimeOnStr>"
        "<fldCall>%3</fldCall>"
        "<fldGridR>%4</fldGridR>"
        "<fldBand>%5</fldBand>"
        "<fldFrequency>%6</fldFrequency>"
        "<fldMode>JS8</fldMode>"
        "<fldOperator>%7</fldOperator>"
        "<fldNameR>%8</fldNameR>"
        "<fldComments>%9</fldComments>"
        "<fldRstS>%10</fldRstS>"
        "<fldRstR>%11</fldRstR>"
        "%12"
        "</CMD>");

      data = data.arg(QSO_date_on.toString("yyyy/MM/dd"));
      data = data.arg(QSO_date_on.toString("H:mm"));
      data = data.arg(call);
      data = data.arg(grid);
      data = data.arg(m_config.bands ()->find(dial_freq).replace("m", ""));
      data = data.arg(Radio::frequency_MHz_string(dial_freq));
      data = data.arg(operator_call);
      data = data.arg(name);
      data = data.arg(comments);
      data = data.arg(rpt_sent);
      data = data.arg(rpt_received);

      int other = 0;
      QStringList additional;
      if(!additionalFields.isEmpty()){
          foreach(auto key, additionalFields.keys()){
              QString n3key;
              if(N3FJP_ADIF_MAP.contains(key)){
                  n3key = N3FJP_ADIF_MAP.value(key);
              } else {
                  other++;
                  n3key = N3FJP_ADIF_MAP.value(QString("*%1").arg(other));
              }

              if(n3key.isEmpty()){
                  break;
              }
              auto value = additionalFields[key].toString();
              additional.append(QString("<%1>%2</%1>").arg(n3key).arg(value));
          }
      }
      data = data.arg(additional.join(""));

      auto host = m_config.n3fjp_server_name();
      auto port = m_config.n3fjp_server_port();

      if(m_n3fjpClient->sendNetworkMessage(host, port, data.toLocal8Bit(), true, 500)){
          QTimer::singleShot(300, this, [this, host, port](){
            m_n3fjpClient->sendNetworkMessage(host, port, "<CMD><CHECKLOG></CMD>", true, 100);
            m_n3fjpClient->sendNetworkMessage(host, port, "\r\n", true, 100);
          });
      } else {
          bool hidden = m_logDlg->isHidden();
          m_logDlg->setHidden(true);
          MessageBox::warning_message (this, tr ("Error sending log to N3FJP"),
                                       tr ("Write failed for \"%1:%2\"").arg (host).arg(port));
          m_logDlg->setHidden(hidden);
      }
  }

  // reload the logbook data
  m_logBook.init();

  clearCallsignSelected();

  displayCallActivity();

  m_dateTimeQSOOn = QDateTime {};
}

void MainWindow::on_actionModeJS8HB_toggled(bool){
    // prep hb mode

    prepareHeartbeatMode(canCurrentModeSendHeartbeat() && ui->actionModeJS8HB->isChecked());
    displayActivity(true);

    setupJS8();
}

void MainWindow::on_actionHeartbeatAcknowledgements_toggled(bool){
    // prep hb ack mode

    prepareHeartbeatMode(canCurrentModeSendHeartbeat() && ui->actionModeJS8HB->isChecked());
    displayActivity(true);

    setupJS8();
}

void MainWindow::on_actionModeMultiDecoder_toggled(bool checked){
    Q_UNUSED(checked);

    displayActivity(true);

    setupJS8();
}

void MainWindow::on_actionModeJS8Normal_triggered(){
    setupJS8();
}

void MainWindow::on_actionModeJS8Fast_triggered(){
    setupJS8();
}

void MainWindow::on_actionModeJS8Turbo_triggered(){
    setupJS8();
}

void MainWindow::on_actionModeJS8Slow_triggered(){
    setupJS8();
}

void MainWindow::on_actionModeJS8Ultra_triggered(){
    setupJS8();
}

void MainWindow::on_actionModeAutoreply_toggled(bool){
    // update the HB ack option (needs autoreply on)
    prepareHeartbeatMode(canCurrentModeSendHeartbeat() && ui->actionModeJS8HB->isChecked());

    // then update the js8 mode
    setupJS8();
}

bool
MainWindow::canCurrentModeSendHeartbeat() const
{
  return (m_nSubMode == Varicode::JS8CallFast   ||
          m_nSubMode == Varicode::JS8CallNormal ||
          m_nSubMode == Varicode::JS8CallSlow);
}

void MainWindow::prepareMonitorControls(){
    // on_monitorButton_toggled(!m_config.monitor_off_at_startup());
    ui->monitorTxButton->setChecked(!m_config.transmit_off_at_startup());
}

void MainWindow::prepareHeartbeatMode(bool enabled){
    // heartbeat is only available in a supported HB mode
    ui->hbMacroButton->setVisible(enabled);
    if(!enabled){
        ui->hbMacroButton->setChecked(false);
    }
    ui->actionHeartbeat->setEnabled(enabled);
    ui->actionModeJS8HB->setEnabled(canCurrentModeSendHeartbeat());
    ui->actionHeartbeatAcknowledgements->setEnabled(enabled && ui->actionModeAutoreply->isChecked());

#if 0
    if(enabled){
        m_config.addGroup("@HB");
    } else {
        m_config.removeGroup("@HB");
    }
#endif

#if 0
    //ui->actionCQ->setEnabled(!enabled);
    //ui->actionFocus_Message_Reply_Area->setEnabled(!enabled);

    // default to not displaying the other buttons
    // ui->cqMacroButton->setVisible(!enabled);
    // ui->replyMacroButton->setVisible(!enabled);
    // ui->snrMacroButton->setVisible(!enabled);
    // ui->infoMacroButton->setVisible(!enabled);
    // ui->macrosMacroButton->setVisible(!enabled);
    // ui->queryButton->setVisible(!enabled);
    // ui->extFreeTextMsgEdit->setVisible(!enabled);
    // if(enabled){
    //     ui->extFreeTextMsgEdit->clear();
    // }

    // show heartbeat and acks in hb mode only
    // ui->actionShow_Band_Heartbeats_and_ACKs->setChecked(enabled);
    // ui->actionShow_Band_Heartbeats_and_ACKs->setVisible(true);
    // ui->actionShow_Band_Heartbeats_and_ACKs->setEnabled(false);
#endif

    // update the HB button immediately
    updateRepeatButtonDisplay();
    updateButtonDisplay();
}

void
MainWindow::setupJS8()
{
  m_nSubMode = Varicode::JS8CallNormal;

  if      (ui->actionModeJS8Normal->isChecked()) m_nSubMode = Varicode::JS8CallNormal;
  else if (ui->actionModeJS8Fast->isChecked())   m_nSubMode = Varicode::JS8CallFast;
  else if (ui->actionModeJS8Turbo->isChecked())  m_nSubMode = Varicode::JS8CallTurbo;
  else if (ui->actionModeJS8Slow->isChecked())   m_nSubMode = Varicode::JS8CallSlow;
  else if (ui->actionModeJS8Ultra->isChecked())  m_nSubMode = Varicode::JS8CallUltra;

  // Only enable heartbeat for modes that support it
  prepareHeartbeatMode(canCurrentModeSendHeartbeat() && ui->actionModeJS8HB->isChecked());

  updateModeButtonText();

  m_wideGraph->setSubMode(m_nSubMode);
  m_wideGraph->setFilterMinimumBandwidth(JS8::Submode::bandwidth(m_nSubMode) +
                                         JS8::Submode::rxThreshold(m_nSubMode) * 2);

  enable_DXCC_entity (m_config.DXCC ());
  m_config.frequencies ()->filter (m_config.region (), Mode::JS8);
  m_FFTSize = NSPS / 2;
  Q_EMIT FFTSize (m_FFTSize);
  setup_status_bar ();
  m_TRperiod = JS8::Submode::period(m_nSubMode);
  m_wideGraph->show();

  Q_ASSERT(NTMAX == 60);
  m_wideGraph->setPeriod(m_TRperiod);
  m_detector->setTRPeriod(NTMAX); // TODO - not thread safe

  updateTextDisplay();
  refreshTextDisplay();
  statusChanged();
}

void
MainWindow::setFreq(int const n)
{
  m_freq = n;
  m_wideGraph->setFreq(n);
  Q_EMIT transmitFrequency (n - m_XIT);
  statusUpdate ();
}

void MainWindow::on_actionQuickDecode_toggled (bool checked)
{
  m_ndepth ^= (-checked ^ m_ndepth) & 0x00000001;
}

void MainWindow::on_actionMediumDecode_toggled (bool checked)
{
  m_ndepth ^= (-checked ^ m_ndepth) & 0x00000002;
}

void MainWindow::on_actionDeepDecode_toggled (bool checked)
{
  m_ndepth ^= (-checked ^ m_ndepth) & 0x00000003;
}

void MainWindow::on_actionDeepestDecode_toggled (bool checked)
{
  m_ndepth ^= (-checked ^ m_ndepth) & 0x00000004;
}

void MainWindow::on_actionErase_ALL_TXT_triggered()          //Erase ALL.TXT
{
  int ret = MessageBox::query_message (this, tr ("Confirm Erase"),
                                         tr ("Are you sure you want to erase file ALL.TXT?"));
  if(ret==MessageBox::Yes) {
    QFile f {m_config.writeable_data_dir ().absoluteFilePath ("ALL.TXT")};
    f.remove();
    m_RxLog=1;
  }
}

void MainWindow::on_actionErase_js8call_log_adi_triggered()
{
  int ret = MessageBox::query_message (this, tr ("Confirm Erase"),
                                       tr ("Are you sure you want to erase file js8call_log.adi?"));
  if(ret==MessageBox::Yes) {
    QFile f {m_config.writeable_data_dir ().absoluteFilePath ("js8call_log.adi")};
    f.remove();

    m_logBook.init();
  }
}

void MainWindow::on_actionOpen_log_directory_triggered ()
{
  QDesktopServices::openUrl (QUrl::fromLocalFile (m_config.writeable_data_dir ().absolutePath ()));
}

void MainWindow::band_changed ()
{
  if (m_config.pwrBandTxMemory() && !m_tune) {
    if (m_pwrBandTxMemory.contains(m_lastBand)) {
      ui->outAttenuation->setValue(m_pwrBandTxMemory[m_lastBand].toInt());
    }
    else {
      m_pwrBandTxMemory[m_lastBand] = ui->outAttenuation->value();
    }
  }
}

void MainWindow::enable_DXCC_entity (bool /*on*/)
{
  m_logBook.init();                        // re-read the log and cty.dat files
  updateGeometry ();
}

void MainWindow::buildFrequencyMenu(QMenu *menu){
    auto custom = menu->addAction("Set a Custom Frequency...");

    connect(custom, &QAction::triggered, this, [this](){
        bool ok = false;
        auto currentFreq = Radio::frequency_MHz_string(dialFrequency());
        QString newFreq = QInputDialog::getText(this, tr("Set a Custom Frequency"),
                                                 tr("Frequency in MHz:"), QLineEdit::Normal,
                                                 currentFreq, &ok).toUpper().trimmed();
        if(!ok){
           return;
        }

        setRig(Radio::frequency(newFreq, 6));
    });

    menu->addSeparator();

    auto frequencies = m_config.frequencies()->frequency_list();
    std::sort(frequencies.begin(), frequencies.end(), [](FrequencyList_v2::Item &a, FrequencyList_v2::Item &b) {
        return a.frequency_ < b.frequency_;
    });

    foreach(auto f, frequencies){
        auto freq = Radio::pretty_frequency_MHz_string(f.frequency_);
        auto const& band = m_config.bands ()->find (f.frequency_);

        auto a = menu->addAction(QString("%1:%2%2%3 MHz").arg(band).arg(QString(" ").repeated(5-band.length())).arg(freq));
        connect(a, &QAction::triggered, this, [this, f](){
            setRig(f.frequency_);
        });
    }
}

void MainWindow::buildHeartbeatMenu(QMenu *menu){
    if(m_hbInterval > 0){
        auto startStop = menu->addAction(ui->hbMacroButton->isChecked() ? "Stop Heartbeat Timer" : "Start Heartbeat Timer");
        connect(startStop, &QAction::triggered, this, [this](){ ui->hbMacroButton->toggle(); });
        menu->addSeparator();
    }

    buildRepeatMenu(menu, ui->hbMacroButton, false, &m_hbInterval);

    menu->addSeparator();
    auto now = menu->addAction("Send Heartbeat Now");
    connect(now, &QAction::triggered, this, &MainWindow::sendHeartbeat);
}

void MainWindow::buildCQMenu(QMenu *menu){
    if(m_cqInterval > 0){
        auto startStop = menu->addAction(ui->cqMacroButton->isChecked() ? "Stop CQ Timer" : "Start CQ Timer");
        connect(startStop, &QAction::triggered, this, [this](){ ui->cqMacroButton->toggle(); });
        menu->addSeparator();
    }

    buildRepeatMenu(menu, ui->cqMacroButton, true, &m_cqInterval);

    menu->addSeparator();
    auto now = menu->addAction("Send CQ Now");
    connect(now, &QAction::triggered, this, [this](){ sendCQ(true); });
}

void MainWindow::buildRepeatMenu(QMenu *menu, QPushButton * button, bool isLowInterval, int * interval){
    QList<QPair<QString, int>> items = {
        {"On demand / do not repeat",  0},
        {"Repeat every 1 minute",      1},
        {"Repeat every 5 minutes",     5},
        {"Repeat every 10 minutes",   10},
        {"Repeat every 15 minutes",   15},
        {"Repeat every 30 minutes",   30},
        {"Repeat every 60 minutes",   60},
        {"Repeat every N minutes (Custom Interval)", -1}, // this needs to be last because of isSet bool
    };

    if(isLowInterval){
        items.removeAt(5); // remove the thirty minute interval
        items.removeAt(5); // remove the sixty minute interval
    } else {
        items.removeAt(1); // remove the one minute interval
        items.removeAt(1); // remove the five minute interval
    }

    auto customFormat = QString("Repeat every %1 minutes (Custom Interval)");

    QActionGroup * group = new QActionGroup(menu);

    bool isSet = false;
    foreach(auto pair, items){
        int minutes = pair.second;
        bool isMatch = *interval == minutes;
        bool isCustom = (minutes == -1 && isSet == false);
        if(isMatch){
            isSet = true;
        }

        auto text = pair.first;
        if(isCustom){
            text = QString(customFormat).arg(*interval);
        }

        auto action = menu->addAction(text);
        action->setData(minutes);
        action->setCheckable(true);
        action->setChecked(isMatch || isCustom);
        group->addAction(action);

        connect(action, &QAction::toggled, this, [this, action, customFormat, minutes, interval, button](bool checked){
            int min = minutes;

            if(checked){

                if(minutes == -1){
                    bool ok = false;
                    min = QInputDialog::getInt(this, "Repeat every N minutes", "Minutes", 0, 1, 1440, 1, &ok);
                    if(!ok){
                        return;
                    }
                    action->setText(QString(customFormat).arg(*interval));
                }

                *interval = min;

                if(min > 0){
                    // force a re-toggle
                    button->setChecked(false);
                }
                button->setChecked(min > 0);
            }
        });
    }
}

void MainWindow::sendHeartbeat(){
    QString mycall = m_config.my_callsign();
    QString mygrid = m_config.my_grid().left(4);

    QStringList parts;
    parts.append(QString("%1:").arg(mycall));

#if JS8_CUSTOMIZE_HB
    auto hb = m_config.hb_message();
#else
    auto hb = QString{};
#endif
    if(hb.isEmpty()){
        parts.append("HEARTBEAT");
        parts.append(mygrid);
    } else {
        parts.append(hb);
    }

    QString message = parts.join(" ").trimmed();

    auto f = findFreeFreqOffset(500, 1000, 50);

    if(freq() <= 1000){
        f = freq();
    }
    else if(m_config.heartbeat_anywhere()){
        f = -1;
    }

    enqueueMessage(PriorityLow + 1, message, f, nullptr);
    processTxQueue();
}

void MainWindow::sendHeartbeatAck(QString to, int snr, QString extra){
#if JS8_HB_ACK_SNR_CONFIGURABLE
    auto message = m_config.heartbeat_ack_snr() ?
        QString("%1 SNR %2 %3").arg(to).arg(Varicode::formatSNR(snr)).arg(extra).trimmed() :
        QString("%1 ACK %2").arg(to).arg(extra).trimmed();
#else
    auto message = QString("%1 HEARTBEAT SNR %2 %3").arg(to).arg(Varicode::formatSNR(snr)).arg(extra).trimmed();
#endif

    auto f = m_config.heartbeat_anywhere() ? -1 : findFreeFreqOffset(500, 1000, 50);

    if(m_config.autoreply_confirmation()){
        confirmThenEnqueueMessage(90, PriorityLow + 1, message, f, [this](){
            processTxQueue();
        });
    } else {
        enqueueMessage(PriorityLow + 1, message, f, nullptr);
        processTxQueue();
    }
}

void MainWindow::on_hbMacroButton_toggled(bool checked){
    if(checked){
        // only clear callsign if we do not allow hbs while in qso
        if(m_config.heartbeat_qso_pause()){
            clearCallsignSelected();
        }

        if(m_hbInterval){
            m_nextHeartbeat = nextTransmitCycle().addSecs(m_hbInterval * 60);

            if(!repeatTimer.isActive()){
                repeatTimer.start();
            }

        } else {
            sendHeartbeat();

            // make this button emulate a single press button
            ui->hbMacroButton->setChecked(false);
        }
    } else {
        m_nextHeartbeat = QDateTime{};
    }

    updateRepeatButtonDisplay();
}

void MainWindow::on_hbMacroButton_clicked(){
}

void MainWindow::sendCQ(bool repeat){
    auto message = m_config.cq_message();
    if(message.isEmpty()){
        QString mygrid = m_config.my_grid().left(4);
        message = QString("CQ CQ CQ %1").arg(mygrid).trimmed();
    }

    clearCallsignSelected();

    addMessageText(replaceMacros(message, buildMacroValues(), true));

    if(repeat || m_config.transmit_directed()) toggleTx(true);
}

void MainWindow::on_cqMacroButton_toggled(bool checked){
    if(checked){
        clearCallsignSelected();

        if(m_cqInterval){
            m_nextCQ = nextTransmitCycle().addSecs(m_cqInterval * 60);

            if(!repeatTimer.isActive()){
                repeatTimer.start();
            }

        } else {
            sendCQ();

            // make this button emulate a single press button
            ui->cqMacroButton->setChecked(false);
        }
    } else {
        m_nextCQ= QDateTime{};
    }

    updateRepeatButtonDisplay();
}

void MainWindow::on_cqMacroButton_clicked(){
}

void MainWindow::on_replyMacroButton_clicked(){
    QString call = callsignSelected();
    if(call.isEmpty()){
        return;
    }

    auto message = m_config.reply_message();
    message = replaceMacros(message, buildMacroValues(), true);
    addMessageText(QString("%1 %2").arg(call).arg(message));

    if(m_config.transmit_directed()) toggleTx(true);
}

void MainWindow::on_snrMacroButton_clicked(){
    QString call = callsignSelected();
    if(call.isEmpty()){
        return;
    }

    auto now = DriftingDateTime::currentDateTimeUtc();
    int callsignAging = m_config.callsign_aging();
    if(!m_callActivity.contains(call)){
        return;
    }

    auto cd = m_callActivity[call];
    if (callsignAging && cd.utcTimestamp.secsTo(now) / 60 >= callsignAging) {
        return;
    }

    auto snr = Varicode::formatSNR(cd.snr);

    addMessageText(QString("%1 SNR %2").arg(call).arg(snr));

    if(m_config.transmit_directed()) toggleTx(true);
}

void MainWindow::on_infoMacroButton_clicked(){
    QString info = m_config.my_info();
    if(info.isEmpty()){
        return;
    }

    addMessageText(QString("INFO %1").arg(replaceMacros(info, buildMacroValues(), true)));

    if(m_config.transmit_directed()) toggleTx(true);
}

void MainWindow::on_statusMacroButton_clicked(){
    QString status = m_config.my_status();
    if(status.isEmpty()){
        return;
    }

    addMessageText(QString("STATUS %1").arg(replaceMacros(status, buildMacroValues(), true)));

    if(m_config.transmit_directed()) toggleTx(true);
}

void MainWindow::setShowColumn(QString tableKey, QString columnKey, bool value){
    m_showColumnsCache[tableKey + columnKey] = QVariant(value);
    displayBandActivity();
    displayCallActivity();
}

bool MainWindow::showColumn(QString tableKey, QString columnKey, bool default_){
    return m_showColumnsCache.value(tableKey + columnKey, QVariant(default_)).toBool();
}

void MainWindow::buildShowColumnsMenu(QMenu *menu, QString tableKey){
    QList<QPair<QString, QString>> columnKeys = {
        {"Frequency Offset", "offset"},
        {"Last heard timestamp", "timestamp"},
        {"SNR", "snr"},
        {"Time Delta", "tdrift"},
        {"Mode Speed", "submode"},
    };

    QMap<QString, bool> defaultOverride = {
        {"submode", false},
        {"tdrift", false},
        {"grid", false},
        {"distance", false}
    };

    if(tableKey == "call"){
        columnKeys.prepend({"Callsign", "callsign"});
        columnKeys.append({
          {"Grid Locator", "grid"},
          {"Distance", "distance"},
          {"Worked Before", "log"},
          {"Logged Name", "logName"},
          {"Logged Comment", "logComment"},
        });
    }

    columnKeys.prepend({"Show Column Labels", "labels"});

    bool first = true;
    foreach(auto p, columnKeys){
        auto columnLabel = p.first;
        auto columnKey = p.second;

        auto a = menu->addAction(columnLabel);
        a->setCheckable(true);


        bool showByDefault = true;
        if(defaultOverride.contains(columnKey)){
            showByDefault = defaultOverride[columnKey];
        }
        a->setChecked(showColumn(tableKey, columnKey, showByDefault));

        connect(a, &QAction::triggered, this, [this, a, tableKey, columnKey](){
            setShowColumn(tableKey, columnKey, a->isChecked());
        });

        if(first){
            menu->addSeparator();
            first = false;
        }
    }
}

void MainWindow::setSortBy(QString key, QString value){
    m_sortCache[key] = QVariant(value);
    displayBandActivity();
    displayCallActivity();
}

QString
MainWindow::getSortBy(QString const & key,
                      QString const & defaultValue) const
{
  return m_sortCache.value(key, QVariant(defaultValue)).toString();
}

MainWindow::SortByReverse
MainWindow::getSortByReverse(QString const & key,
                             QString const & defaultValue) const
{
  auto const sortBy  = getSortBy(key, defaultValue);
  auto const reverse = sortBy.startsWith("-");

  return
  {
    reverse ? sortBy.sliced(1) : sortBy,
    reverse
  };
}

void MainWindow::buildSortByMenu(QMenu * menu, QString key, QString defaultValue, QList<QPair<QString, QString>> values){
    auto currentSortBy = getSortBy(key, defaultValue);

    QActionGroup * g = new QActionGroup(menu);
    g->setExclusive(true);

    foreach(auto p, values){
        auto k = p.first;
        auto v = p.second;
        auto a = menu->addAction(k);
        a->setCheckable(true);
        a->setChecked(v == currentSortBy);
        a->setActionGroup(g);

        connect(a, &QAction::triggered, this, [this, a, key, v](){
            if(a->isChecked()){
                setSortBy(key, v);
            }
        });
    }
}

void MainWindow::buildBandActivitySortByMenu(QMenu * menu){
    buildSortByMenu(menu, "bandActivity", "offset", {
        {"Frequency offset", "offset"},
        {"Last heard timestamp (oldest first)", "timestamp"},
        {"Last heard timestamp (recent first)", "-timestamp"},
        {"SNR (weakest first)", "snr"},
        {"SNR (strongest first)", "-snr"},
        {"Mode Speed (slowest first)", "submode"},
        {"Mode Speed (fastest first)", "-submode"}
    });
}

void MainWindow::buildCallActivitySortByMenu(QMenu * menu){
    buildSortByMenu(menu, "callActivity", "callsign", {
        {"Callsign", "callsign"},
        {"Callsigns Replied (recent first)", "ackTimestamp"},
        {"Frequency offset", "offset"},
        {"Distance (closest first)", "distance"},
        {"Distance (farthest first)", "-distance"},
        {"Last heard timestamp (oldest first)", "timestamp"},
        {"Last heard timestamp (recent first)", "-timestamp"},
        {"SNR (weakest first)", "snr"},
        {"SNR (strongest first)", "-snr"},
        {"Mode Speed (slowest first)", "submode"},
        {"Mode Speed (fastest first)", "-submode"}
    });
}

void MainWindow::buildQueryMenu(QMenu * menu, QString call){
    bool isAllCall = isAllCallIncluded(call);

    // for now, we're going to omit displaying the call...delete this if we want the other functionality
    call = "";

    auto grid = m_config.my_grid();

    bool emptyInfo = m_config.my_info().isEmpty();
    bool emptyGrid = m_config.my_grid().isEmpty();

    auto callAction = menu->addAction(QString("Send a directed message to selected callsign"));
    connect(callAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 ").arg(selectedCall), true);
    });

    menu->addSeparator();

    auto sendReplyAction = menu->addAction(QString("%1 Reply - Send reply message to selected callsign").arg(call).trimmed());
    connect(sendReplyAction, &QAction::triggered, this, [this](){
        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        auto message = m_config.reply_message();
        message = replaceMacros(message, buildMacroValues(), true);
        addMessageText(QString("%1 %2").arg(selectedCall).arg(message), true);
    });

    auto sendSNRAction = menu->addAction(QString("%1 SNR - Send a signal report to the selected callsign").arg(call).trimmed());
    sendSNRAction->setEnabled(m_callActivity.contains(callsignSelected()));
    connect(sendSNRAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        if(!m_callActivity.contains(selectedCall)){
            return;
        }

        auto d = m_callActivity[selectedCall];
        addMessageText(QString("%1 SNR %2").arg(selectedCall).arg(Varicode::formatSNR(d.snr)), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto infoAction = menu->addAction(QString("%1 INFO - Send my station information").arg(call).trimmed());
    infoAction->setDisabled(emptyInfo);
    connect(infoAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 INFO %2").arg(selectedCall).arg(m_config.my_info()), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });


    auto gridAction = menu->addAction(QString("%1 GRID %2 - Send my current station Maidenhead grid locator").arg(call).arg(grid).trimmed());
    gridAction->setDisabled(emptyGrid);
    connect(gridAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 GRID %2").arg(selectedCall).arg(m_config.my_grid()), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    menu->addSeparator();

    auto snrQueryAction = menu->addAction(QString("%1 SNR? - What is my signal report?").arg(call).trimmed());
    snrQueryAction->setDisabled(isAllCall);
    connect(snrQueryAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 SNR?").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto infoQueryAction = menu->addAction(QString("%1 INFO? - What is your station information?").arg(call).trimmed());
    infoQueryAction->setDisabled(isAllCall);
    connect(infoQueryAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 INFO?").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto gridQueryAction = menu->addAction(QString("%1 GRID? - What is your current grid locator?").arg(call).trimmed());
    gridQueryAction->setDisabled(isAllCall);
    connect(gridQueryAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 GRID?").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto stationIdleQueryAction = menu->addAction(QString("%1 STATUS? - What is your station status message?").arg(call).trimmed());
    stationIdleQueryAction->setDisabled(isAllCall);
    connect(stationIdleQueryAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 STATUS?").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto heardQueryAction = menu->addAction(QString("%1 HEARING? - What are the stations are you hearing? (Top 4 ranked by most recently heard)").arg(call).trimmed());
    heardQueryAction->setDisabled(isAllCall);
    connect(heardQueryAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 HEARING?").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

#if 0
    auto retransmitAction = menu->addAction(QString("%1|[MESSAGE] - Please ACK and retransmit the following message").arg(call).trimmed());
    retransmitAction->setDisabled(isAllCall);
    connect(retransmitAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1|[MESSAGE]").arg(selectedCall), true, true);
    });
#endif

    auto alertAction = menu->addAction(QString("%1>[MESSAGE] - Please relay this message to its destination").arg(call).trimmed());
    alertAction->setDisabled(isAllCall);
    connect(alertAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1>[MESSAGE]").arg(selectedCall), true, true);
    });

    auto msgAction = menu->addAction(QString("%1 MSG [MESSAGE] - Please store this message in your inbox").arg(call).trimmed());
    msgAction->setDisabled(isAllCall);
    connect(msgAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 MSG [MESSAGE]").arg(selectedCall), true, true);
    });

    auto msgToAction = menu->addAction(QString("%1 MSG TO:[CALLSIGN] [MESSAGE] - Please store this message at your station for later retreival by [CALLSIGN]").arg(call).trimmed());
    msgToAction->setDisabled(isAllCall);
    connect(msgToAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 MSG TO:[CALLSIGN] [MESSAGE]").arg(selectedCall), true, true);
    });

    auto qsoQueryAction = menu->addAction(QString("%1 QUERY CALL [CALLSIGN]? - Please acknowledge you can communicate directly with [CALLSIGN]").arg(call).trimmed());
    connect(qsoQueryAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 QUERY CALL [CALLSIGN]?").arg(selectedCall), true, true);
    });

    auto qsoQueryMsgsAction = menu->addAction(QString("%1 QUERY MSGS - Do you have any messages for me?").arg(call).trimmed());
    connect(qsoQueryMsgsAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 QUERY MSGS").arg(selectedCall), true, true);
    });

    auto qsoQueryMsgAction = menu->addAction(QString("%1 QUERY MSG [ID] - Please deliver the complete message identified by ID").arg(call).trimmed());
    connect(qsoQueryMsgAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 QUERY MSG [ID]").arg(selectedCall), true, true);
    });

    menu->addSeparator();

    auto agnAction = menu->addAction(QString("%1 AGN? - Please repeat your last transmission").arg(call).trimmed());
    connect(agnAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 AGN?").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto qslQueryAction = menu->addAction(QString("%1 QSL? - Did you receive my last transmission?").arg(call).trimmed());
    connect(qslQueryAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 QSL?").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto qslAction = menu->addAction(QString("%1 QSL - I confirm I received your last transmission").arg(call).trimmed());
    connect(qslAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 QSL").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto yesAction = menu->addAction(QString("%1 YES - I confirm your last inquiry").arg(call).trimmed());
    connect(yesAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 YES").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto noAction = menu->addAction(QString("%1 NO - I do not confirm your last inquiry").arg(call).trimmed());
    connect(noAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 NO").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto hwAction = menu->addAction(QString("%1 HW CPY? - How do you copy?").arg(call).trimmed());
    connect(hwAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 HW CPY?").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto rrAction = menu->addAction(QString("%1 RR - Roger. Received. I copy.").arg(call).trimmed());
    connect(rrAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 RR").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto fbAction = menu->addAction(QString("%1 FB - Fine Business").arg(call).trimmed());
    connect(fbAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 FB").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto sevenThreeAction = menu->addAction(QString("%1 73 - I send my best regards").arg(call).trimmed());
    connect(sevenThreeAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 73").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });

    auto skAction = menu->addAction(QString("%1 SK - End of contact").arg(call).trimmed());
    connect(skAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 SK").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });


    auto ditDitAction = menu->addAction(QString("%1 DIT DIT - End of contact / Two bits").arg(call).trimmed());
    connect(ditDitAction, &QAction::triggered, this, [this](){

        QString selectedCall = callsignSelected();
        if(selectedCall.isEmpty()){
            return;
        }

        addMessageText(QString("%1 DIT DIT").arg(selectedCall), true);

        if(m_config.transmit_directed()) toggleTx(true);
    });
}

void MainWindow::buildRelayMenu(QMenu *menu){
    auto now = DriftingDateTime::currentDateTimeUtc();
    int callsignAging = m_config.callsign_aging();
    foreach(auto cd, m_callActivity.values()){
        if (callsignAging && cd.utcTimestamp.secsTo(now) / 60 >= callsignAging) {
            continue;
        }

        menu->addAction(buildRelayAction(cd.call));
    }
}

QAction* MainWindow::buildRelayAction(QString call){
    QAction *a = new QAction(call, nullptr);
    connect(a, &QAction::triggered, this, [this, call](){
        prependMessageText(QString("%1>").arg(call));
    });
    return a;
}

void MainWindow::buildEditMenu(QMenu *menu, QTextEdit *edit){
    bool hasSelection = !edit->textCursor().selectedText().isEmpty();

    auto cut = menu->addAction("Cu&t");
    cut->setEnabled(hasSelection && !edit->isReadOnly());
    connect(edit, &QTextEdit::copyAvailable, this, [edit, cut](bool copyAvailable){
        cut->setEnabled(copyAvailable && !edit->isReadOnly());
    });
    connect(cut, &QAction::triggered, this, [edit](){
        edit->copy();
        edit->textCursor().removeSelectedText();
    });

    auto copy = menu->addAction("&Copy");
    copy->setEnabled(hasSelection);
    connect(edit, &QTextEdit::copyAvailable, this, [copy](bool copyAvailable){
        copy->setEnabled(copyAvailable);
    });
    connect(copy, &QAction::triggered, edit, &QTextEdit::copy);

    auto paste = menu->addAction("&Paste");
    paste->setEnabled(edit->canPaste());
    connect(paste, &QAction::triggered, edit, &QTextEdit::paste);
}

QMap<QString, QString> MainWindow::buildMacroValues(){
    auto lastActive = DriftingDateTime::currentDateTimeUtc().addSecs(-m_idleMinutes*60);
    QString myIdle = since(lastActive).toUpper().replace("NOW", "0M");
    QString myVersion = version().replace("-devel", "").replace("-rc", "");

    QMap<QString, QString> values = {
        {"<MYCALL>", m_config.my_callsign()},
        {"<MYGRID4>", m_config.my_grid().left(4)},
        {"<MYGRID12>", m_config.my_grid().left(12)},
        {"<MYINFO>", m_config.my_info()},
        {"<MYHB>", m_config.hb_message()},
        {"<MYCQ>", m_config.cq_message()},
        {"<MYREPLY>", m_config.reply_message()},
        {"<MYSTATUS>", m_config.my_status()},

        {"<MYVERSION>", myVersion},
        {"<MYIDLE>", myIdle},
    };

    auto selectedCall = callsignSelected();
    if(m_callActivity.contains(selectedCall)){
        auto cd = m_callActivity[selectedCall];

        values["<CALL>"] = selectedCall;
        values["<TDELTA>"] = QString("%1 ms").arg((int)(1000*cd.tdrift));

        if(cd.snr > -31){
            values["<SNR>"] = Varicode::formatSNR(cd.snr);
        }
    }

    // these macros can have recursive macros
    values["<MYINFO>"]   = replaceMacros(values["<MYINFO>"], values, false);
    values["<MYSTATUS>"]   = replaceMacros(values["<MYSTATUS>"], values, false);
    values["<MYCQ>"]    = replaceMacros(values["<MYCQ>"], values, false);
    values["<MYHB>"]    = replaceMacros(values["<MYHB>"], values, false);
    values["<MYREPLY>"] = replaceMacros(values["<MYREPLY>"], values, false);

    return values;
}

void MainWindow::buildSuggestionsMenu(QMenu *menu, QTextEdit *edit, const QPoint &point){
    if(!m_config.spellcheck()){
        return;
    }

    bool found = false;

    auto c = edit->cursorForPosition(point);
    if(c.charFormat().underlineStyle() != QTextCharFormat::WaveUnderline){
        return;
    }

    c.movePosition(QTextCursor::StartOfWord);
    c.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);

    auto word = c.selectedText().toUpper().trimmed();
    if(word.isEmpty()){
        return;
    }

    QStringList suggestions = JSCChecker::suggestions(word, 5, &found);
    if(suggestions.isEmpty() && !found){
        return;
    }

    if(suggestions.isEmpty()){
        auto a = menu->addAction("No Suggestions");
        a->setDisabled(true);
    } else {
        foreach(auto suggestion, suggestions){
            auto a = menu->addAction(suggestion);

            connect(a, &QAction::triggered, this, [edit, point, suggestion](){
                auto c = edit->cursorForPosition(point);
                c.select(QTextCursor::WordUnderCursor);
                c.insertText(suggestion);
            });
        }
    }

    menu->addSeparator();
}

void MainWindow::buildSavedMessagesMenu(QMenu *menu){
    auto values = buildMacroValues();

    foreach(QString macro, m_config.macros()->stringList()){
        QAction *action = menu->addAction(replaceMacros(macro, values, false));
        connect(action, &QAction::triggered, this, [this, macro](){
            auto values = buildMacroValues();
            addMessageText(replaceMacros(macro, values, true));

            if(m_config.transmit_directed()) toggleTx(true);
        });
    }

    menu->addSeparator();

    auto editAction = new QAction(QString("&Edit Saved Messages"), menu);
    menu->addAction(editAction);
    connect(editAction, &QAction::triggered, this, [this](){
        openSettings(5);
    });

    auto saveAction = new QAction(QString("&Save Current Message"), menu);
    saveAction->setDisabled(ui->extFreeTextMsgEdit->toPlainText().isEmpty());
    menu->addAction(saveAction);
    connect(saveAction, &QAction::triggered, this, [this](){
        auto macros = m_config.macros();
        if(macros->insertRow(macros->rowCount())){
            auto index = macros->index(macros->rowCount()-1);
            macros->setData(index, ui->extFreeTextMsgEdit->toPlainText());
            writeSettings();
        }
    });
}

void MainWindow::on_queryButton_pressed(){
    QMenu *menu = ui->queryButton->menu();
    if(!menu){
        menu = new QMenu(ui->queryButton);
    }
    menu->clear();

    buildQueryMenu(menu, callsignSelected());

    ui->queryButton->setMenu(menu);
    ui->queryButton->showMenu();
}

void MainWindow::on_macrosMacroButton_pressed(){
    QMenu *menu = ui->macrosMacroButton->menu();
    if(!menu){
        menu = new QMenu(ui->macrosMacroButton);
    }
    menu->clear();

    buildSavedMessagesMenu(menu);

    ui->macrosMacroButton->setMenu(menu);
    ui->macrosMacroButton->showMenu();
}

void MainWindow::on_deselectButton_pressed(){
    clearCallsignSelected();
}

void MainWindow::on_tableWidgetRXAll_cellClicked(int /*row*/, int /*col*/){
    ui->tableWidgetCalls->selectionModel()->select(
        ui->tableWidgetCalls->selectionModel()->selection(),
        QItemSelectionModel::Deselect);

    displayCallActivity();
}

void MainWindow::on_tableWidgetRXAll_cellDoubleClicked(int row, int col){
    on_tableWidgetRXAll_cellClicked(row, col);

    // TODO: jsherer - could also parse the messages for the last callsign?
    auto item = ui->tableWidgetRXAll->item(row, 0);
    int offset = item->text().replace(" Hz", "").toInt();

    // switch to the offset of this row
    setFreqOffsetForRestore(offset, false);

    // TODO: prompt mode switch?

    // print the history in the main window...
    int activityAging = m_config.activity_aging();
    QDateTime now = DriftingDateTime::currentDateTimeUtc();
    QDateTime firstActivity = now;
    QString activityText;
    bool isLast = false;
    foreach(auto d, m_bandActivity[offset]){
        if(activityAging && d.utcTimestamp.secsTo(now)/60 >= activityAging){
            continue;
        }
        if(activityText.isEmpty()){
            firstActivity = d.utcTimestamp;
        }
        activityText.append(d.text);

        isLast = (d.bits & Varicode::JS8CallLast) == Varicode::JS8CallLast;
        if(isLast){
            activityText = QString("%1 %2 ").arg(Varicode::rstrip(activityText)).arg(m_config.eot());
        }
    }
    if(!activityText.isEmpty()){
        displayTextForFreq(activityText, offset, firstActivity, false, true, isLast);
    }
}

QString MainWindow::generateCallDetail(QString selectedCall){
    if(selectedCall.isEmpty()){
        return "";
    }

    // heard detail
    QString hearing = m_heardGraphOutgoing.value(selectedCall).values().join(", ");
    QString heardby = m_heardGraphIncoming.value(selectedCall).values().join(", ");
    QStringList detail = {
        QString("<h1>%1</h1>").arg(selectedCall.toHtmlEscaped()),
        hearing.isEmpty() ? "" : QString("<p><strong>HEARING</strong>: %1</p>").arg(hearing.toHtmlEscaped()),
        heardby.isEmpty() ? "" : QString("<p><strong>HEARD BY</strong>: %1</p>").arg(heardby.toHtmlEscaped()),
    };

    return detail.join("\n");
}

void MainWindow::on_tableWidgetCalls_cellClicked(int /*row*/, int /*col*/){
    ui->tableWidgetRXAll->selectionModel()->select(
        ui->tableWidgetRXAll->selectionModel()->selection(),
        QItemSelectionModel::Deselect);

    displayBandActivity();
}

void MainWindow::on_tableWidgetCalls_cellDoubleClicked(int row, int col){
    on_tableWidgetCalls_cellClicked(row, col);

    auto call = callsignSelected();
    addMessageText(call);

#if SHOW_MESSAGE_HISTORY_ON_DOUBLECLICK
    if(m_rxInboxCountCache.value(call, 0) > 0){

        // TODO:
        // CommandDetail d = m_rxCallsignInboxCountCache[call].first();
        // m_rxCallsignInboxCountCache[call].removeFirst();
        //
        // processAlertReplyForCommand(d, d.relayPath, d.cmd);

        Inbox i(inboxPath());
        if(i.open()){
            QList<Message> msgs;
            foreach(auto pair, i.values("UNREAD", "$.params.FROM", call, 0, 1000)){
                msgs.append(pair.second);
            }

            auto mw = new MessageWindow(this);
            mw->populateMessages(msgs);
            mw->show();

            auto pair = i.firstUnreadFrom(call);
            auto id = pair.first;
            auto msg = pair.second;
            auto params = msg.params();

            CommandDetail d;
            d.cmd = params.value("CMD").toString();
            d.extra = params.value("EXTRA").toString();
            d.freq = params.value("OFFSET").toInt();
            d.from = params.value("FROM").toString();
            d.grid = params.value("GRID").toString();
            d.relayPath = params.value("PATH").toString();
            d.snr = params.value("SNR").toInt();
            d.tdrift = params.value("TDRIFT").toFloat();
            d.text = params.value("TEXT").toString();
            d.to = params.value("TO").toString();
            d.utcTimestamp = QDateTime::fromString(params.value("UTC").toString(), "yyyy-MM-dd hh:mm:ss");
            d.utcTimestamp.setUtcOffset(0);

            msg.setType("READ");
            i.set(id, msg);

            m_rxInboxCountCache[call] = max(0, m_rxInboxCountCache.value(call) - 1);

            processAlertReplyForCommand(d, d.relayPath, d.cmd);
        }

    } else {
        addMessageText(call);
    }
#endif
}

void MainWindow::on_tuneButton_clicked (bool checked)
{
  static bool lastChecked = false;
  if (lastChecked == checked) return;
  lastChecked = checked;
  if (checked && m_tune==false) { // we're starting tuning so remember Tx and change pwr to Tune value
    if (m_config.pwrBandTuneMemory ()) {
      m_pwrBandTxMemory[m_lastBand] = ui->outAttenuation->value(); // remember our Tx pwr
      m_PwrBandSetOK = false;
      if (m_pwrBandTuneMemory.contains(m_lastBand)) {
        ui->outAttenuation->setValue(m_pwrBandTuneMemory[m_lastBand].toInt()); // set to Tune pwr
      }
      m_PwrBandSetOK = true;
    }
  }
  if (m_tune) {
    tuneButtonTimer.start(250);
  } else {
    itone[0]=0;
    on_monitorButton_clicked (true);
    m_tune=true;
  }
  Q_EMIT tune (checked);
}

void MainWindow::end_tuning ()
{
  tuneATU_Timer.stop ();        // stop tune watchdog when stopping Tune manually
  on_stopTxButton_clicked ();
  // we're turning off so remember our Tune pwr setting and reset to Tx pwr
  if (m_config.pwrBandTuneMemory() || m_config.pwrBandTxMemory()) {
    m_pwrBandTuneMemory[m_lastBand] = ui->outAttenuation->value(); // remember our Tune pwr
    m_PwrBandSetOK = false;
    ui->outAttenuation->setValue(m_pwrBandTxMemory[m_lastBand].toInt()); // set to Tx pwr
    m_PwrBandSetOK = true;
  }
}

void MainWindow::stop_tuning ()
{
  tuneATU_Timer.stop ();        // stop tune watchdog when stopping Tune manually
  on_tuneButton_clicked(false);
  ui->tuneButton->setChecked (false);
  m_bTxTime=false;
  m_tune=false;
}

void MainWindow::stopTuneATU()
{
  on_tuneButton_clicked(false);
  m_bTxTime=false;
}

void MainWindow::resetPushButtonToggleText(QPushButton *btn){
    bool checked = btn->isChecked();
    auto style = btn->styleSheet();
    if(checked){
        style = style.replace("font-weight:normal;", "font-weight:bold;");
    } else {
        style = style.replace("font-weight:bold;", "font-weight:normal;");
    }
    btn->setStyleSheet(style);

#if PUSH_BUTTON_CHECKMARK
    auto on = "✓ ";
    auto text = btn->text();
    if(checked){
        btn->setText(on + text.replace(on, ""));
    } else {
        btn->setText(text.replace(on, ""));
    }
#endif

#if PUSH_BUTTON_MIN_WIDTH
    int width = 0;
    QList<QPushButton*> btns;
    foreach(auto child, ui->buttonGrid->children()){
        if(!child->isWidgetType()){
            continue;
        }

        if(!child->objectName().contains("Button")){
            continue;
        }

        auto b = qobject_cast<QPushButton*>(child);
        width = qMax(width, b->geometry().width());
        btns.append(b);
    }

    foreach(auto child, btns){
        child->setMinimumWidth(width);
    }
#endif
}

void MainWindow::on_stopTxButton_clicked()                    //Stop Tx
{
  if (m_tune) stop_tuning ();
  if (m_auto and !m_tuneup) auto_tx_mode (false);
  m_btxok=false;

  resetMessage();
  resetAutomaticIntervalTransmissions(false, false);
}

void MainWindow::rigOpen ()
{
  update_dynamic_property (ui->readFreq, "state", "warning");
  ui->readFreq->setText ("CAT");
  ui->readFreq->setEnabled (true);
  m_config.transceiver_online ();
  Q_EMIT m_config.sync_transceiver (true, true);
}

void MainWindow::on_readFreq_clicked()
{
  if (m_transmitting) return;

  if (m_config.transceiver_online ())
    {
      Q_EMIT m_config.sync_transceiver (true, true);
    }
}

void MainWindow::setXIT(int n)
{
  if (m_transmitting && !m_config.tx_qsy_allowed ()) return;

  m_XIT = m_config.split_mode() ? (n / 500) * 500 - 1500 : 0;

  if ((m_monitoring || m_transmitting)
      && m_config.is_transceiver_online()
      && m_config.split_mode())
    {
      // All conditions are met, reset the transceiver Tx dial
      // frequency
      m_freqTxNominal = m_freqNominal + m_XIT;
      Q_EMIT m_config.transceiver_tx_frequency(m_freqTxNominal);
    }

  //Now set the audio Tx freq
  Q_EMIT transmitFrequency(freq() - m_XIT);
}

void
MainWindow::qsy(int const hzDelta)
{
  setRig(m_freqNominal + hzDelta);
  setFreqOffsetForRestore(m_wideGraph->centerFreq(), false);

  // Adjust band activity frequencies.
  
  BandActivity bandActivity;

  for (auto [key, value] : m_bandActivity.asKeyValueRange())
  {
    if (value.isEmpty()) continue;

    auto const newKey = key - hzDelta;

    bandActivity[newKey] = value;
    bandActivity[newKey].last().offset -= hzDelta;
  }

  m_bandActivity.swap(bandActivity);

  // Adjust call activity frequencies.

  for (auto [key, value] : m_callActivity.asKeyValueRange())
  {
    value.offset -= hzDelta;
  }

  displayActivity(true);
}

void MainWindow::drifted(int /*prev*/, int /*cur*/){
    // here we reset the buffer position without clearing the buffer
    // this makes the detected emit the correct k when drifting time
    m_detector->resetBufferPosition();
}

void MainWindow::setFreqOffsetForRestore(int freq, bool shouldRestore){
    changeFreq(freq);
    if(shouldRestore){
        m_shouldRestoreFreq = true;
    } else {
        m_previousFreq = 0;
        m_shouldRestoreFreq = false;
    }
}

bool MainWindow::tryRestoreFreqOffset(){
    if(!m_shouldRestoreFreq || m_previousFreq == 0){
        return false;
    }

    setFreqOffsetForRestore(m_previousFreq, false);
    return true;
}

void
MainWindow::changeFreq(int const newFreq)
{
  // Don't allow QSY if we've already queued a transmission,
  // unless we have that functionality enabled.

  if (isMessageQueuedForTransmit() && !m_config.tx_qsy_allowed()) return;

  // TODO: jsherer - here's where we'd set minimum frequency again (later?)

  m_previousFreq = freq();
  setFreq(std::max(0, newFreq));

  displayDialFrequency();
}

void MainWindow::handle_transceiver_update (Transceiver::TransceiverState const& s)
{
  //qDebug () << "MainWindow::handle_transceiver_update:" << s;
  Transceiver::TransceiverState old_state {m_rigState};

  // GM8JCF: in stopTx2 we maintain PTT if there are still untransmitted JS8 frames and we are holding the PTT
  // KN4CRD: if we're not holding the PTT we need to check to ensure it's safe to transmit
  if (m_config.hold_ptt() || (s.ptt () && !m_rigState.ptt())) // safe to start audio (caveat - DX Lab Suite Commander)
  {
      if (m_tx_when_ready && m_iptt) // waiting to Tx and still needed
      {
          ptt1Timer.start(1000 * m_config.txDelay ()); //Start-of-transmission sequencer delay
      }
      m_tx_when_ready = false;
  }
  m_rigState = s;

  auto old_freqNominal = m_freqNominal;
  if (!old_freqNominal)
    {
      // always take initial rig frequency to avoid start up problems
      // with bogus Tx frequencies
      m_freqNominal = s.frequency ();
    }

  if (old_state.online () == false && s.online () == true)
    {
      // initializing
      on_monitorButton_clicked (!m_config.monitor_off_at_startup ());
      on_monitorTxButton_toggled (!m_config.transmit_off_at_startup ());
    }

  if (s.frequency () != old_state.frequency () || s.split () != m_splitMode)
    {
      m_splitMode = s.split ();
      if (!s.ptt ())
        {
          m_freqNominal = s.frequency ();
          if (old_freqNominal != m_freqNominal)
            {
              m_freqTxNominal = m_freqNominal;
            }

          if (m_monitoring)
            {
              m_lastMonitoredFrequency = m_freqNominal;
            }
          if (m_lastDialFreq != m_freqNominal) {

            m_lastDialFreq = m_freqNominal;
            m_secBandChanged=DriftingDateTime::currentMSecsSinceEpoch()/1000;

            if(m_freqNominal != m_bandHoppedFreq){
                m_bandHopped = false;
            }

            if(s.frequency () < 30000000u) {
                write_frequency_entry("ALL.TXT");
            }

            if (m_config.spot_to_reporting_networks ()) {
              spotSetLocal();
              pskSetLocal();
              aprsSetLocal();
            }
            statusChanged();
            m_wideGraph->setDialFreq(m_freqNominal / 1.e6f);
          }
      } else {
        m_freqTxNominal = s.split () ? s.tx_frequency () : s.frequency ();
      }
  }

  // ensure frequency display is correct
  // setRig();
  updateCurrentBand();
  displayDialFrequency ();
  update_dynamic_property (ui->readFreq, "state", "ok");
  ui->readFreq->setEnabled (false);
  ui->readFreq->setText (s.split () ? "CAT/S" : "CAT");
}

void MainWindow::handle_transceiver_failure (QString const& reason)
{
  update_dynamic_property (ui->readFreq, "state", "error");
  ui->readFreq->setEnabled (true);
  on_stopTxButton_clicked ();
  rigFailure (reason);
}

void MainWindow::rigFailure (QString const& reason)
{
  if (m_first_error)
    {
      // one automatic retry
      QTimer::singleShot (0, this, &MainWindow::rigOpen);
      m_first_error = false;
    }
  else
    {
      m_rigErrorMessageBox.setDetailedText (reason);

      // don't call slot functions directly to avoid recursion
      m_rigErrorMessageBox.exec ();
      auto const clicked_button = m_rigErrorMessageBox.clickedButton ();
      if (clicked_button == m_configurations_button)
        {
          ui->menuConfig->exec (QCursor::pos ());
        }
      else
        {
          switch (m_rigErrorMessageBox.standardButton (clicked_button))
            {
            case MessageBox::Ok:
              m_config.select_tab (1);
              QTimer::singleShot (0, this, &MainWindow::on_actionSettings_triggered);
              break;

            case MessageBox::Retry:
              QTimer::singleShot (0, this, &MainWindow::rigOpen);
              break;

            case MessageBox::Cancel:
              QTimer::singleShot (0, this, &MainWindow::close);
              break;

            default: break;     // squashing compile warnings
            }
        }
      m_first_error = true;     // reset
    }
}

void
MainWindow::transmit()
{
  Q_EMIT sendMessage (freq() - m_XIT,
                      m_nSubMode,
                      m_soundOutput,
                      m_config.audio_output_channel());
}

void
MainWindow::on_outAttenuation_valueChanged(int const a)
{
  if (m_PwrBandSetOK)
  {
    if (!m_tune && m_config.pwrBandTxMemory())   m_pwrBandTxMemory[m_lastBand]   = a; // remember our Tx pwr
    if ( m_tune && m_config.pwrBandTuneMemory()) m_pwrBandTuneMemory[m_lastBand] = a; // remember our Tune pwr
  }

  // Slider interpreted as dB / 100.

  Q_EMIT outAttenuationChanged (a / 10.0);
}

void
MainWindow::spotSetLocal()
{
  Q_EMIT spotClientSetLocalStation (m_config.my_callsign(),
                                    m_config.my_grid(),
                                    replaceMacros(m_config.my_info(), buildMacroValues(), true));
}

void
MainWindow::pskSetLocal()
{
  Q_EMIT pskReporterSetLocalStation (m_config.my_callsign(),
                                     m_config.my_grid(),
                                     replaceMacros(m_config.my_info(), buildMacroValues(), true));
}

void
MainWindow::aprsSetLocal()
{
  Q_EMIT aprsClientSetLocalStation ("APJ8CL", QString::number(APRSISClient::hashCallsign("APJ8CL")));
}

void MainWindow::transmitDisplay (bool transmitting)
{
  if (transmitting == m_transmitting) {
    if (transmitting) {
      ui->signal_meter_widget->setValue(0,0);
      if (m_monitoring) monitor (false);
      m_btxok=true;
    }
  }

  updateTxButtonDisplay();
}

void MainWindow::postDecode (bool is_new, QString const&)
{
#if 0
  auto const& decode = message.trimmed ();
  auto const& parts = decode.left (22).split (' ', QString::SkipEmptyParts);
  if (parts.size () >= 5)
  {
      auto has_seconds = parts[0].size () > 4;
      m_messageClient->decode (is_new
                               , QTime::fromString (parts[0], has_seconds ? "hhmmss" : "hhmm")
                               , parts[1].toInt ()
                               , parts[2].toFloat (), parts[3].toUInt (), parts[4]
                               , decode.mid (has_seconds ? 24 : 22, 21)
                               , QChar {'?'} == decode.mid (has_seconds ? 24 + 21 : 22 + 21, 1)
                               , m_diskData);
  }
#endif

  if(is_new){
      m_rxDirty = true;
  }
}

void
MainWindow::tryNotify(QString const & key)
{
  if (auto const path = m_config.notification_path(key);
                !path.isEmpty())
  {
    emit playNotification(path);
  }
}

void MainWindow::displayTransmit(){
    // Transmit Activity
    update_dynamic_property (ui->startTxButton, "transmitting", m_transmitting);
    update_dynamic_property (ui->monitorTxButton, "transmitting", m_transmitting);
}

void MainWindow::updateModeButtonText(){
    auto selectedCallsign = callsignSelected();

    auto multi = ui->actionModeMultiDecoder->isChecked();
    auto autoreply = ui->actionModeAutoreply->isChecked();
    auto heartbeat = ui->actionModeJS8HB->isEnabled() && ui->actionModeJS8HB->isChecked();
    auto ack = autoreply && ui->actionHeartbeatAcknowledgements->isChecked() && (!m_config.heartbeat_qso_pause() || selectedCallsign.isEmpty());

    auto modeText = JS8::Submode::name(m_nSubMode);
    if(multi){
        modeText += QString("+MULTI");
    }

    if(autoreply){
        if(m_config.autoreply_confirmation()){
            modeText += QString("+AUTO+CONF");
        } else {
            modeText += QString("+AUTO");
        }
    }

    if(heartbeat){
        if(ack){
            modeText += QString("+HB+ACK");
        } else {
            modeText += QString("+HB");
        }
    }

    ui->modeButton->setText(modeText);
}

void MainWindow::updateButtonDisplay(){
    bool isTransmitting = isMessageQueuedForTransmit();

    auto selectedCallsign = callsignSelected(true);
    bool emptyCallsign = selectedCallsign.isEmpty();
    bool emptyInfo = m_config.my_info().isEmpty();
    bool emptyStatus = m_config.my_status().isEmpty();

    ui->hbMacroButton->setDisabled(isTransmitting);
    ui->cqMacroButton->setDisabled(isTransmitting);
    ui->replyMacroButton->setDisabled(isTransmitting || emptyCallsign);
    ui->snrMacroButton->setDisabled(isTransmitting || emptyCallsign);
    ui->infoMacroButton->setDisabled(isTransmitting || emptyInfo);
    ui->statusMacroButton->setDisabled(isTransmitting || emptyStatus);
    ui->macrosMacroButton->setDisabled(isTransmitting);
    ui->queryButton->setDisabled(isTransmitting || emptyCallsign);
    ui->deselectButton->setDisabled(isTransmitting || emptyCallsign);
    ui->queryButton->setText(emptyCallsign ? "Directed" : QString("Directed to %1").arg(selectedCallsign));

    // refresh repeat button text too
    updateRepeatButtonDisplay();

    // update mode button text
    updateModeButtonText();
}

void MainWindow::updateRepeatButtonDisplay(){
    auto selectedCallsign = callsignSelected();
    auto hbBase = ui->actionModeAutoreply->isChecked() && ui->actionHeartbeatAcknowledgements->isChecked() && (!m_config.heartbeat_qso_pause() || selectedCallsign.isEmpty()) ? "HB + ACK" : "HB";
    if(ui->hbMacroButton->isChecked() && m_hbInterval > 0 && m_nextHeartbeat.isValid()){
        auto secs = DriftingDateTime::currentDateTimeUtc().secsTo(m_nextHeartbeat);
        if(secs > 0){
            ui->hbMacroButton->setText(QString("%1 (%2)").arg(hbBase).arg(secs));
        } else {
            ui->hbMacroButton->setText(QString("%1 (now)").arg(hbBase));
        }
    } else {
        ui->hbMacroButton->setText(hbBase);
    }

    if(ui->cqMacroButton->isChecked() && m_cqInterval > 0 && m_nextCQ.isValid()){
        auto secs = DriftingDateTime::currentDateTimeUtc().secsTo(m_nextCQ);
        if(secs > 0){
            ui->cqMacroButton->setText(QString("CQ (%1)").arg(secs));
        } else {
            ui->cqMacroButton->setText(QString("CQ (now)"));
        }
    } else {
        ui->cqMacroButton->setText("CQ");
    }
}

void MainWindow::updateTextDisplay(){
    bool canTransmit = ensureCanTransmit();
    bool isTransmitting = isMessageQueuedForTransmit();
    bool emptyText = ui->extFreeTextMsgEdit->toPlainText().isEmpty();

    ui->startTxButton->setDisabled(!canTransmit || isTransmitting || emptyText);

    if(m_txTextDirty){
        // debounce frame and word count
        if(m_txTextDirtyDebounce.isActive()){
            m_txTextDirtyDebounce.stop();
        }
        m_txTextDirtyDebounce.setSingleShot(true);
        m_txTextDirtyDebounce.start(100);
        m_txTextDirty = false;
    }
}


#if __APPLE__
#define USE_SYNC_FRAME_COUNT 0
#else
#define USE_SYNC_FRAME_COUNT 0
#endif

void MainWindow::refreshTextDisplay(){
    qDebug() << "refreshing text display...";
    auto text = ui->extFreeTextMsgEdit->toPlainText();

#if USE_SYNC_FRAME_COUNT
    auto frames = buildMessageFrames(text);

    QStringList textList;
    qDebug() << "frames:";
    foreach(auto frame, frames){
        auto dt = DecodedText(frame.first, frame.second);
        qDebug() << "->" << frame << dt.message() << Varicode::frameTypeString(dt.frameType());
        textList.append(dt.message());
    }

    auto transmitText = textList.join("");
    auto count = frames.length();

    // ugh...i hate these globals
    m_txTextDirtyLastSelectedCall = callsignSelected(true);
    m_txTextDirtyLastText = text;
    m_txFrameCountEstimate = count;
    m_txTextDirty = false;

    updateTextWordCheckerDisplay();
    updateTextStatsDisplay(transmitText, count);
    updateTxButtonDisplay();

#else
    // prepare selected callsign for directed message
    QString selectedCall = callsignSelected();

    // prepare compound
    QString mycall = m_config.my_callsign();
    QString mygrid = m_config.my_grid().left(4);
    bool forceIdentify = !m_config.avoid_forced_identify();
    bool forceData = false;

    BuildMessageFramesThread *t = new BuildMessageFramesThread(
        mycall,
        mygrid,
        selectedCall,
        text,
        forceIdentify,
        forceData,
        m_nSubMode
    );

    connect(t, &BuildMessageFramesThread::finished, t, &QObject::deleteLater);
    connect(t, &BuildMessageFramesThread::resultReady, this, [this, text](QString transmitText, int frames){
        // ugh...i hate these globals
        m_txTextDirtyLastSelectedCall = callsignSelected(true);
        m_txTextDirtyLastText         = text;
        m_txFrameCountEstimate        = frames;
        m_txTextDirty                 = false;

        updateTextWordCheckerDisplay();
        updateTextStatsDisplay(transmitText, m_txFrameCountEstimate);
        updateTxButtonDisplay();

    });
    t->start();
#endif
}

void MainWindow::updateTextWordCheckerDisplay(){
    if(!m_config.spellcheck()){
        return;
    }

    JSCChecker::checkRange(ui->extFreeTextMsgEdit, 0, -1);
}

void MainWindow::updateTextStatsDisplay(QString text, int count){
    const double fpm = 60.0/m_TRperiod;
    if(count > 0){
        auto words = text.split(" ", Qt::SkipEmptyParts).length();
        auto wpm = QString::number(words/(count/fpm), 'f', 1);
        auto cpm = QString::number(text.length()/(count/fpm), 'f', 1);
        wpm_label.setText(QString("%1wpm / %2cpm").arg(wpm).arg(cpm));
        wpm_label.setVisible(true);
    } else {
        wpm_label.setVisible(false);
        wpm_label.clear();
    }
}

void MainWindow::updateTxButtonDisplay(){
    // can we transmit at all?
    bool canTransmit = ensureCanTransmit();

    // if we're tuning or have a message queued
    if(m_tune || isMessageQueuedForTransmit()){
        int count = m_txFrameCount;
        int left  = m_txFrameQueue.count();
        int sent  = count - left;
        QString buttonText;
        if(m_tune){
            buttonText = State::Tuning.toString();
        } else if(m_transmitting){
            buttonText = State::timed(State::Sending,
                                     ((left + 1) * m_TRperiod) - ((m_sec0 + 1) % m_TRperiod));
        } else {
            buttonText = State::timed(State::Ready,
                                      sent == 1
                                      ?  ((left + 1) * m_TRperiod)
                                      : (((left + 2) * m_TRperiod) - ((m_sec0 + 1) % m_TRperiod)));
        }
        ui->startTxButton->setText(buttonText);
        ui->startTxButton->setEnabled(false);
        ui->startTxButton->setFlat(true);
    } else {
        QString const buttonText = m_txFrameCountEstimate > 0
                                 ? State::timed(State::Send,
                                                m_txFrameCountEstimate * m_TRperiod)
                                 : State::Send.toString();
        ui->startTxButton->setText(buttonText);
        ui->startTxButton->setEnabled(canTransmit && m_txFrameCountEstimate > 0);
        ui->startTxButton->setFlat(false);
    }
}

QString MainWindow::callsignSelected(bool){
    if(!ui->tableWidgetCalls->selectedItems().isEmpty()){
        auto selectedCalls = ui->tableWidgetCalls->selectedItems();
        if(!selectedCalls.isEmpty()){
            auto call = selectedCalls.first()->data(Qt::UserRole).toString();
            if(!call.isEmpty()){
                return call;
            }
        }
    }

    if(!ui->tableWidgetRXAll->selectedItems().isEmpty()){
        int selectedOffset = -1;
        auto selectedItems = ui->tableWidgetRXAll->selectedItems();
        selectedOffset = selectedItems.first()->data(Qt::UserRole).toInt();

        int threshold = 0;
        auto activity = m_bandActivity.value(selectedOffset);
        if(!activity.isEmpty()){
            threshold = JS8::Submode::rxThreshold(activity.last().submode);
        }

        auto keys = m_callActivity.keys();

        std::stable_sort(keys.begin(),
                         keys.end(),
                         [this](QString const & lhs,
                                QString const & rhs)
        {
          auto const lhsTS = m_callActivity[lhs].utcTimestamp;
          auto const rhsTS = m_callActivity[rhs].utcTimestamp;

          return lhsTS == rhsTS ? lhs   < rhs
                                : rhsTS < lhsTS;
        });

        // Return the first callsign at a frequency within the
        // threshold limit of the selected offset, if any.

        auto const offsetLo = selectedOffset - threshold;
        auto const offsetHi = selectedOffset + threshold;

        for (auto const & key : keys)
        {
          if (auto const & d = m_callActivity[key];
                   offsetLo <= d.offset &&
                   d.offset <= offsetHi)
          {
            return d.call;
          }
        }
    }

#if ALLOW_USE_INPUT_TEXT_CALLSIGN
    if(useInputText){
        auto text = ui->extFreeTextMsgEdit->toPlainText().left(11); // Maximum callsign is 6 + / + 4 = 11 characters
        auto calls = Varicode::parseCallsigns(text);
        if(!calls.isEmpty() && text.startsWith(calls.first()) && calls.first() != m_config.my_callsign()){
            return calls.first();
        }
    }
#endif

    return QString();
}

void MainWindow::callsignSelectedChanged(QString /*old*/, QString selectedCall){
    auto placeholderText = QString("Type your outgoing messages here.").toUpper();
    if(selectedCall.isEmpty()){
        // try to restore hb
        if(m_hbPaused){
            ui->hbMacroButton->setChecked(true);
            m_hbPaused = false;
        }
    } else {
        placeholderText = QString("Type your outgoing directed message to %1 here.").arg(selectedCall).toUpper();

        // when we select a callsign, use it as the qso start time
        if(!m_callSelectedTime.contains(selectedCall)){
            m_callSelectedTime[selectedCall] = DriftingDateTime::currentDateTimeUtc();
        }

        if(m_config.heartbeat_qso_pause()){
            // TODO: jsherer - HB issue
            // don't hb if we select a callsign... (but we should keep track so if we deselect, we restore our hb)
            if(ui->hbMacroButton->isChecked()){
                ui->hbMacroButton->setChecked(false);
                m_hbPaused = true;
            }

            // don't cq if we select a callsign... (and it will not be restored otherwise)
            if(ui->cqMacroButton->isChecked()){
                ui->cqMacroButton->setChecked(false);
            }
        }
    }
    ui->extFreeTextMsgEdit->setPlaceholderText(placeholderText);

#if SHOW_CALL_DETAIL_BROWSER
    auto html = generateCallDetail(selectedCall);
    ui->callDetailTextBrowser->setHtml(html);
    ui->callDetailTextBrowser->setVisible(!selectedCall.isEmpty() && (!hearing.isEmpty() || !heardby.isEmpty()));
#endif

    // immediately update the display
    updateButtonDisplay();
    updateTextDisplay();
    statusChanged();

    m_prevSelectedCallsign = selectedCall;
}

void MainWindow::clearCallsignSelected(){
    // remove the date cache
    m_callSelectedTime.remove(m_prevSelectedCallsign);

    // remove the callsign selection
    ui->tableWidgetCalls->clearSelection();
    ui->tableWidgetRXAll->clearSelection();
}

bool MainWindow::isRecentOffset(int submode, int offset){
    if(abs(offset - freq()) <= JS8::Submode::rxThreshold(submode)){
        return true;
    }
    return (
        m_rxRecentCache.contains(offset/10*10) &&
        m_rxRecentCache[offset/10*10]->secsTo(DriftingDateTime::currentDateTimeUtc()) < 120
    );
}

void MainWindow::markOffsetRecent(int offset){
    m_rxRecentCache.insert(offset/10*10, new QDateTime(DriftingDateTime::currentDateTimeUtc()), 10);
    m_rxRecentCache.insert(offset/10*10+10, new QDateTime(DriftingDateTime::currentDateTimeUtc()), 10);
}

bool MainWindow::isDirectedOffset(int offset, bool *pIsAllCall){
    bool isDirected = (
        m_rxDirectedCache.contains(offset/10*10) &&
        m_rxDirectedCache[offset/10*10]->date.secsTo(DriftingDateTime::currentDateTimeUtc()) < 120
    );

    if (isDirected && pIsAllCall) {
        *pIsAllCall = m_rxDirectedCache[offset/10*10]->isAllcall;
    }

    return isDirected;
}

void MainWindow::markOffsetDirected(int offset, bool isAllCall){
    CachedDirectedType *d1 = new CachedDirectedType{ isAllCall, DriftingDateTime::currentDateTimeUtc() };
    CachedDirectedType *d2 = new CachedDirectedType{ isAllCall, DriftingDateTime::currentDateTimeUtc() };
    m_rxDirectedCache.insert(offset/10*10,    d1, 10);
    m_rxDirectedCache.insert(offset/10*10+10, d2, 10);
}

void MainWindow::clearOffsetDirected(int offset){
    m_rxDirectedCache.remove(offset/10*10);
    m_rxDirectedCache.remove(offset/10*10+10);
}

bool MainWindow::isMyCallIncluded(const QString &text){
    QString myCall = Radio::base_callsign(m_config.my_callsign());

    if(myCall.isEmpty()){
        return false;
    }

    if(!text.contains(myCall)){
        return false;
    }

    auto calls = Varicode::parseCallsigns(text);
    return calls.contains(myCall) || calls.contains(m_config.my_callsign());
}

bool MainWindow::isAllCallIncluded(const QString &text){
    return text.contains("@ALLCALL") || text.contains("@HB");
}

bool MainWindow::isGroupCallIncluded(const QString &text){
    return m_config.my_groups().contains(text);
}

void MainWindow::processActivity(bool force) {
    if (!m_rxDirty && !force) {
        return;
    }

    // Recent Rx Activity
    processRxActivity();

    // Process Idle Activity
    processIdleActivity();

    // Grouped Compound Activity
    processCompoundActivity();

    // Buffered Activity
    processBufferedActivity();

    // Command Activity
    processCommandActivity();

    // Process PSKReporter Spots
    processSpots();

    m_rxDirty = false;
}

void MainWindow::resetTimeDeltaAverage(){
    m_driftMsMMA = 0;
    m_driftMsMMA_N = 0;
}

void MainWindow::setDrift(int n){
    m_wideGraph->setDrift(n);
}

void
MainWindow::processIdleActivity()
{
  auto const now = DriftingDateTime::currentDateTimeUtc();

  // if we detect an idle offset, insert an ellipsis into the activity queue and band activity

  for (auto [offset, activity] : m_bandActivity.asKeyValueRange())
  {
    if (activity.isEmpty()) continue;

    auto const last = activity.last();

    if ((last.bits & Varicode::JS8CallLast) == Varicode::JS8CallLast)               continue;
    if ( last.text == m_config.mfi())                                               continue;
    if ( last.utcTimestamp.secsTo(now) < JS8::Submode::period(last.submode) * 1.50) continue;

    ActivityDetail d = {};
    d.text         = m_config.mfi();
    d.isFree       = true;
    d.utcTimestamp = last.utcTimestamp;
    d.snr          = last.snr;
    d.tdrift       = last.tdrift;
    d.dial         = last.dial;
    d.offset       = last.offset;
    d.submode      = last.submode;

    if (hasExistingMessageBuffer(d.submode, offset, false, nullptr))
    {
      m_messageBuffer[offset].msgs.append(d);
    }

    m_rxActivityQueue.append(d);
    activity.append(d);
  }
}

void MainWindow::processRxActivity() {
    if(m_rxActivityQueue.isEmpty()){
        return;
    }

    int freqOffset = freq();

    qDebug() << m_messageBuffer.count() << "message buffers open";

    while (!m_rxActivityQueue.isEmpty()) {
        ActivityDetail d = m_rxActivityQueue.dequeue();

        if(canSendNetworkMessage()){
            sendNetworkMessage("RX.ACTIVITY", d.text, {
                {"_ID", QVariant(-1)},
                {"FREQ", QVariant(d.dial + d.offset)},
                {"DIAL", QVariant(d.dial)},
                {"OFFSET", QVariant(d.offset)},
                {"SNR", QVariant(d.snr)},
                {"SPEED", QVariant(d.submode)},
                {"TDRIFT", QVariant(d.tdrift)},
                {"UTC", QVariant(d.utcTimestamp.toMSecsSinceEpoch())}
            });
        }

        // use the actual frequency and check its delta from our current frequency
        // meaning, if our current offset is 1502 and the d.freq is 1492, the delta is <= 10;
        bool shouldDisplay = abs(d.offset - freqOffset) <= JS8::Submode::rxThreshold(d.submode);

        int prevOffset = d.offset;
        if(hasExistingMessageBuffer(d.submode, d.offset, false, &prevOffset) && (
                (m_messageBuffer[prevOffset].cmd.to == m_config.my_callsign()) ||
                // (isAllCallIncluded(m_messageBuffer[prevOffset].cmd.to))     || // uncomment this if we want to incrementally print allcalls
                (isGroupCallIncluded(m_messageBuffer[prevOffset].cmd.to))
            )
        ){
            d.isBuffered = true;
            shouldDisplay = true;

            if(!m_messageBuffer[prevOffset].compound.isEmpty()){
                //qDebug() << "should display compound too because at this point it hasn't been displayed" << m_messageBuffer[prevOffset].compound.last().call;

                auto lastCompound = m_messageBuffer[prevOffset].compound.last();

                // fixup compound call incremental text
                d.text = QString("%1: %2").arg(lastCompound.call).arg(d.text);
                d.utcTimestamp = qMin(d.utcTimestamp, lastCompound.utcTimestamp);
            }

        } else if(hasClosedExistingMessageBuffer(d.offset)){
            // incremental typeahead should just be displayed...
            // TODO: should the buffer be reopened?
            shouldDisplay = true;

        } else if(d.isDirected && d.text.contains("<....>")){
            // if this is a _partial_ directed message, skip until the complete call comes through.
            continue;

        } else if(d.isDirected && (d.text.contains(": HB ") || d.text.contains(": @ALLCALL HB"))){ // TODO: HEARTBEAT
            // if this is a heartbeat, process elsewhere...
            continue;
        }

        // if this is the first data frame of a standard message, parse the first word callsigns and spot them :)
        if((d.bits & Varicode::JS8CallFirst) == Varicode::JS8CallFirst && !d.isDirected && !d.isCompound){
            auto calls = Varicode::parseCallsigns(d.text);
            if(!calls.isEmpty()){
                auto theirCall = calls.first();
                if(d.text.startsWith(theirCall) && d.text.mid(theirCall.length(), 1) == ":"){
                    CallDetail cd = {};
                    cd.call = theirCall;
                    cd.dial = d.dial;
                    cd.offset = d.offset;
                    cd.snr = d.snr;
                    cd.bits = d.bits;
                    cd.tdrift = d.tdrift;
                    cd.utcTimestamp = d.utcTimestamp;
                    cd.submode = d.submode;
                    logCallActivity(cd, true);
                }
            }
        }


        // TODO: incremental printing of directed messages
        // Display if:
        // 1) this is a directed message header "to" us and should be buffered...
        // 2) or, this is a buffered message frame for a buffer with us as the recipient.

        if(!shouldDisplay){
            continue;
        }

        bool isFirst = (d.bits & Varicode::JS8CallFirst) == Varicode::JS8CallFirst;
        bool isLast = (d.bits & Varicode::JS8CallLast) == Varicode::JS8CallLast;

        // if we're the last message, let's display our EOT character
        if (isLast) {
            d.text = QString("%1 %2 ").arg(Varicode::rstrip(d.text)).arg(m_config.eot());
        }

        // log it to the display!
        displayTextForFreq(d.text, d.offset, d.utcTimestamp, false, isFirst, isLast);

        // if we've received a message to be displayed, we should bump the repeat buttons...
        resetAutomaticIntervalTransmissions(true, false);

        if(isLast){
            clearOffsetDirected(d.offset);
        }

        if(isLast && !d.isBuffered){
            // buffered commands need the rxFrameBlockNumbers cache so it can fixup its display
            // all other "last" data frames can clear the rxFrameBlockNumbers cache so the next message will be on a new line.
            m_rxFrameBlockNumbers.remove(d.offset);
        }
    }

#if 0
    // TODO: this works but should also print in the rx window.
    foreach(auto offset, m_bandActivity.keys()){
        if(seen.contains(offset)){
            continue;
        }

        if(m_bandActivity[offset].isEmpty()){
            continue;
        }

        auto last = m_bandActivity[offset].last();
        if((last.bits & Varicode::JS8CallLast) == Varicode::JS8CallLast){
            continue;
        }

        auto now = DriftingDateTime::currentDateTimeUtc();
        if(last.utcTimestamp.secsTo(now) < m_TRperiod){
            continue;
        }

        ActivityDetail d = {};
        d.text = " . . . ";
        d.isFree = true;
        d.utcTimestamp = now;
        d.snr = -99;

        m_bandActivity[offset].append(d);
    }
#endif
}

void MainWindow::processCompoundActivity() {
    if(m_messageBuffer.isEmpty()){
        return;
    }

    // group compound callsign and directed commands together.
    foreach(auto freq, m_messageBuffer.keys()) {

        auto & buffer = m_messageBuffer[freq];

        qDebug() << "-> grouping buffer for freq" << freq;

        if (buffer.compound.isEmpty()) {
            qDebug() << "-> buffer.compound is empty...skip";
            continue;
        }

        // if we don't have an initialized command, skip...
        int bits = buffer.cmd.bits;
        bool validBits = (
            bits == Varicode::JS8Call                                         ||
            ((bits & Varicode::JS8CallFirst)    == Varicode::JS8CallFirst)    ||
            ((bits & Varicode::JS8CallLast)     == Varicode::JS8CallLast)     ||
            ((bits & Varicode::JS8CallData)     == Varicode::JS8CallData)
        );
        if (!validBits) {
            qDebug() << "-> buffer.cmd bits is invalid...skip";
            continue;
        }

        // if we need two compound calls, but less than two have arrived...skip
        if (buffer.cmd.from == "<....>" && buffer.cmd.to == "<....>" && buffer.compound.length() < 2) {
            qDebug() << "-> buffer needs two compound, but has less...skip";
            continue;
        }

        // if we need one compound call, but non have arrived...skip
        if ((buffer.cmd.from == "<....>" || buffer.cmd.to == "<....>") && buffer.compound.length() < 1) {
            qDebug() << "-> buffer needs one compound, but has less...skip";
            continue;
        }

        if (buffer.cmd.from == "<....>") {
            auto d = buffer.compound.dequeue();
            buffer.cmd.from = d.call;
            buffer.cmd.grid = d.grid;
            buffer.cmd.isCompound = true;
            buffer.cmd.utcTimestamp = qMin(buffer.cmd.utcTimestamp, d.utcTimestamp);

            if ((d.bits & Varicode::JS8CallLast) == Varicode::JS8CallLast) {
                buffer.cmd.bits = d.bits;
            }
        }

        if (buffer.cmd.to == "<....>") {
            auto d = buffer.compound.dequeue();
            buffer.cmd.to = d.call;
            buffer.cmd.isCompound = true;
            buffer.cmd.utcTimestamp = qMin(buffer.cmd.utcTimestamp, d.utcTimestamp);

            if ((d.bits & Varicode::JS8CallLast) == Varicode::JS8CallLast) {
                buffer.cmd.bits = d.bits;
            }
        }

        if ((buffer.cmd.bits & Varicode::JS8CallLast) != Varicode::JS8CallLast) {
            qDebug() << "-> still not last message...skip";
            continue;
        }

        // fixup the datetime with the "minimum" dt seen
        // this will allow us to delete the activity lines
        // when the compound buffered command comes in.
        auto dt = buffer.cmd.utcTimestamp;
        foreach(auto c, buffer.compound){
            dt = qMin(dt, c.utcTimestamp);
        }
        foreach(auto m, buffer.msgs){
            dt = qMin(dt, m.utcTimestamp);
        }
        buffer.cmd.utcTimestamp = dt;

        qDebug() << "buffered compound command ready" << buffer.cmd.from << buffer.cmd.to << buffer.cmd.cmd;

        m_rxCommandQueue.append(buffer.cmd);
        m_messageBuffer.remove(freq);

        // TODO: only if to me?
        m_lastClosedMessageBufferOffset = freq;
    }
}

void
MainWindow::processBufferedActivity()
{
    if (m_messageBuffer.isEmpty()) return;

    foreach(auto freq, m_messageBuffer.keys())
    {
        auto buffer = m_messageBuffer[freq];

        // check to make sure we empty old buffers by getting the latest timestamp
        // and checking to see if it's older than one minute.
        auto dt = DriftingDateTime::currentDateTimeUtc().addDays(-1);
        if(buffer.cmd.utcTimestamp.isValid()){
            dt = qMax(dt, buffer.cmd.utcTimestamp);
        }
        if(!buffer.compound.isEmpty()){
            dt = qMax(dt, buffer.compound.last().utcTimestamp);
        }
        if(!buffer.msgs.isEmpty()){
            dt = qMax(dt, buffer.msgs.last().utcTimestamp);
        }

        // if the buffer has messages older than 1 minute, and we still haven't closed it, let's mark it as the last frame
        if(dt.secsTo(DriftingDateTime::currentDateTimeUtc()) > 60 && !buffer.msgs.isEmpty()){
            buffer.msgs.last().bits |= Varicode::JS8CallLast;
        }

        // but, if the buffer is older than 1.5 minutes, and we still haven't closed it, just remove it and skip
        if(dt.secsTo(DriftingDateTime::currentDateTimeUtc()) > 90){
            m_messageBuffer.remove(freq);
            continue;
        }

        // if the buffer has no messages, skip
        if (buffer.msgs.isEmpty()) {
            continue;
        }

        // if the buffered message hasn't seen the last message, skip
        if ((buffer.msgs.last().bits & Varicode::JS8CallLast) != Varicode::JS8CallLast) {
            continue;
        }

        QString message;
        foreach(auto part, buffer.msgs) {
            message.append(part.text);
        }
        message = Varicode::rstrip(message);

        QString checksum;

        bool valid = false;

        if(Varicode::isCommandBuffered(buffer.cmd.cmd)){
            int checksumSize = Varicode::isCommandChecksumed(buffer.cmd.cmd);

            if(checksumSize == 32) {
                message = Varicode::lstrip(message);
                checksum = message.right(6);
                message = message.left(message.length() - 7);
                valid = Varicode::checksum32Valid(checksum, message);
            } else if(checksumSize == 16) {
                message = Varicode::lstrip(message);
                checksum = message.right(3);
                message = message.left(message.length() - 4);
                valid = Varicode::checksum16Valid(checksum, message);
            } else if (checksumSize == 0) {
                valid = true;
            }
        } else {
            valid = true;
        }


        if (valid) {
            buffer.cmd.bits |= Varicode::JS8CallLast;
            buffer.cmd.text = message;
            buffer.cmd.isBuffered = true;
            m_rxCommandQueue.append(buffer.cmd);
        } else {
            qDebug() << "Buffered message failed checksum...discarding";
            qDebug() << "Checksum:" << checksum;
            qDebug() << "Message:" << message;
        }

        // regardless of valid or not, remove the "complete" buffered message from the buffer cache
        m_messageBuffer.remove(freq);
        m_lastClosedMessageBufferOffset = freq;
    }
}

void MainWindow::processCommandActivity() {
#if 0
    if (!m_txFrameQueue.isEmpty()) {
        return;
    }
#endif

    if (m_rxCommandQueue.isEmpty()) {
        return;
    }

#if 0
    bool processed = false;

    int f = currentFreq();
#endif

    auto now = DriftingDateTime::currentDateTimeUtc();

    while (!m_rxCommandQueue.isEmpty()) {
        auto d = m_rxCommandQueue.dequeue();

        auto selectedCallsign = callsignSelected();
        bool isAllCall = isAllCallIncluded(d.to);
        bool isGroupCall = isGroupCallIncluded(d.to);

        qDebug() << "try processing command" << d.from << d.to << d.cmd << d.dial << d.offset << d.grid << d.extra << isAllCall << isGroupCall;

        // if we need a compound callsign but never got one...skip
        if (d.from == "<....>" || d.to == "<....>") {
            continue;
        }

        // we're only processing a subset of queries at this point
        if (!Varicode::isCommandAllowed(d.cmd)) {
            continue;
        }

        // is this to me?
        bool toMe = d.to == m_config.my_callsign().trimmed() || d.to == Radio::base_callsign(m_config.my_callsign()).trimmed();

        // log call activity...
        CallDetail cd = {};
        cd.call = d.from;
        cd.grid = d.grid;
        cd.snr = d.snr;
        cd.dial = d.dial;
        cd.offset = d.offset;
        cd.bits = d.bits;
        cd.ackTimestamp = d.text.contains(": ACK") || toMe ? d.utcTimestamp : QDateTime{};
        cd.utcTimestamp = d.utcTimestamp;
        cd.tdrift = d.tdrift;
        cd.submode = d.submode;
        logCallActivity(cd, true);
        logHeardGraph(d.from, d.to);

        // PROCESS BUFFERED HEARING FOR EVERYONE
        if (d.cmd == " HEARING"){
            // 1. parse callsigns
            // 2. log it to the heard graph
            auto calls = Varicode::parseCallsigns(d.text);
            foreach(auto call, calls){
                logHeardGraph(d.from, call);
            }
        }

        // PROCESS BUFFERED GRID FOR EVERYONE
        if(d.cmd == " GRID"){
            // 1. parse grids
            // 2. log it to our call activity
            auto grids = Varicode::parseGrids(d.text);
            foreach(auto grid, grids){
                CallDetail cd = {};
                cd.bits = d.bits;
                cd.call = d.from;
                cd.dial = d.dial;
                cd.offset = d.offset;
                cd.grid = grid;
                cd.snr = d.snr;
                cd.utcTimestamp = d.utcTimestamp;
                cd.tdrift = d.tdrift;
                cd.submode = d.submode;

                // PROCESS GRID SPOTS TO APRSIS FOR EVERYONE
                if(d.to == "@APRSIS"){
                    spotAprsGrid(cd.dial, cd.offset, cd.snr, cd.call, cd.grid);
                }

                logCallActivity(cd, true);
            }
        }

        // PROCESS @JS8NET, @APRSIS, AND OTHER GROUP SPOTS FOR EVERYONE
        if (d.to.startsWith("@")){
            spotCmd(d);
        }

        // PROCESS @APRSIS CMD SPOTS FOR EVERYONE
        if (d.to == "@APRSIS"){
            spotAprsCmd(d);
        }

        // PREPARE CMD TEXT STRING
        QStringList textList = {
            QString("%1: %2%3").arg(d.from).arg(d.to).arg(d.cmd)
        };

        if(!d.extra.isEmpty()){
            textList.append(d.extra);
        }

        if(!d.text.isEmpty()){
            textList.append(d.text);
        }

        QString text = textList.join(" ");
        bool isLast = (d.bits & Varicode::JS8CallLast) == Varicode::JS8CallLast;
        if (isLast) {
            // append the eot character to the text
            text = QString("%1 %2 ").arg(Varicode::rstrip(text)).arg(m_config.eot());
        }

        // log the text to directed txt log
        writeMsgTxt(text, d.snr);

        // write all directed messages to api
        if(canSendNetworkMessage()){
            sendNetworkMessage("RX.DIRECTED", text, {
                {"_ID", QVariant(-1)},
                {"FROM", QVariant(d.from)},
                {"TO", QVariant(d.to)},
                {"CMD", QVariant(d.cmd)},
                {"GRID", QVariant(d.grid)},
                {"EXTRA", QVariant(d.extra)},
                {"TEXT", QVariant(text)},
                {"FREQ", QVariant(d.dial+d.offset)},
                {"DIAL", QVariant(d.dial)},
                {"OFFSET", QVariant(d.offset)},
                {"SNR", QVariant(d.snr)},
                {"SPEED", QVariant(d.submode)},
                {"TDRIFT", QVariant(d.tdrift)},
                {"UTC", QVariant(d.utcTimestamp.toMSecsSinceEpoch())}
            });
        }

        // we're only responding to allcalls if we are participating in the allcall group
        // but, don't avoid for heartbeats...those are technically allcalls but are processed differently
        if(isAllCall && m_config.avoid_allcall() && d.cmd != " CQ" && d.cmd != " HB" && d.cmd != " HEARTBEAT"){
            continue;
        }

        // we're only responding to allcall, groupcalls, and our callsign at this point, so we'll end after logging the callsigns we've heard
        if (!isAllCall && !toMe && !isGroupCall) {
            continue;
        }

        ActivityDetail ad = {};
        ad.isLowConfidence = false;
        ad.isFree = true;
        ad.isDirected = true;
        ad.bits = d.bits;
        ad.dial = d.dial;
        ad.offset = d.offset;
        ad.snr = d.snr;
        ad.text = text;
        ad.utcTimestamp = d.utcTimestamp;

        // we'd be double printing here if were on frequency, so let's be "smart" about this...
        bool shouldDisplay = true;

        // don't display ping allcalls
        if(isAllCall && (d.cmd != " " || ad.text.contains("@HB HEARTBEAT"))){
            shouldDisplay = false;
        }

        if(shouldDisplay){
            auto c = ui->textEditRX->textCursor();
            c.movePosition(QTextCursor::End);
            ui->textEditRX->setTextCursor(c);

            // ACKs and SNRs are the most likely source of items to be overwritten (multiple responses at once)...
            // so don't overwrite those (i.e., print each on a new line)
            bool shouldOverwrite = (!d.cmd.contains(" ACK") && !d.cmd.contains(" SNR")); /* && isRecentOffset(d.freq);*/

            if(shouldOverwrite && ui->textEditRX->find(d.utcTimestamp.time().toString(), QTextDocument::FindBackward)){
                // ... maybe we could delete the last line that had this message on this frequency...
                c = ui->textEditRX->textCursor();
                c.movePosition(QTextCursor::StartOfBlock);
                c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                qDebug() << "should display directed message, erasing last rx activity line..." << c.selectedText().toUpper();
                c.removeSelectedText();
                c.deletePreviousChar();
                c.deletePreviousChar();
                /*
                c.deleteChar();
                c.deleteChar();
                */
            }

            // log it to the display!
            displayTextForFreq(ad.text, ad.offset, ad.utcTimestamp, false, true, false);

            /*
            // and send it to the network in case we want to interact with it from an external app...
            if(canSendNetworkMessage()){
                sendNetworkMessage("RX.DIRECTED.ME", ad.text, {
                    {"_ID", QVariant(-1)},
                    {"FROM", QVariant(d.from)},
                    {"TO", QVariant(d.to)},
                    {"CMD", QVariant(d.cmd)},
                    {"GRID", QVariant(d.grid)},
                    {"EXTRA", QVariant(d.extra)},
                    {"TEXT", QVariant(d.text)},
                    {"FREQ", QVariant(ad.dial+ad.offset)},
                    {"DIAL", QVariant(ad.dial)},
                    {"OFFSET", QVariant(ad.offset)},
                    {"SNR", QVariant(ad.snr)},
                    {"SPEED", QVariant(ad.submode)},
                    {"TDRIFT", QVariant(ad.tdrift)},
                    {"UTC", QVariant(ad.utcTimestamp.toMSecsSinceEpoch())}
                });
            }
            */

            if(!isAllCall){
                // if we've received a message to be displayed, we should bump the repeat buttons...
                resetAutomaticIntervalTransmissions(true, false);

                // notification for directed message
                tryNotify("directed");
            }
        }

        // we're only responding to callsigns in our whitelist if we have one defined...
        // make sure the whitelist is empty (no restrictions) or the from callsign or its base callsign is on it
        auto whitelist = m_config.auto_whitelist();
        if(!whitelist.isEmpty() && !(whitelist.contains(d.from) || whitelist.contains(Radio::base_callsign(d.from)))){
            qDebug() << "skipping command for whitelist" << d.from;
            continue;
        }

        // we'll never reply to a blacklisted callsign or base callsign
        auto blacklist = m_config.auto_blacklist();
        if(!blacklist.isEmpty() && (blacklist.contains(d.from) || blacklist.contains(Radio::base_callsign(d.from)))){
            qDebug() << "skipping command for blacklist" << d.from;
            continue;
        }

        // if this is an allcall, check to make sure we haven't replied to their allcall recently (in the past ten minutes)
        // that way we never get spammed by allcalls at too high of a frequency
        if (isAllCall && m_txAllcallCommandCache.contains(d.from) && m_txAllcallCommandCache[d.from]->secsTo(now) / 60 < 15) {
            qDebug() << "skipping command for allcall timeout" << d.from;
            continue;
        }

        // don't actually process any automatic message replies while in idle
        if(m_tx_watchdog){
            qDebug() << "skipping command for idle timeout" << d.from;
            continue;
        }

        // HACK: if this is an autoreply cmd and relay path is populated and cmd is not MSG or MSG TO:, then swap out the relay path
        if(Varicode::isCommandAutoreply(d.cmd) && !d.relayPath.isEmpty() && !d.cmd.startsWith(" MSG") && !d.cmd.startsWith(" QUERY")){
            d.from = d.relayPath;
        }

        // construct a reply, if needed
        QString reply;
        int priority = PriorityNormal;
        int freq = -1;

        // QUERIED SNR
        if (d.cmd == " SNR?" && !isAllCall) {
            reply = QString("%1 SNR %2").arg(d.from).arg(Varicode::formatSNR(d.snr));
        }

        // QUERIED INFO
        else if (d.cmd == " INFO?" && !isAllCall) {
            QString info = m_config.my_info();
            if (info.isEmpty()) {
                continue;
            }

            reply = QString("%1 INFO %2").arg(d.from).arg(replaceMacros(info, buildMacroValues(), true));
        }

        // QUERIED ACTIVE
        else if (d.cmd == " STATUS?" && !isAllCall) {
            QString status = m_config.my_status();
            if (status.isEmpty()) {
                continue;
            }

            reply = QString("%1 STATUS %2").arg(d.from).arg(replaceMacros(status, buildMacroValues(), true));
        }

        // QUERIED GRID
        else if (d.cmd == " GRID?" && !isAllCall) {
            QString grid = m_config.my_grid();
            if (grid.isEmpty()) {
                continue;
            }

            reply = QString("%1 GRID %2").arg(d.from).arg(grid);
        }

        // QUERIED STATIONS HEARD
        else if (d.cmd == " HEARING?" && !isAllCall)
        {
            auto calls = m_callActivity.keys();

            std::stable_sort(calls.begin(),
                             calls.end(),
                             [this](QString const & lhs,
                                    QString const & rhs)
            {
              return m_callActivity[rhs].utcTimestamp <
                     m_callActivity[lhs].utcTimestamp;
            });

            auto const  callsignAging = m_config.callsign_aging();
            auto const  maxStations   = 4;
            auto        i             = 0;
            QStringList lines;

            for (auto const & call : calls)
            {
              if (i    >= maxStations) break;
              if (call == d.from)      continue;

              auto const & cd = m_callActivity[call];

              if (callsignAging && cd.utcTimestamp.secsTo(now) / 60 >= callsignAging)
              {
                continue;
              }

              lines.append(cd.call);
              i++;
            }

            lines.prepend(QString("%1 HEARING").arg(d.from));
            reply = lines.join(' ');
        }

        // PROCESS RELAY
        else if (d.cmd == ">" && !isAllCall && !m_config.relay_off()) {

            // 1. see if there are any more hops to process
            // 2. if so, forward
            // 3. otherwise, display alert & reply dialog

            QString callToPattern = {R"(^(?<callsign>\b(?<prefix>[A-Z0-9]{1,4}\/)?(?<base>([0-9A-Z])?([0-9A-Z])([0-9])([A-Z])?([A-Z])?([A-Z])?)(?<suffix>\/[A-Z0-9]{1,4})?(?<type>[> ]))\b)"};
            QRegularExpression re(callToPattern);
            auto text = d.text;
            auto match = re.match(text);

            // if the text starts with a callsign, and relay is not disabled, and this is not a group callsign, then relay.
            if(match.hasMatch() && !isGroupCall){
                // replace freetext with relayed free text
                if(match.captured("type") != ">"){
                    text = text.replace(match.capturedStart("type"), match.capturedLength("type"), ">");
                }
                reply = QString("%1 *DE* %2").arg(text).arg(d.from);

            // otherwise, as long as we're not an ACK...alert the user and either send an ACK or Message
            } else if(!d.text.startsWith("ACK")) {

                // parse out the callsign path
                auto calls = parseRelayPathCallsigns(d.from, d.text);

                // put these third party calls in the heard list
                foreach(auto call, calls){
                    CallDetail cd = {};
                    cd.call = call;
                    cd.snr = -64;
                    cd.dial = d.dial;
                    cd.offset = d.offset;
                    cd.through = d.from;
                    cd.utcTimestamp = DriftingDateTime::currentDateTimeUtc();
                    cd.tdrift = d.tdrift;
                    cd.submode = d.submode;
                    logCallActivity(cd, false);
                }

                d.relayPath = calls.join('>');

                reply = QString("%1 ACK").arg(d.relayPath);

                // check to see if the relay text contains a command that should be replied to instead of an ack.
                QStringList relayedCmds = d.text.split(" ");
                if(!relayedCmds.isEmpty()){
                    auto first = relayedCmds.first();

                    auto valid = Varicode::isCommandAllowed(first);
                    if(!valid){
                        first = " " + first;
                        valid = Varicode::isCommandAllowed(first);
                        if(valid){
                            relayedCmds.removeFirst();
                        }
                    }

                    // HACK: "MSG TO:" should be supported but contains a space :(
                    if(!relayedCmds.isEmpty()){
                        if(first == " MSG"){
                            auto second = relayedCmds.first();
                            if(second == "TO:"){
                                first = " MSG TO:";
                                relayedCmds.removeFirst();
                            } else if(second.startsWith("TO:")){
                                first = " MSG TO:";
                                relayedCmds.replace(0, second.mid(3));
                            }
                        } else if (first == " QUERY"){
                            auto second = relayedCmds.first();
                            if(second == "MSGS" || second == "MSGS?"){
                                first = " QUERY MSGS";
                                relayedCmds.removeFirst();
                            }
                            else if(second == "CALL"){
                                first = " QUERY CALL";
                                relayedCmds.removeFirst();
                            }
                        }
                    }

                    if(Varicode::isCommandAllowed(first) && Varicode::isCommandAutoreply(first)){
                        CommandDetail rd = {};
                        rd.bits = d.bits;
                        rd.cmd = first;
                        rd.dial = d.dial;
                        rd.offset = d.offset;
                        rd.from = d.from; // note, MSG and QUERY commands are not set with from as the relay path.
                        rd.relayPath = d.relayPath;
                        rd.text = relayedCmds.join(" "); //d.text;
                        rd.to = d.to;
                        rd.utcTimestamp = d.utcTimestamp;

                        m_rxCommandQueue.insert(0, rd);
                        continue;
                    }
                }

#if STORE_RELAY_MSGS_TO_INBOX
                // if we make it here, this is a message
                addCommandToMyInbox(d);
#endif
            }
        }

        // PROCESS MESSAGE STORAGE
        else if (d.cmd == " MSG TO:" && !isAllCall && !isGroupCall && !m_config.relay_off()){

            // store message
            QStringList segs = d.text.split(" ");
            if(segs.isEmpty()){
                continue;
            }

            auto to = segs.first();
            segs.removeFirst();

            auto text = segs.join(" ").trimmed();

            auto calls = parseRelayPathCallsigns(d.from, text);
            d.relayPath = calls.join(">");

            CommandDetail cd = {};
            cd.bits = d.bits;
            cd.cmd = d.cmd;
            cd.extra = d.extra;
            cd.dial = d.dial;
            cd.offset = d.offset;
            cd.from = d.from;
            cd.grid = d.grid;
            cd.relayPath = d.relayPath;
            cd.snr = d.snr;
            cd.tdrift = d.tdrift;
            cd.text = text;
            cd.to = Radio::base_callsign(to);
            cd.utcTimestamp = d.utcTimestamp;
            cd.submode = d.submode;

            qDebug() << "storing message to" << to << ":" << text;

            addCommandToStorage("STORE", cd);

            // we haven't replaced the from with the relay path, so we have to use it for the ack if there is one
            reply = QString("%1 ACK").arg(calls.length() > 1 ? d.relayPath : d.from);
        }

        // PROCESS AGN
        else if (d.cmd == " AGN?" && !isAllCall && !isGroupCall && !m_lastTxMessage.isEmpty()) {
            reply = Varicode::rstrip(m_lastTxMessage);
        }

        // PROCESS ACTIVE HEARTBEAT
        // if we have hb mode enabled and auto reply enabled <del>and auto ack enabled and no callsign is selected</del> update: if we're in HB mode, doesn't matter if a callsign is selected.
        else if ((d.cmd == " HB" || d.cmd == " HEARTBEAT") && canCurrentModeSendHeartbeat() && ui->actionModeJS8HB->isChecked() && ui->actionModeAutoreply->isChecked() && ui->actionHeartbeatAcknowledgements->isChecked()){
            // check to make sure we aren't pausing HB transmissions (ACKs) while a callsign is selected
            if(m_config.heartbeat_qso_pause() && !selectedCallsign.isEmpty()){
                qDebug() << "hb paused during qso";
                continue;
            }

            // check to make sure this callsign isn't blacklisted
            if(m_config.hb_blacklist().contains(d.from) || m_config.hb_blacklist().contains(Radio::base_callsign(d.from))){
                qDebug() << "hb blacklist blocking" << d.from;
                continue;
            }

            // check to see if we have a message for a station who is heartbeating
            QString extra;
            auto mid = getNextMessageIdForCallsign(d.from);
            if(mid != -1){
                extra = QString("MSG ID %1").arg(mid);
            }

            // TODO: require confirmation?
            sendHeartbeatAck(d.from, d.snr, extra);

            if(isAllCall){
                // since all pings are technically @ALLCALL, let's bump the allcall cache here...
                m_txAllcallCommandCache.insert(d.from, new QDateTime(now), 5);
            }

            continue;
        }

        // PROCESS HEARTBEAT SNR
        else if (d.cmd == " HEARTBEAT SNR"){
            qDebug() << "skipping incoming hb snr" << d.text;
            continue;
        }

        // PROCESS CQ
        else if (d.cmd == " CQ"){
            qDebug() << "skipping incoming cq" << d.text;
            continue;
        }

        // PROCESS MSG
        else if (d.cmd == " MSG" && !isAllCall){

            auto text = d.text;

            qDebug() << "adding message to inbox" << text;

            auto calls = parseRelayPathCallsigns(d.from, text);

            d.cmd = " MSG ";
            d.relayPath = calls.join(">");
            d.text = text;

            addCommandToMyInbox(d);

            // notification
            tryNotify("inbox");

            // we haven't replaced the from with the relay path, so we have to use it for the ack if there is one
            reply = QString("%1 ACK").arg(calls.length() > 1 ? d.relayPath : d.from);

#define SHOW_ALERT_FOR_MSG 1
#if SHOW_ALERT_FOR_MSG
            SelfDestructMessageBox * m = new SelfDestructMessageBox(300,
              "New Message Received",
              QString("A new message was received at %1 UTC from %2").arg(d.utcTimestamp.time().toString()).arg(d.from),
              QMessageBox::Information,
              QMessageBox::Ok,
              QMessageBox::Ok,
              false,
              this);

            m->show();
#endif
        }

        // PROCESS ACKS
        else if (d.cmd == " ACK" && !isAllCall){
            qDebug() << "skipping incoming ack" << d.text;

            // notification for ack
            tryNotify("ack");

            // make sure this is explicit
            continue;
        }

        // PROCESS BUFFERED CMD
        else if (d.cmd == " CMD" && !isAllCall){
            qDebug() << "skipping incoming command" << d.text;

            // make sure this is explicit
            continue;
        }

        // PROCESS BUFFERED QUERY
        else if (d.cmd == " QUERY" && !isAllCall){
            auto who = d.from; // keep in mind, this is the sender, not the original requestor if relayed
            auto replyPath = d.from;

            if(d.relayPath.contains(">")){
                auto path = d.relayPath.split(">");
                who = path.last();
                replyPath = d.relayPath;
            }

            QStringList segs = d.text.split(" ");
            if(segs.isEmpty()){
                continue;
            }

            auto cmd = segs.first();
            segs.removeFirst();

            if(cmd == "MSG" && !segs.isEmpty()){
                auto inbox = Inbox(inboxPath());
                if(!inbox.open()){
                    continue;
                }

                bool ok = false;
                int mid = QString(segs.first()).toInt(&ok);
                if(!ok){
                    continue;
                }

                auto msg = inbox.value(mid);
                auto params = msg.params();
                if(params.isEmpty()){
                    continue;
                }

                auto from = params.value("FROM").toString().trimmed();

                // TODO: group messaging - allow any message to a @GROUP to be retrieved by anybody

                auto to = params.value("TO").toString().trimmed();
                if(to != who && to != Radio::base_callsign(who)){
                    continue;
                }

                auto text = params.value("TEXT").toString().trimmed();
                if(text.isEmpty()){
                    continue;
                }

                // mark as delivered (so subsequent HBs and QUERY MSGS don't receive this message)
                msg.setType("DELIVERED");
                inbox.set(mid, msg);

                // and reply
                reply = QString("%1 MSG %2 FROM %3");
                reply = reply.arg(replyPath);
                reply = reply.arg(text);
                reply = reply.arg(from);
            }
        }

        // PROCESS BUFFERED QUERY MSGS
        else if (d.cmd == " QUERY MSGS" && ui->actionModeAutoreply->isChecked()){
            auto who = d.from; // keep in mind, this is the sender, not the original requestor if relayed
            auto replyPath = d.from;

            if(d.relayPath.contains(">")){
                auto path = d.relayPath.split(">");
                who = path.last();
                replyPath = d.relayPath;
            }

            // if this is an allcall or a directed call, check to see if we have a stored message for user.
            // we reply yes if the user would be able to retreive a stored message
            auto mid = getNextMessageIdForCallsign(who);
            if(mid != -1){
                reply = QString("%1 YES MSG ID %2").arg(replyPath).arg(mid);
            }

            // TODO: group messaging - if a isGroupCall, check to see if there's a message id for the group and return it if there's not an individual message

            // if this is not an allcall and we have no messages, reply no.
            if(!isAllCall && reply.isEmpty()){
                reply = QString("%1 NO").arg(replyPath);
            }
        }

        // PROCESS BUFFERED QUERY CALL
        else if (d.cmd == " QUERY CALL" && ui->actionModeAutoreply->isChecked()){
            auto replyPath = d.from;
            if(d.relayPath.contains(">")){
                replyPath = d.relayPath;
            }

            auto who = d.text;
            if(who.isEmpty()){
                continue;
            }

            auto callsigns = Varicode::parseCallsigns(who);
            if(callsigns.isEmpty()){
                continue;
            }

            QStringList replies;
            int callsignAging = m_config.callsign_aging();
            auto baseCall = callsigns.first();
            foreach(auto cd, m_callActivity.values()){
                if (callsignAging && cd.utcTimestamp.secsTo(now) / 60 >= callsignAging) {
                    continue;
                }

                if(baseCall == cd.call || baseCall == Radio::base_callsign(cd.call)){
                    auto r = QString("%1 (%2)").arg(Varicode::formatSNR(cd.snr)).arg(since(cd.utcTimestamp)).trimmed();
                    replies.append(r);
                    break;
                }
            }

            if(!replies.isEmpty()){
                replies.prepend(QString("%1 YES").arg(replyPath));
            }

            reply = replies.join(" ");

            if(!reply.isEmpty()){
                if(isAllCall){
                    m_txAllcallCommandCache.insert(d.from, new QDateTime(now), 25);
                }
            }
        }

#if 0
        // PROCESS ALERT
        else if (d.cmd == "!" && !isAllCall) {

            // create alert dialog
            processAlertReplyForCommand(d, d.from, " ");

            // make sure this is explicit
            continue;
        }
#endif

        // well, if there's no reply, don't do anything...
        if (reply.isEmpty()) {
            continue;
        }

        // do not queue @ALLCALL replies if auto-reply is not checked
        if(!ui->actionModeAutoreply->isChecked() && isAllCall){
            continue;
        }

#if 0
        // TODO: jsherer - HB issue here
        // do not queue a reply if it's a HB and HB is not active
        // if((!ui->hbMacroButton->isChecked() || m_hbInterval <= 0) && d.cmd.contains("HB")){
        //     continue;
        // }
#endif

        // do not queue for reply if there's text in the window
        if(!ui->extFreeTextMsgEdit->toPlainText().isEmpty()){
            continue;
        }

        // do not queue for reply if there's a buffer open to us
        int bufferOffset = 0;
        if(hasExistingMessageBufferToMe(&bufferOffset)){
            qDebug() << "skipping reply due to open buffer" << bufferOffset << m_messageBuffer.count();
            continue;
        }

        // add @ALLCALLs to the @ALLCALL cache
        if(isAllCall){
            m_txAllcallCommandCache.insert(d.from, new QDateTime(now), 25);
        }

        // queue the reply here to be sent when a free interval is available on the frequency that was sent
        // unless, this is an allcall, to which we should be responding on a clear frequency offset
        // we always want to make sure that the directed cache has been updated at this point so we have the
        // most information available to make a frequency selection.
        if(m_config.autoreply_confirmation()){
            confirmThenEnqueueMessage(90, priority, reply, freq, nullptr);
        } else {
            enqueueMessage(priority, reply, freq, nullptr);
        }
    }
}

QString MainWindow::inboxPath(){
    return QDir::toNativeSeparators(m_config.writeable_data_dir().absoluteFilePath("inbox.db3"));
}

void MainWindow::refreshInboxCounts(){
    auto inbox = Inbox(inboxPath());
    if(inbox.open()){
        // reset inbox counts
        m_rxInboxCountCache.clear();

        // compute new counts from db
        auto v = inbox.values("UNREAD", "$", "%", 0, 10000);
        foreach(auto pair, v){
            auto params = pair.second.params();
            auto to = params.value("TO").toString();
            if(to.isEmpty() || (to != m_config.my_callsign() && to != Radio::base_callsign(m_config.my_callsign()))){
                continue;
            }
            auto from = params.value("FROM").toString();
            if(from.isEmpty()){
                continue;
            }

            m_rxInboxCountCache[from] = m_rxInboxCountCache.value(from, 0) + 1;

            if (!m_callActivity.contains(from))
            {
                auto const utc     = params.value("UTC").toString();
                auto const snr     = params.value("SNR").toInt();
                auto const dial    = params.value("DIAL").toInt();
                auto const offset  = params.value("OFFSET").toInt();
                auto const tdrift  = params.value("TDRIFT").toInt();
                auto const submode = params.value("SUBMODE").toInt();

                CallDetail cd;
                cd.call         = from;
                cd.snr          = snr;
                cd.dial         = dial;
                cd.offset       = offset;
                cd.tdrift       = tdrift;
                cd.utcTimestamp = QDateTime::fromString(utc, "yyyy-MM-dd hh:mm:ss");
                cd.utcTimestamp.setTimeZone(QTimeZone::utc());
                cd.ackTimestamp = cd.utcTimestamp;
                cd.submode      = submode;
                logCallActivity(cd, false);
            }
        }
    }
}

bool MainWindow::hasMessageHistory(QString call){
    auto inbox = Inbox(inboxPath());
    if(!inbox.open()){
        return false;
    }

    int store = inbox.count("STORE", "$.params.TO", call);
    int unread = inbox.count("UNREAD", "$.params.FROM", call);
    int read = inbox.count("READ", "$.params.FROM", call);
    return (store + unread + read) > 0;
}

int MainWindow::addCommandToMyInbox(CommandDetail d){
    // local cache for inbox count
    m_rxInboxCountCache[d.from] = m_rxInboxCountCache.value(d.from, 0) + 1;

    // add it to my unread inbox
    return addCommandToStorage("UNREAD", d);
}

int MainWindow::addCommandToStorage(QString type, CommandDetail d){
    // inbox:
    auto inbox = Inbox(inboxPath());
    if(!inbox.open()){
        return -1;
    }

    QVariantMap v = {
        {"UTC", QVariant(d.utcTimestamp.toString("yyyy-MM-dd hh:mm:ss"))},
        {"TO", QVariant(d.to)},
        {"FROM", QVariant(d.from)},
        {"PATH", QVariant(d.relayPath)},
        {"TDRIFT", QVariant(d.tdrift)},
        {"FREQ", QVariant(d.dial+d.offset)},
        {"DIAL", QVariant(d.dial)},
        {"OFFSET", QVariant(d.offset)},
        {"CMD", QVariant(d.cmd)},
        {"SNR", QVariant(d.snr)},
        {"SUBMODE", QVariant(d.submode)},
    };

    if(!d.grid.isEmpty()){
        v["GRID"] = QVariant(d.grid);
    }

    if(!d.extra.isEmpty()){
        v["EXTRA"] = QVariant(d.extra);
    }

    if(!d.text.isEmpty()){
        v["TEXT"] = QVariant(d.text);
    }

    auto m = Message(type, "", v);

    return inbox.append(m);
}

int MainWindow::getNextMessageIdForCallsign(QString callsign){
    auto inbox = Inbox(inboxPath());
    if(!inbox.open()){
        return -1;
    }

    auto v1 = inbox.values("STORE", "$.params.TO", callsign, 0, 10);
    foreach(auto pair, v1){
        auto params = pair.second.params();
        auto text = params.value("TEXT").toString().trimmed();
        if(!text.isEmpty()){
            return pair.first;
        }
    }

    auto v2 = inbox.values("STORE", "$.params.TO", Radio::base_callsign(callsign), 0, 10);
    foreach(auto pair, v2){
        auto params = pair.second.params();
        auto text = params.value("TEXT").toString().trimmed();
        if(!text.isEmpty()){
            return pair.first;
        }
    }

    return -1;
}

QStringList MainWindow::parseRelayPathCallsigns(QString from, QString text){
    QStringList calls;
    QString callDePattern = {R"(\s([*]DE[*]|VIA)\s(?<callsign>\b(?<prefix>[A-Z0-9]{1,4}\/)?(?<base>([0-9A-Z])?([0-9A-Z])([0-9])([A-Z])?([A-Z])?([A-Z])?)(?<suffix>\/[A-Z0-9]{1,4})?)\b)"};
    QRegularExpression re(callDePattern);
    auto iter = re.globalMatch(text);
    while(iter.hasNext()){
        auto match = iter.next();
        calls.prepend(match.captured("callsign"));
    }
    calls.prepend(from);
    return calls;
}


void MainWindow::processSpots() {
    if(!m_config.spot_to_reporting_networks()){
        m_rxCallQueue.clear();
        return;
    }

    if(m_rxCallQueue.isEmpty()){
        return;
    }

    // Is it ok to post spots to PSKReporter?
    int nsec = DriftingDateTime::currentSecsSinceEpoch() - m_secBandChanged;
    bool okToPost = (nsec > (4 * m_TRperiod) / 5);
    if (!okToPost) {
        return;
    }

    while(!m_rxCallQueue.isEmpty()){
        CallDetail d = m_rxCallQueue.dequeue();
        if(d.call.isEmpty()){
            continue;
        }

        if(m_config.spot_blacklist().contains(d.call) || m_config.spot_blacklist().contains(Radio::base_callsign(d.call))){
            continue;
        }

        qDebug() << "spotting call to reporting networks" << d.call << d.snr << d.dial << d.offset;

        spotReport(d.submode, d.dial, d.offset, d.snr, d.call, d.grid);
        pskLogReport("JS8", d.dial, d.offset, d.snr, d.call, d.grid);

        if(canSendNetworkMessage()){
            sendNetworkMessage("RX.SPOT", "", {
                {"_ID", QVariant(-1)},
                {"FREQ", QVariant(d.dial+d.offset)},
                {"DIAL", QVariant(d.dial)},
                {"OFFSET", QVariant(d.offset)},
                {"CALL", QVariant(d.call)},
                {"SNR", QVariant(d.snr)},
                {"GRID", QVariant(d.grid)},
            });
        }
    }
}

void MainWindow::processTxQueue(){
#if IDLE_BLOCKS_TX
    if(m_tx_watchdog){
        return;
    }
#endif

    if(m_txMessageQueue.isEmpty()){
        return;
    }

    // grab the next message...
    auto head = m_txMessageQueue.head();

    // decide if it's ok to transmit...
    int f = head.offset;
    if(f == -1){
        f = freq();
    }

    // we need a valid frequency...
    if(f <= 0){
        return;
    }

    // tx frame queue needs to be empty...
    if(!m_txFrameQueue.isEmpty()){
        return;
    }

    // our message box needs to be empty...
    if(!ui->extFreeTextMsgEdit->toPlainText().isEmpty()){
        return;
    }

    // and if we are a low priority message, we need to have not transmitted in the past 30 seconds...
    if(head.priority <= PriorityLow && m_lastTxStartTime.secsTo(DriftingDateTime::currentDateTimeUtc()) <= 30){
        return;
    }

    // if so... dequeue the next message from the queue...
    auto message = m_txMessageQueue.dequeue();

    // add the message to the outgoing message text box
    addMessageText(message.message, true);

    // check to see if this is a high priority message, or if we have autoreply enabled, or if this is a ping and the ping button is enabled
    if(message.priority >= PriorityHigh          ||
       message.message.contains(" HEARTBEAT ")   ||
       message.message.contains(" HB ")          ||
       message.message.contains(" ACK ")         ||
       ui->actionModeAutoreply->isChecked()
    ){
        // then try to set the frequency...
        setFreqOffsetForRestore(f, true);

        // then prepare to transmit...
        toggleTx(true);
    }

    if(message.callback){
        message.callback();
    }
}

void MainWindow::displayActivity(bool force) {
    if (!m_rxDisplayDirty && !force) {
        return;
    }

    // Band Activity
    displayBandActivity();

    // Call Activity
    displayCallActivity();

    m_rxDisplayDirty = false;
}

// updateBandActivity
void MainWindow::displayBandActivity() {
    auto now = DriftingDateTime::currentDateTimeUtc();

    ui->tableWidgetRXAll->setFont(m_config.table_font());

    // Selected Offset
    int selectedOffset = -1;
    auto selectedItems = ui->tableWidgetRXAll->selectedItems();
    if (!selectedItems.isEmpty()) {
        selectedOffset = selectedItems.first()->data(Qt::UserRole).toInt();
    }

    ui->tableWidgetRXAll->setUpdatesEnabled(false);
    {
        // Scroll Position
        auto const currentScrollPos = ui->tableWidgetRXAll->verticalScrollBar()->value();

        // Clear the table
        ui->tableWidgetRXAll->setRowCount(0);

        // Sort!
        auto const sort = getSortByReverse("bandActivity", "offset");
        auto       keys = m_bandActivity.keys();

        // Base comparison, called by the detail comparisons. We may not need
        // to proceed to the detail comparison at all here, if at least one of
        // the lists is empty. If both are non-empty, delegate to the detail
        // comparison, providing it with the last list elements.

        auto const compare = [this](int const lhsKey,
                                    int const rhsKey,
                                    auto   && detail)
        {
          auto const & lhs = m_bandActivity[lhsKey];
          auto const & rhs = m_bandActivity[rhsKey];

          if (lhs.isEmpty()) return false;
          if (rhs.isEmpty()) return true;

          return detail(lhs.last(),
                        rhs.last());
        };

        // Time stamp comparison, easy stuff, just a total ordering on the
        // UTC time stamp field.

        auto const compareTimestamp = [compare](int const lhsKey,
                                                int const rhsKey)
        {
          return compare(lhsKey,
                         rhsKey,
                         [](auto && lhs,
                            auto && rhs)
          {
            return lhs.utcTimestamp <
                   rhs.utcTimestamp;
          });
        };

        // SNR comparison;  we always want insane SNR values to be at the end
        // of the list and the list is going to be reversed if reverse is set,
        // so we want to set things up so that insane elements are either all
        // at the beginning in the case of a reverse, or all at the end in the
        // standard case. Reverse takes care of itself; we just need to sort
        // out standard.

        auto const compareSNR = [compare,
                                 reverse = sort.reverse](int const lhsKey,
                                                         int const rhsKey)
        {
          return compare(lhsKey,
                         rhsKey,
                         [reverse](auto && lhs,
                                   auto && rhs)
          {
            auto lhsSNR = lhs.snr;
            auto rhsSNR = rhs.snr;

            if (!reverse)
            {
              if (lhsSNR < -60 || lhsSNR > 60) lhsSNR = -lhsSNR;
              if (rhsSNR < -60 || rhsSNR > 60) rhsSNR = -rhsSNR;
            }

            return lhsSNR < rhsSNR;
          });
        };

        // Submode comparison; slow mode isn't at the start of the enumeration;
        // it's in the middle of it. All the other modes are in the expected order.

        auto const compareSubmode = [compare](int const lhsKey,
                                              int const rhsKey)
        {
          return compare(lhsKey,
                         rhsKey,
                         [](auto && lhs,
                            auto && rhs)
          {
            auto lhsSubmode = lhs.submode;
            auto rhsSubmode = rhs.submode;

            if (lhsSubmode == Varicode::JS8CallSlow) lhsSubmode = -lhsSubmode;
            if (rhsSubmode == Varicode::JS8CallSlow) rhsSubmode = -rhsSubmode;

            return lhsSubmode < rhsSubmode;
          });
        };

        // Always perform an initial sort by offset.

        std::stable_sort(keys.begin(), keys.end());

        // If something other than offset was requested as the sort by, perform an
        // additional stable sort by the field requested.

        if      (sort.by == "timestamp") std::stable_sort(keys.begin(), keys.end(), compareTimestamp);
        else if (sort.by == "snr")       std::stable_sort(keys.begin(), keys.end(), compareSNR);
        else if (sort.by == "submode")   std::stable_sort(keys.begin(), keys.end(), compareSubmode);

        // The sort comparators leave things in forward order. If a reverse sort
        // was requested, reverse the keys.

        if (sort.reverse) std::reverse(keys.begin(), keys.end());

        // Build the table
        foreach(int offset, keys) {
            bool isOffsetSelected = (offset == selectedOffset);

            QList < ActivityDetail > items = m_bandActivity[offset];
            if (items.length() > 0) {
                QDateTime timestamp;
                QStringList text;
                QString age;
                int snr = 0;
                float tdrift = 0;
                int submode = -1;

                int activityAging = m_config.activity_aging();

                // hide items that shouldn't appear

                for(int i = 0; i < items.length(); i++){
                    auto item = items[i];

                    bool shouldDisplay = true;

                    // hide aged items
                    if (!isOffsetSelected && activityAging && item.utcTimestamp.secsTo(now) / 60 >= activityAging) {
                        shouldDisplay = false;
                    }

                    // hide heartbeat items
                    if (!ui->actionShow_Band_Heartbeats_and_ACKs->isChecked()){
                        // hide heartbeats and acks if we have heartbeating hidden
                        if(item.text.contains(" @HB ") || item.text.contains(" HEARTBEAT ")){
                            shouldDisplay = false;

                            // hide the previous item if this it shouldn't be displayed either...
                            if(i > 0 && items[i-1].shouldDisplay && items[i-1].text.endsWith(": ")){
                                items[i-1].shouldDisplay = false;
                            }
                        }

                        // if our previous item should not be displayed (or this is the first frame) and we have a MSG ID, then don't display it either.
                        if(
                           (i == 0 || (i > 0 && !items[i-1].shouldDisplay)) &&
                           (item.text.contains(" MSG ID "))
                        ){
                            shouldDisplay = false;
                        }
                    }

                    // hide empty items
                    if (item.text.isEmpty()) {
                        shouldDisplay = false;
                    }

                    // set the visibility of the item
                    items[i].shouldDisplay = shouldDisplay;
                }

                // show the items that should appear
                foreach(ActivityDetail item, items) {
                    if(!item.shouldDisplay){
                        continue;
                    }

                    if (item.isLowConfidence) {
                        item.text = QString("[%1]").arg(item.text);
                    }

                    if ((item.bits & Varicode::JS8CallLast) == Varicode::JS8CallLast) {
                        // append the eot character to the text
                        item.text = QString("%1 %2 ").arg(Varicode::rstrip(item.text)).arg(m_config.eot());
                    }
                    text.append(item.text);
                    snr = item.snr;
                    age = since(item.utcTimestamp);
                    timestamp = item.utcTimestamp;
                    tdrift = item.tdrift;
                    submode = item.submode;
                }

                auto joined = Varicode::rstrip(text.join(""));
                if (joined.isEmpty()) {
                    continue;
                }

                ui->tableWidgetRXAll->insertRow(ui->tableWidgetRXAll->rowCount());
                int row = ui->tableWidgetRXAll->rowCount() - 1;
                int col = 0;

                auto offsetItem = new QTableWidgetItem(QString("%1 Hz").arg(offset));
                offsetItem->setData(Qt::UserRole, QVariant(offset));
                offsetItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                ui->tableWidgetRXAll->setItem(row, col++, offsetItem);

                auto ageItem = new QTableWidgetItem(age);
                ageItem->setTextAlignment(Qt::AlignCenter);
                ageItem->setToolTip(timestamp.toString());
                ui->tableWidgetRXAll->setItem(row, col++, ageItem);

                auto snrText = Varicode::formatSNR(snr);
                auto snrItem = new QTableWidgetItem(snrText.isEmpty() ? "" : QString("%1 dB").arg(snrText));
                snrItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                ui->tableWidgetRXAll->setItem(row, col++, snrItem);

                auto tdriftItem = new QTableWidgetItem(QString("%1 ms").arg((int)(1000*tdrift)));
                tdriftItem->setData(Qt::UserRole, QVariant(tdrift));
                tdriftItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                ui->tableWidgetRXAll->setItem(row, col++, tdriftItem);

                auto name = JS8::Submode::name(submode);
                auto submodeItem = new QTableWidgetItem(name.left(1).replace("H", "N"));
                submodeItem->setToolTip(name);
                submodeItem->setData(Qt::UserRole, QVariant(name));
                submodeItem->setTextAlignment(Qt::AlignCenter);
                ui->tableWidgetRXAll->setItem(row, col++, submodeItem);

                // align right if eliding...
                int colWidth = ui->tableWidgetRXAll->columnWidth(3);
                auto textItem = new QTableWidgetItem(joined);
                auto html = QString("<qt/>%1").arg(joined.toHtmlEscaped());
                html = html.replace(m_config.eot(), m_config.eot() + "<br/><br/>");
                html = html.replace(QRegularExpression("([<]br[/][>])+$"), "");
                textItem->setToolTip(html);

                QFontMetrics fm(textItem->font());
                auto elidedText = fm.elidedText(joined, Qt::ElideLeft, colWidth);
                auto flag = Qt::AlignLeft | Qt::AlignVCenter;
                if (elidedText != joined) {
                    flag = Qt::AlignRight | Qt::AlignVCenter;
                    textItem->setText(joined);
                }
                textItem->setTextAlignment(flag);

                ui->tableWidgetRXAll->setItem(row, col++, textItem);

                if (isOffsetSelected) {
                    for(int i = 0; i < ui->tableWidgetRXAll->columnCount(); i++){
                        ui->tableWidgetRXAll->item(row, i)->setSelected(true);
                    }
                }

                bool isDirectedAllCall = false;
                if(
                    (isDirectedOffset(offset, &isDirectedAllCall) && !isDirectedAllCall) || isMyCallIncluded(text.last())
                ){
                    for(int i = 0; i < ui->tableWidgetRXAll->columnCount(); i++){
                        ui->tableWidgetRXAll->item(row, i)->setBackground(QBrush(m_config.color_MyCall()));
                    }
                }

                if(!text.isEmpty()){
                    auto const    list = joined.split(QRegularExpression("[:> ]"), Qt::SkipEmptyParts);
                    QSet<QString> words(list.begin(), list.end());

                    if(words.contains("CQ")){
                        for(int i = 0; i < ui->tableWidgetRXAll->columnCount(); i++){
                            ui->tableWidgetRXAll->item(row, i)->setBackground(QBrush(m_config.color_CQ()));
                        }
                    }

                    auto matchingSecondaryWords = m_config.secondary_highlight_words() & words;
                    if (!matchingSecondaryWords.isEmpty()){
                        for(int i = 0; i < ui->tableWidgetRXAll->columnCount(); i++){
                            ui->tableWidgetRXAll->item(row, i)->setBackground(QBrush(m_config.color_secondary_highlight()));
                        }
                    }

                    auto matchingPrimaryWords = m_config.primary_highlight_words() & words;
                    if (!matchingPrimaryWords.isEmpty()){
                        for(int i = 0; i < ui->tableWidgetRXAll->columnCount(); i++){
                            ui->tableWidgetRXAll->item(row, i)->setBackground(QBrush(m_config.color_primary_highlight()));
                        }
                    }
                }
            }
        }

        // Set table color
        auto style = QString("QTableWidget { background:%1; selection-background-color:%2; alternate-background-color:%1; color:%3; } "
                             "QTableWidget::item:selected { background-color: %2; color: %3; }");
        style = style.arg(m_config.color_table_background().name());
        style = style.arg(m_config.color_table_highlight().name());
        style = style.arg(m_config.color_table_foreground().name());
        ui->tableWidgetRXAll->setStyleSheet(style);

        // Set the table palette for inactive selected row
        auto p = ui->tableWidgetRXAll->palette();

        p.setColor(QPalette::Highlight, m_config.color_table_highlight());
        p.setColor(QPalette::HighlightedText, m_config.color_table_foreground());
        p.setColor(QPalette::Inactive, QPalette::Highlight, p.color(QPalette::Active, QPalette::Highlight));
        ui->tableWidgetRXAll->setPalette(p);

        // Set item fonts
        for(int row = 0; row < ui->tableWidgetRXAll->rowCount(); row++){
            for(int col = 0; col < ui->tableWidgetRXAll->columnCount(); col++){
                auto item = ui->tableWidgetRXAll->item(row, col);
                if(item){
                    item->setFont(m_config.table_font());
                }
            }
        }

        // Column labels
        ui->tableWidgetRXAll->horizontalHeader()->setVisible(showColumn("band", "labels"));

        // Hide columns
        ui->tableWidgetRXAll->setColumnHidden(0, !showColumn("band", "offset"));
        ui->tableWidgetRXAll->setColumnHidden(1, !showColumn("band", "timestamp"));
        ui->tableWidgetRXAll->setColumnHidden(2, !showColumn("band", "snr"));
        ui->tableWidgetRXAll->setColumnHidden(3, !showColumn("band", "tdrift", false));
        ui->tableWidgetRXAll->setColumnHidden(4, !showColumn("band", "submode", false));

        // Resize the table columns
        ui->tableWidgetRXAll->resizeColumnToContents(0);
        ui->tableWidgetRXAll->resizeColumnToContents(1);
        ui->tableWidgetRXAll->resizeColumnToContents(2);
        ui->tableWidgetRXAll->resizeColumnToContents(3);
        ui->tableWidgetRXAll->resizeColumnToContents(4);

        // Reset the scroll position
        ui->tableWidgetRXAll->verticalScrollBar()->setValue(currentScrollPos);
    }
    ui->tableWidgetRXAll->setUpdatesEnabled(true);
}

// updateCallActivity
void MainWindow::displayCallActivity() {
    auto now = DriftingDateTime::currentDateTimeUtc();

    ui->tableWidgetCalls->setFont(m_config.table_font());

    // Selected callsign
    QString selectedCall = callsignSelected();

    auto currentScrollPos = ui->tableWidgetCalls->verticalScrollBar()->value();

    ui->tableWidgetCalls->setUpdatesEnabled(false);
    {
        // Clear the table
        ui->tableWidgetCalls->setRowCount(0);
        createGroupCallsignTableRows(ui->tableWidgetCalls, selectedCall); // isAllCallIncluded(selectedCall)); // || isGroupCallIncluded(selectedCall));

        // Build the table

        auto const sort = getSortByReverse("callActivity", "callsign");
        auto       keys = m_callActivity.keys();

        auto const compareOffset = [this](QString const & lhsKey,
                                          QString const & rhsKey)
        {
            return m_callActivity[lhsKey].offset <
                   m_callActivity[rhsKey].offset;
        };

        auto const compareDistance = [this,
                                      reverse = sort.reverse,
                                      my_grid = m_config.my_grid(),
                                      miles   = m_config.miles()](QString const & lhsKey,
                                                                  QString const & rhsKey)
        {
          auto const lhs = Distance(my_grid, m_callActivity[lhsKey].grid, miles);
          auto const rhs = Distance(my_grid, m_callActivity[rhsKey].grid, miles);

          // We always want invalid distances to be at the end of the list,
          // and the list is going to be reversed if reverse is set, so we
          // want to set things up so that invalid elements are either all
          // at the beginning in the case of a reverse, or all at the end
          // in the standard case.

          if      (!lhs) return  reverse && rhs;
          else if (!rhs) return !reverse;
          else           return lhs < rhs;
        };

        auto const compareTimestamp = [this](QString const & lhsKey,
                                             QString const & rhsKey)
        {
          return m_callActivity[lhsKey].utcTimestamp <
                 m_callActivity[rhsKey].utcTimestamp;
        };

        auto const compareAckTimestamp = [this](QString const & lhsKey,
                                                QString const & rhsKey)
        {
          return m_callActivity[rhsKey].ackTimestamp <
                 m_callActivity[lhsKey].ackTimestamp;
        };

        auto const compareSNR = [this,
                                 reverse = sort.reverse](QString const & lhsKey,
                                                         QString const & rhsKey)
        {
          auto lhs = m_callActivity[lhsKey].snr;
          auto rhs = m_callActivity[rhsKey].snr;

          // We always want insane SNR values to be at the end of the list,
          // and the list is going to be reversed if reverse is set, so we
          // want to set things up so that insane elements are either all
          // at the beginning in the case of a reverse, or all at the end
          // in the standard case. Reverse takes care of itself; we just
          // need to sort out standard.

          if (!reverse)
          {
            if (lhs < -60 || lhs > 60) lhs = -lhs;
            if (rhs < -60 || rhs > 60) rhs = -rhs;
          }

          return lhs < rhs;
        };

        auto const compareSubmode = [this](QString const & lhsKey,
                                           QString const & rhsKey)
        {
          auto lhs = m_callActivity[lhsKey].submode;
          auto rhs = m_callActivity[rhsKey].submode;

          // Slow mode isn't at the start of the enumeration; it's in the
          // middle of it. All the other modes are in the expected order.

          if (lhs == Varicode::JS8CallSlow) lhs = -lhs;
          if (rhs == Varicode::JS8CallSlow) rhs = -rhs;

          return lhs < rhs;
        };

        // Always perform an initial sort by callsign.

        std::stable_sort(keys.begin(), keys.end());

        // If something other than callsign was requested as the sort by, perform an
        // additional stable sort by the field requested.

        if      (sort.by == "offset")       std::stable_sort(keys.begin(), keys.end(), compareOffset);
        else if (sort.by == "distance")     std::stable_sort(keys.begin(), keys.end(), compareDistance);
        else if (sort.by == "timestamp")    std::stable_sort(keys.begin(), keys.end(), compareTimestamp);
        else if (sort.by == "ackTimestamp") std::stable_sort(keys.begin(), keys.end(), compareAckTimestamp);
        else if (sort.by == "snr")          std::stable_sort(keys.begin(), keys.end(), compareSNR);
        else if (sort.by == "submode")      std::stable_sort(keys.begin(), keys.end(), compareSubmode);

        // The sort comparators leave things in forward order. If a reverse sort
        // was requested, reverse the keys.

        if (sort.reverse) std::reverse(keys.begin(), keys.end());

        // pin messages to the top
        std::stable_sort(keys.begin(), keys.end(), [this](QString const & lhsKey,
                                                          QString const & rhsKey)
        {
          auto const lhs = (int)!(m_rxInboxCountCache.value(lhsKey, 0) > 0);
          auto const rhs = (int)!(m_rxInboxCountCache.value(rhsKey, 0) > 0);

          return lhs < rhs;
        });

        bool showIconColumn = false;

        int callsignAging = m_config.callsign_aging();
        foreach(QString call, keys) {
            if(call.trimmed().isEmpty()){
                continue;
            }

            CallDetail d = m_callActivity[call];
            if(d.call.trimmed().isEmpty()){
                continue;
            }

            bool isCallSelected = (call == selectedCall);

            // icon flags (flag -> star -> empty)
            bool hasMessage = m_rxInboxCountCache.value(d.call, 0) > 0;

            // display telephone icon if called cq in the past 5 minutes
            bool hasCQ = d.cqTimestamp.isValid() && d.cqTimestamp.secsTo(now) / 60 < 5;

            // display star if they've acked a message from us
            bool hasACK = d.ackTimestamp.isValid();

            if (!isCallSelected && !hasMessage && callsignAging && d.utcTimestamp.secsTo(now) / 60 >= callsignAging) {
                continue;
            }

            ui->tableWidgetCalls->insertRow(ui->tableWidgetCalls->rowCount());
            int row = ui->tableWidgetCalls->rowCount() - 1;
            int col = 0;

#if SHOW_THROUGH_CALLS
            QString displayCall = d.through.isEmpty() ? d.call : QString("%1>%2").arg(d.through).arg(d.call);
#else
            QString displayCall = d.call;
#endif
            bool hasThrough = !d.through.isEmpty();

            auto iconItem = new QTableWidgetItem(hasMessage ? "\u2691" : hasACK ? "\u2605" : hasCQ ? "\u260E" : hasThrough ? "\u269F" : "");
            iconItem->setData(Qt::UserRole, QVariant(d.call));
            iconItem->setToolTip(
                hasMessage ? "Message Available" :
                hasACK ? QString("Hearing Your Station (%1)").arg(since(d.ackTimestamp)) :
                hasCQ ? QString("Calling CQ (%1)").arg(since(d.cqTimestamp)) :
                hasThrough ? QString("Heard Through Relay (%1)").arg(d.through) :
                "");
            iconItem->setTextAlignment(Qt::AlignCenter);
            ui->tableWidgetCalls->setItem(row, col++, iconItem);
            if(hasMessage || hasACK || hasCQ || hasThrough){
                showIconColumn = true;
            }

            auto displayItem = new QTableWidgetItem(displayCall);
            displayItem->setData(Qt::UserRole, QVariant(d.call));
            displayItem->setToolTip(generateCallDetail(displayCall));
            ui->tableWidgetCalls->setItem(row, col++, displayItem);

#if ONLY_SHOW_HEARD_CALLSIGNS
            if(d.utcTimestamp.isValid()){
#else
            if(true){
#endif
                auto ageItem = new QTableWidgetItem(since(d.utcTimestamp));
                ageItem->setTextAlignment(Qt::AlignCenter);
                ageItem->setToolTip(d.utcTimestamp.toString());
                ui->tableWidgetCalls->setItem(row, col++, ageItem);

                auto snrText = Varicode::formatSNR(d.snr);
                auto snrItem = new QTableWidgetItem(snrText.isEmpty() ? "" : QString("%1 dB").arg(snrText));
                snrItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                ui->tableWidgetCalls->setItem(row, col++, snrItem);

                auto offsetItem = new QTableWidgetItem(QString("%1 Hz").arg(d.offset));
                offsetItem->setData(Qt::UserRole, QVariant(d.offset));
                offsetItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                ui->tableWidgetCalls->setItem(row, col++, offsetItem);

                auto tdriftItem = new QTableWidgetItem(QString("%1 ms").arg((int)(1000*d.tdrift)));
                tdriftItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                ui->tableWidgetCalls->setItem(row, col++, tdriftItem);

                auto name = JS8::Submode::name(d.submode);
                auto modeItem = new QTableWidgetItem(name.left(1).replace("H", "N"));
                modeItem->setToolTip(name);
                modeItem->setData(Qt::UserRole, QVariant(name));
                modeItem->setTextAlignment(Qt::AlignCenter);
                ui->tableWidgetCalls->setItem(row, col++, modeItem);

                auto gridItem = new QTableWidgetItem(QString("%1").arg(d.grid.trimmed().left(4)));
                gridItem->setToolTip(d.grid.trimmed());
                ui->tableWidgetCalls->setItem(row, col++, gridItem);

                auto distanceItem = new QTableWidgetItem(Distance(m_config.my_grid(), d.grid, m_config.miles()).toString());
                distanceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                ui->tableWidgetCalls->setItem(row, col++, distanceItem);

                QString flag;
                if(m_logBook.hasWorkedBefore(d.call, "")){
                    // unicode checkmark
                    flag = "\u2713";
                }
                auto workedBeforeItem = new QTableWidgetItem(flag);
                workedBeforeItem->setTextAlignment(Qt::AlignCenter);
                ui->tableWidgetCalls->setItem(row, col++, workedBeforeItem);

                QString logDetailGrid;
                QString logDetailDate;
                QString logDetailName;
                QString logDetailComment;
                bool gridItemEmpty = gridItem->text().isEmpty();

                if((gridItemEmpty && showColumn("call", "grid")) || showColumn("call", "log") || showColumn("call", "logName") || showColumn("call", "logComment")){
                    m_logBook.findCallDetails(d.call, logDetailGrid, logDetailDate, logDetailName, logDetailComment);
                }

                if(gridItemEmpty && !logDetailGrid.isEmpty()){
                    gridItem->setText(logDetailGrid.trimmed().left(4));
                    gridItem->setToolTip(logDetailGrid.trimmed());
                    distanceItem->setText(Distance(m_config.my_grid(), logDetailGrid, m_config.miles()).toString());

                    // update the call activity cache with the loaded grid
                    if(m_callActivity.contains(d.call)){
                        m_callActivity[call].grid = logDetailGrid.trimmed();
                    }
                }

                if(!logDetailDate.isEmpty()){
                    auto lastLogged = QDate::fromString(logDetailDate, "yyyyMMdd");

                    workedBeforeItem->setToolTip(QString("Last Logged: %1").arg(lastLogged.toString()));
                }

                auto logNameItem = new QTableWidgetItem(logDetailName);
                logNameItem->setTextAlignment(Qt::AlignCenter);
                logNameItem->setToolTip(logDetailName);
                ui->tableWidgetCalls->setItem(row, col++, logNameItem);

                auto logCommentItem = new QTableWidgetItem(logDetailComment);
                logCommentItem->setTextAlignment(Qt::AlignCenter);
                logCommentItem->setToolTip(logDetailComment);
                ui->tableWidgetCalls->setItem(row, col++, logCommentItem);

            } else {
                ui->tableWidgetCalls->setItem(row, col++, new QTableWidgetItem("")); // age
                ui->tableWidgetCalls->setItem(row, col++, new QTableWidgetItem("")); // snr
                ui->tableWidgetCalls->setItem(row, col++, new QTableWidgetItem("")); // freq
                ui->tableWidgetCalls->setItem(row, col++, new QTableWidgetItem("")); // tdrift
                ui->tableWidgetCalls->setItem(row, col++, new QTableWidgetItem("")); // mode
                ui->tableWidgetCalls->setItem(row, col++, new QTableWidgetItem("")); // grid
                ui->tableWidgetCalls->setItem(row, col++, new QTableWidgetItem("")); // distance
                ui->tableWidgetCalls->setItem(row, col++, new QTableWidgetItem("")); // worked before
                ui->tableWidgetCalls->setItem(row, col++, new QTableWidgetItem("")); // log name
                ui->tableWidgetCalls->setItem(row, col++, new QTableWidgetItem("")); // log comment
            }

            if (isCallSelected) {
                for(int i = 0; i < ui->tableWidgetCalls->columnCount(); i++){
                    ui->tableWidgetCalls->item(row, i)->setSelected(true);
                }
            }

            if(hasCQ){
                for(int i = 0; i < ui->tableWidgetCalls->columnCount(); i++){
                    ui->tableWidgetCalls->item(row, i)->setBackground(QBrush(m_config.color_CQ()));
                }
            }

            if (m_config.secondary_highlight_words().contains(call)){
                for(int i = 0; i < ui->tableWidgetCalls->columnCount(); i++){
                    ui->tableWidgetCalls->item(row, i)->setBackground(QBrush(m_config.color_secondary_highlight()));
                }
            }

            if (m_config.primary_highlight_words().contains(call)){
                for(int i = 0; i < ui->tableWidgetCalls->columnCount(); i++){
                    ui->tableWidgetCalls->item(row, i)->setBackground(QBrush(m_config.color_primary_highlight()));
                }
            }
        }

        // Set table color
        auto style = QString("QTableWidget { background:%1; selection-background-color:%2; alternate-background-color:%1; color:%3; } "
                             "QTableWidget::item:selected { background-color: %2; color: %3; }");
        style = style.arg(m_config.color_table_background().name());
        style = style.arg(m_config.color_table_highlight().name());
        style = style.arg(m_config.color_table_foreground().name());
        ui->tableWidgetCalls->setStyleSheet(style);

        // Set the table palette for inactive selected row
        auto p = ui->tableWidgetCalls->palette();
        p.setColor(QPalette::Highlight, m_config.color_table_highlight());
        p.setColor(QPalette::HighlightedText, m_config.color_table_foreground());
        p.setColor(QPalette::Inactive, QPalette::Highlight, p.color(QPalette::Active, QPalette::Highlight));
        ui->tableWidgetCalls->setPalette(p);

        // Set item fonts
        for(int row = 0; row < ui->tableWidgetCalls->rowCount(); row++){
            auto bold = ui->tableWidgetCalls->item(row,  0)->text() == "\u2691";
            for(int col = 0; col < ui->tableWidgetCalls->columnCount(); col++){
                auto item = ui->tableWidgetCalls->item(row, col);
                if(item){
                    auto f = m_config.table_font();
                    if(bold){
                        f.setBold(true);
                    }
                    item->setFont(f);
                }
            }
        }

        // Column labels
        ui->tableWidgetCalls->horizontalHeader()->setVisible(showColumn("call", "labels"));

        // Hide columns
        ui->tableWidgetCalls->setColumnHidden(0, !showIconColumn);
        ui->tableWidgetCalls->setColumnHidden(1, !showColumn("call", "callsign"));
        ui->tableWidgetCalls->setColumnHidden(2, !showColumn("call", "timestamp"));
        ui->tableWidgetCalls->setColumnHidden(3, !showColumn("call", "snr"));
        ui->tableWidgetCalls->setColumnHidden(4, !showColumn("call", "offset"));
        ui->tableWidgetCalls->setColumnHidden(5, !showColumn("call", "tdrift", false));
        ui->tableWidgetCalls->setColumnHidden(6, !showColumn("call", "submode", false));
        ui->tableWidgetCalls->setColumnHidden(7, !showColumn("call", "grid", false));
        ui->tableWidgetCalls->setColumnHidden(8, !showColumn("call", "distance", false));
        ui->tableWidgetCalls->setColumnHidden(9, !showColumn("call", "log"));
        ui->tableWidgetCalls->setColumnHidden(10, !showColumn("call", "logName"));
        ui->tableWidgetCalls->setColumnHidden(11, !showColumn("call", "logComment"));

        // Resize the table columns
        ui->tableWidgetCalls->resizeColumnToContents(0);
        ui->tableWidgetCalls->resizeColumnToContents(1);
        ui->tableWidgetCalls->resizeColumnToContents(2);
        ui->tableWidgetCalls->resizeColumnToContents(3);
        ui->tableWidgetCalls->resizeColumnToContents(4);
        ui->tableWidgetCalls->resizeColumnToContents(5);
        ui->tableWidgetCalls->resizeColumnToContents(6);
        ui->tableWidgetCalls->resizeColumnToContents(7);
        ui->tableWidgetCalls->resizeColumnToContents(8);
        ui->tableWidgetCalls->resizeColumnToContents(9);
        ui->tableWidgetCalls->resizeColumnToContents(10);

        // Reset the scroll position
        ui->tableWidgetCalls->verticalScrollBar()->setValue(currentScrollPos);
    }
    ui->tableWidgetCalls->setUpdatesEnabled(true);
}

void MainWindow::emitPTT(bool on){
    qDebug() << "PTT:" << on;

    Q_EMIT m_config.transceiver_ptt(on);

    // emit to network
    sendNetworkMessage("RIG.PTT", on ? "on" : "off", {
        {"_ID", QVariant(-1)},
        {"PTT", QVariant(on)},
        {"UTC", QVariant(DriftingDateTime::currentDateTimeUtc().toMSecsSinceEpoch())},
    });
}

void MainWindow::emitTones(){
    if(!canSendNetworkMessage()){
        return;
    }

    // emit tone numbers to network
    QVariantList t;
    for(int i = 0; i < JS8_NUM_SYMBOLS; i++){
        //qDebug() << "tone" << i << "=" << itone[i];
        t.append(QVariant((int)itone[i]));
    }

    sendNetworkMessage("TX.FRAME", "", {
        {"_ID", QVariant(-1)},
        {"TONES", t}
    });
}

void MainWindow::udpNetworkMessage(Message const &message)
{
    if(!m_config.udpEnabled()){
        return;
    }

    if(!m_config.accept_udp_requests()){
        return;
    }

    networkMessage(message);
}

void MainWindow::tcpNetworkMessage(Message const &message)
{
    if(!m_config.tcpEnabled()){
        return;
    }

    if(!m_config.accept_tcp_requests()){
        return;
    }

    networkMessage(message);
}

void MainWindow::networkMessage(Message const &message)
{
    auto type = message.type();

    if(type == "PING"){
        return;
    }

    auto id = message.id();

    qDebug() << "try processing network message" << type << id;

    // Inspired by FLDigi
    // TODO: MAIN.RX - Turn on RX
    // TODO: MAIN.TX - Transmit
    // TODO: MAIN.PTT - PTT
    // TODO: MAIN.TUNE - Tune
    // TODO: MAIN.HALT - Halt
    // TODO: MAIN.AUTO - Auto
    // TODO: MAIN.SPOT - Spot
    // TODO: MAIN.HB - HB

    // RIG.GET_FREQ - Get the current Frequency
    // RIG.SET_FREQ - Set the current Frequency
    if(type == "RIG.GET_FREQ"){
        sendNetworkMessage("RIG.FREQ", "", {
            {"_ID", id},
            {"FREQ", QVariant((quint64)dialFrequency() + freq())},
            {"DIAL", QVariant((quint64)dialFrequency())},
            {"OFFSET", QVariant((quint64)freq())}
        });
        return;
    }

    if(type == "RIG.SET_FREQ"){
        auto params = message.params();
        if(params.contains("DIAL")){
            bool ok = false;
            auto f = params["DIAL"].toInt(&ok);
            if(ok){
                setRig(f);
                displayDialFrequency();
            }
        }
        if(params.contains("OFFSET")){
            bool ok = false;
            auto f = params["OFFSET"].toInt(&ok);
            if(ok){
                setFreqOffsetForRestore(f, false);
            }
        }
    }

    // STATION.GET_CALLSIGN - Get the current callsign
    // STATION.GET_GRID - Get the current grid locator
    // STATION.SET_GRID - Set the current grid locator
    // STATION.GET_INFO - Get the current station qth
    // STATION.SET_INFO - Set the current station qth
    if(type == "STATION.GET_CALLSIGN"){
        sendNetworkMessage("STATION.CALLSIGN", m_config.my_callsign(), {
            {"_ID", id},
        });
        return;
    }

    if(type == "STATION.GET_GRID"){
        sendNetworkMessage("STATION.GRID", m_config.my_grid(), {
            {"_ID", id},
        });
        return;
    }

    if(type == "STATION.SET_GRID"){
        m_config.set_dynamic_location(message.value());
        sendNetworkMessage("STATION.GRID", m_config.my_grid(), {
            {"_ID", id},
        });
        return;
    }

    if(type == "STATION.GET_INFO"){
        sendNetworkMessage("STATION.INFO", m_config.my_info(), {
            {"_ID", id},
        });
        return;
    }

    if(type == "STATION.SET_INFO"){
        m_config.set_dynamic_station_info(message.value());
        sendNetworkMessage("STATION.INFO", m_config.my_info(), {
            {"_ID", id},
        });
        return;
    }

    if(type == "STATION.GET_STATUS"){
        sendNetworkMessage("STATION.STATUS", m_config.my_status(), {
            {"_ID", id},
        });
        return;
    }

    if(type == "STATION.SET_STATUS"){
        m_config.set_dynamic_station_status(message.value());
        sendNetworkMessage("STATION.STATUS", m_config.my_status(), {
            {"_ID", id},
        });
        return;
    }

    // RX.GET_CALL_ACTIVITY
    // RX.GET_CALL_SELECTED
    // RX.GET_BAND_ACTIVITY
    // RX.GET_TEXT

    if(type == "RX.GET_CALL_ACTIVITY"){
        auto now = DriftingDateTime::currentDateTimeUtc();
        int callsignAging = m_config.callsign_aging();
        QVariantMap calls = {
            {"_ID", id},
        };

        foreach(auto cd, m_callActivity.values()){
            if (callsignAging && cd.utcTimestamp.secsTo(now) / 60 >= callsignAging) {
                continue;
            }
            QVariantMap detail;
            detail["SNR"] = QVariant(cd.snr);
            detail["GRID"] = QVariant(cd.grid);
            detail["UTC"] = QVariant(cd.utcTimestamp.toMSecsSinceEpoch());
            calls[cd.call] = QVariant(detail);
        }

        sendNetworkMessage("RX.CALL_ACTIVITY", "", calls);
        return;
    }

    if(type == "RX.GET_CALL_SELECTED"){
        sendNetworkMessage("RX.CALL_SELECTED", callsignSelected(), {
            {"_ID", id},
        });
        return;
    }

    if(type == "RX.GET_BAND_ACTIVITY"){
        QVariantMap offsets = {
            {"_ID", id},
        };
        for (auto const [offset, activity] : m_bandActivity.asKeyValueRange())
        {
            if (activity.isEmpty()) continue;

            auto const d = activity.last();

            offsets[QString("%1").arg(offset)] = QVariant(QVariantMap {
              { "FREQ",   QVariant(d.dial + d.offset)                  },
              { "DIAL",   QVariant(d.dial)                             },
              { "OFFSET", QVariant(d.offset)                           },
              { "TEXT",   QVariant(d.text)                             },
              { "SNR",    QVariant(d.snr)                              },
              { "UTC",    QVariant(d.utcTimestamp.toMSecsSinceEpoch()) }
            });
        }

        sendNetworkMessage("RX.BAND_ACTIVITY", "", offsets);
        return;
    }

    if(type == "RX.GET_TEXT"){
        sendNetworkMessage("RX.TEXT", ui->textEditRX->toPlainText().right(1024), {
            {"_ID", id},
        });
        return;
    }

    // TX.GET_TEXT
    // TX.SET_TEXT
    // TX.SEND_MESSAGE

    if(type == "TX.GET_TEXT"){
        sendNetworkMessage("TX.TEXT", ui->extFreeTextMsgEdit->toPlainText().right(1024), {
            {"_ID", id},
        });
        return;
    }

    if(type == "TX.SET_TEXT"){
        addMessageText(message.value(), true);
        sendNetworkMessage("TX.TEXT", ui->extFreeTextMsgEdit->toPlainText().right(1024), {
            {"_ID", id},
        });
        return;
    }

    if(type == "TX.SEND_MESSAGE"){
        auto text = message.value();
        if(!text.isEmpty()){
            enqueueMessage(PriorityNormal, text, -1, nullptr);
            processTxQueue();
            return;
        }
    }

    // MODE.GET_SPEED
    // MODE.SET_SPEED
    if(type == "MODE.GET_SPEED"){
        sendNetworkMessage("MODE.SPEED", "", {
            {"_ID", id},
            {"SPEED", m_nSubMode},
        });
        return;
    }

    if(type == "MODE.SET_SPEED"){
        bool ok = false;
        int speed = message.params().value("SPEED", QVariant(m_nSubMode)).toInt(&ok);
        if(ok){
            if(speed == Varicode::JS8CallNormal){
                ui->actionModeJS8Normal->setChecked(true);
            }
            if(speed == Varicode::JS8CallFast){
                ui->actionModeJS8Fast->setChecked(true);
            }
            if(speed == Varicode::JS8CallTurbo){
                ui->actionModeJS8Turbo->setChecked(true);
            }
            if(speed == Varicode::JS8CallSlow){
                ui->actionModeJS8Slow->setChecked(true);
            }
            if(speed == Varicode::JS8CallUltra){
                ui->actionModeJS8Ultra->setChecked(true);
            }
        }
        sendNetworkMessage("MODE.SPEED", "", {
            {"_ID", id},
            {"SPEED", m_nSubMode},
        });
        return;
    }

    // INBOX.GET_MESSAGES
    // INBOX.STORE_MESSAGE
    if(type == "INBOX.GET_MESSAGES"){
        QString selectedCall = message.params().value("CALLSIGN", "").toString();
        if(selectedCall.isEmpty()){
            selectedCall = "%";
        }

        Inbox inbox(inboxPath());
        if(!inbox.open()){
            return;
        }

        QList<QPair<int, Message> > msgs;
        msgs.append(inbox.values("STORE", "$.params.TO", selectedCall, 0, 1000));
        msgs.append(inbox.values("READ", "$.params.FROM", selectedCall, 0, 1000));
        foreach(auto pair, inbox.values("UNREAD", "$.params.FROM", selectedCall, 0, 1000)){
            msgs.append(pair);
        }
        std::stable_sort(msgs.begin(), msgs.end(), [](QPair<int, Message> const &a, QPair<int, Message> const &b){
            return QVariant::compare(a.second.params().value("UTC"),
                                     b.second.params().value("UTC")) == QPartialOrdering::Greater;
        });

        QVariantList l;
        foreach(auto pair, msgs){
            l << pair.second.toVariantMap();
        }

        sendNetworkMessage("INBOX.MESSAGES", "", {
            {"_ID", id},
            {"MESSAGES", l},
        });
        return;
    }

    if(type == "INBOX.STORE_MESSAGE"){
        QString selectedCall = message.params().value("CALLSIGN", "").toString();
        if(selectedCall.isEmpty()){
            return;
        }

        QString text = message.params().value("TEXT", "").toString();
        if(text.isEmpty()){
            return;
        }

        CommandDetail d = {};
        d.cmd = " MSG ";
        d.to = selectedCall;
        d.from = m_config.my_callsign();
        d.relayPath = d.from;
        d.text = text;
        d.utcTimestamp = DriftingDateTime::currentDateTimeUtc();
        d.submode = m_nSubMode;

        auto mid = addCommandToStorage("STORE", d);

        sendNetworkMessage("INBOX.MESSAGE", "", {
            {"_ID", id},
            {"ID", mid},
        });
        return;
    }

    // WINDOW.RAISE

    if(type == "WINDOW.RAISE"){
        setWindowState(Qt::WindowActive);
        activateWindow();
        raise();
        return;
    }

    qDebug() << "Unable to process networkMessage:" << type;
}

bool MainWindow::canSendNetworkMessage(){
    return m_config.udpEnabled() || m_config.tcpEnabled();
}

void MainWindow::sendNetworkMessage(QString const &type, QString const &message){
    if(!canSendNetworkMessage()){
        return;
    }

    auto m = Message(type, message);

    if(m_config.udpEnabled()){
        m_messageClient->send(m);
    }

    if(m_config.tcpEnabled()){
        m_messageServer->send(m);
    }
}

void MainWindow::sendNetworkMessage(QString const &type, QString const &message, QVariantMap const &params)
{
    if(!canSendNetworkMessage()){
        return;
    }

    auto m = Message(type, message, params);

    if(m_config.udpEnabled()){
        m_messageClient->send(m);
    }

    if(m_config.tcpEnabled()){
        m_messageServer->send(m);
    }
}

void
MainWindow::pskReporterError(QString const & message)
{
  qDebug() << "PSK Reporter Error:" << message;

  showStatusMessage (tr ("Spotting to PSK Reporter unavailable"));
}

void MainWindow::setRig (Frequency f)
{
  if (f)
  {
      m_freqNominal = f;
      m_freqTxNominal = m_freqNominal;
  }

  if(m_transmitting && !m_config.tx_qsy_allowed ()) return;

  if ((m_monitoring || m_transmitting) && m_config.transceiver_online ())
    {
      if (m_transmitting && m_config.split_mode ())
        {
          Q_EMIT m_config.transceiver_tx_frequency (m_freqTxNominal);
        }
      else
        {
          Q_EMIT m_config.transceiver_frequency (m_freqNominal);
        }
    }
}

void MainWindow::statusUpdate ()
{
    if(canSendNetworkMessage()){
        sendNetworkMessage("STATION.STATUS", "", {
            {"FREQ", QVariant(dialFrequency() + freq())},
            {"DIAL", QVariant(dialFrequency())},
            {"OFFSET", QVariant(freq())},
            {"SPEED", QVariant(m_nSubMode)},
            {"SELECTED", QVariant(callsignSelected())},
        });
    }
}

void MainWindow::childEvent (QChildEvent * e)
{
  if (e->child ()->isWidgetType ())
    {
      switch (e->type ())
        {
        case QEvent::ChildAdded: add_child_to_event_filter (e->child ()); break;
        case QEvent::ChildRemoved: remove_child_from_event_filter (e->child ()); break;
        default: break;
        }
    }
  QMainWindow::childEvent (e);
}

// add widget and any child widgets to our event filter so that we can
// take action on key press ad mouse press events anywhere in the main window
void MainWindow::add_child_to_event_filter (QObject * target)
{
  if (target && target->isWidgetType ())
    {
      target->installEventFilter (this);
    }
  auto const& children = target->children ();
  for (auto iter = children.begin (); iter != children.end (); ++iter)
    {
      add_child_to_event_filter (*iter);
    }
}

// recursively remove widget and any child widgets from our event filter
void MainWindow::remove_child_from_event_filter (QObject * target)
{
  auto const& children = target->children ();
  for (auto iter = children.begin (); iter != children.end (); ++iter)
    {
      remove_child_from_event_filter (*iter);
    }
  if (target && target->isWidgetType ())
    {
      target->removeEventFilter (this);
    }
}

void MainWindow::resetIdleTimer(){
    if(m_idleMinutes){
      m_idleMinutes = 0;
      qDebug() << "idle" << m_idleMinutes << "minutes";
    }
}

void MainWindow::incrementIdleTimer(){
    m_idleMinutes++;
    qDebug() << "increment idle to" << m_idleMinutes << "minutes";
}

void MainWindow::tx_watchdog (bool triggered)
{
  auto prior = m_tx_watchdog;
  m_tx_watchdog = triggered;
  if (triggered)
    {
      m_bTxTime=false;
      if (m_tune) stop_tuning ();
      if (m_auto) auto_tx_mode (false);
      stopTx();
      tx_status_label.setStyleSheet ("QLabel{background-color: #000000; color:#ffffff; }");
      tx_status_label.setText ("Idle timeout");

      // if the watchdog is triggered...we're no longer active
      bool wasAuto = ui->actionModeAutoreply->isChecked();
      bool wasHB = ui->hbMacroButton->isChecked();
      bool wasCQ = ui->cqMacroButton->isChecked();

      // save the button states
      ui->actionModeAutoreply->setChecked(false);
      ui->hbMacroButton->setChecked(false);
      ui->cqMacroButton->setChecked(false);

      // clear the tx queues
      resetMessageTransmitQueue();

      QMessageBox * msgBox = new QMessageBox(this);
      msgBox->setIcon(QMessageBox::Information);
      msgBox->setWindowTitle("Idle Timeout");
      msgBox->setInformativeText(QString("You have been idle for more than %1 minutes.").arg(m_config.watchdog()));
      msgBox->addButton(QMessageBox::Ok);

      connect(msgBox, &QMessageBox::finished, this, [this, wasAuto, wasHB, wasCQ](int /*result*/) {
          // restore the button states
          ui->actionModeAutoreply->setChecked(wasAuto);
          ui->hbMacroButton->setChecked(wasHB);
          ui->cqMacroButton->setChecked(wasCQ);

          this->tx_watchdog(false);
      });
      msgBox->setModal(true);
      msgBox->show();
    }
  if (prior != triggered) statusUpdate ();
}

void MainWindow::write_frequency_entry (QString const& file_name){
  if(!m_config.write_logs()){
      return;
  }

  // Write freq changes to ALL.TXT only below 30 MHz.
  QFile f2 {m_config.writeable_data_dir ().absoluteFilePath (file_name)};
  if (f2.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
    QTextStream out(&f2);
    out << DriftingDateTime::currentDateTimeUtc().toString("yyyy-MM-dd hh:mm:ss")
        << "  " << qSetRealNumberPrecision (12) << (m_freqNominal / 1.e6) << " MHz  "
        << "JS8" << Qt::endl;
    f2.close();
  } else {
    QTimer::singleShot(0, [
      this,
      message = tr("Cannot open \"%1\" for append: %2").arg(f2.fileName()).arg(f2.errorString())
    ]{
      MessageBox::warning_message(this, tr("Log File Error"), message);
    });
  }
}

void MainWindow::write_transmit_entry (QString const& file_name)
{
  if(!m_config.write_logs()){
      return;
  }

  QFile f {m_config.writeable_data_dir ().absoluteFilePath (file_name)};
  if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
    {
      QTextStream out(&f);
      auto time = DriftingDateTime::currentDateTimeUtc ();
      time = time.addSecs (-(time.time ().second () % m_TRperiod));
      auto dt = DecodedText(m_currentMessage, m_currentMessageBits, m_nSubMode);
      out << time.toString("yyyy-MM-dd hh:mm:ss")
          << "  Transmitting " << qSetRealNumberPrecision (12) << (m_freqNominal / 1.e6)
          << " MHz  " << "JS8"
          << ":  " << dt.message() << Qt::endl;
      f.close();
    }
  else
    {
      QTimer::singleShot(0, [
        this,
        message = tr("Cannot open \"%1\" for append: %2").arg(f.fileName()).arg(f.errorString())
      ] {
        MessageBox::warning_message(this, tr("Log File Error"), message);
      });
    }
}


void MainWindow::writeAllTxt(QString message, int bits)
{
  if(!m_config.write_logs()){
      return;
  }

  // Write decoded text to file "ALL.TXT".
  QFile f {m_config.writeable_data_dir ().absoluteFilePath ("ALL.TXT")};
      if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        QTextStream out(&f);
        if(m_RxLog==1) {
          out << DriftingDateTime::currentDateTimeUtc().toString("yyyy-MM-dd hh:mm:ss")
              << "  " << qSetRealNumberPrecision (12) << (m_freqNominal / 1.e6) << " MHz  "
              << "JS8" << Qt::endl;
          m_RxLog=0;
        }
        auto dt = DecodedText(message, bits, m_nSubMode);
        out << dt.message() << Qt::endl;
        f.close();
      } else {
        MessageBox::warning_message (this, tr ("File Open Error")
                                     , tr ("Cannot open \"%1\" for append: %2")
                                     .arg (f.fileName ()).arg (f.errorString ()));
      }
}

void MainWindow::writeMsgTxt(QString message, int snr)
{
  if(!m_config.write_logs()){
      return;
  }

  // Write decoded text to file "DIRECTED.TXT".
  QFile f {m_config.writeable_data_dir ().absoluteFilePath ("DIRECTED.TXT")};
  if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        QTextStream out(&f);

        QStringList output = {
            DriftingDateTime::currentDateTimeUtc().toString("yyyy-MM-dd hh:mm:ss"),
            Radio::frequency_MHz_string(m_freqNominal),
            QString::number(freq()),
            Varicode::formatSNR(snr),
            message
        };

        out << output.join("\t") << Qt::endl;

        f.close();
    } else {
        MessageBox::warning_message (this, tr ("File Open Error")
                                     , tr ("Cannot open \"%1\" for append: %2")
                                    .arg (f.fileName ()).arg (f.errorString ()));
    }
}

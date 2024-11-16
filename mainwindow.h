// -*- Mode: C++ -*-
#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#ifdef QT5
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QDateTime>
#include <QList>
#include <QAudioDevice>
#include <QScopedPointer>
#include <QDir>
#include <QProgressDialog>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <QMainWindow>
#include <QTableWidget>
#include <QTextEdit>
#include <QLabel>
#include <QProgressBar>

#include <functional>

#include "AudioDevice.hpp"
#include "commons.h"
#include "Radio.hpp"
#include "Modes.hpp"
#include "FrequencyList.hpp"
#include "Configuration.hpp"
#include "Transceiver.hpp"
#include "DisplayManual.hpp"
#include "PSKReporter.hpp"
#include "logbook/logbook.h"
#include "commons.h"
#include "MessageBox.hpp"
#include "NetworkAccessManager.hpp"
#include "qpriorityqueue.h"
#include "varicode.h"
#include "MessageClient.hpp"
#include "MessageServer.h"
#include "TCPClient.h"
#include "SpotClient.h"
#include "APRSISClient.h"
#include "NotificationAudio.h"
#include "ProcessThread.h"
#include "Decoder.h"

#define NUM_ISCAT_SYMBOLS 1291             //30*11025/256

extern int volatile itone[NUM_ISCAT_SYMBOLS];   //Audio tones for all Tx symbols

//--------------------------------------------------------------- MainWindow
namespace Ui {
  class MainWindow;
}

class QSettings;
class QLineEdit;
class QFont;
class QHostInfo;
class WideGraph;
class LogQSO;
class Transceiver;
class MessageClient;
class QTime;
class HelpTextWindow;
class SoundOutput;
class Modulator;
class SoundInput;
class Detector;
class MultiSettings;
class DecodedText;
class JSCChecker;

using namespace std;
typedef std::function<void()> Callback;


class MainWindow : public QMainWindow
{
  Q_OBJECT;

  struct CallDetail;
  struct CommandDetail;
public:
  using Frequency = Radio::Frequency;
  using FrequencyDelta = Radio::FrequencyDelta;
  using Mode = Modes::Mode;

  explicit MainWindow(QString const & program_info,
                      QDir    const & temp_directory,
                      bool            multiple,
                      MultiSettings * settings,
                      QSharedMemory * shdmem,
                      unsigned        downSampleFactor,
                      QWidget       * parent = nullptr);
  ~MainWindow();

private:
  void initDecoderSubprocess();

  struct SortByReverse
  {
    QString by;
    bool    reverse;
  };

public slots:
  void showSoundInError(const QString& errorMsg);
  void showSoundOutError(const QString& errorMsg);
  void showStatusMessage(const QString& statusMsg);
  void dataSink(qint64 frames);
  void guiUpdate();
  void readFromStdout(QProcess * proc);
  void setXIT(int n);
  void qsy(int hzDelta);
  void drifted(int prev, int cur);
  void setFreqOffsetForRestore(int freq, bool shouldRestore);
  bool tryRestoreFreqOffset();
  void changeFreq(int);

  bool hasExistingMessageBufferToMe(int *pOffset);
  bool hasExistingMessageBuffer(int submode, int offset, bool drift, int *pPrevOffset);
  bool hasClosedExistingMessageBuffer(int offset);
  void logCallActivity(CallDetail d, bool spot=true);
  void logHeardGraph(QString from, QString to);
  QString lookupCallInCompoundCache(QString const &call);
  void cacheActivity(QString key);
  void restoreActivity(QString key);
  void clearActivity();
  void clearBandActivity();
  void clearRXActivity();
  void clearCallActivity();
  void createGroupCallsignTableRows(QTableWidget *table, const QString &selectedCall);
  void displayTextForFreq(QString text, int freq, QDateTime date, bool isTx, bool isNewLine, bool isLast);
  void writeNoticeTextToUI(QDateTime date, QString text);
  int writeMessageTextToUI(QDateTime date, QString text, int freq, bool isTx, int block=-1);
  bool isMessageQueuedForTransmit();
  bool isInDecodeDelayThreshold(int seconds);
  void prependMessageText(QString text);
  void addMessageText(QString text, bool clear=false, bool selectFirstPlaceholder=false);
  void confirmThenEnqueueMessage(int timeout, int priority, QString message, int offset, Callback c);
  void enqueueMessage(int priority, QString message, int offset, Callback c);
  void resetMessage();
  void resetMessageUI();
  void restoreMessage();
  void initializeDummyData();
  bool ensureCallsignSet(bool alert=true);
  bool ensureKeyNotStuck(QString const& text);
  bool ensureNotIdle();
  bool ensureCanTransmit();
  bool ensureCreateMessageReady(const QString &text);
  QString createMessage(QString const& text, bool *pDisableTypeahead);
  QString appendMessage(QString const& text, bool isData, bool *pDisableTypeahead);
  QString createMessageTransmitQueue(QString const& text, bool reset, bool isData, bool *pDisableTypeahead);
  void resetMessageTransmitQueue();
  QPair<QString, int> popMessageFrame();
  void tryNotify(const QString &key);
  void processDecodedLine(QByteArray t);

protected:
  void keyPressEvent (QKeyEvent *) override;
  void closeEvent(QCloseEvent *) override;
  void childEvent(QChildEvent *) override;
  bool eventFilter(QObject *, QEvent *) override;

private slots:
  void initialize_fonts ();
  void on_menuModeJS8_aboutToShow();
  void on_menuControl_aboutToShow();
  void on_actionCheck_for_Updates_triggered();
  void on_actionEnable_Monitor_RX_toggled(bool checked);
  void on_actionEnable_Transmitter_TX_toggled(bool checked);
  void on_actionEnable_Reporting_SPOT_toggled(bool checked);
  void on_actionEnable_Tuning_Tone_TUNE_toggled(bool checked);
  void on_menuWindow_aboutToShow();
  void on_actionFocus_Message_Receive_Area_triggered();
  void on_actionFocus_Message_Reply_Area_triggered();
  void on_actionFocus_Band_Activity_Table_triggered();
  void on_actionFocus_Call_Activity_Table_triggered();
  void on_actionClear_All_Activity_triggered();
  void on_actionClear_Band_Activity_triggered();
  void on_actionClear_RX_Activity_triggered();
  void on_actionClear_Call_Activity_triggered();
  void on_actionSetOffset_triggered();
  void on_actionShow_Fullscreen_triggered(bool checked);
  void on_actionShow_Statusbar_triggered(bool checked);
  void on_actionShow_Frequency_Clock_triggered(bool checked);
  void on_actionShow_Band_Activity_triggered(bool checked);
  void on_actionShow_Band_Heartbeats_and_ACKs_triggered(bool checked);
  void on_actionShow_Call_Activity_triggered(bool checked);
  void on_actionShow_Waterfall_triggered(bool checked);
  void on_actionShow_Waterfall_Controls_triggered(bool checked);
  void on_actionShow_Waterfall_Time_Drift_Controls_triggered(bool checked);
  void on_actionReset_Window_Sizes_triggered();
  void on_actionSettings_triggered();
  void openSettings(int tab=0);
  void prepareApi();
  void prepareSpotting();
  void on_spotButton_clicked(bool checked);
  void on_monitorButton_clicked (bool);
  void on_actionAbout_triggered();
  void resetPushButtonToggleText(QPushButton *btn);
  void on_stopTxButton_clicked();
  void on_dialFreqUpButton_clicked();
  void on_dialFreqDownButton_clicked();
  void on_actionAdd_Log_Entry_triggered();
  void on_actionOpen_log_directory_triggered ();
  void on_actionCopyright_Notice_triggered();
  bool decode(qint32 k);
  bool isDecodeReady(int submode, qint32 k, qint32 k0, qint32 *pCurrentDecodeStart, qint32 *pNextDecodeStart, qint32 *pStart, qint32 *pSz, qint32 *pCycle);
  bool decodeEnqueueReady(qint32 k, qint32 k0);
  bool decodeEnqueueReadyExperiment(qint32 k, qint32 k0);
  bool decodeProcessQueue(qint32 *pSubmode);
  void decodeStart();
  void decodeBusy(bool b);
  void decodeDone ();
  void decodeCheckHangingDecoder();
  void on_startTxButton_toggled(bool checked);
  void toggleTx(bool start);
  void on_logQSOButton_clicked();
  void on_actionModeJS8HB_toggled(bool checked);
  void on_actionModeJS8Normal_triggered();
  void on_actionModeJS8Fast_triggered();
  void on_actionModeJS8Turbo_triggered();
  void on_actionModeJS8Slow_triggered();
  void on_actionModeJS8Ultra_triggered();
  void on_actionHeartbeatAcknowledgements_toggled(bool checked);
  void on_actionModeMultiDecoder_toggled(bool checked);
  void on_actionModeAutoreply_toggled(bool checked);
  bool canCurrentModeSendHeartbeat() const;
  void prepareMonitorControls();
  void prepareHeartbeatMode(bool enabled);
  void on_actionQuickDecode_toggled (bool);
  void on_actionMediumDecode_toggled (bool);
  void on_actionDeepDecode_toggled (bool);
  void on_actionDeepestDecode_toggled (bool);
  void f11f12(int n);
  void on_actionErase_ALL_TXT_triggered();
  void on_actionErase_js8call_log_adi_triggered();
  void startTx();
  void startTx2();
  void stopTx();
  void stopTx2();
  void buildFrequencyMenu(QMenu *menu);
  void buildHeartbeatMenu(QMenu *menu);
  void buildCQMenu(QMenu *menu);
  void buildRepeatMenu(QMenu *menu, QPushButton * button, bool isLowInterval, int * interval);
  void sendHeartbeat();
  void sendHeartbeatAck(QString to, int snr, QString extra);
  void on_hbMacroButton_toggled(bool checked);
  void on_hbMacroButton_clicked();
  void sendCQ(bool repeat=false);
  void on_cqMacroButton_toggled(bool checked);
  void on_cqMacroButton_clicked();
  void on_replyMacroButton_clicked();
  void on_snrMacroButton_clicked();
  void on_infoMacroButton_clicked();
  void on_statusMacroButton_clicked();
  void setShowColumn(QString tableKey, QString columnKey, bool value);
  bool showColumn(QString tableKey, QString columnKey, bool default_=true);
  void buildShowColumnsMenu(QMenu *menu, QString tableKey);
  void setSortBy(QString key, QString value);
  QString getSortBy(QString const & key, QString const & defaultValue) const;
  SortByReverse getSortByReverse(QString const & key, QString const & defaultValue) const;
  void buildSortByMenu(QMenu * menu, QString key, QString defaultValue, QList<QPair<QString, QString> > values);
  void buildBandActivitySortByMenu(QMenu * menu);
  void buildCallActivitySortByMenu(QMenu * menu);
  void buildQueryMenu(QMenu *, QString callsign);
  QMap<QString, QString> buildMacroValues();
  void buildSuggestionsMenu(QMenu *menu, QTextEdit *edit, const QPoint &point);
  void buildSavedMessagesMenu(QMenu *menu);
  void buildRelayMenu(QMenu *menu);
  QAction* buildRelayAction(QString call);
  void buildEditMenu(QMenu *, QTextEdit *);
  void on_queryButton_pressed();
  void on_macrosMacroButton_pressed();
  void on_deselectButton_pressed();
  void on_tableWidgetRXAll_cellClicked(int row, int col);
  void on_tableWidgetRXAll_cellDoubleClicked(int row, int col);
  QString generateCallDetail(QString selectedCall);
  void on_tableWidgetCalls_cellClicked(int row, int col);
  void on_tableWidgetCalls_cellDoubleClicked(int row, int col);
  QList<QPair<QString, int>> buildMessageFrames(QString const& text, bool isData, bool *pDisableTypeahead);
  bool prepareNextMessageFrame();
  bool isFreqOffsetFree(int f, int bw);
  int findFreeFreqOffset(int fmin, int fmax, int bw);
  void checkRepeat();
  void setDrift(int n);
  void on_tuneButton_clicked (bool);
  void acceptQSO (QDateTime const&, QString const& call, QString const& grid
                  , Frequency dial_freq, QString const& mode, QString const& submode
                  , QString const& rpt_sent, QString const& rpt_received
                  , QString const& comments
                  , QString const& name, QDateTime const& QSO_date_on, QString const& operator_call
                  , QString const& my_call, QString const& my_grid, QByteArray const& ADIF, QVariantMap const &additionalFields);
  void on_readFreq_clicked();
  void on_outAttenuation_valueChanged (int);
  void rigOpen ();
  void handle_transceiver_update (Transceiver::TransceiverState const&);
  void handle_transceiver_failure (QString const& reason);
  void band_changed ();
  void monitor (bool);
  void end_tuning ();
  void stop_tuning ();
  void stopTuneATU();
  void auto_tx_mode(bool);
  void on_monitorButton_toggled(bool checked);
  void on_monitorTxButton_toggled(bool checked);
  void on_tuneButton_toggled(bool checked);
  void on_spotButton_toggled(bool checked);

  void emitPTT(bool on);
  void emitTones();
  void udpNetworkMessage(Message const &message);
  void tcpNetworkMessage(Message const &message);
  void networkMessage(Message const &message);
  bool canSendNetworkMessage();
  void sendNetworkMessage(QString const &type, QString const &message);
  void sendNetworkMessage(QString const &type, QString const &message, const QVariantMap &params);
  void pskReporterError (QString const &);
  void TxAgain();
  void checkVersion(bool alertOnUpToDate);
  void checkStartupWarnings ();
  void clearCallsignSelected();
  void refreshTextDisplay();

private:
  Q_SIGNAL void apiSetMaxConnections(int n);
  Q_SIGNAL void apiSetServer(QString host, quint16 port);
  Q_SIGNAL void apiStartServer();
  Q_SIGNAL void apiStopServer();

  Q_SIGNAL void aprsClientEnqueueSpot(QString by_call, QString from_call, QString grid, QString comment);
  Q_SIGNAL void aprsClientEnqueueThirdParty(QString by_call, QString from_call, QString text);
  Q_SIGNAL void aprsClientSetSkipPercent(float skipPercent);
  Q_SIGNAL void aprsClientSetServer(QString host, quint16 port);
  Q_SIGNAL void aprsClientSetPaused(bool paused);
  Q_SIGNAL void aprsClientSetLocalStation(QString mycall, QString passcode);
  Q_SIGNAL void aprsClientSendReports();

  Q_SIGNAL void pskReporterSendReport(bool);
  Q_SIGNAL void pskReporterSetLocalStation(QString, QString, QString);
  Q_SIGNAL void pskReporterAddRemoteStation(QString, QString, Radio::Frequency, QString, int);

  Q_SIGNAL void spotClientSetLocalStation(QString, QString, QString);
  Q_SIGNAL void spotClientEnqueueCmd(QString, QString, QString, QString, QString, QString, QString, int, int, int, int);
  Q_SIGNAL void spotClientEnqueueSpot(QString, QString, int, int, int, int);

  Q_SIGNAL void decodedLineReady(QByteArray t);
  Q_SIGNAL void playNotification(const QString &name);
  Q_SIGNAL void initializeNotificationAudioOutputStream(const QAudioDevice &, unsigned) const;
  Q_SIGNAL void initializeAudioOutputStream (QAudioDevice,
      unsigned channels, unsigned msBuffered) const;
  Q_SIGNAL void stopAudioOutputStream () const;
  Q_SIGNAL void startAudioInputStream (QAudioDevice const&,
      int framesPerBuffer, AudioDevice * sink,
      unsigned downSampleFactor, AudioDevice::Channel) const;
  Q_SIGNAL void suspendAudioInputStream () const;
  Q_SIGNAL void resumeAudioInputStream () const;
  Q_SIGNAL void startDetector (AudioDevice::Channel) const;
  Q_SIGNAL void FFTSize (unsigned) const;
  Q_SIGNAL void detectorClose () const;
  Q_SIGNAL void finished () const;
  Q_SIGNAL void transmitFrequency (double) const;
  Q_SIGNAL void endTransmitMessage (bool quick = false) const;
  Q_SIGNAL void tune (bool = true) const;
  Q_SIGNAL void sendMessage (double frequency, int submode, SoundOutput *, AudioDevice::Channel) const;
  Q_SIGNAL void outAttenuationChanged (qreal) const;
  Q_SIGNAL void toggleShorthand () const;

private:
  void writeAllTxt(QString message, int bits);
  void writeMsgTxt(QString message, int snr);

  void currentTextChanged();
  void tableSelectionChanged(QItemSelection const &,
                             QItemSelection const &);
  void setupJS8();

  int freq() const { return m_freq; }

  void setFreq(int);

  QString m_nextFreeTextMsg;

  NetworkAccessManager m_network_manager;
  bool m_valid;
  [[maybe_unused]] bool m_multiple; // Used only in Windows builds
  MultiSettings * m_multi_settings;
  QPushButton * m_configurations_button;
  QSettings * m_settings;
  bool m_settings_read;
  QScopedPointer<Ui::MainWindow> ui;

  // other windows
  Configuration m_config;
  MessageBox m_rigErrorMessageBox;

  QScopedPointer<WideGraph> m_wideGraph;
  QScopedPointer<LogQSO> m_logDlg;
  QScopedPointer<HelpTextWindow> m_shortcuts;
  QScopedPointer<HelpTextWindow> m_prefixes;
  QScopedPointer<HelpTextWindow> m_mouseCmnds;

  Transceiver::TransceiverState m_rigState;
  Frequency  m_lastDialFreq;
  QString m_lastBand;

  Detector * m_detector;
  unsigned m_FFTSize;
  SoundInput * m_soundInput;
  Modulator * m_modulator;
  SoundOutput * m_soundOutput;
  NotificationAudio * m_notification;

  QThread m_networkThread;
  QThread m_audioThread;
  QThread m_notificationAudioThread;
  Decoder m_decoder;

  qint64  m_secBandChanged;

  Frequency m_freqNominal;
  Frequency m_freqTxNominal;

  int     m_freq;

  qint32  m_XIT;
  qint32  m_ndepth;
  qint32  m_sec0;
  qint32  m_RxLog;
  qint32  m_nutc0;
  qint32  m_TRperiod;
  qint32  m_inGain;
  qint32  m_idleMinutes;
  qint32  m_nSubMode;
  FrequencyList_v2::const_iterator m_frequency_list_fcal_iter;
  qint32  m_i3bit;

  bool    m_btxok;		//True if OK to transmit
  bool    m_decoderBusy;
  QString m_decoderBusyBand;
  QMap<qint32, qint32> m_lastDecodeStartMap;  // submode, decode k start position
  Radio::Frequency m_decoderBusyFreq;
  QDateTime m_decoderBusyStartTime;
  bool    m_auto;
  bool    m_restart;
  bool    m_bDecoded;
  int     m_currentMessageType;
  QString m_currentMessage;
  int     m_currentMessageBits;
  int     m_lastMessageType;
  QString m_lastMessageSent;
  bool    m_tuneup;
  bool    m_bTxTime;

  int			m_ihsym;
  int			m_npts8;
  float		m_px;
  float   m_pxmax;
  float		m_df3;
  quint32 m_iptt = 0;
  quint32 m_iptt0;
  bool		m_btxok0;
  double	m_onAirFreq0;
  bool		m_first_error;

  char    m_msg[100][80];

  // labels in status bar
  QLabel tx_status_label;
  QLabel config_label;
  QLabel mode_label;
  QLabel last_tx_label;
  QLabel auto_tx_label;
  QProgressBar progressBar;
  QLabel wpm_label;

  //QPointer<QProcess> proc_js8;

  QTimer m_guiTimer;
  QTimer ptt1Timer;                 //StartTx delay
  QTimer ptt0Timer;                 //StopTx delay
  QTimer logQSOTimer;
  QTimer tuneButtonTimer;
  QTimer tuneATU_Timer;
  QTimer TxAgainTimer;
  QTimer minuteTimer;
  QTimer repeatTimer;

  QString m_baseCall;
  QString m_hisCall;
  QString m_hisGrid;
  QString m_appDir;
  QString m_palette;
  QString m_dateTime;
  QString m_rptSent;
  QString m_rptRcvd;
  QString m_msgSent0;
  QString m_opCall;

  struct CallDetail
  {
    QString call;
    QString through;
    QString grid;
    int dial;
    int offset;
    QDateTime cqTimestamp;
    QDateTime ackTimestamp;
    QDateTime utcTimestamp;
    int snr;
    int bits;
    float tdrift;
    int submode;
  };

  struct CommandDetail
  {
    bool isCompound;
    bool isBuffered;
    QString from;
    QString to;
    QString cmd;
    int dial;
    int offset;
    QDateTime utcTimestamp;
    int snr;
    int bits;
    QString grid;
    QString text;
    QString extra;
    float tdrift;
    int submode;
    QString relayPath;
  };

  struct ActivityDetail
  {
    bool isFree;
    bool isLowConfidence;
    bool isCompound;
    bool isDirected;
    bool isBuffered;
    int bits;
    int dial;
    int offset;
    QString text;
    QDateTime utcTimestamp;
    int snr;
    bool shouldDisplay;
    float tdrift;
    int submode;
  };

  struct MessageBuffer {
    CommandDetail cmd;
    QQueue<CallDetail> compound;
    QList<ActivityDetail> msgs;
  };

  QString m_prevSelectedCallsign;
  int m_bandActivityWidth;
  int m_callActivityWidth;
  int m_textActivityWidth;
  int m_waterfallHeight;
  bool m_bandActivityWasVisible;
  bool m_rxDirty;
  bool m_rxDisplayDirty;
  int m_txFrameCountEstimate;
  int m_txFrameCount;
  int m_txFrameCountSent;
  QTimer m_txTextDirtyDebounce;
  bool m_txTextDirty;
  QString m_txTextDirtyLastText;
  QString m_txTextDirtyLastSelectedCall;
  QString m_lastTxMessage;
  QString m_totalTxMessage;
  QDateTime m_lastTxStartTime;
  QDateTime m_lastTxStopTime;
  qint32 m_driftMsMMA;
  qint32 m_driftMsMMA_N;

  enum Priority {
    PriorityLow    =   10,
    PriorityNormal =  100,
    PriorityHigh   = 1000
  };

  struct PrioritizedMessage {
      QDateTime date;
      int priority;
      QString message;
      int offset;
      Callback callback;

      friend bool operator <(PrioritizedMessage const &a, PrioritizedMessage const &b){
          if(a.priority < b.priority){
              return true;
          }
          return a.date < b.date;
      }
  };

  struct CachedDirectedType {
      bool isAllcall;
      QDateTime date;
  };

  struct DecodeParams {
      int submode;
      int start;
      int sz;
  };

  struct CachedFrame {
    QDateTime date;
    int submode;
    int freq;
  };

  using BandActivity = QMap<int, QList<ActivityDetail>>;

  QQueue<DecodeParams> m_decoderQueue;
  QMap<QString, CachedFrame> m_messageDupeCache; // message frame -> date seen, submode seen, freq offset seen
  QVariantMap m_showColumnsCache; // table column:key -> show boolean
  QVariantMap m_sortCache; // table key -> sort by
  QPriorityQueue<PrioritizedMessage> m_txMessageQueue; // messages to be sent
  QQueue<QPair<QString, int>> m_txFrameQueue; // frames to be sent
  QQueue<ActivityDetail> m_rxActivityQueue; // all rx activity queue
  QQueue<CommandDetail> m_rxCommandQueue; // command queue for processing commands
  QQueue<CallDetail> m_rxCallQueue; // call detail queue for spots to pskreporter
  QMap<QString, QString> m_compoundCallCache; // base callsign -> compound callsign
  QCache<QString, QDateTime> m_txAllcallCommandCache; // callsign -> last tx
  QCache<int, QDateTime> m_rxRecentCache; // freq -> last rx
  QCache<int, CachedDirectedType> m_rxDirectedCache; // freq -> last directed rx
  QCache<QString, int> m_rxCallCache; // call -> last freq seen
  QMap<int, int> m_rxFrameBlockNumbers; // freq -> block
  BandActivity m_bandActivity; // freq -> [(text, last timestamp), ...]
  QMap<int, MessageBuffer> m_messageBuffer; // freq -> (cmd, [frames, ...])
  int m_lastClosedMessageBufferOffset;
  QMap<QString, CallDetail> m_callActivity; // call -> (last freq, last timestamp)

  QMap<QString, QSet<QString>> m_heardGraphOutgoing; // callsign -> [stations who've this callsign has heard]
  QMap<QString, QSet<QString>> m_heardGraphIncoming; // callsign -> [stations who've heard this callsign]

  QMap<QString, int> m_rxInboxCountCache; // call -> count

  QMap<QString, QMap<QString, CallDetail>> m_callActivityBandCache; // band -> call activity
  QMap<QString, QMap<int, QList<ActivityDetail>>> m_bandActivityBandCache; // band -> band activity
  QMap<QString, QString> m_rxTextBandCache; // band -> rx text
  QMap<QString, QMap<QString, QSet<QString>>> m_heardGraphOutgoingBandCache; // band -> heard in
  QMap<QString, QMap<QString, QSet<QString>>> m_heardGraphIncomingBandCache; // band -> heard out

  QMap<QString, QDateTime> m_callSelectedTime; // call -> timestamp when callsign was last selected
  QSet<QString> m_callSeenHeartbeat; // call
  int m_previousFreq;
  bool m_shouldRestoreFreq;
  bool m_bandHopped;
  Frequency m_bandHoppedFreq;

  int m_hbInterval;
  int m_cqInterval;
  bool m_hbPaused;
  QDateTime m_nextHeartbeat;
  QDateTime m_nextCQ;
  QDateTime m_dateTimeQSOOn;
  QDateTime m_dateTimeLastTX;

  QSharedMemory *mem_js8;

  LogBook m_logBook;
  unsigned m_msAudioOutputBuffered;
  unsigned m_framesAudioInputBuffered;
  unsigned m_downSampleFactor;
  QThread::Priority m_audioThreadPriority;
  QThread::Priority m_notificationAudioThreadPriority;
  QThread::Priority m_decoderThreadPriority;
  QThread::Priority m_networkThreadPriority;
  bool m_splitMode;
  bool m_monitoring;
  bool m_tx_when_ready;
  bool m_transmitting;
  bool m_tune;
  bool m_deadAirTone;
  bool m_tx_watchdog;           // true when watchdog triggered
  bool m_block_pwr_tooltip;
  bool m_PwrBandSetOK;
  Frequency m_lastMonitoredFrequency;
  MessageClient * m_messageClient;
  MessageServer * m_messageServer;
  TCPClient * m_n3fjpClient;
  PSKReporter * m_pskReporter;
  SpotClient *m_spotClient;
  APRSISClient *m_aprsClient;
  DisplayManual m_manual;
  QVariantHash m_pwrBandTxMemory; // Remembers power level by band
  QVariantHash m_pwrBandTuneMemory; // Remembers power level by band for tuning
  QByteArray m_geometryNoControls;

  //---------------------------------------------------- private functions
  void readSettings();
  void set_application_font (QFont const&);
  void writeSettings();
  void createStatusBar();
  void statusChanged();
  void transmit();
  void rigFailure (QString const& reason);
  void spotSetLocal();
  void pskSetLocal ();
  void aprsSetLocal ();
  void spotReport(int submode, int dial, int offset, int snr, QString const & callsign, QString const & grid);
  void spotCmd(CommandDetail const & cmd);
  void spotAprsCmd(CommandDetail const & cmd);
  void pskLogReport(QString const & mode, int dial, int offset, int snr, QString const & callsign, QString const & grid);
  void spotAprsGrid(int dial, int offset, int snr, QString callsign, QString grid);
  Radio::Frequency dialFrequency();
  void setSubmode(int submode);
  void updateCurrentBand();
  void displayDialFrequency ();
  void transmitDisplay (bool);
  void postDecode (bool is_new, QString const& message);
  void displayTransmit();
  void updateModeButtonText();
  void updateButtonDisplay();
  void updateRepeatButtonDisplay();
  void updateTextDisplay();
  void updateTextWordCheckerDisplay();
  void updateTextStatsDisplay(QString text, int count);
  void updateTxButtonDisplay();
  bool isMyCallIncluded(QString const &text);
  bool isAllCallIncluded(QString const &text);
  bool isGroupCallIncluded(const QString &text);
  QString callsignSelected(bool useInputText=false);
  void callsignSelectedChanged(QString old, QString current);
  bool isRecentOffset(int submode, int offset);
  void markOffsetRecent(int offset);
  bool isDirectedOffset(int offset, bool *pIsAllCall);
  void markOffsetDirected(int offset, bool isAllCall);
  void clearOffsetDirected(int offset);
  void processActivity(bool force=false);
  void resetTimeDeltaAverage();
  void processRxActivity();
  void processIdleActivity();
  void processCompoundActivity();
  void processBufferedActivity();
  void processCommandActivity();
  QString inboxPath();
  void refreshInboxCounts();
  bool hasMessageHistory(QString call);
  int addCommandToMyInbox(CommandDetail d);
  int addCommandToStorage(QString type, CommandDetail d);
  int getNextMessageIdForCallsign(QString callsign);
  QStringList parseRelayPathCallsigns(QString from, QString text);
  void processSpots();
  void processTxQueue();
  void displayActivity(bool force=false);
  void displayBandActivity();
  void displayCallActivity();
  void enable_DXCC_entity (bool on);
  void setRig (Frequency = 0);  // zero frequency means no change
  QDateTime nextTransmitCycle();
  void resetAutomaticIntervalTransmissions(bool stopCQ, bool stopHB);
  void resetCQTimer(bool stop);
  void resetHeartbeatTimer(bool stop);
  void subProcessFailed (QString program, QStringList args, int exitCode, int status, QString errorString);
  void subProcessError (QString program, QStringList arguments, int errorCode, QString errorString);
  void statusUpdate ();
  void on_the_minute ();
  void tryBandHop();
  void add_child_to_event_filter (QObject *);
  void remove_child_from_event_filter (QObject *);
  void setup_status_bar ();

  void resetIdleTimer();
  void incrementIdleTimer();
  void tx_watchdog (bool triggered);
  void write_frequency_entry (QString const& file_name);
  void write_transmit_entry (QString const& file_name);
};

extern int killbyname(const char* progName);

#endif // MAINWINDOW_H

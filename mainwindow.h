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
#include <QFuture>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QSplashScreen>
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
#include "keyeater.h"
#include "NotificationAudio.h"
#include "ProcessThread.h"
#include "Decoder.h"

#define NUM_JT4_SYMBOLS 206                //(72+31)*2, embedded sync
#define NUM_JT65_SYMBOLS 126               //63 data + 63 sync
#define NUM_JT9_SYMBOLS 85                 //69 data + 16 sync
#define NUM_WSPR_LF_SYMBOLS 412            //300 data + 109 sync + 3 ramp
#define NUM_ISCAT_SYMBOLS 1291             //30*11025/256
#define NUM_MSK144_SYMBOLS 144             //s8 + d48 + s8 + d80
#define NUM_QRA64_SYMBOLS 84               //63 data + 21 sync

#define NUM_CW_SYMBOLS 250
#define TX_SAMPLE_RATE 48000
#define N_WIDGETS 33

extern int volatile itone[NUM_ISCAT_SYMBOLS];   //Audio tones for all Tx symbols
extern int volatile icw[NUM_CW_SYMBOLS];	    //Dits for CW ID

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
class MessageAveraging;
class MessageClient;
class QTime;
class HelpTextWindow;
class SoundOutput;
class Modulator;
class SoundInput;
class Detector;
class MultiSettings;
class EqualizationToolsDialog;
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

  explicit MainWindow(QDir const& temp_directory, bool multiple, MultiSettings *,
                      QSharedMemory *shdmem, unsigned downSampleFactor,
                      QSplashScreen *,
                      QWidget *parent = nullptr);
  ~MainWindow();

private:
  void initDecoderSubprocess();

public slots:
  void showSoundInError(const QString& errorMsg);
  void showSoundOutError(const QString& errorMsg);
  void showStatusMessage(const QString& statusMsg);
  void dataSink(qint64 frames);
  void diskDat();
  void guiUpdate();
  void readFromStdout(QProcess * proc);
  void setXIT(int n, Frequency base = 0u);
  void qsy(int hzDelta);
  void drifted(int prev, int cur);
  void setFreqOffsetForRestore(int freq, bool shouldRestore);
  bool tryRestoreFreqOffset();
  void setFreq4(int rxFreq, int txFreq);

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
  int rxThreshold(int submode);
  int rxSnrThreshold(int submode);
  void processDecodedLine(QByteArray t);

protected:
  void keyPressEvent (QKeyEvent *) override;
  void closeEvent(QCloseEvent *) override;
  void childEvent(QChildEvent *) override;
  bool eventFilter(QObject *, QEvent *) override;

private slots:
  void initialize_fonts ();
  void on_tx1_editingFinished();
  void on_tx2_editingFinished();
  void on_tx3_editingFinished();
  void on_tx4_editingFinished();
  void on_tx5_currentTextChanged (QString const&);
  void on_tx6_editingFinished();
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
  void on_autoButton_clicked (bool);
  void on_labDialFreq_clicked();
  void resetPushButtonToggleText(QPushButton *btn);
  void on_stopTxButton_clicked();
  void on_dialFreqUpButton_clicked();
  void on_dialFreqDownButton_clicked();
  void on_stopButton_clicked();
  void on_actionAdd_Log_Entry_triggered();
  void on_actionRelease_Notes_triggered ();
  void on_actionFT8_DXpedition_Mode_User_Guide_triggered();
  void on_actionOnline_User_Guide_triggered();
  void on_actionLocal_User_Guide_triggered();
  void on_actionOpen_triggered();
  void on_actionOpen_next_in_directory_triggered();
  void on_actionDecode_remaining_files_in_directory_triggered();
  void on_actionDelete_all_wav_files_in_SaveDir_triggered();
  void on_actionOpen_log_directory_triggered ();
  void on_actionOpen_Save_Directory_triggered();
  void on_actionNone_triggered();
  void on_actionSave_all_triggered();
  void on_actionKeyboard_shortcuts_triggered();
  void on_actionSpecial_mouse_commands_triggered();
  void on_actionSolve_FreqCal_triggered();
  void on_actionCopyright_Notice_triggered();
  bool decode(qint32 k);
  bool isDecodeReady(int submode, qint32 k, qint32 k0, qint32 *pCurrentDecodeStart, qint32 *pNextDecodeStart, qint32 *pStart, qint32 *pSz, qint32 *pCycle);
  bool decodeEnqueueReady(qint32 k, qint32 k0);
  bool decodeEnqueueReadyExperiment(qint32 k, qint32 k0);
  bool decodeProcessQueue(qint32 *pSubmode);
  void decodeStart();
  void decodePrepareSaveAudio(int submode);
  void decodeBusy(bool b);
  void decodeDone ();
  void decodeCheckHangingDecoder();
  void on_EraseButton_clicked();
  void set_dateTimeQSO(int m_ntx);
  void set_ntx(int n);
  void on_txrb1_toggled(bool status);
  void on_txrb1_doubleClicked ();
  void on_txrb2_toggled(bool status);
  void on_txrb3_toggled(bool status);
  void on_txrb4_toggled(bool status);
  void on_txrb4_doubleClicked ();
  void on_txrb5_toggled(bool status);
  void on_txrb5_doubleClicked ();
  void on_txrb6_toggled(bool status);
  void on_txb1_clicked();
  void on_txb1_doubleClicked ();
  void on_txb2_clicked();
  void on_txb3_clicked();
  void on_txb4_clicked();
  void on_txb4_doubleClicked ();
  void on_txb5_clicked();
  void on_txb5_doubleClicked ();
  void on_txb6_clicked();
  void on_startTxButton_toggled(bool checked);
  void toggleTx(bool start);
  void on_rbNextFreeTextMsg_toggled (bool status);
  void on_lookupButton_clicked();
  void on_addButton_clicked();
  void on_dxCallEntry_textChanged (QString const&);
  void on_dxGridEntry_textChanged (QString const&);
  void on_dxCallEntry_returnPressed ();
  void on_genStdMsgsPushButton_clicked();
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
  bool canCurrentModeSendHeartbeat();
  void prepareMonitorControls();
  void prepareHeartbeatMode(bool enabled);
  void on_actionJS8_triggered();
  void on_TxFreqSpinBox_valueChanged(int arg1);
  void on_actionSave_decoded_triggered();
  void on_actionQuickDecode_toggled (bool);
  void on_actionMediumDecode_toggled (bool);
  void on_actionDeepDecode_toggled (bool);
  void on_actionDeepestDecode_toggled (bool);
  void bumpFqso(int n);
  void on_actionErase_ALL_TXT_triggered();
  void on_actionErase_FoxQSO_txt_triggered();
  void on_actionErase_js8call_log_adi_triggered();
  void startTx();
  void startTx2();
  void stopTx();
  void stopTx2();
  void on_pbCallCQ_clicked();
  void on_pbAnswerCaller_clicked();
  void on_pbSendRRR_clicked();
  void on_pbAnswerCQ_clicked();
  void on_pbSendReport_clicked();
  void on_pbSend73_clicked();
  void on_rbGenMsg_clicked(bool checked);
  void on_rbFreeText_clicked(bool checked);
  void on_clearAction_triggered(QObject * sender);
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
  QString getSortBy(QString key, QString defaultValue);
  void buildSortByMenu(QMenu * menu, QString key, QString defaultValue, QList<QPair<QString, QString> > values);
  void buildBandActivitySortByMenu(QMenu * menu);
  void buildCallActivitySortByMenu(QMenu * menu);
  void buildQueryMenu(QMenu *, QString callsign);
  QMap<QString, QString> buildMacroValues();
  QString replaceMacros(QString const &text, QMap<QString, QString> values, bool prune);
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
  void on_tableWidgetRXAll_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
  QString generateCallDetail(QString selectedCall);
  void on_tableWidgetCalls_cellClicked(int row, int col);
  void on_tableWidgetCalls_cellDoubleClicked(int row, int col);
  void on_tableWidgetCalls_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
  void on_freeTextMsg_currentTextChanged (QString const&);
  void on_textEditRX_mouseDoubleClicked();
  void on_nextFreeTextMsg_currentTextChanged (QString const&);
  void on_extFreeTextMsgEdit_currentTextChanged (QString const&);
  int currentFreqOffset();
  QList<QPair<QString, int>> buildMessageFrames(QString const& text, bool isData, bool *pDisableTypeahead);
  bool prepareNextMessageFrame();
  bool isFreqOffsetFree(int f, int bw);
  int findFreeFreqOffset(int fmin, int fmax, int bw);
  void checkRepeat();
  QString calculateDistance(QString const& grid, int *pDistance=nullptr, int *pAzimuth=nullptr);
  void setDrift(int n);
  void on_rptSpinBox_valueChanged(int n);
  void killFile();
  void on_tuneButton_clicked (bool);
  void on_pbR2T_clicked();
  void on_pbT2R_clicked();
  void on_turboButton_clicked();
  void acceptQSO (QDateTime const&, QString const& call, QString const& grid
                  , Frequency dial_freq, QString const& mode, QString const& submode
                  , QString const& rpt_sent, QString const& rpt_received
                  , QString const& comments
                  , QString const& name, QDateTime const& QSO_date_on, QString const& operator_call
                  , QString const& my_call, QString const& my_grid, QByteArray const& ADIF, const QMap<QString, QVariant> &additionalFields);
  void on_bandComboBox_currentIndexChanged (int index);
  void on_bandComboBox_activated (int index);
  void on_readFreq_clicked();
  void on_pbTxMode_clicked();
  void on_RxFreqSpinBox_valueChanged(int n);
  void on_outAttenuation_valueChanged (int);
  void rigOpen ();
  void handle_transceiver_update (Transceiver::TransceiverState const&);
  void handle_transceiver_failure (QString const& reason);
  void on_actionAstronomical_data_toggled (bool);
  void on_actionShort_list_of_add_on_prefixes_and_suffixes_triggered();
  void band_changed (Frequency);
  void monitor (bool);
  void end_tuning ();
  void stop_tuning ();
  void stopTuneATU();
  void auto_tx_mode(bool);
  void on_monitorButton_toggled(bool checked);
  void on_monitorTxButton_toggled(bool checked);
  void on_tuneButton_toggled(bool checked);
  void on_spotButton_toggled(bool checked);

  void on_actionMessage_averaging_triggered();
  void on_actionFox_Log_triggered();
  void on_actionInclude_averaging_toggled (bool);
  void on_actionInclude_correlation_toggled (bool);
  void on_actionEnable_AP_DXcall_toggled (bool);
  void VHF_features_enabled(bool b);
  void on_sbSubmode_valueChanged(int n);
  void on_cbShMsgs_toggled(bool b);
  void on_cbSWL_toggled(bool b);
  void on_cbTx6_toggled(bool b);
  void emitPTT(bool on);
  void emitTones();
  void udpNetworkMessage(Message const &message);
  void tcpNetworkMessage(Message const &message);
  void networkMessage(Message const &message);
  bool canSendNetworkMessage();
  void sendNetworkMessage(QString const &type, QString const &message);
  void sendNetworkMessage(QString const &type, QString const &message, const QMap<QString, QVariant> &params);
  void udpNetworkError (QString const&);
  void tcpNetworkError (QString const&);
  void on_ClrAvgButton_clicked();
  void on_syncSpinBox_valueChanged(int n);
  void on_TxPowerComboBox_currentIndexChanged(int);
  void on_sbTxPercent_valueChanged(int n);
  void TxAgain();
  void uploadResponse(QString response);
  void on_WSPRfreqSpinBox_valueChanged(int n);
  void on_pbTxNext_clicked(bool b);
  void on_actionMeasure_reference_spectrum_triggered();
  void on_actionErase_reference_spectrum_triggered();
  void on_actionMeasure_phase_response_triggered();
  void on_sbFtol_valueChanged (int);
  void on_sbCQTxFreq_valueChanged(int n);
  void on_cbCQTx_toggled(bool b);
  void splash_done (); 
  void on_measure_check_box_stateChanged (int);
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
  Q_SIGNAL void sendMessage (unsigned symbolsLength, double framesPerSymbol,
      double frequency, double toneSpacing,
      SoundOutput *, AudioDevice::Channel = AudioDevice::Mono,
      bool synchronize = true, bool fastMode = false, double dBSNR = 99.,
                             int TRperiod=60) const;
  Q_SIGNAL void outAttenuationChanged (qreal) const;
  Q_SIGNAL void toggleShorthand () const;

private:
  void writeAllTxt(QString message, int bits);
  void writeMsgTxt(QString message, int snr);
  void hideMenus(bool b);

  NetworkAccessManager m_network_manager;
  bool m_valid;
  QSplashScreen * m_splash;
  QString m_revision;
  bool m_multiple;
  MultiSettings * m_multi_settings;
  QPushButton * m_configurations_button;
  QSettings * m_settings;
  bool m_settings_read;
  QScopedPointer<Ui::MainWindow> ui;

  // other windows
  Configuration m_config;
  MessageBox m_rigErrorMessageBox;
  QScopedPointer<EqualizationToolsDialog> m_equalizationToolsDialog;

  bool m_isWideGraphMDI;
  QScopedPointer<WideGraph> m_wideGraph;
  QScopedPointer<LogQSO> m_logDlg;
  QScopedPointer<HelpTextWindow> m_shortcuts;
  QScopedPointer<HelpTextWindow> m_prefixes;
  QScopedPointer<HelpTextWindow> m_mouseCmnds;
  QScopedPointer<MessageAveraging> m_msgAvgWidget;

  Transceiver::TransceiverState m_rigState;
  Frequency  m_lastDialFreq;
  QString m_lastBand;
  QString m_lastCallsign;

  Detector * m_detector;
  unsigned m_FFTSize;
  SoundInput * m_soundInput;
  Modulator * m_modulator;
  SoundOutput * m_soundOutput;
  NotificationAudio * m_notification;

  QMutex m_networkThreadMutex;
  QThread m_networkThread;
  QThread m_audioThread;
  QThread m_notificationAudioThread;
  Decoder m_decoder;

  qint64  m_msErase;
  qint64  m_secBandChanged;
  qint64  m_freqMoon;
  qint64  m_fullFoxCallTime;

  Frequency m_freqNominal;
  Frequency m_freqTxNominal;

  double  m_s6;
  double  m_tRemaining;

  float   m_DTtol;
  float   m_t0;
  float   m_t1;
  float   m_t0Pick;
  float   m_t1Pick;
  float   m_fCPUmskrtd;

  qint32  m_waterfallAvg;
  qint32  m_ntx;
  bool m_gen_message_is_cq;
  bool m_send_RR73;
  qint32  m_timeout;
  qint32  m_XIT;
  qint32  m_setftx;
  qint32  m_ndepth;
  qint32  m_sec0;
  qint32  m_RxLog;
  qint32  m_nutc0;
  qint32  m_ntr;
  qint32  m_tx;
  qint32  m_hsym;
  qint32  m_TRperiod;
  qint32  m_nsps;
  qint32  m_hsymStop;
  qint32  m_inGain;
  qint32  m_ncw;
  qint32  m_secID;
  qint32  m_idleMinutes;
  qint32  m_nSubMode;
  qint32  m_nclearave;
  qint32  m_minSync;
  qint32  m_dBm;
  qint32  m_pctx;
  qint32  m_nseq;
  qint32  m_k0;
  qint32  m_kdone;
  qint32  m_nPick;
  FrequencyList_v2::const_iterator m_frequency_list_fcal_iter;
  qint32  m_nTx73;
  qint32  m_UTCdisk;
  qint32  m_wait;
  qint32  m_i3bit;
  qint32  m_isort;
  qint32  m_max_dB;
  qint32  m_nDXped=0;
  qint32  m_nSortedHounds=0;
  qint32  m_nHoundsCalling=0;
  qint32  m_Nlist=12;
  qint32  m_Nslots=5;
  qint32  m_nFoxMsgTimes[5]={0,0,0,0,0};
  qint32  m_tAutoOn;
//  qint32  m_maxQSOs;
  qint32  m_tFoxTx=0;
  qint32  m_tFoxTx0=0;
  qint32  m_maxStrikes=3;      //Max # of repeats: 3 strikes and you're out
  qint32  m_maxFoxWait=3;      //Max wait time for expected Hound replies
  qint32  m_foxCQtime=10;      //CQs at least every 5 minutes
  qint32  m_tFoxTxSinceCQ=999; //Fox Tx cycles since most recent CQ
  qint32  m_nFoxFreq;          //Audio freq at which Hound received a call from Fox
  qint32  m_nSentFoxRrpt=0;    //Serial number for next R+rpt Hound will send to Fox

  bool    m_btxok;		//True if OK to transmit
  bool    m_diskData;
  bool    m_loopall;
  bool    m_decoderBusy;
  QString m_decoderBusyBand;
  QMap<qint32, qint32> m_lastDecodeStartMap;  // submode, decode k start position
  Radio::Frequency m_decoderBusyFreq;
  QDateTime m_decoderBusyStartTime;
  bool    m_auto;
  bool    m_restart;
  bool    m_startAnother;
  bool    m_saveDecoded;
  bool    m_saveAll;
  bool    m_widebandDecode;
  bool    m_call3Modified;
  bool    m_bDecoded;
  bool    m_noSuffix;
  bool    m_blankLine;
  bool    m_decodedText2;
  bool    m_freeText;
  bool    m_sentFirst73;
  int     m_currentMessageType;
  QString m_currentMessage;
  int     m_currentMessageBits;
  int     m_lastMessageType;
  QString m_lastMessageSent;
  bool    m_bShMsgs;
  bool    m_bSWL;
  bool    m_uploading;
  bool    m_txNext;
  bool    m_grid6;
  bool    m_tuneup;
  bool    m_bTxTime;
  bool    m_bTransmittedEcho;
  bool    m_bDoubleClickAfterCQnnn;
  bool    m_bRefSpec;
  bool    m_bClearRefSpec;
  bool    m_bTrain;
  bool    m_bUseRef;
  bool    m_bAltV;
  bool    m_bNoMoreFiles;
  bool    m_bQRAsyncWarned;
  bool    m_bDoubleClicked;
  bool    m_bCallingCQ;
  bool    m_bAutoReply;
  bool    m_bCheckedContest;
  bool    m_bWarnedSplit=false;
  bool    m_bWarnedHound=false;

  enum
    {
      CALLING,
      REPLYING,
      REPORT,
      ROGER_REPORT,
      ROGERS,
      SIGNOFF
    }
    m_QSOProgress;

  int           m_extFreeTxtPos;
  int			m_ihsym;
  int			m_nzap;
  int			m_npts8;
  float		m_px;
  float   m_pxmax;
  float		m_df3;
  int			m_iptt0;
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
  QLabel band_hopping_label;
  QProgressBar progressBar;
  QLabel watchdog_label;
  QLabel wpm_label;

  QFuture<void> m_wav_future;
  QFutureWatcher<void> m_wav_future_watcher;
  QFutureWatcher<void> watcher3;
  QFutureWatcher<QString> m_saveWAVWatcher;

  //QPointer<QProcess> proc_js8;

  QTimer m_guiTimer;
  QTimer ptt1Timer;                 //StartTx delay
  QTimer ptt0Timer;                 //StopTx delay
  QTimer logQSOTimer;
  QTimer killFileTimer;
  QTimer tuneButtonTimer;
  QTimer uploadTimer;
  QTimer tuneATU_Timer;
  QTimer TxAgainTimer;
  QTimer minuteTimer;
  QTimer splashTimer;
  QTimer p1Timer;
  QTimer repeatTimer;

  QString m_path;
  QString m_baseCall;
  QString m_hisCall;
  QString m_hisGrid;
  QString m_appDir;
  QString m_palette;
  QString m_dateTime;
  QString m_mode;
  QString m_modeTx;
  QString m_fnameWE;            // save path without extension
  QString m_rpt;
  QString m_rptSent;
  QString m_rptRcvd;
  QString m_qsoStart;
  QString m_qsoStop;
  QString m_cmnd;
  QString m_msgSent0;
  QString m_fileToSave;
  QString m_calls;
  QString m_CQtype;
  QString m_opCall;
  QString m_houndCallers;        //Sorted list of Hound callers
  QString m_fm0;
  QString m_fm1;

  QSet<QString> m_pfx;
  QSet<QString> m_sfx;

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

  QQueue<DecodeParams> m_decoderQueue;
  QMap<QString, CachedFrame> m_messageDupeCache; // message frame -> date seen, submode seen, freq offset seen
  QMap<QString, QVariant> m_showColumnsCache; // table column:key -> show boolean
  QMap<QString, QVariant> m_sortCache; // table key -> sort by
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
  QMap<int, QList<ActivityDetail>> m_bandActivity; // freq -> [(text, last timestamp), ...]
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

  JSCChecker * m_checker;

  QMap<QString, QDateTime> m_callSelectedTime; // call -> timestamp when callsign was last selected
  QSet<QString> m_callSeenHeartbeat; // call
  int m_previousFreq;
  bool m_shouldRestoreFreq;
  bool m_bandHopped;
  Frequency m_bandHoppedFreq;

  struct FoxQSO       //Everything we need to know about QSOs in progress (or recently logged).
  {
    QString grid;       //Hound's declared locator
    QString sent;       //Report sent to Hound
    QString rcvd;       //Report received from Hound
    qint32  ncall;      //Number of times report sent to Hound
    qint32  nRR73;      //Number of times RR73 sent to Hound
    qint32  tFoxRrpt;   //m_tFoxTx (Fox Tx cycle counter) when R+rpt was received from Hound
    qint32  tFoxTxRR73; //m_tFoxTx when RR73 was sent to Hound
  };

  QMap<QString,FoxQSO> m_foxQSO;       //Key = HoundCall, value = parameters for QSO in progress
  QMap<QString,QString> m_loggedByFox; //Key = HoundCall, value = logged band

  QQueue<QString> m_houndQueue;        //Selected Hounds available for starting a QSO
  QQueue<QString> m_foxQSOinProgress;  //QSOs in progress: Fox has sent a report
  QQueue<qint64>  m_foxRateQueue;

  int m_hbInterval;
  int m_cqInterval;
  bool m_hbPaused;
  bool m_cqPaused;
  QDateTime m_nextHeartbeat;
  QDateTime m_nextCQ;
  QDateTime m_dateTimeQSOOn;
  QDateTime m_dateTimeLastTX;

  QSharedMemory *mem_js8;

  LogBook m_logBook;
  QString m_QSOText;
  unsigned m_msAudioOutputBuffered;
  unsigned m_framesAudioInputBuffered;
  unsigned m_downSampleFactor;
  QThread::Priority m_audioThreadPriority;
  QThread::Priority m_notificationAudioThreadPriority;
  QThread::Priority m_decoderThreadPriority;
  QThread::Priority m_networkThreadPriority;
  bool m_bandEdited;
  bool m_splitMode;
  bool m_monitoring;
  bool m_tx_when_ready;
  bool m_transmitting;
  bool m_tune;
  bool m_deadAirTone;
  bool m_tx_watchdog;           // true when watchdog triggered
  bool m_block_pwr_tooltip;
  bool m_PwrBandSetOK;
  bool m_bVHFwarned;
  bool m_bDisplayedOnce;
  Frequency m_lastMonitoredFrequency;
  double m_toneSpacing;
  int m_firstDecode;
  MessageClient * m_messageClient;
  MessageServer * m_messageServer;
  TCPClient * m_n3fjpClient;
  PSKReporter m_psk_Reporter;
  SpotClient *m_spotClient;
  APRSISClient *m_aprsClient;
  DisplayManual m_manual;
  QHash<QString, QVariant> m_pwrBandTxMemory; // Remembers power level by band
  QHash<QString, QVariant> m_pwrBandTuneMemory; // Remembers power level by band for tuning
  QByteArray m_geometryNoControls;
  QVector<double> m_phaseEqCoefficients;

  //---------------------------------------------------- private functions
  void readSettings();
  void set_application_font (QFont const&);
  void setDecodedTextFont (QFont const&);
  void writeSettings();
  void createStatusBar();
  void updateStatusBar();
  void clearDX ();
  void lookup();
  void ba2msg(QByteArray ba, char* message);
  void msgtype(QString t, QLineEdit* tx);
  void stub();
  void statusChanged();
  void fixStop();
  int computePeriodForSubmode(int submode);
  int computeBandwidthForSubmode(int submode);
  int computeStop(int submode, int period);
  //int computeCurrentCycle(int period);
  //int computeCycleStartForDecode(int cycle, int period);
  int computeCycleForDecode(int submode, int k);
  int computeAltCycleForDecode(int submode, int k, int offsetFrames);
  int computeFramesPerCycleForDecode(int submode);
  int computePeriodStartDelayForDecode(int submode);
  int computeFramesPerSymbolForDecode(int submode);
  int computeFramesNeededForDecode(int submode);
  bool shortList(QString callsign);
  void transmit (double snr = 99.);
  void rigFailure (QString const& reason);
  void spotSetLocal();
  void pskSetLocal ();
  void aprsSetLocal ();
  void spotReport(int submode, int dial, int offset, int snr, QString callsign, QString grid);
  void spotCmd(CommandDetail const & cmd);
  void spotAprsCmd(CommandDetail const & cmd);
  void pskLogReport(QString mode, int dial, int offset, int snr, QString callsign, QString grid);
  void spotAprsGrid(int dial, int offset, int snr, QString callsign, QString grid);
  Radio::Frequency dialFrequency();
  void setSubmode(int submode);
  int submodeNameToSubmode(QString speed);
  QString submodeName(int submode);
  void updateCurrentBand();
  void displayDialFrequency ();
  void transmitDisplay (bool);
  void locationChange(QString const& location);
  void postDecode (bool is_new, QString const& message);
  void displayTransmit();
  void updateModeButtonText();
  void updateButtonDisplay();
  void updateRepeatButtonDisplay();
  void updateTextDisplay();
  void updateFrameCountEstimate(int count);
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
  void switch_mode (Mode);
  void WSPR_scheduling ();
  void freqCalStep();
  void setRig (Frequency = 0);  // zero frequency means no change
  void WSPR_history(Frequency dialFreq, int ndecodes);
  QString WSPR_hhmm(int n);
  void CQTxFreq();
  QString save_wave_file (QString const& name
                          , short const * data
                          , int seconds
                          , QString const& my_callsign
                          , QString const& my_grid
                          , QString const& mode
                          , qint32 sub_mode
                          , Frequency frequency
                          , QString const& his_call
                          , QString const& his_grid) const;
  void read_wav_file (QString const& fname);
  QDateTime nextTransmitCycle();
  void resetAutomaticIntervalTransmissions(bool stopCQ, bool stopHB);
  void resetCQTimer(bool stop);
  void resetHeartbeatTimer(bool stop);
  void subProcessFailed (QString program, QStringList args, int exitCode, int status, QString errorString);
  void subProcessError (QString program, QStringList arguments, int errorCode, QString errorString);
  void statusUpdate ();
  void update_watchdog_label ();
  void on_the_minute ();
  void tryBandHop();
  void add_child_to_event_filter (QObject *);
  void remove_child_from_event_filter (QObject *);
  void setup_status_bar (bool vhf);

  void resetIdleTimer();
  void incrementIdleTimer();
  void tx_watchdog (bool triggered);
  qint64  nWidgets(QString t);
  void displayWidgets(qint64 n);
  void vhfWarning();
  QChar current_submode () const; // returns QChar {0} if sub mode is
                                  // not appropriate
  void write_frequency_entry (QString const& file_name);
  void write_transmit_entry (QString const& file_name);
};

extern int killbyname(const char* progName);
extern void getDev(int* numDevices,char hostAPI_DeviceName[][50],
                   int minChan[], int maxChan[],
                   int minSpeed[], int maxSpeed[]);
extern int next_tx_state(int pctx);

#endif // MAINWINDOW_H

#include "logqso.h"

#include <QString>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QUdpSocket>
#include <QComboBox>
#include <QAbstractItemView>

#include "logbook/adif.h"
#include "MessageBox.hpp"
#include "Configuration.hpp"
#include "Bands.hpp"
#include "MaidenheadLocatorValidator.hpp"
#include "DriftingDateTime.h"

#include "ui_logqso.h"
#include "moc_logqso.cpp"

LogQSO::LogQSO(QString const& programTitle, QSettings * settings
               , Configuration const * config, QWidget *parent)
  : QDialog {parent, Qt::WindowStaysOnTopHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint}
  , ui(new Ui::LogQSO)
  , m_settings (settings)
  , m_config {config}
{
  ui->setupUi(this);
  setWindowTitle(programTitle + " - Log QSO");
  ui->grid->setValidator (new MaidenheadLocatorValidator {this});

  auto b = ui->buttonBox->button(QDialogButtonBox::Save);
  if(b){
      b->setText("Add to Log");
  }
}

LogQSO::~LogQSO ()
{
}

bool LogQSO::acceptText(QString text){
    auto w = focusWidget();
    if(!w){
        return false;
    }

    auto name = QString(w->metaObject()->className());
    if(name != "QLineEdit"){
        return false;
    }

    auto l = static_cast<QLineEdit*>(w);
    if(l->text().isEmpty()){
        // set
        l->setText(text);
    } else {
        // append
        l->setText(QString("%1 %2").arg(l->text()).arg(text));
    }

    return true;
}

QString LogQSO::currentCall(){
    return ui->call->text().trimmed();
}

void LogQSO::on_start_now_button_pressed(){
  ui->start_date_time->setDateTime(DriftingDateTime::currentDateTimeUtc());
}

void LogQSO::on_end_now_button_pressed(){
  ui->end_date_time->setDateTime(DriftingDateTime::currentDateTimeUtc());
}

void LogQSO::on_add_new_field_button_pressed(){
    createAdditionalField();
}

void LogQSO::createAdditionalField(QString key, QString value){
    QLineEdit * l = new QLineEdit(this);
    if(!value.isEmpty()){
        l->setText(value);
    }

    QComboBox * c = new QComboBox(this);
    c->insertItems(0, ADIF_FIELDS);
    c->insertItem(0, "");
    c->setEditable(true);
    c->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(c, &QComboBox::currentTextChanged, this, [l](const QString &text){
       l->setProperty("fieldKey", QVariant(text));
    });
    c->setCurrentText(key);

    auto layout = static_cast<QFormLayout*>(ui->additionalFields->layout());
    layout->addRow(c, l);

    // set tab ordering
    if(m_additionalFieldsControls.isEmpty()){
        setTabOrder(ui->cbComments, c);
    } else {
        setTabOrder(m_additionalFieldsControls.last(), c);
    }
    setTabOrder(c, l);
    setTabOrder(l, ui->add_new_field_button);
    c->setFocus();

    m_additionalFieldsControls.append(l);
    ui->additionalFields->setVisible(true);
    ui->additionalFields->adjustSize();

    // update the window layout
    updateGeometry();
}

QMap<QString, QVariant> LogQSO::collectAdditionalFields(){
    QMap<QString, QVariant> additionalFields;
    foreach(auto field, m_additionalFieldsControls){
        auto key = field->property("fieldKey").toString();
        if(key.isEmpty()){
            continue;
        }
        additionalFields[key] = QVariant(field->text());
    }
    return additionalFields;
}

void LogQSO::resetAdditionalFields(){
    ui->additionalFields->setVisible(false);

    if(!m_additionalFieldsControls.isEmpty()){
        auto layout = static_cast<QFormLayout*>(ui->additionalFields->layout());

#if QT_VERSION >= 0x050800
        for(int i = 0, count = layout->rowCount(); i < count; i++){
            layout->removeRow(0);
        }
#else
        QLayoutItem *child;
        while((child = layout->takeAt(0)) != 0){
            delete child;
        }
#endif

        m_additionalFieldsControls.clear();
    }

    setTabOrder(ui->cbComments, ui->add_new_field_button);
    updateGeometry();
}

void LogQSO::loadSettings ()
{
  m_settings->beginGroup ("LogQSO");
  restoreGeometry (m_settings->value ("geometry", saveGeometry ()).toByteArray ());
  ui->cbComments->setChecked (m_settings->value ("SaveComments", false).toBool ());
  m_comments = m_settings->value ("LogComments", "").toString();

  resetAdditionalFields();
  auto additionalFields = m_settings->value("AdditionalFields", {}).toStringList();
  QSet<QString> additionalFieldsSet;
  foreach(auto key, additionalFields){
      if(key.isEmpty()){
          continue;
      }
      if(additionalFieldsSet.contains(key)){
          continue;
      }

      createAdditionalField(key);
      additionalFieldsSet.insert(key);
  }

  m_settings->endGroup ();
}

void LogQSO::storeSettings () const
{
  m_settings->beginGroup ("LogQSO");
  m_settings->setValue ("geometry", saveGeometry ());
  m_settings->setValue ("SaveComments", ui->cbComments->isChecked ());
  m_settings->setValue ("LogComments", m_comments);

  auto additionalFields = QStringList{};
  foreach(auto field, m_additionalFieldsControls){
      auto key = field->property("fieldKey").toString();
      if(key.isEmpty()){
          continue;
      }
      additionalFields.append(key);
  }
  m_settings->setValue ("AdditionalFields", additionalFields);

  m_settings->endGroup ();
}

void LogQSO::initLogQSO(QString const& hisCall, QString const& hisGrid, QString mode,
                        QString const& rptSent, QString const& rptRcvd,
                        QDateTime const& dateTimeOn, QDateTime const& dateTimeOff,
                        Radio::Frequency dialFreq, QString const& myCall, QString const& myGrid,
                        bool toDATA, bool dBtoComments, bool bFox, QString const& opCall, QString const& comments)
{
  if(!isHidden()) return;

  loadSettings();

  ui->call->setFocus();
  ui->call->setText(hisCall);
  ui->grid->setText(hisGrid);
  ui->name->setText("");
  ui->comments->setText("");

  if (ui->cbComments->isChecked ()) ui->comments->setText(m_comments);

  if(dBtoComments) {
    QString t=mode;
    if(rptSent!="") t+="  Sent: " + rptSent;
    if(rptRcvd!="") t+="  Rcvd: " + rptRcvd;
    ui->comments->setText(t);
  }

  if(!comments.isEmpty()){
    ui->comments->setText(comments);
  }

  if(toDATA) mode="DATA";

  ui->mode->setText(mode);
  ui->sent->setText(rptSent);
  ui->rcvd->setText(rptRcvd);
  ui->start_date_time->setDateTime (dateTimeOn);
  ui->end_date_time->setDateTime (dateTimeOff);

  m_dialFreq=dialFreq;
  m_myCall=myCall;
  m_myGrid=myGrid;

  ui->band->setText (m_config->bands ()->find (dialFreq));
  ui->loggedOperator->setText(opCall);

  if(bFox) {
    accept();
  } else {
    show ();
  }
}

void LogQSO::accept()
{
  QString hisCall,hisGrid,mode,submode,rptSent,rptRcvd,dateOn,dateOff,timeOn,timeOff,band,operator_call;
  QString comments,name;

  hisCall=ui->call->text().toUpper();
  hisGrid=ui->grid->text().toUpper();
  mode = ui->mode->text().toUpper();
  if(mode == "JS8"){
    mode="MFSK";
    submode="JS8";
  }
  rptSent=ui->sent->text();
  rptRcvd=ui->rcvd->text();
  m_dateTimeOn = ui->start_date_time->dateTime ();
  m_dateTimeOff = ui->end_date_time->dateTime ();
  band=ui->band->text();
  name=ui->name->text();
  comments=ui->comments->text();
  m_comments=comments;
  QString strDialFreq(QString::number(m_dialFreq / 1.e6,'f',6));
  operator_call = ui->loggedOperator->text();
  //Log this QSO to ADIF file "js8call_log.adi"
  QString filename = "js8call_log.adi";  // TODO allow user to set
  ADIF adifile;
  auto adifilePath = QDir {QStandardPaths::writableLocation (QStandardPaths::AppDataLocation)}.absoluteFilePath (filename);
  adifile.init(adifilePath);

  auto additionalFields = collectAdditionalFields();

  QByteArray ADIF {adifile.QSOToADIF (hisCall, hisGrid, mode, submode, rptSent, rptRcvd, m_dateTimeOn, m_dateTimeOff, band
                                      , comments, name, strDialFreq, m_myCall, m_myGrid, operator_call, additionalFields)};

  if (!adifile.addQSOToFile (ADIF))
  {
    MessageBox::warning_message (this, tr ("Log file error"),
                                 tr ("Cannot open \"%1\"").arg (adifilePath));
  }

  //Log this QSO to file "js8call.log"
  static QFile f {QDir {QStandardPaths::writableLocation (QStandardPaths::AppDataLocation)}.absoluteFilePath ("js8call.log")};
  if(!f.open(QIODevice::Text | QIODevice::Append)) {
    MessageBox::warning_message (this, tr ("Log file error"),
                                 tr ("Cannot open \"%1\" for append").arg (f.fileName ()),
                                 tr ("Error: %1").arg (f.errorString ()));
  } else {
    QStringList logEntryItems = {
      m_dateTimeOn.date().toString("yyyy-MM-dd"),
      m_dateTimeOn.time().toString("hh:mm:ss"),
      m_dateTimeOff.date().toString("yyyy-MM-dd"),
      m_dateTimeOff.time().toString("hh:mm:ss"),
      hisCall,
      hisGrid,
      strDialFreq,
      (mode == "MFSK" ? "JS8" : mode),
      rptSent,
      rptRcvd,
      comments,
      name
    };

    if(!additionalFields.isEmpty()){
        foreach(auto value, additionalFields.values()){
            logEntryItems.append(value.toString());
        }
    }

    QTextStream out(&f);
    out << logEntryItems.join(",") << Qt::endl;
    out.flush();
    flushFileBuffer(f);
    f.close();
  }

  Q_EMIT acceptQSO (m_dateTimeOff, hisCall, hisGrid, m_dialFreq, mode, submode, rptSent, rptRcvd, comments, name,m_dateTimeOn, operator_call, m_myCall, m_myGrid, ADIF, additionalFields);

  //Clean up and finish logging
  ui->call->clear();

  QDialog::accept();
}

// closeEvent is only called from the system menu close widget for a
// modeless dialog so we use the hideEvent override to store the
// window settings
void LogQSO::hideEvent (QHideEvent * e)
{
  storeSettings ();
  QDialog::hideEvent (e);
}

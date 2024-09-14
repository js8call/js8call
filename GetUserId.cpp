#include "GetUserId.hpp"

#include <stdexcept>

#include <QApplication>
#include <QString>
#include <QDialog>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>

//
// Dialog to get callsign
//
class CallsignDialog final
  : public QDialog
{
  Q_OBJECT;

private:
  Q_DISABLE_COPY (CallsignDialog);

public:
  explicit CallsignDialog (QWidget * parent = nullptr)
    : QDialog {parent}
  {
    setWindowTitle (QApplication::applicationName () + " - " + tr ("Callsign"));
    
    callsign_.setValidator (new QRegularExpressionValidator {QRegularExpression {"[A-Za-z0-9]+"}, this});
    
    auto form_layout = new QFormLayout ();
    form_layout->addRow ("&Callsign:", &callsign_);
    
    auto main_layout = new QVBoxLayout (this);
    main_layout->addLayout (form_layout);
    
    auto button_box = new QDialogButtonBox {QDialogButtonBox::Ok | QDialogButtonBox::Cancel};
    main_layout->addWidget (button_box);

    connect (button_box, &QDialogButtonBox::accepted, this, &CallsignDialog::accept);
    connect (button_box, &QDialogButtonBox::rejected, this, &CallsignDialog::reject);
  }
  
  QString callsign () const {return callsign_.text ();}
  
private:
  QLineEdit callsign_;
};

#include "GetUserId.moc"

QString get_user_id ()
{
  // get the users callsign so we can use it to persist the
  // settings and log file against a unique tag
  QString id;
  {
    CallsignDialog dialog;
    while (id.isEmpty ())
      {
        if (QDialog::Accepted == dialog.exec ())
          {
            id = dialog.callsign ().toUpper ();
          }
        else
          {
            throw std::runtime_error ("Callsign required");
          }
      }
  }

  return id;
}

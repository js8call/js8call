#include "messagereplydialog.h"
#include <QSet>
#include "EventFilter.hpp"
#include "varicode.h"
#include "ui_messagereplydialog.h"


MessageReplyDialog::MessageReplyDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MessageReplyDialog)
{
    ui->setupUi(this);
    ui->textEdit->installEventFilter(new EventFilter::EnterKeyPress([this](QKeyEvent * const event)
    {
      if (event->modifiers() & Qt::ShiftModifier) return false;
      this->accept();
      return true;
    }, this));
}

MessageReplyDialog::~MessageReplyDialog()
{
    delete ui;
}

void MessageReplyDialog::setLabel(QString value){
    ui->label->setText(value);
}

void MessageReplyDialog::setTextValue(QString text){
    ui->textEdit->setPlainText(text);
}

QString MessageReplyDialog::textValue() const {
    return ui->textEdit->toPlainText();
}

void MessageReplyDialog::on_textEdit_textChanged(){
    auto text = ui->textEdit->toPlainText();

    QString x;
    QString::const_iterator i;
    for(i = text.constBegin(); i != text.constEnd(); i++){
        auto ch = (*i).toUpper().toLatin1();
        if(ch == 10 || (32 <= ch && ch <= 126)){
            // newline or printable 7-bit ascii
            x += ch;
        }
    }

    if(x != text){
      int pos = ui->textEdit->textCursor().position();
      int maxpos = x.size();
      ui->textEdit->setPlainText(x);
      QTextCursor c = ui->textEdit->textCursor();
      c.setPosition(pos < maxpos ? pos : maxpos, QTextCursor::MoveAnchor);

      ui->textEdit->setTextCursor(c);
    }
}

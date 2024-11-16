#ifndef MESSAGEREPLYDIALOG_H
#define MESSAGEREPLYDIALOG_H

#include <QDialog>
#include <QString>

namespace Ui {
class MessageReplyDialog;
}

class QTextEdit;

class MessageReplyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MessageReplyDialog(QWidget *parent = 0);
    ~MessageReplyDialog();

    void setLabel(QString);
    void setTextValue(QString);
    QString textValue() const;

    QTextEdit * textEdit();


private slots:
    void on_textEdit_textChanged();

private:
    Ui::MessageReplyDialog *ui;
};

#endif // MESSAGEREPLAYDIALOG_H

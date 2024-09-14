#ifndef SELFDESTRUCTMESSAGEBOX_H
#define SELFDESTRUCTMESSAGEBOX_H

#include <QWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTimer>

class SelfDestructMessageBox : public QMessageBox
{
    Q_OBJECT

public:
    SelfDestructMessageBox(int timeout,
                           const QString& title,
                           const QString& text,
                           QMessageBox::Icon icon,
                           QMessageBox::StandardButtons buttons = QMessageBox::Ok | QMessageBox::Cancel,
                           QMessageBox::StandardButton defaultButton = QMessageBox::Ok,
                           bool show_countdown = false,
                           QWidget* parent = nullptr,
                           Qt::WindowFlags flags = Qt::WindowFlags());

    void showEvent(QShowEvent* event) override;

    void setShowCountdown(bool countdown){ m_show_countdown = countdown; }

private slots:
    void tick();

private:
    bool m_show_countdown;
    int m_timeout;
    QString m_text;
    QTimer m_timer;
};

#endif // SELFDESTRUCTMESSAGEBOX_H

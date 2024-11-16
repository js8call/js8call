#include "SelfDestructMessageBox.h"

SelfDestructMessageBox::SelfDestructMessageBox(
        int timeout,
        const QString& title,
        const QString& text,
        QMessageBox::Icon icon,
        QMessageBox::StandardButtons buttons,
        QMessageBox::StandardButton defaultButton,
        bool show_countdown,
        QWidget* parent,
        Qt::WindowFlags flags)
  : QMessageBox(icon, title, text, buttons, parent, flags),
    m_show_countdown(show_countdown),
    m_timeout(timeout),
    m_text(text)
{
    setDefaultButton(defaultButton);

    connect(&m_timer, &QTimer::timeout, this, &SelfDestructMessageBox::tick);
    m_timer.setInterval(1000);
}

void SelfDestructMessageBox::showEvent(QShowEvent* event)
{
    tick();
    m_timer.start();
    QMessageBox::showEvent(event);
}

void SelfDestructMessageBox::tick(){
    m_timeout--;

    if(m_timeout){
        if(m_show_countdown){
            setText(m_text.arg(m_timeout));
        } else {

            // if we don't show the countdown in the text, show it on the default button
            auto d = defaultButton();
            if(d){
                auto text = d->text();
                if(text.contains(" (")){
                    text = text.split(" (").first();
                }

                text = text.append(QString(" (%1) ").arg(m_timeout));
                d->setText(text);
            }

        }
        return;
    }

    // stop the timer
    m_timer.stop();

    // click the default
    auto d = defaultButton();
    if(d){
        d->click();
    }
}

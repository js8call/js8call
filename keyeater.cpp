#include "keyeater.h"


bool KeyPressEater::eventFilter(QObject *obj, QEvent *event){
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        bool processed = false;
        emit this->keyPressed(obj, keyEvent, &processed);
        if(processed){
            return true;
        }
    }

    // standard event processing
    //return QObject::eventFilter(obj, event);
    return false;
}

bool EscapeKeyPressEater::eventFilter(QObject *, QEvent *event){
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if(keyEvent->key() == Qt::Key_Escape){
            return true;
        }
    }

    // standard event processing
    //return QObject::eventFilter(obj, event);
    return false;
}

bool EnterKeyPressEater::eventFilter(QObject *obj, QEvent *event){
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if(keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return){
            bool processed = false;
            emit this->enterKeyPressed(obj, keyEvent, &processed);
            if(processed){
                return true;
            }
        }
    }

    // standard event processing
    //return QObject::eventFilter(obj, event);
    return false;
}

bool MousePressEater::eventFilter(QObject *obj, QEvent *event){
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        bool processed = false;
        emit this->mousePressed(obj, mouseEvent, &processed);
        if(processed){
            return true;
        }
    }

    // standard event processing
    //return QObject::eventFilter(obj, event);
    return false;
}

bool MouseDoubleClickEater::eventFilter(QObject *obj, QEvent *event){
    if (event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        bool processed = false;
        emit this->mouseDoubleClicked(obj, mouseEvent, &processed);
        if(processed){
            return true;
        }
    }

    // standard event processing
    //return QObject::eventFilter(obj, event);
    return false;
}

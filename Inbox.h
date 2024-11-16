#ifndef INBOX_H
#define INBOX_H

/**
 * (C) 2018 Jordan Sherer <kn4crd@gmail.com> - All Rights Reserved
 **/

#include <QObject>
#include <QString>
#include <QPair>
#include <QVariant>

#include "vendor/sqlite3/sqlite3.h"

#include "Message.hpp"


class Inbox
{
public:
    explicit Inbox(QString path);
    ~Inbox();

    // Low-Level Interface
    bool isOpen();
    bool open();
    void close();
    QString error();
    int count(QString type, QString query, QString match);
    QList<QPair<int, Message>> values(QString type, QString query, QString match, int offset, int limit);
    Message value(int key);
    int append(Message value);
    bool set(int key, Message value);
    bool del(int key);

    // High-Level Interface
    int countUnreadFrom(QString from);
    QPair<int, Message> firstUnreadFrom(QString from);

signals:

public slots:

private:
    QString path_;
    sqlite3 * db_;
};

#endif // INBOX_H

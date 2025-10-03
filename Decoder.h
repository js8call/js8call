#ifndef DECODER_H
#define DECODER_H

/**
 * (C) 2019 Jordan Sherer <kn4crd@gmail.com> - All Rights Reserved
 **/

#include "ProcessThread.h"

#include <QLoggingCategory>
#include <QByteArray>
#include <QPointer>
#include <QProcess>


class Worker : public QObject{
    Q_OBJECT
public:
    ~Worker();
public slots:
    void start(QString path, QStringList args);
    void quit();

    QProcess* process() const { return m_proc.data(); }
private:
    void setProcess(QProcess *proc, int msecs=1000);

signals:
    void ready(QByteArray t);
    void error(int errorCode, QString errorString);
    void finished(int exitCode, int statusCode, QString errorString);

private:
    QScopedPointer<QProcess> m_proc;
};


class Decoder: public QObject
{
    Q_OBJECT
public:
    Decoder(QObject *parent=nullptr);
    ~Decoder();

    void lock();
    void unlock();

    QString program() const {
        if(!m_worker.isNull() && m_worker->process() != nullptr){
            return m_worker->process()->program();
        }
        return {};
    }

    QStringList arguments() const {
        if(!m_worker.isNull() && m_worker->process() != nullptr){
            return m_worker->process()->arguments();
        }
        return {};
    }

private:
    Worker* createWorker();

public slots:
    void start(QThread::Priority priority);
    void quit();
    bool wait();

    void processStart(QString path, QStringList args);
    void processReady(QByteArray t);
    void processQuit();

    void processError(int errorCode, QString errorString);
    void processFinished(int exitCode, int statusCode, QString errorString);

signals:
    void startWorker(QString path, QStringList args);
    void quitWorker();

    void ready(QByteArray t);
    void error(int errorCode, QString errorString);
    void finished(int exitCode, int statusCode, QString errorString);

private:
    QPointer<Worker> m_worker;
    QThread m_thread;
};

Q_DECLARE_LOGGING_CATEGORY(decoder_js8)

#endif // DECODER_H

/**
 * This file is part of JS8Call.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * (C) 2019 Jordan Sherer <kn4crd@gmail.com> - All Rights Reserved
 *
 **/

#include "Decoder.h"

#include "commons.h"

#include <QTimer>


Decoder::Decoder(QObject *parent):
    QObject(parent)
{
}

Decoder::~Decoder(){
}

//
void Decoder::lock(){
    // NOOP
}

//
void Decoder::unlock(){
    // NOOP
}

//
Worker* Decoder::createWorker(){
    auto worker = new Worker();
    worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &Decoder::startWorker, worker, &Worker::start);
    connect(this, &Decoder::quitWorker, worker, &Worker::quit);
    connect(worker, &Worker::ready, this, &Decoder::processReady);
    connect(worker, &Worker::error, this, &Decoder::processError);
    connect(worker, &Worker::finished, this, &Decoder::processFinished);
    return worker;
}

//
void Decoder::start(QThread::Priority priority){
    m_thread.start(priority);
}

//
void Decoder::quit(){
    m_thread.quit();
}

//
bool Decoder::wait(){
    return m_thread.wait();
}

//
void Decoder::processStart(QString path, QStringList args){
    if(m_worker.isNull()){
        m_worker = createWorker();
    }

    emit startWorker(path, args);
}

//
void Decoder::processReady(QByteArray t){
    emit ready(t);
}

//
void Decoder::processQuit(){
    emit quitWorker();
}

//
void Decoder::processError(int errorCode, QString errorString){
    if(JS8_DEBUG_DECODE) qDebug() << "decoder process error" << errorCode << errorString;
    emit error(errorCode, errorString);
}

//
void Decoder::processFinished(int exitCode, int statusCode, QString errorString){
    if(JS8_DEBUG_DECODE) qDebug() << "decoder process finished" << exitCode << statusCode << errorString;
    emit finished(exitCode, statusCode, errorString);
}

////////////////////////////////////////
//////////////// WORKER ////////////////
////////////////////////////////////////

//
Worker::~Worker(){
}

//
void Worker::setProcess(QProcess *proc, int msecs){
    if(!m_proc.isNull()){
        bool b = m_proc->waitForFinished(msecs);
        if(!b) m_proc->close();
        m_proc.reset();
    }

    if(proc){
        m_proc.reset(proc);
    }
}

//
void Worker::start(QString path, QStringList args){
    if(JS8_DEBUG_DECODE) qDebug() << "decoder process starting...";

    auto proc = new QProcess(this);

    connect(proc, &QProcess::readyReadStandardOutput,
            [this, proc](){
                while(proc->canReadLine()){
                    emit ready(proc->readLine());
                }
            });

    connect(proc, &QProcess::errorOccurred,
            [this, proc] (QProcess::ProcessError errorCode) {
              emit error(int(errorCode), proc->errorString());
            });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, proc] (int exitCode, QProcess::ExitStatus status) {
              emit finished(exitCode, int(status), QString{proc->readAllStandardError()});
            });

    QProcessEnvironment env {QProcessEnvironment::systemEnvironment ()};
    env.insert ("OMP_STACKSIZE", "4M");
    proc->setProcessEnvironment (env);
    proc->start(path, args, QIODevice::ReadWrite | QIODevice::Unbuffered);

    setProcess(proc);
}

//
void Worker::quit(){
    setProcess(nullptr);
}

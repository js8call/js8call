#include "messagewindow.h"
#include "ui_messagewindow.h"
#include "moc_messagewindow.cpp"

#include <QDateTime>
#include <QMenu>

#include "Radio.hpp"
#include "keyeater.h"

template<typename T>
QList<T> listCopyReverse(QList<T> const &list){
    QList<T> newList = QList<T>();
    auto iter = list.end();
    while(iter != list.begin()){
        newList.append(*(--iter));
    }
    return newList;
}

MessageWindow::MessageWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MessageWindow)
{
    ui->setupUi(this);

    // connect selection model changed
    connect(ui->messageTableWidget->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MessageWindow::on_messageTableWidget_selectionChanged);

    // reply when key pressed in the reply box
    auto eke = new EnterKeyPressEater();
    connect(eke, &EnterKeyPressEater::enterKeyPressed, this, [this](QObject *, QKeyEvent * e, bool *pProcessed){
        if(e->modifiers() & Qt::ShiftModifier){
            if(pProcessed) *pProcessed = false;
            return;
        }

        if(pProcessed) *pProcessed = true;
        ui->replyPushButton->click();
    });
    ui->replytextEdit->installEventFilter(eke);

    ui->messageTableWidget->horizontalHeader()->setVisible(true);
    ui->messageTableWidget->resizeColumnsToContents();

    ui->messageTableWidget->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto deleteAction = new QAction("Delete", ui->messageTableWidget);
    connect(deleteAction, &QAction::triggered, this, [this](){
        auto items = ui->messageTableWidget->selectedItems();
        if(items.isEmpty()){
            return;
        }
        auto item = items.first();
        auto col = ui->messageTableWidget->item(item->row(), 1);
        if(!col){
            return;
        }
        bool ok = false;
        auto mid = col->data(Qt::UserRole).toInt(&ok);
        if(!ok){
            return;
        }

        ui->messageTableWidget->removeRow(item->row());

        emit this->deleteMessage(mid);
    });
    ui->messageTableWidget->addAction(deleteAction);
}

MessageWindow::~MessageWindow()
{
    delete ui;
}

void MessageWindow::setCall(const QString &call){
    setWindowTitle(QString("Messages: %1").arg(call == "%" ? "All" : call));
}

void MessageWindow::populateMessages(QList<QPair<int, Message> > msgs){
    for(int i = ui->messageTableWidget->rowCount(); i >= 0; i--){
        ui->messageTableWidget->removeRow(i);
    }

    ui->messageTableWidget->setUpdatesEnabled(false);
    {
        foreach(auto pair, msgs){
            auto mid = pair.first;
            auto msg = pair.second;
            auto params = msg.params();

            int row = ui->messageTableWidget->rowCount();
            ui->messageTableWidget->insertRow(row);

            int col = 0;

            auto typeItem = new QTableWidgetItem(msg.type() == "UNREAD" ? "\u2691" : "");
            typeItem->setData(Qt::UserRole, msg.type());
            typeItem->setTextAlignment(Qt::AlignCenter);
            ui->messageTableWidget->setItem(row, col++, typeItem);

            auto midItem = new QTableWidgetItem(QString::number(mid));
            midItem->setData(Qt::UserRole, mid);
            midItem->setTextAlignment(Qt::AlignCenter);
            ui->messageTableWidget->setItem(row, col++, midItem);

            auto date = params.value("UTC").toString();
            auto timestamp = QDateTime::fromString(date, "yyyy-MM-dd hh:mm:ss");
            auto dateItem = new QTableWidgetItem(timestamp.toString());
            dateItem->setData(Qt::UserRole, timestamp);
            dateItem->setTextAlignment(Qt::AlignCenter);
            ui->messageTableWidget->setItem(row, col++, dateItem);

            auto dial = (quint64)params.value("DIAL").toInt();
            auto dialItem = new QTableWidgetItem(QString("%1 MHz").arg(Radio::pretty_frequency_MHz_string(dial)));
            dialItem->setData(Qt::UserRole, dial);
            dialItem->setTextAlignment(Qt::AlignCenter);
            ui->messageTableWidget->setItem(row, col++, dialItem);

            auto path = params.value("PATH").toString();
            auto segs = listCopyReverse(path.split(">"));
            auto fromItem = new QTableWidgetItem(segs.join(" via "));
            fromItem->setData(Qt::UserRole, path);
            fromItem->setTextAlignment(Qt::AlignCenter);
            ui->messageTableWidget->setItem(row, col++, fromItem);

            auto to = params.value("TO").toString();
            auto toItem = new QTableWidgetItem(to);
            toItem->setData(Qt::UserRole, to);
            toItem->setTextAlignment(Qt::AlignCenter);
            ui->messageTableWidget->setItem(row, col++, toItem);

            auto text = params.value("TEXT").toString();
            auto textItem = new QTableWidgetItem(text);
            textItem->setData(Qt::UserRole, text);
            textItem->setTextAlignment(Qt::AlignVCenter);
            ui->messageTableWidget->setItem(row, col++, textItem);
        }

        ui->messageTableWidget->resizeColumnToContents(0);
        ui->messageTableWidget->resizeColumnToContents(1);
        ui->messageTableWidget->resizeColumnToContents(2);
        ui->messageTableWidget->resizeColumnToContents(3);
        ui->messageTableWidget->resizeColumnToContents(4);
        ui->messageTableWidget->resizeColumnToContents(5);
    }
    ui->messageTableWidget->setUpdatesEnabled(true);

    if(ui->messageTableWidget->rowCount() > 0){
        ui->messageTableWidget->selectRow(0);
    }
}

QString MessageWindow::prepareReplyMessage(QString path, QString text){
    return QString("%1 MSG %2").arg(path).arg(text);
}

void MessageWindow::on_messageTableWidget_selectionChanged(const QItemSelection &/*selected*/, const QItemSelection &/*deselected*/){
    auto row = ui->messageTableWidget->currentRow();

    // message column
    auto item = ui->messageTableWidget->item(row, ui->messageTableWidget->columnCount()-1);
    if(!item){
        return;
    }

    auto text = item->data(Qt::UserRole).toString();
    ui->messageTextEdit->setPlainText(text);
}

void MessageWindow::on_replyPushButton_clicked(){
    auto row = ui->messageTableWidget->currentRow();

    // from column
    auto item = ui->messageTableWidget->item(row, ui->messageTableWidget->columnCount()-3);
    if(!item){
        return;
    }

    auto path = item->data(Qt::UserRole).toString();
    auto text = "[MESSAGE]";
    auto message = prepareReplyMessage(path, text);

    emit replyMessage(message);
}

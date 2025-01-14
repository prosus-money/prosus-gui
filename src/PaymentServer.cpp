// Copyright (c) 2012-2017, The CryptoNote developers
// Copyleft (c) 2016-2018, Prosus Corp RTD
// Distributed under the MIT/X11 software license

#include <QApplication>
#include <cstdlib>
#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFileOpenEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStringList>
#if QT_VERSION < 0x050000
#include <QUrl>
#endif
#include "PaymentServer.h"
#include "Settings.h"
#include "CurrencyAdapter.h"

using namespace boost;
using namespace WalletGui;

const int IPC_CONNECT_TIMEOUT = 1000; // milliseconds
const QString IPC_PREFIX("prosus:");

static QString ipcServerName()
{
    QString name("ProsusMoney");

    return name;
}

//
// This stores payment requests received before
// the main GUI window is up and ready to ask the user
// to send payment.
//
static QStringList savedPaymentRequests;

//
// Sending to the server is done synchronously, at startup.
// If the server isn't already running, startup continues,
// and the items in savedPaymentRequest will be handled
// when uiReady() is called.
//
bool PaymentServer::ipcSendCommandLine()
{
    bool fResult = false;

    const QStringList& args = qApp->arguments();
    for (int i = 1; i < args.size(); i++)
    {
        if (!args[i].startsWith(IPC_PREFIX, Qt::CaseInsensitive))
            continue;
        savedPaymentRequests.append(args[i]);
    }

    foreach (const QString& arg, savedPaymentRequests)
    {
        QLocalSocket* socket = new QLocalSocket();
        socket->connectToServer(ipcServerName(), QIODevice::WriteOnly);
        if (!socket->waitForConnected(IPC_CONNECT_TIMEOUT))
            return false;

        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);
        out << arg;
        out.device()->seek(0);
        socket->write(block);
        socket->flush();

        socket->waitForBytesWritten(IPC_CONNECT_TIMEOUT);
        socket->disconnectFromServer();
        delete socket;
        fResult = true;
    }
    return fResult;
}

PaymentServer::PaymentServer(QApplication* parent) : QObject(parent), saveURIs(true)
{
    // Install global event filter to catch QFileOpenEvents on the mac (sent when you click "prosus:" links)
    parent->installEventFilter(this);

    QString name = ipcServerName();

    // Clean up old socket leftover from a crash:
    QLocalServer::removeServer(name);

    uriServer = new QLocalServer(this);

    if (!uriServer->listen(name))
        qDebug() << tr("Cannot start ProsusMoney: click-to-pay handler");
    else
        connect(uriServer, SIGNAL(newConnection()), this, SLOT(handleURIConnection()));
}

bool PaymentServer::eventFilter(QObject *object, QEvent *event)
{
    // clicking on prosus: URLs creates FileOpen events on the Mac:
    if (event->type() == QEvent::FileOpen)
    {
        QFileOpenEvent* fileEvent = static_cast<QFileOpenEvent*>(event);
        if (!fileEvent->url().isEmpty())
        {
            if (saveURIs) // Before main window is ready:
                savedPaymentRequests.append(fileEvent->url().toString());
            else
                Q_EMIT receivedURI(fileEvent->url().toString());
            return true;
        }
    }
    return false;
}

void PaymentServer::uiReady()
{
    saveURIs = false;
    foreach (const QString& s, savedPaymentRequests)
        Q_EMIT receivedURI(s);

    savedPaymentRequests.clear();
}

void PaymentServer::handleURIConnection()
{
    QLocalSocket *clientConnection = uriServer->nextPendingConnection();

    while (clientConnection->bytesAvailable() < (int)sizeof(quint32))
        clientConnection->waitForReadyRead();

    connect(clientConnection, SIGNAL(disconnected()),
            clientConnection, SLOT(deleteLater()));

    QDataStream in(clientConnection);
    in.setVersion(QDataStream::Qt_4_0);
    if (clientConnection->bytesAvailable() < (int)sizeof(quint16)) {
        return;
    }
    QString message;
    in >> message;

    if (saveURIs)
        savedPaymentRequests.append(message);
    else
        Q_EMIT receivedURI(message);
}

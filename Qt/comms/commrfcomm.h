#ifndef COMMRFCOMM_H
#define COMMRFCOMM_H

#include "comm.h"
#include <QBluetoothSocket>
#include <QBluetoothUuid>

class CommRFCOMM : public Comm
{
    Q_OBJECT
public:
    explicit CommRFCOMM(QObject *parent = nullptr);
    void open(const QBluetoothDeviceInfo& deviceInfo) override;
    void close() override;
protected:
    qint64 write(const QByteArray &data) override;
private slots:
    void onStateChanged();
    void onErrorOccurred(QBluetoothSocket::SocketError error);
private:
    QBluetoothSocket* m_socket = nullptr;
    const QBluetoothUuid m_serviceUUID = QBluetoothUuid(QStringLiteral("EDF00000-EDFE-DFED-FEDF-EDFEDFEDFEDF"));
};

#endif // COMMRFCOMM_H

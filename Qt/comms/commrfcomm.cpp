#include "commrfcomm.h"

#include <QBluetoothLocalDevice>
#include <QBluetoothDeviceInfo>

CommRFCOMM::CommRFCOMM(QObject *parent)
    : Comm{parent}
{
    m_socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol);
    connect(m_socket, &QIODevice::readyRead, this, &CommRFCOMM::onReadyRead);
    connect(m_socket, &QBluetoothSocket::stateChanged, this, &CommRFCOMM::onStateChanged);
    connect(m_socket, &QBluetoothSocket::errorOccurred, this, &CommRFCOMM::onErrorOccurred);
}

void CommRFCOMM::open(const QBluetoothDeviceInfo &deviceInfo)
{
    m_socket->connectToService(deviceInfo.address(), m_serviceUUID);
}

void CommRFCOMM::close()
{
    m_socket->disconnectFromService();
    m_socket->close();
}

qint64 CommRFCOMM::write(const QByteArray &data)
{
    return m_socket->write(data);
}

void CommRFCOMM::onStateChanged()
{
    QBluetoothSocket::SocketState state = m_socket->state();
    qDebug() << "onStateChanged():" << state;
    if(state == QBluetoothSocket::SocketState::ConnectedState)
    {
        emit stateChanged(true);
        emit showMessage(tr("Device Connected"));
    }
    else if(state == QBluetoothSocket::SocketState::UnconnectedState)
    {
        close();
        emit stateChanged(false);
        emit showMessage(tr("Device Disconnected"));
    }
}

void CommRFCOMM::onErrorOccurred(QBluetoothSocket::SocketError error)
{
    qDebug() << "Error:" << error;
    // the close() there is necessary
    // otherwise the signal QBluetoothSocket::error() will always be emitted.
    // calling close() in CommRFCOMM::onStateChanged() doesn't take effect.
    if(error == QBluetoothSocket::SocketError::RemoteHostClosedError)
        close();
}

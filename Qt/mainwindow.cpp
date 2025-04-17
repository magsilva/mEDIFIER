#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "comms/commrfcomm.h"
#include "comms/commble.h"

#include <QDebug>
#include <QScroller>
#include <QScrollBar>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QAction>
#include <QMenu>
#ifdef Q_OS_ANDROID
#include <QtAndroid>
#include <QAndroidJniEnvironment>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_sysTrayIcon(new QSystemTrayIcon(this))
{
    // MainWindow only have one instance so this should work
    m_ptr = this;
    ui->setupUi(this);

#ifdef Q_OS_ANDROID
    m_settings = new QSettings("wh201906", "mEDIFIER");
#else
    // Firstly, find it in appConfig directory
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/" + "preference.ini";
    if(!QFileInfo::exists(configPath))
    {
        QDir d{configPath};
        if (!d.mkpath(d.absolutePath())) {
            // If no config file is found, create one in current working directory
            configPath = "preference.ini";
        }
    }
    m_settings = new QSettings(configPath, QSettings::IniFormat);
    m_settings->setIniCodec("UTF-8");

    restoreGeometry(m_settings->value("MainWidget/geometry").toByteArray());
    restoreState(m_settings->value("MainWidget/windowState").toByteArray());
#endif

    m_deviceForm = new DeviceForm;
    // m_device = new BaseDevice; // in changeDevice()
    m_devForm = new DevForm;

    m_deviceForm->setSettings(m_settings);

    ui->tabWidget->insertTab(0, m_deviceForm, tr("Device"));
    ui->tabWidget->setCurrentIndex(0);

    connect(m_deviceForm, &DeviceForm::connectTo, this, &MainWindow::connectToDevice);
    connect(m_deviceForm, &DeviceForm::disconnectDevice, this, &MainWindow::disconnectDevice);
    connect(m_deviceForm, &DeviceForm::showMessage, this, &MainWindow::showMessage);
    connect(this, &MainWindow::commStateChanged, m_deviceForm, &DeviceForm::onCommStateChanged);
    connect(m_devForm, &DevForm::showMessage, this, &MainWindow::showMessage);
    connect(this, &MainWindow::devMessage, m_devForm, &DevForm::handleDevMessage);

    loadDeviceInfo();
    changeDevice("basedevice");

#ifdef Q_OS_ANDROID
    ui->statusBar->hide();
#endif
    QScroller::grabGesture(ui->scrollArea);
//    ui->scrollArea->horizontalScrollBar()->setEnabled(false);

    if ( m_settings->value("Global/EnableTray", false).toBool() )
    {
        createTrayIcon();
    }

    m_autoConnect = m_settings->value("Global/AutoConnect", false).toBool();
    if ( m_autoConnect )
        autoConnect();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::loadDeviceInfo()
{
    QFile deviceInfoFile(":/devices/deviceinfo.json");
    deviceInfoFile.open(QIODevice::ReadOnly);
    QJsonDocument deviceInfoDoc = QJsonDocument::fromJson(deviceInfoFile.readAll());
    deviceInfoFile.close();
    if(deviceInfoDoc.isObject())
        m_deviceInfo = new QJsonObject(deviceInfoDoc.object());
    else
        qDebug() << "Failed to load Device Info";

    for(auto it = m_deviceInfo->constBegin(); it != m_deviceInfo->constEnd(); ++it)
    {
        QJsonObject details = it->toObject();
        ui->deviceBox->addItem(tr(details["Name"].toString().toUtf8()), it.key());
        QString serviceUUID = details["UniqueServiceUUID"].toString();
        if(!serviceUUID.isEmpty())
            m_deviceServiceMap[QBluetoothUuid(serviceUUID)] = it.key();
    }
}

void MainWindow::switchToDevice()
{
    auto lastDevice = m_settings->value("Global/LastAudioDeviceName", QString()).toString();
    if ( !lastDevice.isEmpty() )
    {
        // changeDevice( lastDevice );
        //
        ui->tabWidget->setCurrentIndex(1);
        QDateTime killTime = QDateTime::currentDateTime().addSecs(5);
        while ( !m_connected && QDateTime::currentDateTime() < killTime )
        {
            qApp->processEvents(QEventLoop::AllEvents,100);
        }
        // QTimer::singleShot(100, [&] {emit readSettings();});
        // emit readSettings();
        if (m_device)
            m_device->readSettings(true);

    }
}

void MainWindow::connectToDevice(const QString& address, bool isBLE)
{
    if(m_comm != nullptr)
    {
        m_comm->deleteLater();
        m_comm = nullptr;
    }
    if(isBLE)
        m_comm = new CommBLE;
    else
        m_comm = new CommRFCOMM;

    connect(m_comm, &Comm::stateChanged, this, &MainWindow::onCommStateChanged);
    connect(m_comm, &Comm::showMessage, this, &MainWindow::showMessage);
    connectDevice2Comm();

    m_comm->open(address);

    if (m_autoConnect)
    {
        switchToDevice();
    }

    if ( m_sysTrayIcon && m_sysTrayIcon->isVisible() )
    {
        m_sysTrayIcon->setIcon( m_deviceConnected_normal );
    }

}

void MainWindow::disconnectDevice()
{
    m_comm->close();
    m_connected = false;
    emit commStateChanged(false);
    processDeviceStatus(DeviceStatus::NO_DEVICE);
}

void MainWindow::onCommStateChanged(bool state)
{
    if(!m_connected && state)
    {
        m_connected = true;
        emit commStateChanged(true);
        m_trayDeviceMenu->setVisible(true);
    }
    else if(m_connected && !state)
    {
        m_connected = false;
        emit commStateChanged(false);
        m_trayDeviceMenu->setVisible(false);
    }
}

void MainWindow::on_readSettingsButton_clicked()
{
    if(m_connected)
        emit readSettings();
    else
        showMessage(tr("Device not connected"));
}

void MainWindow::connectToAudio(const QString& address)
{
#ifdef Q_OS_ANDROID
    QAndroidJniEnvironment androidEnv;
    QAndroidJniObject addressObj;
    if(address.isEmpty())
    {
        m_settings->beginGroup("Global");
        QString lastAddress = m_settings->value("LastAudioDeviceAddress").toString();
        m_settings->endGroup();
        if(lastAddress.isEmpty())
        {
            qDebug() << "Info: last audio device address is empty";
            return;
        }
        qDebug() << "Info: using last audio device address:" << lastAddress;
        addressObj = QAndroidJniObject::fromString(lastAddress);
    }
    else
    {
        qDebug() << "Info: audio device address:" << address;
        addressObj = QAndroidJniObject::fromString(address);
    }
    QtAndroid::androidActivity().callMethod<void>("connectToDevice", "(Ljava/lang/String;)V", addressObj.object<jstring>());
#else
    Q_UNUSED(address)
#endif
}

void MainWindow::updateLastAudioDeviceAddress(const QString& address)
{
    m_settings->beginGroup("Global");
    m_settings->setValue("LastAudioDeviceAddress", address);
    const BaseDevice* ptr = qobject_cast<BaseDevice*>(QObject::sender());
    if (ptr)
        m_settings->setValue("LastAudioDeviceName", ptr->deviceName());
    m_settings->endGroup();
}

void MainWindow::showMessage(const QString& msg)
{
#ifdef Q_OS_ANDROID
    QtAndroid::runOnAndroidThread([ = ]
    {
        QAndroidJniObject javaString = QAndroidJniObject::fromString(msg);
        QAndroidJniObject toast = QAndroidJniObject::callStaticObjectMethod("android/widget/Toast", "makeText",
                "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;",
                QtAndroid::androidActivity().object(),
                javaString.object(),
                jint(0)); // short toast
        toast.callMethod<void>("show");
    });
#else
    ui->statusBar->showMessage(msg, 2000); // 2000ms is the duration of short toast
#endif
}

void MainWindow::changeDevice(const QString& deviceName)
{
    // the deviceName is the key in deviceinfo.json, not "Name"
    if(!m_deviceInfo->contains(deviceName))
        return;
    if(m_device != nullptr)
        m_device->deleteLater();
    QJsonObject details = m_deviceInfo->value(deviceName).toObject();
    m_device = new BaseDevice;
    m_device->setDeviceName(deviceName);
    m_device->setWindowTitle(tr(details["Name"].toString().toUtf8()));
    m_device->setMaxNameLength(details["MaxNameLength"].toInt());
    const QVariantList hiddenFeatureList = details["HiddenFeatures"].toArray().toVariantList();
    for(const auto& it : hiddenFeatureList)
    {
        m_device->hideWidget(it.toString());
    }

    connect(this, &MainWindow::readSettings, m_device, &BaseDevice::readSettings);
    connect(m_device, &BaseDevice::showMessage, this, &MainWindow::showMessage);
    connect(m_device, &BaseDevice::connectToAudio, this, &MainWindow::connectToAudio);
    connect(m_device, &BaseDevice::updateLastAudioDeviceAddress, this, &MainWindow::updateLastAudioDeviceAddress);

    connectDevice2Comm();

    ui->tabWidget->setTabText(1, m_device->windowTitle());
    ui->scrollAreaWidgetContents->layout()->addWidget(m_device);
}

void MainWindow::on_deviceBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    changeDevice(ui->deviceBox->currentData().toString());
}

void MainWindow::connectDevice2Comm()
{
    if(m_device == nullptr || m_comm == nullptr)
    {
        qDebug() << "Error: Failed to connect Device to Comm";
        return;
    }
    connect(m_device, QOverload<const QByteArray&, bool>::of(&BaseDevice::sendCommand), m_comm, QOverload<const QByteArray&, bool>::of(&Comm::sendCommand));
    connect(m_device, QOverload<const char*, bool>::of(&BaseDevice::sendCommand), m_comm, QOverload<const char*, bool>::of(&Comm::sendCommand));
    connect(m_comm, &Comm::newData, m_device, &BaseDevice::processData);
    connect(m_comm, &Comm::deviceFeature, this, &MainWindow::processDeviceFeature);
    connect(m_device, &BaseDevice::updateStatus, this, &MainWindow::processDeviceStatus);

    // Calling MainWindow::connectDevice2Comm() indicates the m_device is reconnected
    // Clear the cached MAC address
    m_device->clearAddress();
}

void MainWindow::processDeviceFeature(const QString& feature, bool isBLE)
{
    if(isBLE)
    {
        QBluetoothUuid serviceUUID = QBluetoothUuid(feature);
        qDebug() << "Device service UUID:" << feature;
        if(m_deviceServiceMap.contains(serviceUUID))
        {
            int index = ui->deviceBox->findData(m_deviceServiceMap[serviceUUID]);
            ui->deviceBox->setCurrentIndex(index); // triggers changeDevice()
            showMessage(tr("Device detected") + ": " + ui->deviceBox->currentText());
        }
    }
}

void MainWindow::processDeviceStatus(const DeviceStatus &status)
{
    switch (status) {
    case NORMAL:
        m_sysTrayIcon->setIcon(m_deviceConnected_normal);
        break;
    case NOISE_CANCELATION:
        m_sysTrayIcon->setIcon(m_deviceConnected_noiseCancelation);
        break;
    case AMBIENT_SOUND:
        m_sysTrayIcon->setIcon(m_deviceConnected_ambientSound);
        break;
    default:
        m_sysTrayIcon->setIcon(m_noDevice);
        break;
    }
}

void MainWindow::on_trayMenuModeChanged()
{
    if ( m_autoConnect && !m_connected )
        autoConnect();

    auto ptr = qobject_cast<QAction*>(QObject::sender());
    if ( ptr->objectName() == "mNormalSound" )
    {
        m_device->setDeviceStatus(DeviceStatus::NORMAL);
    }
    else if ( ptr->objectName() == "mAmbientSound" )
    {
        m_device->setDeviceStatus(DeviceStatus::AMBIENT_SOUND);
    }
    else if ( ptr->objectName() == "mReductionSound" )
    {
        m_device->setDeviceStatus(DeviceStatus::NOISE_CANCELATION);
    }
    else
    {
        qDebug() << "unknown action called";
    }
}

void MainWindow::on_trayMenuDevicePowerOff()
{
    auto ans = QMessageBox::question(this, tr("Powering off the device"), tr("Are you sure?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
    if ( ans == QMessageBox::Yes )
    {
        changeDeviceStatus( DeviceStatus::POWEROFF );
    }
}

void MainWindow::on_trayMenuDeviceDisconnect()
{
    auto ans = QMessageBox::question(this, tr("Disconnecting the device"), tr("Are you sure?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
    if ( ans == QMessageBox::Yes )
    {
        changeDeviceStatus( DeviceStatus::DISCONNECT );
    }
}

void MainWindow::on_tabWidget_tabBarClicked(int index)
{
    Q_UNUSED(index);
    m_clickCounter++;
    if(m_clickCounter >= 8)
    {
        m_isDevMode = !m_isDevMode;
        m_clickCounter = 0;
        if(m_isDevMode)
        {
            showMessage(tr("Dev Mode ON"));
            qInstallMessageHandler(devMessageHandler);
            if(ui->tabWidget->indexOf(m_devForm) == -1)
                ui->tabWidget->addTab(m_devForm, m_devForm->windowTitle());
        }
        else
        {
            showMessage(tr("Dev Mode OFF"));
            qInstallMessageHandler(0);
            int devTabId = ui->tabWidget->indexOf(m_devForm);
            if(devTabId != -1)
                ui->tabWidget->removeTab(devTabId);
        }
    }
    QTimer::singleShot(5000, [&] {m_clickCounter = 0;});
}

void MainWindow::closeEvent(QCloseEvent *event)
{
#ifndef Q_OS_ANDROID
    m_settings->setValue("MainWidget/geometry", saveGeometry());
    m_settings->setValue("MainWidget/windowState", saveState());
#endif
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* e)
{
#ifndef Q_OS_ANDROID
    switch (e->type())
    {
    case QEvent::WindowStateChange:
    {
        if (this->windowState() & Qt::WindowMinimized)
        {
            e->ignore();
            QTimer::singleShot(100, this, SLOT(hide()));
            return;
        }

        break;
    }
    default:
        break;
    }
#endif
    QMainWindow::changeEvent(e);
}

void MainWindow::timerEvent(QTimerEvent *e)
{
#ifndef Q_OS_ANDROID
#endif
    QMainWindow::timerEvent(e);
}

void MainWindow::createTrayIcon()
{
    m_noDevice = QIcon(":/icons/white_headphones_no-device.png");
    m_deviceConnected_normal = QIcon(":/icons/white_headphones_connected.png");
    m_deviceConnected_noiseCancelation = QIcon(":/icons/headphones_noise-cancelation.png");
    m_deviceConnected_ambientSound = QIcon(":/icons/headphones_ambient.png");

    m_sysTrayIcon->setIcon( m_noDevice );
    setWindowIcon( m_noDevice );

    QAction *hide_action = new QAction( "Show app", m_sysTrayIcon );
    connect( hide_action, &QAction::triggered, this, &MainWindow::show );

    QAction *quit_action = new QAction( "Exit", m_sysTrayIcon );
    connect( quit_action, &QAction::triggered, this, &MainWindow::close );

    createDeviceTrayMenu();

    QMenu *tray_icon_menu = new QMenu(this);
    tray_icon_menu->addMenu(m_trayDeviceMenu);
    tray_icon_menu->addAction( hide_action );
    tray_icon_menu->addAction( quit_action );

    m_sysTrayIcon->setContextMenu( tray_icon_menu );

    m_sysTrayIcon->show();
}

void MainWindow::createDeviceTrayMenu()
{
    m_trayDeviceMenu = new QMenu(tr("Device"),this);

    QAction *normal_action = new QAction( "Normal", m_trayDeviceMenu );
    normal_action->setObjectName("mNormalSound");
    connect( normal_action, &QAction::triggered, this, &MainWindow::on_trayMenuModeChanged );

    QAction *ambient_action = new QAction( "Ambient sound", m_trayDeviceMenu );
    ambient_action->setObjectName("mAmbientSound");
    connect( ambient_action, &QAction::triggered, this, &MainWindow::on_trayMenuModeChanged );

    QAction *reduction_action = new QAction( "Noise reduction", m_trayDeviceMenu );
    reduction_action->setObjectName("mReductionSound");
    connect( reduction_action, &QAction::triggered, this, &MainWindow::on_trayMenuModeChanged );

    QAction *disconnect_action = new QAction( "Disconnect", m_trayDeviceMenu );
    connect( disconnect_action, &QAction::triggered, this, &MainWindow::on_trayMenuDeviceDisconnect );

    QAction *poweroff_action = new QAction( "Power-off", m_trayDeviceMenu );
    connect( poweroff_action, &QAction::triggered, this, &MainWindow::on_trayMenuDevicePowerOff );

    m_trayDeviceMenu->addSection(tr("Sound mode"));
    m_trayDeviceMenu->addAction( normal_action );
    m_trayDeviceMenu->addAction( ambient_action );
    m_trayDeviceMenu->addAction( reduction_action );

    m_trayDeviceMenu->addSection("Device");
    m_trayDeviceMenu->addAction( disconnect_action );
    m_trayDeviceMenu->addAction( poweroff_action );

    m_trayDeviceMenu->setVisible(false);
}

void MainWindow::changeDeviceStatus(const DeviceStatus &status)
{
    Q_UNUSED(status)
}

void MainWindow::autoConnect()
{
    if ( m_autoConnect )
    {
        m_settings->beginGroup("DeviceForm");
        auto addressStr = m_settings->value("LastDeviceAddress").toString();
        bool isBLE = m_settings->value("LastDeviceType").toString() == tr("BLE");
        m_settings->endGroup();

        connectToDevice(addressStr,isBLE);
    }
}


void MainWindow::devMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if(m_ptr != nullptr)
        emit m_ptr->devMessage(type, context, msg);
}


MainWindow* MainWindow::m_ptr = nullptr;

const char* MainWindow::m_translatedNames[] =
{
    QT_TR_NOOP("Generic Device"),
    QT_TR_NOOP("W820NB"),
    QT_TR_NOOP("W820NB Double Gold"),
    QT_TR_NOOP("W200BT Plus"),
};

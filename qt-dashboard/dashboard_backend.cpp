#include "dashboard_backend.h"
#include <algorithm>
#include <QDebug>
#include <QImageReader>
#include <QMutexLocker>

namespace {
QByteArray imageFormatHint(const QString &format)
{
    const QString lowerFormat = format.toLower();

    if (lowerFormat.contains(QStringLiteral("jpeg")) || lowerFormat.contains(QStringLiteral("jpg"))) {
        return QByteArrayLiteral("JPG");
    }

    if (lowerFormat.contains(QStringLiteral("png"))) {
        return QByteArrayLiteral("PNG");
    }

    return {};
}
}

DashboardBackend::DashboardBackend(QObject *parent)
    : QObject{parent}
{
    //connect timer signal to timertick slot to emulate speed increase
    // later, the onTimerTick itself will replaced with ros updating the variable
    // connect(&m_timer, &QTimer::timeout,this, &DashboardBackend::onTimerTick);
    // m_timer.start(50);
    m_vehicleNode = std::make_shared<VehicleDataNode>(this);
    m_cameraNode = std::make_shared<RpiCameraStreamNode>(this);
    m_executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    m_executor->add_node(m_vehicleNode);
    m_executor->add_node(m_cameraNode);
    m_rosThread = std::thread([this](){
        m_executor->spin();
    });
    // m_rosThread.detach();
}

// void DashboardBackend::onTimerTick()
// // Called every 50ms by the timer.
// // Simulates changing speed and rpm — later replaced by ROS 2 subscriber data.
// {
//     // Simulate speed ramping up to 200 then back to 0
//     if (m_increasing) {
//         m_speed += 1.0;
//         if (m_speed >= 200.0) m_increasing = false;
//     } else {
//         m_speed -= 1.0;
//         if (m_speed <= 0.0) m_increasing = true;
//     }

//     // Derive rpm from speed (simplified simulation)
//     m_rpm = static_cast<int>(m_speed * 35);

//     // Emit signals — this is what tells QML the values have changed.
//     // Without emit, QML bindings never re-evaluate even if m_speed changed.
//     emit speedChanged();
//     emit rpmChanged();
// }
void DashboardBackend::setSpeed(double speed)
{
    this->m_speed = std::min(std::max(speed, 0.0), 200.0);
    this->m_rpm = static_cast<int>(this->m_speed * 35);
    emit speedChanged();
    emit rpmChanged();
}

int DashboardBackend::cameraFrameSequence() const
{
    QMutexLocker locker(&m_cameraFrameMutex);
    return m_cameraFrameSequence;
}

void DashboardBackend::setCameraFrame(const QByteArray &imageData, const QString &format)
{
    QImage frame;
    const QByteArray formatHint = imageFormatHint(format);
    bool loaded = false;

    if (!formatHint.isEmpty()) {
        loaded = frame.loadFromData(imageData, formatHint.constData());
    }

    if (!loaded) {
        loaded = frame.loadFromData(imageData);
    }

    if (!loaded) {
        ++m_cameraDecodeFailures;
        if (m_cameraDecodeFailures == 1 || m_cameraDecodeFailures % 30 == 0) {
            qWarning() << "Failed to decode camera frame"
                       << "format:" << format
                       << "bytes:" << imageData.size()
                       << "supported image formats:" << QImageReader::supportedImageFormats();
        }
        return;
    }

    {
        QMutexLocker locker(&m_cameraFrameMutex);
        m_cameraFrame = frame;
        ++m_cameraFrameSequence;
    }

    emit cameraFrameChanged();
}

QImage DashboardBackend::cameraFrame() const
{
    QMutexLocker locker(&m_cameraFrameMutex);
    return m_cameraFrame;
}


DashboardBackend::~DashboardBackend()
{
    if (m_executor) {
        m_executor->cancel();
    }
    rclcpp::shutdown();
    if (m_rosThread.joinable()) {
        m_rosThread.join();
    }
}

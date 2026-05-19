#include "rpicameraimageprovider.h"
#include "dashboard_backend.h"

#include <QColor>

RpiCameraImageProvider::RpiCameraImageProvider(DashboardBackend *dashboard)
    : QQuickImageProvider(QQuickImageProvider::Image), m_dashboard(dashboard)
{
}

QImage RpiCameraImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(id)

    QImage frame = m_dashboard->cameraFrame();
    if (frame.isNull()) {
        const QSize fallbackSize = requestedSize.isValid() ? requestedSize : QSize(640, 480);
        frame = QImage(fallbackSize, QImage::Format_RGB32);
        frame.fill(QColor("#0A0A1A"));
    }

    if (size) {
        *size = frame.size();
    }

    if (requestedSize.isValid()) {
        return frame.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return frame;
}

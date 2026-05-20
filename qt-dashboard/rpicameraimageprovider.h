#ifndef RPICAMERAIMAGEPROVIDER_H
#define RPICAMERAIMAGEPROVIDER_H

#include <QQuickImageProvider>

class DashboardBackend;

class RpiCameraImageProvider : public QQuickImageProvider
{
private:
    DashboardBackend *m_dashboard;

public:
    explicit RpiCameraImageProvider(DashboardBackend *dashboard);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

#endif // RPICAMERAIMAGEPROVIDER_H

#ifndef DASHBOARD_BACKEND_H
#define DASHBOARD_BACKEND_H

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTimer>
#include "vehicledatanode.h"
#include "rpicamerastreamnode.h"
#include <rclcpp/rclcpp.hpp>
#include <thread>

class DashboardBackend : public QObject
{
    Q_OBJECT
    double m_speed = 0.0;   // backing variable for speed property
    int    m_rpm   = 0;     // backing variable for rpm property
    QTimer m_timer;         // the timer that drives updates
    bool   m_increasing = true; // direction flag for simulation
    mutable QMutex m_cameraFrameMutex;
    QImage m_cameraFrame;
    int m_cameraFrameSequence = 0;
    int m_cameraDecodeFailures = 0;

    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(int rpm READ rpm NOTIFY rpmChanged)
    Q_PROPERTY(int cameraFrameSequence READ cameraFrameSequence NOTIFY cameraFrameChanged)

    std::shared_ptr<VehicleDataNode> m_vehicleNode;
    std::shared_ptr<RpiCameraStreamNode> m_cameraNode;
    std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> m_executor;
    std::thread m_rosThread;
// private slots:
        // void onTimerTick();

public:
    explicit DashboardBackend(QObject *parent = nullptr);
    inline double speed() const{return m_speed;};
    inline int rpm() const {return m_rpm;};
    int cameraFrameSequence() const;
    void setSpeed(double speed);
    void setCameraFrame(const QByteArray &imageData, const QString &format);
    QImage cameraFrame() const;
    ~DashboardBackend();


signals:
    // API declatation for signals that will be filled with code generate from MOC (Meta-Object Compiler)
    void speedChanged();
    void rpmChanged();
    void cameraFrameChanged();

};

#endif // DASHBOARD_BACKEND_H

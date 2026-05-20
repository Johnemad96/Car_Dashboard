#include "rpicamerastreamnode.h"
#include "dashboard_backend.h"

#include <QByteArray>
#include <QMetaObject>
#include <QString>

RpiCameraStreamNode::RpiCameraStreamNode(DashboardBackend *dashboard)
    : Node("rpi_camera_stream_subscriber"), m_dashboard(dashboard)
{
    auto qos = rclcpp::QoS(rclcpp::KeepLast(10));
    qos.best_effort();

    m_subscription = create_subscription<sensor_msgs::msg::CompressedImage>(
        "/camera/image_compressed", qos,
        [this](sensor_msgs::msg::CompressedImage::SharedPtr msg) {
            ++m_messageCount;
            if (m_messageCount == 1 || m_messageCount % 30 == 0) {
                RCLCPP_INFO(
                    get_logger(),
                    "Received camera frame %d: format='%s', bytes=%zu",
                    m_messageCount,
                    msg->format.c_str(),
                    msg->data.size());
            }

            if (msg->data.empty()) {
                RCLCPP_WARN(get_logger(), "Received empty camera frame");
                return;
            }

            const QByteArray imageData(
                reinterpret_cast<const char *>(msg->data.data()),
                static_cast<int>(msg->data.size()));
            const QString format = QString::fromStdString(msg->format);

            QMetaObject::invokeMethod(
                m_dashboard,
                [dashboard = m_dashboard, imageData, format]() {
                    dashboard->setCameraFrame(imageData, format);
                },
                Qt::QueuedConnection);
        });
}

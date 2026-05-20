#ifndef RPICAMERASTREAMNODE_H
#define RPICAMERASTREAMNODE_H

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>

class DashboardBackend;

class RpiCameraStreamNode : public rclcpp::Node
{
private:
    rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr m_subscription;
    DashboardBackend *m_dashboard;
    int m_messageCount = 0;

public:
    explicit RpiCameraStreamNode(DashboardBackend *dashboard);
};

#endif // RPICAMERASTREAMNODE_H

#ifndef VEHICLEDATANODE_H
#define VEHICLEDATANODE_H
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>

class DashboardBackend;

class VehicleDataNode : public rclcpp::Node
{
private:
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr subscription_;
    DashboardBackend *m_dashboard;

public:
    VehicleDataNode(DashboardBackend* dashboard);
};

#endif // VEHICLEDATANODE_H

#include "vehicledatanode.h"
#include "dashboard_backend.h"

#include <QObject>

VehicleDataNode::VehicleDataNode(DashboardBackend* dashboard)
    :Node("speed_subscriber"), m_dashboard(dashboard)
{
    subscription_ = create_subscription<std_msgs::msg::Float64>(
        "speed", 10,
        [this](std_msgs::msg::Float64::SharedPtr msg){
            // QMetaObject::invokeMethod(dashboard->setSpeed(double(msg->data)));
            QMetaObject::invokeMethod(m_dashboard,
                                      [msg,this](){
                                        this->m_dashboard->setSpeed((double)(msg->data));
                                        }
                                      ,Qt::QueuedConnection);
        }
        );
}

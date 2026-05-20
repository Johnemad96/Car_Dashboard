# sourcing the environment setup script for the Yocto SDK
unset LD_LIBRARY_PATH
source /opt/poky/5.0.16/environment-setup-cortexa72-poky-linux
# exporting the ROS 2 Humble Python packages to the PYTHONPATH environment variable
export PYTHONPATH=$OECORE_TARGET_SYSROOT/opt/ros/humble/lib/python3.12/site-packages:$PYTHONPATH

# Target sysroot general Python site-packages (catkin_pkg lives here, NOT in the ROS path)
export PYTHONPATH=$OECORE_TARGET_SYSROOT/usr/lib/python3.12/site-packages:$PYTHONPATH

export AMENT_PREFIX_PATH=$OECORE_TARGET_SYSROOT/opt/ros/humble
